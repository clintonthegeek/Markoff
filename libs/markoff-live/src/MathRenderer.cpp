// SPDX-License-Identifier: GPL-3.0-or-later
#include "MathRenderer.h"

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <jkqtmathtext/jkqtmathtext.h>

namespace Markoff {

namespace {

struct CacheKey {
    QString latex;
    bool displayMode;
    qreal fontSize;
    qreal dpr;
    bool operator==(const CacheKey &o) const {
        return displayMode == o.displayMode
            && qFuzzyCompare(fontSize, o.fontSize)
            && qFuzzyCompare(dpr, o.dpr)
            && latex == o.latex;
    }
};

size_t qHash(const CacheKey &k, size_t seed = 0) noexcept
{
    return ::qHash(k.latex, seed) ^ ::qHash(k.displayMode)
         ^ ::qHash(k.fontSize) ^ ::qHash(k.dpr);
}

QHash<CacheKey, QImage> &imageCache()
{
    static QHash<CacheKey, QImage> cache;
    return cache;
}

QMutex &cacheMutex()
{
    static QMutex m;
    return m;
}

QImage renderUncached(const QString &latex, bool displayMode,
                       qreal fontSize, qreal dpr)
{
    JKQTMathText mt;
    mt.useXITS();
    const qreal effective = fontSize > 0 ? fontSize : MathRenderer::DefaultInlineFontSize;
    mt.setFontSize(displayMode ? effective * MathRenderer::DefaultDisplayBoost
                                : effective);

    // JKQTMathText wants the LaTeX wrapped in $...$ for both inline and
    // display modes; the displayMode flag here only adjusts sizing.
    const QString wrapped = QStringLiteral("$") + latex + QStringLiteral("$");
    if (!mt.parse(wrapped))
        return {};

    // Render at dpr × base DPI for supersampling. `resolution_factor`
    // passed to JKQTMathText is also bumped to dpr so glyph strokes use
    // more pixel detail before the final rasterization — without this,
    // JKQTMathText renders at 96 DPI internally and setting dpr alone
    // just changes how Qt *tags* the image, not how it's actually drawn.
    QImage img = mt.drawIntoImage(false, Qt::transparent, 2,
                                    /*resolution_factor=*/dpr,
                                    /*dpi=*/96);
    if (img.isNull())
        return {};

    img.setDevicePixelRatio(dpr);
    return img;
}

} // namespace

QImage MathRenderer::render(const QString &latex, bool displayMode,
                             qreal fontSize, qreal dpr)
{
    if (latex.isEmpty())
        return {};

    const CacheKey key{latex, displayMode, fontSize, dpr};
    {
        QMutexLocker lock(&cacheMutex());
        auto it = imageCache().constFind(key);
        if (it != imageCache().constEnd())
            return *it;
    }

    QImage img = renderUncached(latex, displayMode, fontSize, dpr);

    {
        QMutexLocker lock(&cacheMutex());
        imageCache().insert(key, img);
    }
    return img;
}

QString MathRenderer::renderToDataUri(const QString &latex, bool displayMode,
                                       qreal fontSize)
{
    QImage img = render(latex, displayMode, fontSize, 2.0);
    if (img.isNull())
        return {};

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(ba.toBase64());
}

void MathRenderer::clearCache()
{
    QMutexLocker lock(&cacheMutex());
    imageCache().clear();
}

} // namespace Markoff
