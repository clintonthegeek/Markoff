// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighter.h>

#include <markoff/core/Theme.h>

#include <QFontMetricsF>
#include <QTextBlock>
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

void InlineHighlighter::setFindSpans(const QList<FindSpan> &spans)
{
    if (m_findSpans == spans) return;
    m_findSpans = spans;
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
    // Span offsets are block-relative (across the whole markoff CRDT block,
    // which can contain embedded `\n`s — a multi-line paragraph). But Qt
    // splits the QTextDocument into one QTextBlock per `\n`, and
    // QSyntaxHighlighter::highlightBlock is invoked once per QTextBlock with
    // a `text` argument that is just *that* line. setFormat(i, ...) here
    // takes line-relative indices. Translate by subtracting the line's
    // document-start position; trim spans that don't intersect this line.
    const int lineStart = currentBlock().position();
    const int lineLen   = text.length();
    for (const Markoff::SourceSpan &span : std::as_const(m_spans)) {
        if (span.charLength <= 0) continue;
        const int relStart = span.charOffset - lineStart;
        const int relEnd   = relStart + span.charLength;
        if (relEnd <= 0 || relStart >= lineLen) continue;  // span outside this line
        const int from = std::max(0, relStart);
        const int to   = std::min(lineLen, relEnd);
        const bool hide = delimiterShouldHide(span);
        if (hide) {
            // Apply per-char hidden format (negative letterSpacing tuned per glyph).
            for (int i = from; i < to; ++i) {
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
        for (int i = from; i < to; ++i) {
            QTextCharFormat merged = format(i);
            merged.merge(spanFmt);
            setFormat(i, 1, merged);
        }
    }

    // Find-pass — paints search-match background on top of any existing
    // inline-pass formats. FindSpan byte offsets are block-document-relative
    // (UTF-8); convert to QChar (UTF-16) positions, then to line-relative
    // via lineStart (same translation pattern as the inline-pass above).
    // Backgrounds-only; foreground/weight/style untouched. The find pass
    // wins over Highlight when both apply to the same range — intentional
    // per the find-highlighting spec.
    if (!m_findSpans.isEmpty()) {
        const QString docText = document()->toPlainText();
        const QByteArray docUtf8 = docText.toUtf8();
        auto byteToQt = [&](quint32 byteOff) -> int {
            if (static_cast<int>(byteOff) >= docUtf8.size())
                return docText.size();
            return QString::fromUtf8(docUtf8.left(static_cast<int>(byteOff))).size();
        };
        for (const FindSpan &fs : std::as_const(m_findSpans)) {
            if (fs.byteLength == 0) continue;
            const int qStart = byteToQt(fs.byteOffset);
            const int qEnd   = byteToQt(fs.byteOffset + fs.byteLength);
            const int relStart = qStart - lineStart;
            const int relEnd   = qEnd   - lineStart;
            if (relEnd <= 0 || relStart >= lineLen) continue;
            const int from = std::max(0, relStart);
            const int to   = std::min(lineLen, relEnd);
            const QColor bg = fs.isCurrent
                ? m_theme->color(Markoff::Theme::Slot::SearchActiveMatchBackground)
                : m_theme->color(Markoff::Theme::Slot::SearchMatchBackground);
            if (!bg.isValid()) continue;
            for (int i = from; i < to; ++i) {
                QTextCharFormat merged = format(i);
                merged.setBackground(bg);  // overpaint — find pass wins over Highlight
                setFormat(i, 1, merged);
            }
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
        QFont mono = m_theme->font(Markoff::Theme::FontRole::Monospace);
        fmt.setFontFamilies(mono.families());
        const qreal basePt = mono.pointSizeF() > 0 ? mono.pointSizeF() : 11.0;
        fmt.setFontPointSize(basePt * m_fontScale);
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

void InlineHighlighter::setFontScale(qreal s)
{
    if (s <= 0) s = 1.0;
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
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
