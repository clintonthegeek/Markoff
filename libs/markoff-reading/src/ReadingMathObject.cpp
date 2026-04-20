// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "ReadingMathObject.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSizeF>
#include <QTextDocument>
#include <QTextFormat>

#include <jkqtmathtext/jkqtmathtext.h>

namespace Corbomite::ReadingView {

namespace {

struct CacheKey {
    QString latex;
    bool displayMode;
    qreal fontSize;
    bool operator==(const CacheKey &o) const
    {
        return displayMode == o.displayMode
            && qFuzzyCompare(fontSize, o.fontSize)
            && latex == o.latex;
    }
};

inline size_t qHashVal(const CacheKey &k) noexcept
{
    return ::qHash(k.latex) ^ ::qHash(k.displayMode) ^ ::qHash(k.fontSize);
}

QHash<size_t, QImage> &imageCache()
{
    static QHash<size_t, QImage> c;
    return c;
}
QMutex &cacheMutex()
{
    static QMutex m;
    return m;
}

QImage renderMath(const QString &latex, bool displayMode, qreal fontSize)
{
    if (latex.trimmed().isEmpty())
        return {};

    const qreal effective = fontSize > 0 ? fontSize : 14.0;

    CacheKey key{ latex, displayMode, effective };
    const size_t h = qHashVal(key);
    {
        QMutexLocker lock(&cacheMutex());
        auto it = imageCache().constFind(h);
        if (it != imageCache().constEnd()) return *it;
    }

    JKQTMathText mt;
    mt.useXITS();
    const qreal displayBoost = 14.0 / 12.0;
    mt.setFontSize(displayMode ? effective * displayBoost : effective);

    const QString wrapped = QStringLiteral("$") + latex + QStringLiteral("$");
    if (!mt.parse(wrapped))
        return {};

    QImage img = mt.drawIntoImage(false, Qt::transparent, 2, 2.0, 96);
    if (img.isNull())
        return {};
    img.setDevicePixelRatio(2.0);

    {
        QMutexLocker lock(&cacheMutex());
        imageCache().insert(h, img);
    }
    return img;
}

qreal fontSizeForDoc(QTextDocument *doc)
{
    if (!doc) return 0.0;
    const qreal s = doc->defaultFont().pointSizeF();
    return s > 0 ? s : 0.0;
}

} // namespace

ReadingMathObject::ReadingMathObject(QObject *parent)
    : QObject(parent)
{
}

QSizeF ReadingMathObject::intrinsicSize(QTextDocument *doc, int,
                                        const QTextFormat &format)
{
    const QString latex = format.property(SourceProperty).toString();
    const bool displayMode = format.property(DisplayProperty).toBool();
    QImage img = renderMath(latex, displayMode, fontSizeForDoc(doc));
    if (img.isNull())
        return QSizeF(14.0, 16.0);
    return QSizeF(img.width() / img.devicePixelRatio(),
                  img.height() / img.devicePixelRatio());
}

void ReadingMathObject::drawObject(QPainter *painter, const QRectF &rect,
                                   QTextDocument *doc, int,
                                   const QTextFormat &format)
{
    const QString latex = format.property(SourceProperty).toString();
    const bool displayMode = format.property(DisplayProperty).toBool();
    QImage img = renderMath(latex, displayMode, fontSizeForDoc(doc));
    if (img.isNull()) {
        painter->save();
        painter->setPen(Qt::red);
        painter->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, latex);
        painter->restore();
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->drawImage(rect, img);
    painter->restore();
}

} // namespace Corbomite::ReadingView
