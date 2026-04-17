// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageBlockItem.h"
#include <markoff/ResourceProvider.h>

#include <QPainter>
#include <QRegularExpression>

namespace Markoff {

ImageBlockItem::ImageBlockItem(const QString &markdown, qreal maxWidth,
                               ResourceProvider *provider,
                               QGraphicsItem *parent)
    : BlockItem(parent)
    , m_markdown(markdown)
    , m_maxWidth(maxWidth)
    , m_provider(provider)
{
    parseMarkdown();
    loadImage();
}

void ImageBlockItem::parseMarkdown()
{
    const QString src = m_markdown.trimmed();

    // Obsidian wiki embed: ![[name]] or ![[name|alt]]
    if (src.startsWith(QStringLiteral("![[")) && src.endsWith(QStringLiteral("]]"))) {
        QString inner = src.mid(3, src.size() - 5);
        int pipe = inner.indexOf(QLatin1Char('|'));
        if (pipe >= 0) {
            m_imageName = inner.left(pipe);
            m_altText = inner.mid(pipe + 1);
        } else {
            m_imageName = inner;
            m_altText = inner;
        }
        return;
    }

    // Standard markdown: ![alt](url)
    static const QRegularExpression re(
        QStringLiteral(R"(^!\[([^\]]*)\]\(([^)]+)\)$)"));
    auto match = re.match(src);
    if (match.hasMatch()) {
        m_altText = match.captured(1);
        m_imageName = match.captured(2);
    }
}

void ImageBlockItem::loadImage()
{
    if (m_imageName.isEmpty()) {
        m_missing = true;
        m_displayWidth = m_maxWidth;
        m_displayHeight = 60;
        return;
    }

    // Resolve via ResourceProvider if available
    QUrl url;
    if (m_provider)
        url = m_provider->resolveImage(m_imageName);

    if (url.isValid() && url.isLocalFile())
        m_image = QImage(url.toLocalFile());

    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0) {
        m_missing = true;
        m_displayWidth = m_maxWidth;
        m_displayHeight = 60;
        return;
    }

    // Scale to fit maxWidth, preserving aspect ratio
    m_displayWidth = qMin(static_cast<qreal>(m_image.width()), m_maxWidth);
    qreal scale = m_displayWidth / m_image.width();
    m_displayHeight = m_image.height() * scale;
}

QRectF ImageBlockItem::boundingRect() const
{
    return {0, 0, m_displayWidth, m_displayHeight};
}

void ImageBlockItem::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem * /*option*/,
                           QWidget * /*widget*/)
{
    painter->save();

    if (m_missing) {
        // Placeholder: rounded rect with text
        QRectF rect = boundingRect();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor(0xcc, 0xcc, 0xcc), 1));
        painter->setBrush(QColor(0xf8, 0xf8, 0xf8));
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        painter->setPen(QColor(0x99, 0x99, 0x99));
        QString label = m_imageName.isEmpty()
            ? QStringLiteral("Image not found")
            : QStringLiteral("Image not found: %1").arg(m_imageName);
        painter->drawText(rect, Qt::AlignCenter, label);
    } else {
        // Render the image
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(boundingRect(), m_image);
    }

    painter->restore();
    paintSelectionOverlay(painter, boundingRect());
}

QString ImageBlockItem::toMarkdown() const
{
    return m_markdown;
}

} // namespace Markoff
