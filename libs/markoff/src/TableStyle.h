// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLESTYLE_H
#define MARKOFF_TABLESTYLE_H

#include <QColor>

namespace Markoff {

struct TableStyle {
    QColor headerBackground{240, 240, 240};
    QColor gridLineColor{208, 208, 208};
    QColor headerSeparatorColor{160, 160, 160};
    qreal cellPadding = 8.0;
    qreal gridLineWidth = 1.0;
    qreal headerSeparatorWidth = 2.0;
};

} // namespace Markoff

#endif // MARKOFF_TABLESTYLE_H
