// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/InlineFormatHighlighter.h>

#include <markoff-parser/TreeSitterParser.h>

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
    if (m_quickDoc)
        setDocument(m_quickDoc->textDocument());
    else
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

void InlineFormatHighlighter::highlightBlock(const QString &text)
{
    Q_UNUSED(text)
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
        if (s.isLink && !s.isDelimiter) {
            fmt.setFontUnderline(true);
            fmt.setForeground(QColor(0x00, 0x66, 0xcc));
            hasFormat = true;
        }

        if (hasFormat)
            setFormat(s.charOffset, s.charLength, fmt);
    }
}

}  // namespace Markoff::View::Qml
