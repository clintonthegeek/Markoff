// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighterAttached.h>
#include <markoff/live/InlineHighlighter.h>

#include <QQuickTextDocument>
#include <QTextDocument>

namespace Markoff::Live {

InlineHighlighterAttached::InlineHighlighterAttached(QObject *parent)
    : QObject(parent) {}

InlineHighlighterAttached::~InlineHighlighterAttached() = default;

void InlineHighlighterAttached::setTarget(QQuickTextDocument *target)
{
    if (m_target == target) return;
    m_target = target;
    rebuildHighlighter();
    emit targetChanged();
}

QVariantList InlineHighlighterAttached::spans() const
{
    QVariantList out;
    out.reserve(m_spans.size());
    for (const auto &s : m_spans) out.append(QVariant::fromValue(s));
    return out;
}

void InlineHighlighterAttached::setSpans(const QVariantList &v)
{
    QList<Markoff::SourceSpan> next;
    next.reserve(v.size());
    for (const QVariant &item : v) {
        if (item.canConvert<Markoff::SourceSpan>())
            next.append(item.value<Markoff::SourceSpan>());
    }
    if (next == m_spans) return;
    m_spans = next;
    if (m_highlighter) m_highlighter->setInlineSpans(m_spans);
    emit spansChanged();
}

void InlineHighlighterAttached::setTheme(const Markoff::Theme *theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    if (m_highlighter) m_highlighter->setTheme(m_theme);
    emit themeChanged();
}

void InlineHighlighterAttached::rebuildHighlighter()
{
    if (m_highlighter) {
        m_highlighter->deleteLater();
        m_highlighter = nullptr;
    }
    if (!m_target) return;
    QTextDocument *doc = m_target->textDocument();
    if (!doc) return;
    m_highlighter = new InlineHighlighter(doc);
    m_highlighter->setParent(this);
    m_highlighter->setTheme(m_theme);
    m_highlighter->setInlineSpans(m_spans);
}

}  // namespace Markoff::Live
