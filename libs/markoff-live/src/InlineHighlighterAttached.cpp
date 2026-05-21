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

QVariantList InlineHighlighterAttached::findSpans() const
{
    QVariantList out;
    out.reserve(m_findSpans.size());
    for (const auto &s : m_findSpans) out.append(QVariant::fromValue(s));
    return out;
}

void InlineHighlighterAttached::setFindSpans(const QVariantList &v)
{
    QList<Markoff::Live::FindSpan> next;
    next.reserve(v.size());
    for (const QVariant &item : v) {
        if (item.canConvert<Markoff::Live::FindSpan>())
            next.append(item.value<Markoff::Live::FindSpan>());
    }
    if (next == m_findSpans) return;
    m_findSpans = next;
    if (m_highlighter) m_highlighter->setFindSpans(m_findSpans);
    Q_EMIT findSpansChanged();
}

void InlineHighlighterAttached::setTheme(const Markoff::Theme *theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    if (m_highlighter) m_highlighter->setTheme(m_theme);
    emit themeChanged();
}

void InlineHighlighterAttached::setFontScale(qreal s)
{
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    if (m_highlighter) m_highlighter->setFontScale(m_fontScale);
    Q_EMIT fontScaleChanged();
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
    m_highlighter->setFontScale(m_fontScale);
    m_highlighter->setInlineSpans(m_spans);
    m_highlighter->setFindSpans(m_findSpans);
    m_highlighter->setLocalCaretPosition(m_caretPos);
    m_highlighter->setSelectionRange(m_selStart, m_selEnd);
}

void InlineHighlighterAttached::setCaretPosition(int qtPos)
{
    if (m_caretPos == qtPos) return;
    m_caretPos = qtPos;
    if (m_highlighter) m_highlighter->setLocalCaretPosition(qtPos);
    Q_EMIT caretPositionChanged();
}

void InlineHighlighterAttached::setSelectionStart(int qtPos)
{
    if (m_selStart == qtPos) return;
    m_selStart = qtPos;
    if (m_highlighter) m_highlighter->setSelectionRange(m_selStart, m_selEnd);
    Q_EMIT selectionStartChanged();
}

void InlineHighlighterAttached::setSelectionEnd(int qtPos)
{
    if (m_selEnd == qtPos) return;
    m_selEnd = qtPos;
    if (m_highlighter) m_highlighter->setSelectionRange(m_selStart, m_selEnd);
    Q_EMIT selectionEndChanged();
}

}  // namespace Markoff::Live
