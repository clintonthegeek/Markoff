// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>

namespace Markoff::Live {

LiveSelectionView::LiveSelectionView(QObject *parent) : QObject(parent) {}

bool LiveSelectionView::hasSelection() const
{
    return m_cursorState && m_cursorState->hasSelection();
}

void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    if (!m_cursorState || !m_cursorState->model()) return;
    if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
    const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
    m_cursorState->syncFromTextEdit(anchor, qtPos);
    m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::extend(int blockIndex, int qtPos)
{
    if (!m_cursorState || !m_cursorState->model()) return;
    if (blockIndex < 0 || blockIndex >= m_cursorState->model()->rowCount()) return;
    const auto anchor = m_cursorState->model()->recordAt(blockIndex).blockAnchor;
    m_cursorState->syncFromTextEdit(anchor, qtPos);
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::clear()
{
    if (!m_cursorState) return;
    m_cursorState->clearSelectionAnchor();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::selectAll()
{
    if (!m_cursorState) return;
    m_cursorState->selectAllBlocks();
    m_cursorState->syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::deleteSelection()
{
    if (!m_cursorState) return;
    m_cursorState->deleteSelectionRange();
    Q_EMIT selectionChanged();
}

QPoint LiveSelectionView::rangeForBlock(int blockIndex) const
{
    if (!m_cursorState) return QPoint(-1, -1);
    return m_cursorState->selectionRangeForBlock(blockIndex);
}

void LiveSelectionView::copyToClipboard() const
{
    if (!m_cursorState) return;
    m_cursorState->copySelectionToClipboard();
}

int LiveSelectionView::anchorBlock() const
{
    if (!m_cursorState) return -1;
    const auto a = m_cursorState->selectionAnchor();
    if (!a) return -1;
    return m_cursorState->rowForBlock(a->block);
}

int LiveSelectionView::anchorQtPos() const
{
    if (!m_cursorState) return -1;
    const auto a = m_cursorState->selectionAnchor();
    if (!a) return -1;
    return static_cast<int>(a->qtPos);
}

int LiveSelectionView::activeBlock() const
{
    if (!m_cursorState) return -1;
    const auto tc = m_cursorState->currentTextCaret();
    if (!tc) return -1;
    return m_cursorState->rowForBlock(tc->block);
}

int LiveSelectionView::activeQtPos() const
{
    if (!m_cursorState) return -1;
    const auto tc = m_cursorState->currentTextCaret();
    if (!tc) return -1;
    return static_cast<int>(tc->cachedQtPos);
}

}  // namespace Markoff::Live
