// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QString>

#include <markoff/core/BlockId.h>

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Canvas {

/**
 * Everything the paint path needs to know about a block that is not its
 * text: font, colors, spacing, decorations.
 *
 * This struct plus presentationFor() is the ONE per-kind switch in the
 * leaf (plan T1). Resist adding a second one — if paint code starts
 * asking `blockKind()` again, the answer belongs in here instead.
 */
struct BlockStyle {
    QFont  font;
    QColor foreground;
    QColor background;          //!< invalid = no background rect
    qreal  topMargin    = 0;
    qreal  bottomMargin = 0;
    qreal  leftIndent   = 0;    //!< added to the view's page margin

    /// ListItem bullet/number, painted as decoration to the left of the
    /// content. Empty for every other kind. The buffer never contains it:
    /// the parser narrows ListItem ranges to post-marker content, so this
    /// is display-only and must not enter any byte-offset arithmetic.
    QString marker;

    /// HorizontalRule paints a line and has no text.
    bool isRule = false;

    /// Background rect spans the full text column rather than hugging the
    /// text (code blocks, quotes).
    bool fullWidthBackground = false;

    /// Quote bar down the left edge.
    bool hasQuoteBar = false;
};

/// Derive the presentation for one block. Pure: reads the document's kind
/// and attrs, never writes.
BlockStyle presentationFor(const MarkoffDocument &doc, BlockId id,
                           const Theme &theme);

}  // namespace Markoff::Canvas
