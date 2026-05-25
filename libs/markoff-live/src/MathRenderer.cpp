// SPDX-License-Identifier: GPL-3.0-or-later
#include "MathRenderer.h"
#include <jkqtmathtext/jkqtmathtext.h>

namespace Markoff::Live {

MathRenderer::MathRenderer(QObject *parent) : QObject(parent) {}

QPixmap MathRenderer::render(const QString &latex, bool /*displayMode*/, qreal pointSize) const
{
    JKQTMathText mt;
    mt.useXITS();
    mt.setFontSize(pointSize);
    m_lastOk = mt.parse(latex);
    if (!m_lastOk)
        return QPixmap{};
    return mt.drawIntoPixmap();
}

}  // namespace Markoff::Live
