// SPDX-License-Identifier: GPL-3.0-or-later
#include "InlineFormatting.h"

#include <markoff/core/Theme.h>

namespace Markoff::Canvas::Detail {

namespace {

/// Mirrors InlineHighlighter::delimiterShouldHide (markoff-live), minus the
/// selection-range reveal (T7's scope is caret-driven visibility only; no
/// selection-touches-delimiter criterion exists for this leaf).
bool delimiterShouldHide(const SourceSpan &span, int caretQCharOrNegative)
{
    if (!span.isDelimiter)
        return false;
    if (span.parentCharStart < 0 || span.parentCharEnd < 0)
        return false;

    // Always shown regardless of caret — parity with live, though these
    // flags never occur in this leaf's content-only ListItem/BlockQuote
    // buffers today.
    if (span.isTag || span.isListMarker || span.isBlockquoteMarker)
        return false;

    if (caretQCharOrNegative >= 0 &&
        caretQCharOrNegative >= span.parentCharStart - 1 &&
        caretQCharOrNegative <= span.parentCharEnd + 1) {
        return false;
    }

    return true;
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

QList<QTextLayout::FormatRange> inlineFormatRanges(
    const QList<SourceSpan> &spans, int caretQCharOrNegative,
    const Theme &theme, const QColor &invisibleColor)
{
    QList<QTextLayout::FormatRange> ranges;
    ranges.reserve(spans.size());

    for (const SourceSpan &span : spans) {
        if (span.charLength <= 0)
            continue;

        if (span.isDelimiter && delimiterShouldHide(span, caretQCharOrNegative)) {
            QTextCharFormat fmt;
            if (invisibleColor.isValid())
                fmt.setForeground(invisibleColor);
            ranges.push_back({span.charOffset, span.charLength, fmt});
            continue;
        }

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
        if (any)
            ranges.push_back({span.charOffset, span.charLength, fmt});
    }

    return ranges;
}

}  // namespace Markoff::Canvas::Detail
