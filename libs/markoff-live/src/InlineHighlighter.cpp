// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighter.h>

#include <markoff/core/Theme.h>

namespace Markoff::Live {

InlineHighlighter::InlineHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

InlineHighlighter::~InlineHighlighter() = default;

void InlineHighlighter::setInlineSpans(const QList<Markoff::SourceSpan> &spans)
{
    if (m_spans == spans) return;
    m_spans = spans;
    rehighlight();
}

void InlineHighlighter::setTheme(const Markoff::Theme *theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    rehighlight();
}

void InlineHighlighter::highlightBlock(const QString &text)
{
    Q_UNUSED(text);
    if (!m_theme) return;
    for (const Markoff::SourceSpan &span : std::as_const(m_spans)) {
        const QTextCharFormat spanFmt = formatFor(span);
        if (spanFmt == QTextCharFormat()) continue;
        if (span.charLength <= 0) continue;
        // Merge span format into existing per-character formats so that
        // overlapping spans accumulate properties rather than replacing them.
        for (int i = span.charOffset; i < span.charOffset + span.charLength; ++i) {
            QTextCharFormat merged = format(i);
            merged.merge(spanFmt);
            setFormat(i, 1, merged);
        }
    }
}

QTextCharFormat InlineHighlighter::formatFor(const Markoff::SourceSpan &span) const
{
    if (!m_theme) return QTextCharFormat();
    QTextCharFormat fmt;
    bool any = false;

    auto applyEmphasis = [&](Markoff::Theme::Slot slot) {
        const QColor c = m_theme->color(slot);
        if (c.isValid()) fmt.setForeground(c);
        if (m_theme->isBold(slot))   fmt.setFontWeight(QFont::Bold);
        if (m_theme->isItalic(slot)) fmt.setFontItalic(true);
        any = true;
    };

    if (span.bold)   applyEmphasis(Markoff::Theme::Slot::BoldEmphasis);
    if (span.italic) applyEmphasis(Markoff::Theme::Slot::ItalicEmphasis);

    if (span.strikethrough) {
        fmt.setFontStrikeOut(true);
        const QColor c = m_theme->color(Markoff::Theme::Slot::StrikeEmphasis);
        if (c.isValid()) fmt.setForeground(c);
        any = true;
    }

    if (span.code) {
        const QColor fg = m_theme->color(Markoff::Theme::Slot::InlineCode);
        const QColor bg = m_theme->color(Markoff::Theme::Slot::CodeBlockBackground);
        if (fg.isValid()) fmt.setForeground(fg);
        if (bg.isValid()) fmt.setBackground(bg);
        // Apply monospace font family without clobbering other font properties
        // (e.g. strikethrough). Merge the monospace font via fontMerge so that
        // only the family/size change; decoration flags set earlier are preserved.
        QFont mono = m_theme->font(Markoff::Theme::FontRole::Monospace);
        fmt.setFontFamilies(mono.families());
        fmt.setFontPointSize(mono.pointSizeF() > 0 ? mono.pointSizeF() : fmt.fontPointSize());
        any = true;
    }

    if (span.highlight) {
        const QColor c = m_theme->color(Markoff::Theme::Slot::Highlight);
        if (c.isValid()) fmt.setBackground(c);
        any = true;
    }
    if (span.isLink) {
        applyEmphasis(Markoff::Theme::Slot::Link);
        fmt.setFontUnderline(true);
    }
    if (span.isWikilink) {
        applyEmphasis(Markoff::Theme::Slot::WikiLink);
        fmt.setFontUnderline(true);
    }
    if (span.isTag) applyEmphasis(Markoff::Theme::Slot::Tag);

    return any ? fmt : QTextCharFormat();
}

}  // namespace Markoff::Live
