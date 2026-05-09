// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighter.h>

#include <markoff/core/Theme.h>

#include <QFontMetricsF>
#include <algorithm>

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
    if (!m_theme) return;
    for (const Markoff::SourceSpan &span : std::as_const(m_spans)) {
        if (span.charLength <= 0) continue;
        const bool hide = delimiterShouldHide(span);
        if (hide) {
            // Apply per-char hidden format (negative letterSpacing tuned per glyph).
            for (int i = span.charOffset; i < span.charOffset + span.charLength; ++i) {
                if (i < 0 || i >= text.length()) continue;
                QTextCharFormat merged = format(i);
                merged.merge(hiddenFormatForChar(text[i]));
                setFormat(i, 1, merged);
            }
            continue;
        }
        const QTextCharFormat spanFmt = formatFor(span);
        if (spanFmt == QTextCharFormat()) continue;
        // Merge span format into existing per-character formats so that
        // overlapping spans accumulate properties rather than replacing them.
        for (int i = span.charOffset; i < span.charOffset + span.charLength; ++i) {
            if (i < 0 || i >= text.length()) continue;
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

void InlineHighlighter::setLocalCaretPosition(int qtPos)
{
    if (m_localCaretPos == qtPos) return;
    m_localCaretPos = qtPos;
    rehighlight();
}

void InlineHighlighter::setSelectionRange(int startQtPos, int endQtPos)
{
    if (m_selStart == startQtPos && m_selEnd == endQtPos) return;
    m_selStart = startQtPos;
    m_selEnd   = endQtPos;
    rehighlight();
}

bool InlineHighlighter::delimiterShouldHide(const Markoff::SourceSpan &span) const
{
    if (!span.isDelimiter) return false;
    if (span.parentCharStart < 0 || span.parentCharEnd < 0) return false;

    // These kinds are always shown regardless of caret/selection.
    if (span.isTag)              return false;  // tag # is visual identity (spec §4.1)
    if (span.isListMarker)       return false;  // list bullets always shown (spec §4.2)
    if (span.isBlockquoteMarker) return false;  // blockquote > always shown (spec §4.2)

    // Selection touches the parent range? Reveal.
    if (m_selStart >= 0 && m_selEnd >= 0) {
        const int lo = std::min(m_selStart, m_selEnd);
        const int hi = std::max(m_selStart, m_selEnd);
        if (lo <= span.parentCharEnd && hi >= span.parentCharStart) return false;
    }

    // Caret in [parentCharStart - 1, parentCharEnd + 1]? Reveal.
    // m_localCaretPos == -1 means "no caret in this block" — never reveal.
    if (m_localCaretPos >= 0 &&
        m_localCaretPos >= span.parentCharStart - 1 &&
        m_localCaretPos <= span.parentCharEnd + 1) {
        return false;
    }

    return true;  // hide
}

QTextCharFormat InlineHighlighter::hiddenFormatForChar(QChar ch) const
{
    QTextCharFormat fmt;
    if (!m_theme) return fmt;
    fmt = m_theme->charFormat(Markoff::Theme::Slot::HiddenMarker);
    // QTextCharFormat has no direct letter-spacing setters; must go via QFont.
    QFont f = fmt.font();
    const qreal advance = QFontMetricsF(f).horizontalAdvance(ch);
    f.setLetterSpacing(QFont::AbsoluteSpacing, -advance);
    fmt.setFont(f);
    return fmt;
}

}  // namespace Markoff::Live
