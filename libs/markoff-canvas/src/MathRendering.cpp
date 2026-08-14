// SPDX-License-Identifier: GPL-3.0-or-later
#include "MathRendering.h"

#include <jkqtmathtext/jkqtmathtext.h>

namespace Markoff::Canvas::Detail {

QPixmap renderMathPixmap(const QString &latex, qreal pixelSize,
                         const QColor &foreground, const QColor &backgroundColor)
{
    if (latex.isEmpty())
        return QPixmap();

    JKQTMathText mt;
    mt.useXITS();
    mt.setFontSizePixels(pixelSize);
    mt.setFontColor(foreground);
    if (!mt.parse(latex))
        return QPixmap();
    return mt.drawIntoPixmap(false, backgroundColor);
}

QString stripMathDelimiters(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("$$")) && t.endsWith(QStringLiteral("$$")) && t.size() >= 4)
        return t.mid(2, t.size() - 4).trimmed();
    if (t.startsWith(QLatin1Char('$')) && t.endsWith(QLatin1Char('$')) && t.size() >= 2)
        return t.mid(1, t.size() - 2).trimmed();
    return t;
}

}  // namespace Markoff::Canvas::Detail
