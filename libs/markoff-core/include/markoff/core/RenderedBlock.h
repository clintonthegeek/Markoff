// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

struct MARKOFF_CORE_EXPORT RenderedBlock {
    enum class Kind { Image, Svg, Highlighted, Empty };

    Kind             kind = Kind::Empty;
    QImage           image;
    QString          svg;
    QList<CodeSpan>  spans;
    QSize            preferredSize;
    QString          fallbackText;
};

}  // namespace Markoff
