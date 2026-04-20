// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SELECTIONMANAGER_H
#define MARKOFF_SELECTIONMANAGER_H

#include <QObject>
#include <QList>
#include <QPointF>

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QMimeData;

namespace Markoff {

class SelectableItem;

enum class SelectionMode { None, WithinItem, CrossBoundary };

class SelectionManager : public QObject {
    Q_OBJECT
public:
    explicit SelectionManager(QObject *parent = nullptr);

    /// Set the ordered list of items in the scene (top to bottom by Y).
    void setItems(const QList<SelectableItem *> &items);

    /// Mouse event handlers. Return true if the event was consumed
    /// (caller should NOT call the base class).
    bool handleMousePress(const QPointF &scenePos, Qt::KeyboardModifiers modifiers);
    bool handleMouseMove(const QPointF &scenePos);
    bool handleMouseRelease(const QPointF &scenePos);

    /// Key event handler for Escape (clear cross-boundary selection).
    bool handleKeyPress(QKeyEvent *event);

    /// Select all items (cross-boundary selection spanning the entire document).
    void selectAll();

    /// Create MIME data from current selection for clipboard.
    QMimeData *createMimeData() const;

    /// Clear all selection state across all items.
    void clearSelection();

    /// Whether there is any active cross-boundary selection.
    bool hasSelection() const;

    /// Force-extend the selection to a scene position during auto-scroll.
    /// Works regardless of current mode — handles within-item, boundary
    /// crossing, and cross-boundary extension in one call.
    void extendSelectionTo(const QPointF &scenePos);

    /// Enter or advance keyboard-driven cross-boundary selection.
    /// Called when Shift+Arrow hits an item boundary.
    void beginOrExtendKeyboardSelection(SelectableItem *anchorItem,
                                         int anchorTextPos,
                                         SelectableItem *targetItem,
                                         int targetTextPos);

    /// Access current selection endpoints (for advancing keyboard selection).
    SelectableItem *anchorItem() const { return m_anchorItem; }
    SelectableItem *currentItem() const { return m_currentItem; }

    /// Current mode (for testing and UI feedback).
    SelectionMode mode() const { return m_mode; }

Q_SIGNALS:
    void modeChanged(Markoff::SelectionMode mode);

private:
    void setMode(SelectionMode mode);
    void applySelectionSkipCurrent();

    void applySelection();
    SelectableItem *itemAt(const QPointF &scenePos) const;
    QString serializeAsMarkdown() const;

    SelectionMode m_mode = SelectionMode::None;

    SelectableItem *m_anchorItem = nullptr;
    int m_anchorTextPos = -1;

    SelectableItem *m_currentItem = nullptr;
    int m_currentTextPos = -1;

    bool m_mouseDragging = false;
    QList<SelectableItem *> m_items;
};

} // namespace Markoff

#endif // MARKOFF_SELECTIONMANAGER_H
