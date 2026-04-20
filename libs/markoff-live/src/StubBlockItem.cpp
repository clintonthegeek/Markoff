// SPDX-License-Identifier: GPL-3.0-or-later
#include "StubBlockItem.h"
#include <QPainter>

namespace Markoff {

StubBlockItem::StubBlockItem(const QString &markdown, qreal width, qreal height,
                             QGraphicsItem *parent)
    : BlockItem(parent)
    , m_markdown(markdown)
    , m_width(width)
    , m_height(height)
{
}

QRectF StubBlockItem::boundingRect() const
{
    return {0, 0, m_width, m_height};
}

void StubBlockItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem * /*option*/,
                          QWidget * /*widget*/)
{
    painter->fillRect(boundingRect(), Qt::lightGray);
    paintSelectionOverlay(painter, boundingRect());
}

QString StubBlockItem::toMarkdown() const
{
    return m_markdown;
}

} // namespace Markoff
