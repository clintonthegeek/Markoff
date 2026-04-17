// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_BLOCKDATA_H
#define MARKOFF_BLOCKDATA_H

#include <QTextBlockUserData>
#include <QPixmap>

namespace Markoff {

class MarkoffBlockData : public QTextBlockUserData {
public:
    enum DisplayMode { Raw, Rendered };

    DisplayMode displayMode = Raw;
    int renderedHeight = -1;
    QPixmap renderedCache;
    bool cacheValid = false;
};

} // namespace Markoff
#endif
