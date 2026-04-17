// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockItem.h"
#include <QPainter>

namespace Markoff {

BlockItem::BlockItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
}

void BlockItem::setFullySelected(bool selected)
{
    if (m_fullySelected == selected)
        return;
    m_fullySelected = selected;
    update();
}

void BlockItem::paintSelectionOverlay(QPainter *painter, const QRectF &rect)
{
    if (!m_fullySelected)
        return;
    painter->fillRect(rect, QColor(51, 153, 255, 80));
}

} // namespace Markoff
