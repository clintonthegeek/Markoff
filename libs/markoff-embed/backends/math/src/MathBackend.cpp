// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/embed/math/MathBackend.h>

#include <QColor>
#include <QImage>
#include <QPixmap>

#include <jkqtmathtext/jkqtmathtext.h>

namespace Markoff::Embed {

QString stripMathDelimiters(const QString &source)
{
    QString latex = source.trimmed();
    if (latex.startsWith(QStringLiteral("$$")) && latex.endsWith(QStringLiteral("$$"))
        && latex.size() >= 4) {
        latex = latex.mid(2, latex.size() - 4);
    } else if (latex.startsWith(QLatin1Char('$')) && latex.endsWith(QLatin1Char('$'))
               && latex.size() >= 2) {
        latex = latex.mid(1, latex.size() - 2);
    }
    return latex.trimmed();
}

RenderedBlock MathBackend::render(const RenderRequest &request)
{
    RenderedBlock block;
    const QString latex = stripMathDelimiters(request.source);
    if (latex.isEmpty()) {
        block.kind = RenderedBlock::Kind::Error;
        block.error = QStringLiteral("empty math source");
        block.fallbackText = request.source;
        return block;
    }

    JKQTMathText math;
    math.useXITS();
    const qreal pixelSize = request.fontPixelSize > 0.0
        ? request.fontPixelSize
        : 16.0 * qMax<qreal>(0.25, request.fontScale);
    // jkqtmathtext lays out at the pixel size it is given and only tags the
    // result with a DPR, so render at size x DPR to get real HiDPI pixels and
    // then divide the size back out for the DIP-space preferred size.
    const qreal dpr = qMax(1.0, request.devicePixelRatio);
    math.setFontSizePixels(pixelSize * dpr);
    math.setFontColor(request.foreground.isValid() ? request.foreground : QColor(Qt::black));
    if (!math.parse(latex)) {
        block.kind = RenderedBlock::Kind::Error;
        block.error = QStringLiteral("jkqtmathtext could not parse the expression");
        block.fallbackText = request.source;
        return block;
    }

    // jkqtmathtext has no transparent-background path; the caller supplies the
    // colour its block paints, so the raster never shows a seam.
    const QColor background = request.background.isValid()
        ? request.background : QColor(Qt::white);
    const QPixmap pixmap = math.drawIntoPixmap(false, background, 0, dpr);
    if (pixmap.isNull()) {
        block.kind = RenderedBlock::Kind::Error;
        block.error = QStringLiteral("jkqtmathtext produced no pixmap");
        block.fallbackText = request.source;
        return block;
    }

    block.kind = RenderedBlock::Kind::Image;
    block.image = pixmap.toImage();
    block.image.setDevicePixelRatio(dpr);
    block.preferredSize = QSizeF(pixmap.width() / dpr, pixmap.height() / dpr);
    block.fallbackText = request.source;
    return block;
}

}  // namespace Markoff::Embed
