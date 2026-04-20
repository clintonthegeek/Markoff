// SPDX-License-Identifier: GPL-3.0-or-later
#include "MathTextObject.h"
#include "MathRenderer.h"

#include <QImage>
#include <QPainter>
#include <QSizeF>
#include <QTextDocument>
#include <QTextFormat>

namespace Markoff {

MathTextObject::MathTextObject(QObject *parent)
    : QObject(parent)
{
}

// Pull the font point size from the document's default font, falling back
// to MathRenderer's default. The document is the natural place to read this
// since the editor's setTheme() updates the default font on each text item.
static qreal fontSizeForDoc(QTextDocument *doc)
{
    if (!doc) return 0;
    const qreal s = doc->defaultFont().pointSizeF();
    return s > 0 ? s : 0;
}

QSizeF MathTextObject::intrinsicSize(QTextDocument *doc, int /*posInDocument*/,
                                      const QTextFormat &format)
{
    const QString latex = format.property(SourceProperty).toString();
    const bool displayMode = format.property(DisplayProperty).toBool();

    QImage img = MathRenderer::render(latex, displayMode, fontSizeForDoc(doc));
    if (img.isNull()) {
        // Fallback: claim a small placeholder size so the line doesn't collapse.
        return QSizeF(12.0, 14.0);
    }
    // intrinsicSize is in document (logical) coordinates. The image was
    // rendered with devicePixelRatio set, so its logical size is
    // pixelSize / dpr.
    return QSizeF(img.width() / img.devicePixelRatio(),
                  img.height() / img.devicePixelRatio());
}

void MathTextObject::drawObject(QPainter *painter, const QRectF &rect,
                                 QTextDocument *doc, int /*posInDocument*/,
                                 const QTextFormat &format)
{
    const QString latex = format.property(SourceProperty).toString();
    const bool displayMode = format.property(DisplayProperty).toBool();

    QImage img = MathRenderer::render(latex, displayMode, fontSizeForDoc(doc));
    if (img.isNull()) {
        // Fallback: paint the raw source as plain text so the user can see
        // something is wrong without breaking the layout.
        painter->save();
        painter->setPen(Qt::red);
        painter->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, latex);
        painter->restore();
        return;
    }
    // Enable smooth transforms so downsampling from our 2x cached image to
    // the logical rect doesn't produce nearest-neighbor blockiness on
    // regular (1x) displays. Also turn on Antialiasing for the draw call.
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->drawImage(rect, img);
    painter->restore();
}

} // namespace Markoff
