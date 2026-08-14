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
    /// content. Empty for every other kind, AND empty for a task-list item
    /// (`isTaskItem` below) — a task item paints a checkbox glyph in this
    /// same decoration slot instead of bracket text (P4.7), so the two are
    /// mutually exclusive by construction rather than by a second
    /// `blockKind()` check at paint time. The buffer never contains this:
    /// the parser narrows ListItem ranges to post-marker content, so this
    /// is display-only and must not enter any byte-offset arithmetic.
    QString marker;

    /// True for a ListItem whose `MarkerStyle` attr is "task" (`- [ ]`/
    /// `- [x]`) — tells the paint/hit-test paths to use the checkbox-glyph
    /// decoration instead of `marker` text (P4.7). The Checked attr's
    /// current value, display-only like `marker` above (the CRDT attr,
    /// not this bool, is what a toggle click writes).
    bool isTaskItem = false;
    bool taskChecked = false;

    /// HorizontalRule paints a line and has no text.
    bool isRule = false;

    /// Table (T9): the block's content is a grid of cells, laid out and
    /// painted by BlockLayoutCache/View's table-specific paths rather than
    /// the single per-block QTextLayout the rest of this switch presumes.
    bool isTable = false;

    /// CodeBlock (P4.6): tells BlockLayoutCache::rebuildInline to attempt
    /// token-color formatting via CodeHighlighting::codeTokenFormatRanges
    /// on top of the plain monospace/background already set above. Kept
    /// here rather than a second `doc.blockKind(id)` switch in
    /// BlockLayoutCache, per this struct's own "resist a second per-kind
    /// switch" rule.
    bool isCodeBlock = false;

    /// Background rect spans the full text column rather than hugging the
    /// text (code blocks, quotes).
    bool fullWidthBackground = false;

    /// Quote bar down the left edge.
    bool hasQuoteBar = false;
};

/// Derive the presentation for one block. Pure: reads the document's kind
/// and attrs, never writes. `fontScale` (contract-v2 P3.5, MarkdownView's
/// own `setFontScale`) multiplies every slot's pixel size on top of the
/// theme's own sizing/multiplier — it is the leaf's, not the theme's,
/// concept, so it stays a separate parameter rather than folding into
/// `Theme` itself.
BlockStyle presentationFor(const MarkoffDocument &doc, BlockId id,
                           const Theme &theme, qreal fontScale = 1.0);

}  // namespace Markoff::Canvas
