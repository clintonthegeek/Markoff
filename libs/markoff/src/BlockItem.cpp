// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockItem.h"
#include <markoff/Theme.h>
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

void BlockItem::setTheme(const Theme &theme)
{
    if (theme.paint.blockSelectionOverlay.isValid()) {
        m_selectionOverlay = theme.paint.blockSelectionOverlay;
        if (m_fullySelected) update();
    }
}

void BlockItem::paintSelectionOverlay(QPainter *painter, const QRectF &rect)
{
    if (!m_fullySelected)
        return;
    painter->fillRect(rect, m_selectionOverlay);
}

} // namespace Markoff
