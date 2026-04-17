// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_IMAGEBLOCKITEM_H
#define MARKOFF_IMAGEBLOCKITEM_H

#include "BlockItem.h"
#include <QImage>
#include <QString>

namespace Markoff {

class ResourceProvider;

/// Block-level image rendered from standalone `![alt](url)` or `![[image]]`.
class ImageBlockItem : public BlockItem {
    Q_OBJECT
public:
    explicit ImageBlockItem(const QString &markdown, qreal maxWidth,
                            ResourceProvider *provider,
                            QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QString toMarkdown() const override;

private:
    void parseMarkdown();
    void loadImage();

    QString m_markdown;
    QString m_altText;
    QString m_imageName;  // raw name/url from markdown
    QImage m_image;
    qreal m_maxWidth;
    qreal m_displayWidth = 0;
    qreal m_displayHeight = 0;
    ResourceProvider *m_provider = nullptr;
    bool m_missing = false;
};

} // namespace Markoff

#endif // MARKOFF_IMAGEBLOCKITEM_H
