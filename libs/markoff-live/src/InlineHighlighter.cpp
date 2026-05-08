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
        const QTextCharFormat fmt = formatFor(span);
        if (fmt == QTextCharFormat()) continue;
        if (span.charLength <= 0) continue;
        setFormat(span.charOffset, span.charLength, fmt);
    }
}

QTextCharFormat InlineHighlighter::formatFor(const Markoff::SourceSpan &span) const
{
    Q_UNUSED(span);
    // Step B2 starts populating this; for now: return default = no paint.
    return QTextCharFormat();
}

}  // namespace Markoff::Live
