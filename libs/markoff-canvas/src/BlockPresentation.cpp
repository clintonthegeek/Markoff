// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockPresentation.h"

#include <QFontMetricsF>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

#include "CalloutBlocks.h"
#include "FootnoteDefBlocks.h"
#include "MediaBlocks.h"

namespace Markoff::Canvas {

namespace {

/// Indent step for nested list items, in units of the body font's height.
constexpr qreal kIndentSteps = 1.6;

/// Heading level → Theme slot. The Theme owns the sizes and colors; this
/// function owns nothing but the mapping.
Theme::Slot headingSlot(int level)
{
    switch (level) {
    case 1:  return Theme::Slot::Heading1;
    case 2:  return Theme::Slot::Heading2;
    case 3:  return Theme::Slot::Heading3;
    case 4:  return Theme::Slot::Heading4;
    case 5:  return Theme::Slot::Heading5;
    default: return Theme::Slot::Heading6;
    }
}

/// Build a QFont from a Theme slot's family/size/weight/slant. Uses pixel
/// sizing because pixelSizeFor() has already folded in the slot's size
/// multiplier — going back through point sizes would drop it. `fontScale`
/// (P3.5) multiplies on top of that, so it lands on every block uniformly
/// regardless of which slot it uses.
QFont fontForSlot(const Theme &theme, Theme::Slot slot, qreal fontScale)
{
    QFont f(theme.familyFor(slot));
    f.setPixelSize(qMax(1, qRound(theme.pixelSizeFor(slot) * fontScale)));
    f.setBold(theme.isBold(slot));
    f.setItalic(theme.isItalic(slot));
    return f;
}

int intAttr(const MarkoffDocument &doc, BlockId id, const AttrName &name,
            int fallback)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return fallback;
    if (const int *p = std::get_if<int>(&it.value()))
        return *p;
    return fallback;
}

bool boolAttr(const MarkoffDocument &doc, BlockId id, const AttrName &name,
              bool fallback)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return fallback;
    if (const bool *p = std::get_if<bool>(&it.value()))
        return *p;
    return fallback;
}

}  // namespace

