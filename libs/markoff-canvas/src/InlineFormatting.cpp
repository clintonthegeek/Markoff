// SPDX-License-Identifier: GPL-3.0-or-later
#include "InlineFormatting.h"

#include <markoff/core/Theme.h>

namespace Markoff::Canvas::Detail {

namespace {

bool touchedByAnyCursor(const SourceSpan &span, const QList<int> &cursorsInBlock)
{
    for (const int c : cursorsInBlock) {
        if (c >= span.parentCharStart - 1 && c <= span.parentCharEnd + 1)
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
    if (span.parentCharStart < 0 || span.parentCharEnd < 0)
        return false;
    if (span.isTag || span.isListMarker || span.isBlockquoteMarker)
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
