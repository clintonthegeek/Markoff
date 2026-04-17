// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SELECTIONSCENE_H
#define MARKOFF_SELECTIONSCENE_H

#include "SelectionManager.h"
#include <QGraphicsScene>

namespace Markoff {

class SelectableItem;

/// QGraphicsScene subclass that wires SelectionManager to real mouse
/// events. Handles ungrabMouse() on cross-boundary transition and
/// delegates key events for Ctrl+C, Ctrl+A, Escape.
class SelectionScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit SelectionScene(QObject *parent = nullptr);

    /// Set the ordered list of selectable items (top to bottom by Y).
    void setSelectableItems(const QList<SelectableItem *> &items);

    SelectionManager *selectionManager() { return &m_selectionMgr; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    SelectionManager m_selectionMgr{this};
};

} // namespace Markoff

#endif // MARKOFF_SELECTIONSCENE_H