BlockStyle presentationFor(const MarkoffDocument &doc, BlockId id,
                           const Theme &theme, qreal fontScale)
{
    BlockStyle s;
    s.font       = fontForSlot(theme, Theme::Slot::TextDefault, fontScale);
    s.foreground = theme.color(Theme::Slot::TextDefault);

    const qreal em = QFontMetricsF(s.font).height();
    s.topMargin    = em * 0.35;
    s.bottomMargin = em * 0.35;

    switch (doc.blockKind(id)) {
    case BlockKind::Paragraph: {
        // Footnote definition (P5.5, `FootnoteDefBlocks::parseFootnoteDef`):
        // a real Paragraph block (see that file's doc comment for why —
        // markoff-parser's extract() copies but does not strip these lines
        // from the body), given back-reference styling — the marker
        // decoration slot (same one list bullets/checkboxes use) shows the
        // `[^label]` back-reference, and the block's own text renders in
        // Link-slot color/italic so it reads as distinct from a plain
        // paragraph at a glance, without a second paint code path.
        const QByteArray text = doc.blockText(id);
        const Detail::FootnoteDefInfo fn =
            Detail::parseFootnoteDef(QString::fromUtf8(text));
        if (fn.isFootnoteDef) {
            s.isFootnoteDef = true;
            s.marker        = QStringLiteral("[^%1]").arg(fn.label);
            s.foreground    = theme.color(Theme::Slot::Link);
            s.font.setItalic(true);
            break;
        }

        // Image/Embed shape, still tagged Paragraph (punch-list [cluster-k],
        // "empty-alt embeds render blank until clicked into"): kind
        // promotion to BlockKind::Image only happens interactively
        // (View::promoteCaretBlockKind, fired from an actual document EDIT
        // with the caret in this exact block — see its own doc comment).
        // A block loaded from disk and never typed into (the common case
        // for every image in a real vault note) therefore stays
        // BlockKind::Paragraph forever, which used to fall through to
        // ordinary paragraph text-layout rendering below — and since the
        // "for link-type parents" delimiter-hiding rule in
        // TreeSitterParser.cpp hides an image span's ENTIRE byte range
        // (there's no `image_description`-is-visible-text carve-out the
        // way `link_text` gets one), that ordinary rendering path shows
        // NOTHING at all for ANY still-Paragraph image line — with or
        // without alt text; verified empirically, not just for the empty-
        // alt case the finding first reported. Sniffing the shape here
        // mirrors the footnote-def carve-out just above (a real Paragraph
        // block content-sniffed into a different presentation, with no
        // document mutation) rather than requiring a load-time or click-
        // time kind promotion, which would be a markoff-core change.
        if (text.startsWith("![")) {
            const Detail::ImageBlockInfo info = Detail::parseImageBlock(text);
            s.isEmbedBlock = info.isEmbed;
            s.isImageBlock = !info.isEmbed;
        }
        break;
    }

    case BlockKind::Heading: {
        const int level = qBound(1, intAttr(doc, id, AttrNames::Level, 1), 6);
        const Theme::Slot slot = headingSlot(level);
        s.font       = fontForSlot(theme, slot, fontScale);
        s.foreground = theme.color(slot);
        const qreal hem = QFontMetricsF(s.font).height();
        s.topMargin    = hem * 0.6;
        s.bottomMargin = hem * 0.3;
        break;
    }

    case BlockKind::CodeBlock:
        s.font       = fontForSlot(theme, Theme::Slot::CodeBlock, fontScale);
        s.foreground = theme.color(Theme::Slot::CodeBlock);
        s.background = theme.color(Theme::Slot::CodeBlockBackground);
        s.fullWidthBackground = true;
        s.leftIndent = em * 0.5;
        s.isCodeBlock = true;
        break;

    case BlockKind::ListItem: {
        const int indent = qMax(0, intAttr(doc, id, AttrNames::IndentLevel, 0));
        s.leftIndent = em * kIndentSteps * (indent + 1);

        const auto attrs = doc.blockAttrs(id);
        bool isTask = false;
        if (const auto it = attrs.constFind(AttrNames::MarkerStyle); it != attrs.cend()) {
            if (const QString *v = std::get_if<QString>(&it.value()))
                isTask = (*v == QStringLiteral("task"));
        }
        if (isTask) {
            // Checkbox glyph (P4.7) replaces the bracket-text marker in the
            // same decoration slot — see BlockPresentation.h's note on why
            // `marker` stays empty here.
            s.isTaskItem = true;
            if (const auto ci = attrs.constFind(AttrNames::Checked); ci != attrs.cend()) {
                if (const bool *cv = std::get_if<bool>(&ci.value()))
                    s.taskChecked = *cv;
            }
        } else {
            // Single source of truth for the marker text (queue #8.3) — do
            // not re-derive it from markerStyle/markerNumber attrs.
            s.marker = QString::fromUtf8(doc.listItemDisplayMarker(id)).trimmed();
        }
        s.topMargin    = em * 0.15;
        s.bottomMargin = em * 0.15;
        break;
    }

    case BlockKind::BlockQuote: {
        const int depth = qMax(1, intAttr(doc, id, AttrNames::BlockQuoteDepth, 1));
        s.font       = fontForSlot(theme, Theme::Slot::Quote, fontScale);
        s.foreground = theme.color(Theme::Slot::Quote);
        s.background = theme.color(Theme::Slot::QuoteBackground);
        s.fullWidthBackground = true;
        s.hasQuoteBar = true;
        s.leftIndent  = em * 1.0 * depth;

        // Callout (P5.5): only the run's FIRST block carries the `[!type]`
        // marker (a continuation paragraph in the same BlockQuoteRunId, or
        // a plain quote, is never callout-shaped) — CalloutBlocks::
        // parseCallout is naturally false for those, so they fall through
        // to the plain-quote styling above unchanged.
        const Detail::CalloutInfo callout =
            Detail::parseCallout(QString::fromUtf8(doc.blockText(id)));
        if (callout.isCallout) {
            s.isCallout     = true;
            s.calloutIcon   = callout.icon;
            s.calloutLabel  = callout.label;
            s.foreground    = theme.color(callout.slot);
            // "body indent" (plan wording): a further step beyond the
            // plain quote's own depth-scaled indent.
            s.leftIndent   += em * 0.6;
            // Reserve a header band above the text layout's own start
            // (contentY = e.y + topMargin) for the icon+label row —
            // View::paintEvent paints into [e.y, e.y + old topMargin)
            // when style.isCallout, then the text layout starts exactly
            // where it always did relative to the (now taller) topMargin.
            s.topMargin    += QFontMetricsF(s.font).height() * 1.4;
        }
        break;
    }

    case BlockKind::HorizontalRule:
        s.isRule       = true;
        s.topMargin    = em * 0.5;
        s.bottomMargin = em * 0.5;
        break;

    case BlockKind::Math:
        // Math (P5.3): Slot::Math falls through to the Monospace font
        // role (Theme.cpp), same as live's MathDelegate uses for its raw-
        // source Text element — the block's own text layout (always built,
        // never skipped) is the "reveal source" representation, and it
        // should look like source, not body text.
        s.font       = fontForSlot(theme, Theme::Slot::Math, fontScale);
        s.foreground = theme.color(Theme::Slot::Math);
        s.isMathDisplay = boolAttr(doc, id, AttrNames::DisplayMode, false);
        break;

    case BlockKind::Table:
        s.isTable = true;
        break;

    // Image / Embed (P5.4): KindInference maps both `![alt](src)` and
    // `![[target]]` to this one BlockKind (its own "starts with ![" rule),
    // so MediaBlocks::parseImageBlock reparses the buffer to tell them
    // apart. BlockLayoutCache::rebuildInline does the actual pixmap/
    // placeholder-label work (it, not this pure function, holds the
    // injected ImageResourceLookup/EmbedRegistry seams) — this switch only
    // sets which of the two mutually-exclusive flags applies.
    case BlockKind::Image: {
        const QByteArray text = doc.blockText(id);
        const Detail::ImageBlockInfo info = Detail::parseImageBlock(text);
        s.isEmbedBlock = info.isEmbed;
        s.isImageBlock = !info.isEmbed;
        break;
    }

    // Out of spike scope entirely (spec §5), rendered as their source text
    // in the body font so the document stays navigable. (Mermaid diagrams
    // arrive as a fenced ```mermaid CodeBlock, not this BlockKind — see
    // BlockLayoutCache::rebuildInline's mermaid-pixmap handling instead;
    // BlockKind::Mermaid itself is never assigned by the load path.)
    case BlockKind::Mermaid:
    case BlockKind::HtmlBlock:
        break;
    }

    return s;
}

}  // namespace Markoff::Canvas
