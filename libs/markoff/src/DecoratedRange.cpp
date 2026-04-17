// SPDX-License-Identifier: GPL-3.0-or-later
#include "DecoratedRange.h"
#include <QHash>

namespace Markoff {

QColor DecoratedRange::colorForCalloutType(const QString &type)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("note"),     QColor(0x44, 0x8a, 0xff)},
        {QStringLiteral("info"),     QColor(0x44, 0x8a, 0xff)},
        {QStringLiteral("todo"),     QColor(0x44, 0x8a, 0xff)},
        {QStringLiteral("abstract"), QColor(0x00, 0xb8, 0xd4)},
        {QStringLiteral("summary"),  QColor(0x00, 0xb8, 0xd4)},
        {QStringLiteral("tldr"),     QColor(0x00, 0xb8, 0xd4)},
        {QStringLiteral("tip"),      QColor(0x00, 0xbf, 0xa5)},
        {QStringLiteral("hint"),     QColor(0x00, 0xbf, 0xa5)},
        {QStringLiteral("important"),QColor(0x00, 0xbf, 0xa5)},
        {QStringLiteral("success"),  QColor(0x00, 0xc8, 0x53)},
        {QStringLiteral("check"),    QColor(0x00, 0xc8, 0x53)},
        {QStringLiteral("done"),     QColor(0x00, 0xc8, 0x53)},
        {QStringLiteral("question"), QColor(0xff, 0xab, 0x00)},
        {QStringLiteral("help"),     QColor(0xff, 0xab, 0x00)},
        {QStringLiteral("faq"),      QColor(0xff, 0xab, 0x00)},
        {QStringLiteral("warning"),  QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("caution"),  QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("attention"),QColor(0xff, 0x91, 0x00)},
        {QStringLiteral("failure"),  QColor(0xff, 0x52, 0x52)},
        {QStringLiteral("fail"),     QColor(0xff, 0x52, 0x52)},
        {QStringLiteral("missing"),  QColor(0xff, 0x52, 0x52)},
        {QStringLiteral("danger"),   QColor(0xff, 0x17, 0x44)},
        {QStringLiteral("error"),    QColor(0xff, 0x17, 0x44)},
        {QStringLiteral("bug"),      QColor(0xff, 0x17, 0x44)},
        {QStringLiteral("example"),  QColor(0x7c, 0x4d, 0xff)},
        {QStringLiteral("quote"),    QColor(0x9e, 0x9e, 0x9e)},
        {QStringLiteral("cite"),     QColor(0x9e, 0x9e, 0x9e)},
    };
    return colors.value(type.toLower(), QColor(0x44, 0x8a, 0xff));
}

} // namespace Markoff
