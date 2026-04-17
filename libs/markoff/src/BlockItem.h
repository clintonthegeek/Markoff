// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_BLOCKITEM_H
#define MARKOFF_BLOCKITEM_H

#include "SelectableItem.h"
#include <QGraphicsObject>

namespace Markoff {

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

protected:
    /// Call from subclass paint() to draw selection overlay on top.
    void paintSelectionOverlay(QPainter *painter, const QRectF &rect);

private:
    bool m_fullySelected = false;
};

} // namespace Markoff

#endif // MARKOFF_BLOCKITEM_H
