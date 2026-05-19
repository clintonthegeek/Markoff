// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveFindController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockRecord.h>

namespace Markoff::Live {

LiveFindController::LiveFindController(QObject *parent) : QObject(parent) {}
LiveFindController::~LiveFindController() = default;

void LiveFindController::setBlockModel(LiveBlockModel *m)   { m_blockModel = m; }
void LiveFindController::setCursorState(LiveCursorState *s) { m_cursorState = s; }

void LiveFindController::setNeedle(const QString &n)
{
    if (n == m_needle) return;
    m_needle = n;
    Q_EMIT needleChanged();
    if (m_isActive) recomputeMatches();
}

void LiveFindController::activate()
{
    if (m_isActive) return;
    m_isActive = true;
    Q_EMIT activeChanged();
    recomputeMatches();
}

void LiveFindController::deactivate()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_matches.clear();
    m_currentIndex = -1;
    Q_EMIT activeChanged();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
}

void LiveFindController::findNext()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    Q_EMIT currentMatchChanged();
    seekToCurrent();
}

void LiveFindController::findPrevious()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    Q_EMIT currentMatchChanged();
    seekToCurrent();
}

void LiveFindController::recomputeMatches()
{
    m_matches.clear();
    m_currentIndex = -1;
    if (!m_blockModel || m_needle.isEmpty()) {
        Q_EMIT matchesChanged();
        Q_EMIT currentMatchChanged();
        return;
    }
    const int rows = m_blockModel->rowCount();
    for (int r = 0; r < rows; ++r) {
        const auto &rec = m_blockModel->recordAt(r);
        const QString &text = rec.text;
        int from = 0;
        while (true) {
            const int idx = text.indexOf(m_needle, from, Qt::CaseInsensitive);
            if (idx < 0) break;
            m_matches.append(Match{r, idx, static_cast<int>(m_needle.size())});
            from = idx + 1;
        }
    }
    if (!m_matches.isEmpty()) m_currentIndex = 0;
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
    if (m_currentIndex >= 0) seekToCurrent();
}

void LiveFindController::seekToCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_matches.size()) return;
    if (!m_cursorState) return;
    const Match &m = m_matches[m_currentIndex];
    m_cursorState->requestTextCaretAtRow(m.row, m.startQtPos);
}

} // namespace Markoff::Live
