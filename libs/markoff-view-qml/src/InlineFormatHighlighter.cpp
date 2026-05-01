// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/InlineFormatHighlighter.h>

#include <markoff-parser/TreeSitterParser.h>

#include <QBitArray>
#include <QFont>
#include <QQuickTextDocument>
#include <QTextCharFormat>

namespace Markoff::View::Qml {

InlineFormatHighlighter::InlineFormatHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

QQuickTextDocument *InlineFormatHighlighter::quickDocument() const
{
    return m_quickDoc;
}

void InlineFormatHighlighter::setQuickDocument(QQuickTextDocument *doc)
{
    if (m_quickDoc == doc) return;
    m_quickDoc = doc;
    if (m_quickDoc) {
        setDocument(m_quickDoc->textDocument());
        rehighlight();  // apply pre-populated spans immediately
    } else
        setDocument(nullptr);
    Q_EMIT quickDocumentChanged();
}

QString InlineFormatHighlighter::source() const
{
    return m_source;
}

void InlineFormatHighlighter::setSource(const QString &src)
{
    if (m_source == src) return;
    m_source = src;
    Q_EMIT sourceChanged();
    rebuildSpans();
    rehighlight();
}

void InlineFormatHighlighter::rebuildSpans()
{
    m_spans.clear();
    if (m_source.isEmpty()) return;

    Markoff::TreeSitterParser parser;
    if (!parser.parse(m_source))
        return;

    m_spans = parser.buildSpanMap();
}

void InlineFormatHighlighter::applySpeculativeFormats(const QString &source)
{
    if (source.isEmpty()) return;

    // Collect confirmed code-span ranges to avoid speculating inside code.
    QList<QPair<int,int>> codeRanges;
    for (const SourceSpan &s : m_spans) {
        if (s.code && !s.isDelimiter && s.charLength > 0)
            codeRanges.append({s.charOffset, s.charOffset + s.charLength});
    }
    auto inCodeRange = [&](int pos) {
        for (const auto &r : codeRanges)
            if (pos >= r.first && pos < r.second) return true;
        return false;
    };

    // Process delimiters longest-first. Track consumed positions.
    const int len = source.length();
    QBitArray consumed(len, false);

    struct DelimSpec { QString d; bool bold, italic, code, strike, highlight; };
    const DelimSpec specs[] = {
        { QStringLiteral("**"), true,  false, false, false, false },
        { QStringLiteral("__"), true,  false, false, false, false },
        { QStringLiteral("~~"), false, false, false, true,  false },
        { QStringLiteral("=="), false, false, false, false, true  },
        { QStringLiteral("*"),  false, true,  false, false, false },
        { QStringLiteral("_"),  false, true,  false, false, false },
        { QStringLiteral("`"),  false, false, true,  false, false },
    };

    for (const DelimSpec &spec : specs) {
        const int dlen = spec.d.length();
        // Find all non-consumed, non-code-range occurrences.
        QList<int> positions;
        for (int i = 0; i <= len - dlen; ++i) {
            if (consumed[i]) continue;
            if (inCodeRange(i)) continue;
            if (QStringView(source).mid(i, dlen) == spec.d) {
                // Mark as consumed.
                for (int j = 0; j < dlen; ++j) consumed[i + j] = true;
                positions.append(i);
            }
        }
        if (positions.isEmpty() || positions.size() % 2 == 0) continue;
        // Odd count → unclosed opener. The last position is the unclosed one.
        const int openerStart = positions.last();
        const int contentStart = openerStart + dlen;
        if (contentStart >= len) continue;  // opener at very end, nothing to style

        QTextCharFormat fmt;
        if (spec.bold)      fmt.setFontWeight(QFont::Bold);
        if (spec.italic)    fmt.setFontItalic(true);
        if (spec.code)    { fmt.setFontFamilies({ QStringLiteral("Monospace") });
                            fmt.setBackground(QColor(0xf0, 0xf0, 0xf0)); }
        if (spec.strike)    fmt.setFontStrikeOut(true);
        if (spec.highlight) fmt.setBackground(QColor(0xff, 0xff, 0x00));

        setFormat(contentStart, len - contentStart, fmt);
    }
}

void InlineFormatHighlighter::highlightBlock(const QString &text)
{
    // Apply speculative formats first so confirmed spans can overwrite them.
    applySpeculativeFormats(text);

    if (m_spans.isEmpty()) return;

    // QSyntaxHighlighter is called once per QTextBlock. For single-block
    // delegates (paragraph, heading) there is only one block, so all spans
    // apply here. charOffset in SourceSpan is relative to the full parsed
    // source; since source == the delegate's text for paragraphs, and for
    // headings the source may contain a `# ` prefix that is NOT in the
    // displayed text — but we pass the displayed text as `source`, so the
    // offsets are always relative to what is displayed.
    for (const Markoff::SourceSpan &s : m_spans) {
        if (s.isDelimiter) continue;  // delimiters are hidden in live preview
        // All format branches below can assume !isDelimiter.
        if (s.charLength <= 0) continue;

        QTextCharFormat fmt;
        bool hasFormat = false;

        if (s.bold) {
            fmt.setFontWeight(QFont::Bold);
            hasFormat = true;
        }
        if (s.italic) {
            fmt.setFontItalic(true);
            hasFormat = true;
        }
        if (s.strikethrough) {
            fmt.setFontStrikeOut(true);
            hasFormat = true;
        }
        if (s.code) {
            fmt.setFontFamilies({ QStringLiteral("Monospace") });
            fmt.setBackground(QColor(0xf0, 0xf0, 0xf0));
            hasFormat = true;
        }
        if (s.highlight) {
            fmt.setBackground(QColor(0xff, 0xff, 0x00));
            hasFormat = true;
        }
        if (s.isLink) {
            fmt.setFontUnderline(true);
            fmt.setForeground(QColor(0x00, 0x66, 0xcc));
            hasFormat = true;
        }

        if (hasFormat)
            setFormat(s.charOffset, s.charLength, fmt);
    }
}

}  // namespace Markoff::View::Qml
