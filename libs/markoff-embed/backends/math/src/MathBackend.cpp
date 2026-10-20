// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/embed/math/MathBackend.h>

#include <QColor>
#include <QImage>

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
    // The font is set at DIP size and the raster is scaled by the DPR, which
    // is what drawIntoImage() is for: it lays the text out in DIP units and
    // hands back a DPR-tagged image, so the glyphs stay crisp and the
    // logical size is the DIP size. (Pre-multiplying the font by the DPR
    // instead — the old pixmap path's shape — would double-count the scale
    // and report twice the logical size.)
    const qreal dpr = qMax(1.0, request.devicePixelRatio);
    math.setFontSizePixels(pixelSize);
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
    // A QImage, not a QPixmap: this runs on the embed runtime's worker
    // thread, and QPixmap is a GUI-thread resource (Qt's own rule). The
    // image path draws the same pixels and carries the DPR itself, so the
    // artifact that crosses back to the GUI thread is unchanged.
    const QImage image = math.drawIntoImage(false, background, 0, dpr);
    if (image.isNull()) {
        block.kind = RenderedBlock::Kind::Error;
        block.error = QStringLiteral("jkqtmathtext produced no image");
        block.fallbackText = request.source;
        return block;
    }

    block.kind = RenderedBlock::Kind::Image;
    block.image = image;
    block.image.setDevicePixelRatio(dpr);
    block.preferredSize = QSizeF(image.width() / dpr, image.height() / dpr);
    block.fallbackText = request.source;
    return block;
}

}  // namespace Markoff::Embed
