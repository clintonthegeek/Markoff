// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/SearchController.h>

#include <QRegularExpression>

#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

namespace Markoff {

SearchController::SearchController(MarkoffDocument *doc,
                                   SearchAdapter *adapter,
                                   QObject *parent)
    : QObject(parent), m_doc(doc), m_adapter(adapter)
{
    connect(m_doc, &MarkoffDocument::contentsChanged,
            this, [this](qsizetype, qsizetype, qsizetype) {
                recomputeMatches();
            });
}

SearchController::~SearchController() = default;

void SearchController::setFlags(Flags f)
{
    m_flags = f;
    recomputeMatches();
}

SearchController::Flags SearchController::flags() const { return m_flags; }

void SearchController::setQuery(const QString &q)
{
    m_query = q;
    recomputeMatches();
}

QString SearchController::query() const { return m_query; }
int SearchController::matchCount() const { return m_matches.size(); }
int SearchController::currentIndex() const { return m_current; }
const QVector<TextSpan> &SearchController::matches() const { return m_matches; }

void SearchController::recomputeMatches()
{
    m_matches.clear();

    if (m_query.isEmpty()) {
        m_current = -1;
        m_adapter->clearMatchHighlight();
        Q_EMIT matchesChanged();
        Q_EMIT currentMatchChanged(m_current);
        return;
    }

    const QString text = m_doc->toMarkdown();

    if (m_flags.regex) {
        QRegularExpression::PatternOptions opts =
            QRegularExpression::NoPatternOption;
        if (!m_flags.caseSensitive)
            opts |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(m_query, opts);
        if (!re.isValid()) {
            m_current = -1;
            m_adapter->clearMatchHighlight();
            Q_EMIT matchesChanged();
            Q_EMIT currentMatchChanged(m_current);
            return;
        }
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;  // avoid infinite hits
            m_matches.append({m.capturedStart(), m.capturedLength()});
        }
    } else {
        const Qt::CaseSensitivity cs = m_flags.caseSensitive
            ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int from = 0;
        while (true) {
            int idx = text.indexOf(m_query, from, cs);
            if (idx < 0) break;

            if (m_flags.wholeWord) {
                const bool leftBoundary =
                    idx == 0 || !text.at(idx - 1).isLetterOrNumber();
                const int endIdx = idx + m_query.size();
                const bool rightBoundary =
                    endIdx == text.size() || !text.at(endIdx).isLetterOrNumber();
                if (!leftBoundary || !rightBoundary) {
                    from = idx + 1;
                    continue;
                }
            }

            m_matches.append({idx, m_query.size()});
            from = idx + m_query.size();
        }
    }

    if (m_matches.isEmpty()) {
        m_current = -1;
    } else {
        const int cursor = m_adapter->cursorSourceOffset();
        m_current = 0;
        for (int i = 0; i < m_matches.size(); ++i) {
            if (m_matches[i].offset >= cursor) {
                m_current = i;
                break;
            }
        }
    }

    notifyAdapterHighlight();
    scrollToCurrent();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged(m_current);
}

void SearchController::notifyAdapterHighlight()
{
    m_adapter->highlightMatches(m_matches);
}

void SearchController::scrollToCurrent()
{
    if (m_current >= 0 && m_current < m_matches.size())
        m_adapter->scrollMatchIntoView(m_matches[m_current]);
}

void SearchController::next()
{
    if (m_matches.isEmpty()) return;
    const int last = m_matches.size() - 1;
    if (m_current < last) {
        ++m_current;
    } else if (m_flags.wrap) {
        m_current = 0;
    } else {
        return;
    }
    scrollToCurrent();
    Q_EMIT currentMatchChanged(m_current);
}

void SearchController::prev()
{
    if (m_matches.isEmpty()) return;
    if (m_current > 0) {
        --m_current;
    } else if (m_flags.wrap) {
        m_current = m_matches.size() - 1;
    } else {
        return;
    }
    scrollToCurrent();
    Q_EMIT currentMatchChanged(m_current);
}

}  // namespace Markoff
