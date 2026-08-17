// SPDX-License-Identifier: GPL-3.0-or-later
#include "InlineFormatting.h"

#include <markoff/core/Theme.h>

namespace Markoff::Canvas::Detail {

namespace {

// Boundary rule (fixes [cluster-k] P6 "reveal radius too wide"): a caret
// position `c` is a GAP index (QTextCursor convention — c chars precede
// it), so the span's own gaps run from parentCharStart (immediately
// before its first char) to parentCharEnd (immediately after its last
// char, i.e. parentCharStart + charLength-of-parent). "Touched" is
// deliberately asymmetric across those two gaps, not a symmetric ±1 pad:
//
//   - c == parentCharStart (caret sits in the gap just BEFORE the span,
//     e.g. "my|**bold**") is NOT touched. That gap belongs to the
//     preceding text, not the token — hiding here is what let the old
//     code's "-1" reveal an untouched span merely adjacent on the left.
//   - parentCharStart < c <= parentCharEnd (anywhere from just inside the
//     opening delimiter through the gap immediately after the closing
//     delimiter, e.g. "my **bold|** word" or "my **bold**| word") IS
//     touched. The end-of-span gap is intentionally included (not just
//     "< parentCharEnd"): a caret that has just typed/landed on the
//     closing delimiter is still actively composing the token and should
//     keep seeing raw markup, matching CodeMirror/Obsidian Live Preview.
//   - Anything further out — one more whitespace-separated character on
//     either side ("my |**bold** word" already covered above; "my
//     **bold** |word" on the right) — is NOT touched. Reveal only
//     follows the caret while it is actually inside or immediately
//     trailing the token, never merely adjacent across a gap character.
//
// This intentionally does NOT special-case the left side the way the end
// is special-cased on the right: entering a token from the left starts a
// fresh edit (still "outside" until the caret passes the first char),
// whereas the position right after the closing delimiter is where the
// caret naturally sits mid-composition immediately after typing it.
bool touchedByAnyCursor(const SourceSpan &span, const QList<int> &cursorsInBlock)
{
    for (const int c : cursorsInBlock) {
        if (c > span.parentCharStart && c <= span.parentCharEnd)
            return true;
    }
    return false;
}

void applyEmphasis(QTextCharFormat &fmt, const Theme &theme, Theme::Slot slot)
{
    const QColor c = theme.color(slot);
    if (c.isValid())
        fmt.setForeground(c);
    if (theme.isBold(slot))
        fmt.setFontWeight(QFont::Bold);
    if (theme.isItalic(slot))
        fmt.setFontItalic(true);
}

}  // namespace

bool delimiterShouldHide(const SourceSpan &span, const QList<int> &cursorsInBlock)
{
    if (!span.isDelimiter)
        return false;
    if (span.isTag || span.isListMarker || span.isBlockquoteMarker)
        return false;

    // Code fences (P2.2, spec §4.2): per-BLOCK reveal, not per-span — the
    // whole fence line shows only when the caret is anywhere in the code
    // block, matching Obsidian. Unlike the ATX heading marker (below),
    // fenced_code_block_delimiter/info_string/language spans are never
    // given a parent range by the parser (they aren't part of any inline
    // tree — collectParentRanges only walks inline ASTs), so this has to
    // be a canvas-local rule rather than reusing parentCharStart/End.
    if (span.isCodeBlockFence)
        return cursorsInBlock.isEmpty();

    // Math delimiters ($, $$ — P5.3): per-BLOCK reveal too, same rule as
    // code fences, per the plan's own wording ("Caret-in-block reveals
    // source (same reveal rule as code fences)") rather than the generic
    // parentCharStart/End per-span mechanism below. This sidesteps a real
    // parser gap: TreeSitterParser's collectParentRanges lists latex_span
    // as a formatting-parent node type, but the grammar never actually
    // emits that node (its own comment: "does NOT have a separate
    // latex_span node type — both `$x^2$` and `$$x^2$$` are parsed as
    // latex_block", which collectParentRanges does NOT list) — so a math
    // delimiter's parentCharStart/End would always be -1 and never hide.
    // Fixing that is a markoff-parser change this task doesn't name; the
    // per-block rule below needs none of it.
    if (span.math)
        return cursorsInBlock.isEmpty();

    // The ATX heading marker (`# `) needs NO special case here despite
    // also being per-block in Obsidian: the parser already gives that
    // specific span a parent range spanning the WHOLE heading line (post-
    // process 1, TreeSitterParser.cpp), not just its own bytes — a nested
    // delimiter inside the heading's text (e.g. "# **Bold**"'s "**") gets
    // its own narrow range instead, overwritten by post-process 2, so it
    // correctly keeps per-span reveal. The touchedByAnyCursor check below
    // already does the right thing for both cases without knowing which
    // one it's looking at.
    if (span.parentCharStart < 0 || span.parentCharEnd < 0)
        return false;
    if (touchedByAnyCursor(span, cursorsInBlock))
        return false;
    return true;
}

QList<std::pair<int, int>> omittedDelimiterRanges(const QList<SourceSpan> &spans,
                                                   const QList<int> &cursorsInBlock)
{
    QList<std::pair<int, int>> out;
    for (const SourceSpan &span : spans) {
        if (span.charLength <= 0)
            continue;
        if (delimiterShouldHide(span, cursorsInBlock))
            out.push_back({span.charOffset, span.charLength});
    }
    return out;
}

QList<QTextLayout::FormatRange> inlineFormatRanges(
    const QList<SourceSpan> &spans, const QList<int> &cursorsInBlock,
    const Theme &theme, const ProjectionMap &projection)
{
    QList<QTextLayout::FormatRange> ranges;
    ranges.reserve(spans.size());

    for (const SourceSpan &span : spans) {
        if (span.charLength <= 0)
            continue;
        if (span.isDelimiter && delimiterShouldHide(span, cursorsInBlock))
            continue;  // omitted from the layout text entirely — nothing to format

        QTextCharFormat fmt;
        bool any = false;
        if (span.bold)   { applyEmphasis(fmt, theme, Theme::Slot::BoldEmphasis);   any = true; }
        if (span.italic) { applyEmphasis(fmt, theme, Theme::Slot::ItalicEmphasis); any = true; }
        if (span.code) {
            const QColor fg = theme.color(Theme::Slot::InlineCode);
            const QColor bg = theme.color(Theme::Slot::CodeBlockBackground);
            if (fg.isValid()) fmt.setForeground(fg);
            if (bg.isValid()) fmt.setBackground(bg);
            fmt.setFontFamilies(theme.font(Theme::FontRole::Monospace).families());
            any = true;
        }
        // Math (P5.3): "$…$" inline spans render as a styled inline run,
        // code-like — QTextLayout has no inline-object-replacement path
        // without a backing QTextDocument (C3 forbids one), so a real
        // glyph-rendered pixmap mid-line is not available; this is the
        // documented fallback (plan P5.3: "render inline math as a styled
        // span in-line (code-like)"), not a placeholder for it. Applies
        // uniformly to a Math BLOCK's own text too when its source is
        // revealed (caret inside) — same "reveal shows source styled like
        // source" shape as CodeBlock.
        if (span.math) {
            const QColor fg = theme.color(Theme::Slot::Math);
            const QColor bg = theme.color(Theme::Slot::CodeBlockBackground);
            if (fg.isValid()) fmt.setForeground(fg);
            if (bg.isValid()) fmt.setBackground(bg);
            fmt.setFontFamilies(theme.font(Theme::FontRole::Monospace).families());
            any = true;
        }
        if (span.strikethrough) {
            fmt.setFontStrikeOut(true);
            const QColor c = theme.color(Theme::Slot::StrikeEmphasis);
            if (c.isValid()) fmt.setForeground(c);
            any = true;
        }
        if (span.highlight) {
            const QColor c = theme.color(Theme::Slot::Highlight);
            if (c.isValid()) fmt.setBackground(c);
            any = true;
        }
        // isLink/isWikilink/isTag: mutually exclusive in practice (a span's
        // parent-formatting propagation sets at most one of these three per
        // the parser's isLinkParent walk), same Theme::Slot mapping as
        // live's InlineHighlighter::formatFor.
        if (span.isLink) {
            applyEmphasis(fmt, theme, Theme::Slot::Link);
            fmt.setFontUnderline(true);
            any = true;
        }
        if (span.isWikilink) {
            applyEmphasis(fmt, theme, Theme::Slot::WikiLink);
            fmt.setFontUnderline(true);
            any = true;
        }
        if (span.isTag) {
            applyEmphasis(fmt, theme, Theme::Slot::Tag);
            any = true;
        }
        // Footnote refs ([^1]) are outside the live highlighter's 8-kind
        // table entirely (live's E1 test explicitly keeps them out of
        // scope) and there is no dedicated Theme::Slot for them — adding
        // one would be a markoff-core seam this task doesn't name. Render
        // as superscript only (Obsidian parity, spec §5.3's "footnote refs
        // superscripted"), no color override: a leaf-local rendering
        // decision, not a slot mapping.
        if (span.isFootnoteRef) {
            fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            any = true;
        }
        if (!any)
            continue;

        const int layoutStart = projection.fullQCharToLayoutQChar(span.charOffset);
        const int layoutEnd   = projection.fullQCharToLayoutQChar(span.charOffset + span.charLength);
        if (layoutEnd > layoutStart)
            ranges.push_back({layoutStart, layoutEnd - layoutStart, fmt});
    }

    return ranges;
}

}  // namespace Markoff::Canvas::Detail
