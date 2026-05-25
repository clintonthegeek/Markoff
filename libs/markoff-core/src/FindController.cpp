// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/FindController.h>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff {

FindController::FindController(MarkoffDocument *doc, QObject *parent)
    : QObject(parent), m_doc(doc)
{
    if (m_doc) {
        connect(m_doc, &MarkoffDocument::d2DocumentChanged,
                this, [this]() { if (m_isActive) recomputeMatches(); });
    }
}

FindController::~FindController() = default;

void FindController::setNeedle(const QString &n)
{
    if (n == m_needle) return;
    m_needle = n;
    Q_EMIT needleChanged();
    if (m_isActive) recomputeMatches();
}

void FindController::setFlags(SearchEngine::FindFlags f)
{
    if (f == m_flags) return;
    m_flags = f;
    if (m_isActive) recomputeMatches();
}

void FindController::activate()
{
    if (m_isActive) return;
    m_isActive = true;
    Q_EMIT activeChanged();
    recomputeMatches();
}

void FindController::deactivate()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_matches.clear();
    m_currentIndex = -1;
    Q_EMIT activeChanged();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
}

void FindController::findNext()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    Q_EMIT currentMatchChanged();
    Q_EMIT navigationRequested(m_matches[m_currentIndex]);
}

void FindController::findPrevious()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    Q_EMIT currentMatchChanged();
    Q_EMIT navigationRequested(m_matches[m_currentIndex]);
}

void FindController::recomputeMatches()
{
    const int prevCurrent = m_currentIndex;
    m_matches.clear();
    if (!m_doc || m_needle.isEmpty()) {
        m_currentIndex = -1;
        Q_EMIT matchesChanged();
        if (prevCurrent != m_currentIndex) Q_EMIT currentMatchChanged();
        return;
    }
    const QList<SearchHit> hits =
        SearchEngine::findByBlock(*m_doc, m_needle, m_flags);
    m_matches.reserve(hits.size());
    for (const SearchHit &h : hits) {
        m_matches.append(Match{ h.blockId, h.matchStart, h.matchLen });
    }
    m_currentIndex = m_matches.isEmpty() ? -1 : 0;
    Q_EMIT matchesChanged();
    if (prevCurrent != m_currentIndex) Q_EMIT currentMatchChanged();
    // No navigationRequested here. Typing must never seek.
}

}  // namespace Markoff
