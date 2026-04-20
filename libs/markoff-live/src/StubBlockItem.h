// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_STUBBLOCKITEM_H
#define MARKOFF_STUBBLOCKITEM_H

#include "BlockItem.h"

namespace Markoff {

/// Minimal BlockItem for testing. Fixed size, stores a markdown string.
class StubBlockItem : public BlockItem {
    Q_OBJECT
public:
    explicit StubBlockItem(const QString &markdown, qreal width, qreal height,
                           QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QString toMarkdown() const override;

private:
    QString m_markdown;
    qreal m_width;
    qreal m_height;
};

} // namespace Markoff

#endif // MARKOFF_STUBBLOCKITEM_H
