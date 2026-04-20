// SPDX-License-Identifier: GPL-3.0-or-later
#include "CheckboxTextObject.h"

#include <markoff/Theme.h>

#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QTextFormat>

namespace Markoff {

CheckboxTextObject::CheckboxTextObject(QObject *parent)
    : QObject(parent)
    , m_checkedFill(76, 175, 80)
    , m_checkMark(255, 255, 255)
    , m_uncheckedOutline(158, 158, 158)
{
}

void CheckboxTextObject::setTheme(const Theme &theme)
{
    if (theme.paint.checkboxCheckedFill.isValid())
        m_checkedFill = theme.paint.checkboxCheckedFill;
    if (theme.paint.checkboxCheckMark.isValid())
        m_checkMark = theme.paint.checkboxCheckMark;
    if (theme.paint.checkboxUncheckedOutline.isValid())
        m_uncheckedOutline = theme.paint.checkboxUncheckedOutline;
}

static qreal checkboxSize(QTextDocument *doc)
{
    if (!doc) return 14.0;
    const qreal s = doc->defaultFont().pointSizeF();
    return s > 0 ? s : 14.0;
}

QSizeF CheckboxTextObject::intrinsicSize(QTextDocument *doc, int /*posInDocument*/,
                                          const QTextFormat & /*format*/)
{
    const qreal s = checkboxSize(doc);
    return QSizeF(s, s);
}

void CheckboxTextObject::drawObject(QPainter *painter, const QRectF &rect,
                                     QTextDocument * /*doc*/, int /*posInDocument*/,
                                     const QTextFormat &format)
{
    const bool checked = format.property(CheckedProperty).toBool();

    // Inset slightly so the box doesn't touch line edges
    const qreal inset = rect.height() * 0.1;
    QRectF box = rect.adjusted(inset, inset, -inset, -inset);
    const qreal radius = box.height() * 0.2;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (checked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_checkedFill);
        painter->drawRoundedRect(box, radius, radius);

        QPainterPath check;
        const qreal x = box.left();
        const qreal y = box.top();
        const qreal w = box.width();
        const qreal h = box.height();
        check.moveTo(x + w * 0.2, y + h * 0.5);
        check.lineTo(x + w * 0.4, y + h * 0.72);
        check.lineTo(x + w * 0.8, y + h * 0.28);

        painter->setPen(QPen(m_checkMark, box.height() * 0.15, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(check);
    } else {
        painter->setPen(QPen(m_uncheckedOutline, 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(box, radius, radius);
    }

    painter->restore();
}

} // namespace Markoff
