// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelectionScene.h"
#include "SelectableItem.h"
#include "MarkdownTextItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

namespace Markoff {

SelectionScene::SelectionScene(QObject *parent)
    : QGraphicsScene(parent)
{
}

void SelectionScene::setSelectableItems(const QList<SelectableItem *> &items)
{
    m_selectionMgr.setItems(items);
}

void SelectionScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // Only engage SelectionManager for left button. Right-click should
    // NOT clear the selection — it opens the context menu.
    if (event->button() == Qt::LeftButton) {
        bool consumed = m_selectionMgr.handleMousePress(event->scenePos(),
                                                         event->modifiers());
        if (!consumed)
            QGraphicsScene::mousePressEvent(event);
    } else {
        QGraphicsScene::mousePressEvent(event);
    }
}

void SelectionScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    SelectionMode prevMode = m_selectionMgr.mode();
    bool consumed = m_selectionMgr.handleMouseMove(event->scenePos());

    // On transition to CrossBoundary: break Qt's implicit mouse grab
    if (prevMode == SelectionMode::WithinItem
        && m_selectionMgr.mode() == SelectionMode::CrossBoundary) {
        if (mouseGrabberItem())
            mouseGrabberItem()->ungrabMouse();
    }

    if (!consumed)
        QGraphicsScene::mouseMoveEvent(event);
}

void SelectionScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        bool consumed = m_selectionMgr.handleMouseRelease(event->scenePos());
        if (!consumed)
            QGraphicsScene::mouseReleaseEvent(event);
    } else {
        QGraphicsScene::mouseReleaseEvent(event);
    }
}

void SelectionScene::keyPressEvent(QKeyEvent *event)
{
    bool consumed = m_selectionMgr.handleKeyPress(event);
    if (!consumed)
        QGraphicsScene::keyPressEvent(event);
}

} // namespace Markoff
