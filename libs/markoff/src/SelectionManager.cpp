// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelectionManager.h"
#include "SelectableItem.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QGraphicsItem>
#include <limits>

namespace Markoff {

SelectionManager::SelectionManager(QObject *parent)
    : QObject(parent)
{
}

void SelectionManager::setMode(SelectionMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    emit modeChanged(m_mode);
}

void SelectionManager::setItems(const QList<SelectableItem *> &items)
{
    m_items = items;
}

bool SelectionManager::handleMousePress(const QPointF &scenePos,
                                        Qt::KeyboardModifiers modifiers)
{
    SelectableItem *pressedItem = itemAt(scenePos);

    // Shift+Click: extend from existing anchor to new position
    if (modifiers & Qt::ShiftModifier && m_anchorItem) {
        m_currentItem = pressedItem ? pressedItem : m_anchorItem;
        m_currentTextPos = m_currentItem->hitTest(scenePos);
        setMode(SelectionMode::CrossBoundary);
        applySelection();
        return true; // consumed — we handle this entirely
    }

    // Click without Shift: clear any existing cross-boundary selection
    if (m_mode == SelectionMode::CrossBoundary || hasSelection()) {
        clearSelection();
    }

    m_anchorItem = pressedItem;
    if (!m_anchorItem) {
        setMode(SelectionMode::None);
        return false;
    }
    m_anchorTextPos = m_anchorItem->hitTest(scenePos);
    m_mouseDragging = true;
    setMode(SelectionMode::WithinItem);
    return false; // let Qt handle the press normally
}

bool SelectionManager::handleMouseMove(const QPointF &scenePos)
{
    if (m_mode == SelectionMode::None)
        return false;
    if (!m_mouseDragging)
        return false; // keyboard selection in progress — ignore mouse

    SelectableItem *hoverItem = itemAt(scenePos);

    if (m_mode == SelectionMode::WithinItem) {
        if (!m_anchorItem)
            return false;
        // Check if we've left the anchor item
        QGraphicsItem *gi = m_anchorItem->asGraphicsItem();
        if (gi->boundingRect().contains(gi->mapFromScene(scenePos)))
            return false; // still within item, let Qt handle
        // Transition to CrossBoundary
        setMode(SelectionMode::CrossBoundary);
    }

    // CrossBoundary mode
    m_currentItem = hoverItem ? hoverItem : m_anchorItem;
    m_currentTextPos = m_currentItem->hitTest(scenePos);
    applySelection();
    return true; // consumed — don't call base class
}

bool SelectionManager::handleMouseRelease(const QPointF &scenePos)
{
    Q_UNUSED(scenePos);
    if (m_mode == SelectionMode::CrossBoundary) {
        // Keep the cross-boundary selection live after mouse release.
        // The user can still Ctrl+C. Cleared on next click or Escape.
        m_mouseDragging = false;
        return true; // consumed (don't send release to base class)
    }
    m_mouseDragging = false;
    setMode(SelectionMode::None);
    return false;
}

bool SelectionManager::handleKeyPress(QKeyEvent *event)
{
    // Escape: clear cross-boundary selection (modal state exit, not a
    // user-remappable shortcut — stays here rather than becoming a QAction)
    if (event->key() == Qt::Key_Escape && m_mode == SelectionMode::CrossBoundary) {
        clearSelection();
        return true;
    }

    return false;
}

void SelectionManager::selectAll()
{
    if (m_items.isEmpty())
        return;
    m_anchorItem = m_items.first();
    m_anchorTextPos = 0;
    m_currentItem = m_items.last();
    m_currentTextPos = m_currentItem->isTextItem()
        ? m_currentItem->documentLength()
        : -1;
    setMode(SelectionMode::CrossBoundary);
    applySelection();
}

QMimeData *SelectionManager::createMimeData() const
{
    auto *data = new QMimeData;
    data->setText(serializeAsMarkdown());
    return data;
}

void SelectionManager::clearSelection()
{
    for (auto *item : m_items) {
        if (item->isTextItem())
            item->clearSelection();
        else
            item->setFullySelected(false);
    }
    m_anchorItem = nullptr;
    m_currentItem = nullptr;
    m_anchorTextPos = -1;
    m_currentTextPos = -1;
    m_mouseDragging = false;
    setMode(SelectionMode::None);
}

void SelectionManager::extendSelectionTo(const QPointF &scenePos)
{
    if (!m_anchorItem)
        return;

    // Force into CrossBoundary mode if not already
    if (m_mode != SelectionMode::CrossBoundary)
        setMode(SelectionMode::CrossBoundary);

    SelectableItem *target = itemAt(scenePos);
    if (!target)
        return;

    m_currentItem = target;
    m_currentTextPos = target->hitTest(scenePos);
    applySelection();
}

void SelectionManager::beginOrExtendKeyboardSelection(
    SelectableItem *anchorItem, int anchorTextPos,
    SelectableItem *targetItem, int targetTextPos)
{
    // anchorTextPos == -1 means "keep existing anchor" (extending)
    if (anchorTextPos >= 0) {
        m_anchorItem = anchorItem;
        m_anchorTextPos = anchorTextPos;
    }
    // Update current endpoint
    m_currentItem = targetItem;
    m_currentTextPos = targetTextPos;
    setMode(SelectionMode::CrossBoundary);
    // Don't call applySelection() — the caller (SceneCoordinator) manages
    // visuals directly. The current item's selection is driven by its
    // TextControl as the user presses Shift+Arrow.
}

bool SelectionManager::hasSelection() const
{
    if (m_mode == SelectionMode::CrossBoundary && m_anchorItem && m_currentItem)
        return true;
    for (auto *item : m_items) {
        if (item->isTextItem() && !item->selectedMarkdown().isEmpty())
            return true;
        if (!item->isTextItem() && item->isFullySelected())
            return true;
    }
    return false;
}

void SelectionManager::applySelection()
{
    if (!m_anchorItem || !m_currentItem)
        return;

    int anchorIdx = m_items.indexOf(m_anchorItem);
    int currentIdx = m_items.indexOf(m_currentItem);
    if (anchorIdx < 0 || currentIdx < 0)
        return;

    bool forward = currentIdx >= anchorIdx;
    int lo = qMin(anchorIdx, currentIdx);
    int hi = qMax(anchorIdx, currentIdx);

    for (int i = 0; i < m_items.size(); ++i) {
        SelectableItem *item = m_items[i];

        if (i < lo || i > hi) {
            if (item->isTextItem())
                item->clearSelection();
            else
                item->setFullySelected(false);
        } else if (i == anchorIdx && i == currentIdx) {
            if (item->isTextItem())
                item->setSelection(m_anchorTextPos, m_currentTextPos);
            else
                item->setFullySelected(true);
        } else if (i == anchorIdx) {
            if (item->isTextItem()) {
                int end = item->documentLength();
                if (forward)
                    item->setSelection(m_anchorTextPos, end);
                else
                    item->setSelection(m_anchorTextPos, 0);
            } else {
                item->setFullySelected(true);
            }
        } else if (i == currentIdx) {
            if (item->isTextItem()) {
                int end = item->documentLength();
                if (forward)
                    item->setSelection(0, m_currentTextPos);
                else
                    item->setSelection(end, m_currentTextPos);
            } else {
                item->setFullySelected(true);
            }
        } else {
            if (item->isTextItem()) {
                int end = item->documentLength();
                item->setSelection(0, end);
            } else {
                item->setFullySelected(true);
            }
        }
    }
}

SelectableItem *SelectionManager::itemAt(const QPointF &scenePos) const
{
    if (m_items.isEmpty())
        return nullptr;

    // Direct hit
    for (auto *item : m_items) {
        QGraphicsItem *gi = item->asGraphicsItem();
        if (gi->sceneBoundingRect().contains(scenePos))
            return item;
    }

    // No hit — find nearest item by Y distance
    qreal y = scenePos.y();
    SelectableItem *nearest = m_items.first();
    qreal nearestDist = std::numeric_limits<qreal>::max();

    for (auto *item : m_items) {
        QRectF rect = item->asGraphicsItem()->sceneBoundingRect();
        qreal dist = 0;
        if (y < rect.top())
            dist = rect.top() - y;
        else if (y > rect.bottom())
            dist = y - rect.bottom();
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = item;
        }
    }
    return nearest;
}

QString SelectionManager::serializeAsMarkdown() const
{
    if (!m_anchorItem || !m_currentItem)
        return {};

    int anchorIdx = m_items.indexOf(m_anchorItem);
    int currentIdx = m_items.indexOf(m_currentItem);
    if (anchorIdx < 0 || currentIdx < 0)
        return {};

    int lo = qMin(anchorIdx, currentIdx);
    int hi = qMax(anchorIdx, currentIdx);
    QString result;

    for (int i = lo; i <= hi; ++i) {
        if (i > lo) {
            bool prevIsBlock = !m_items[i - 1]->isTextItem();
            bool currIsBlock = !m_items[i]->isTextItem();
            result += (prevIsBlock || currIsBlock)
                ? QStringLiteral("\n\n") : QStringLiteral("\n");
        }
        SelectableItem *item = m_items[i];
        if (i == anchorIdx || i == currentIdx) {
            if (item->isTextItem())
                result += item->selectedMarkdown();
            else
                result += item->toMarkdown();
        } else {
            if (item->isTextItem())
                result += item->allMarkdown();
            else
                result += item->toMarkdown();
        }
    }
    return result;
}

} // namespace Markoff
