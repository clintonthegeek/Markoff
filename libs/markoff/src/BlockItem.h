// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_BLOCKITEM_H
#define MARKOFF_BLOCKITEM_H

#include "SelectableItem.h"
#include <QColor>
#include <QGraphicsObject>

namespace Markoff {

struct Theme;

/// Base class for non-text scene items (tables, code blocks, images).
/// Provides fully-selected overlay painting and the SelectableItem
/// interface with non-text defaults.
class BlockItem : public QGraphicsObject, public SelectableItem {
    Q_OBJECT
public:
    explicit BlockItem(QGraphicsItem *parent = nullptr);

    // SelectableItem
    QGraphicsItem *asGraphicsItem() override { return this; }
    bool isTextItem() const override { return false; }
    void setFullySelected(bool selected) override;
    bool isFullySelected() const override { return m_fullySelected; }

    /// Update the selection-overlay color used by `paintSelectionOverlay`.
    /// Typically called from `SceneCoordinator::setTheme()`. Subclasses
    /// with additional paint colors should override, call this base, and
    /// then read their own fields from `theme.paint`.
    virtual void setTheme(const Theme &theme);

protected:
    /// Call from subclass paint() to draw selection overlay on top.
    void paintSelectionOverlay(QPainter *painter, const QRectF &rect);

private:
    bool m_fullySelected = false;
    QColor m_selectionOverlay{51, 153, 255, 80};
};

} // namespace Markoff

#endif // MARKOFF_BLOCKITEM_H
