// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QSizeF>
#include <QString>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

/// One rendered artifact for an embedded block. Backends produce this; the
/// embed runtime owns rasterising `svg` and pacing the result. Nothing here
/// refers to the document: a renderer is a pure source → artifact function.
struct MARKOFF_CORE_EXPORT RenderedBlock {
    enum class Kind {
        Empty,        ///< nothing to paint (also the "no artifact yet" state)
        Svg,          ///< `svg` holds an SVG document, rasterised by the runtime
        Image,        ///< `image` is ready to paint (raster engines)
        Scene,        ///< reserved: a backend-owned display list (not implemented)
        Highlighted,  ///< token colouring only (code fences)
        Error         ///< render failed; `error` says why
    };

    Kind            kind = Kind::Empty;
    QImage          image;
    QByteArray      svg;
    QList<CodeSpan> spans;          ///< Highlighted only
    QSizeF          preferredSize;  ///< DIP; invalid = measure from the artifact
    QString         fallbackText;   ///< source to paint on Empty/Error
    QString         error;          ///< one line, for the block tooltip
};

}  // namespace Markoff
