// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include <QHash>
#include <QTextLayout>

#include <markoff/core/BlockId.h>

#include "BlockPresentation.h"
#include "ProjectionMap.h"

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Canvas {

/// Padding inside a table cell, on all four sides, in DIPs (T9). Shared
/// between BlockLayoutCache's geometry pass and View's paint/hit-test paths
/// so cell content lands at the same point it was measured against.
constexpr qreal kTableCellPadding = 4.0;

/**
 * Derived layout state for the document's blocks, in document order.
 *
 * Authority (spec §2): this is a CACHE, not a model. Every entry is
 * keyed by (BlockId, blockEditSequence) and is rebuilt from blockText()
 * when that key moves. Nothing is ever patched in place from view-side
 * state — there is no view-side state to patch it from.
 *
 * Laziness: sync() gives every block an *estimated* height computed from
 * font metrics and a newline count, without building a QTextLayout.
 * realizeRange() builds real layouts only for the blocks intersecting a
 * y-range (the viewport plus a margin). y-positions are a prefix sum over
 * whichever height each block currently has, so the scroll range is
 * correct-ish immediately and exact for the region you have looked at.
 */
class BlockLayoutCache {
public:
    /// One table cell (T9): its content's byte range within the block's
    /// blockText() (absolute, block-relative — usable directly as
    /// d2ApplyBufferEdit arguments) and its own single-line QTextLayout.
    struct TableCell {
        int startByte = 0;
        int endByte   = 0;
        std::unique_ptr<QTextLayout> layout;  //!< null until realized
        /// Byte<->layout-QChar map for this cell's own text (spec §4.2,
        /// plan P2.3): built together with `layout`, cell-relative (byte 0
        /// == startByte). No delimiter omission happens inside a cell yet,
        /// so this is presently an identity map modulo UTF-8 width — but it
        /// is still the one sanctioned conversion path (C4), replacing the
        /// ad hoc coords:: calls hit-test/paint used before P2.3.
        ProjectionMap projection;
    };

    struct Entry {
        BlockId id;
        std::unique_ptr<QTextLayout> layout;  //!< null until realized; unused for tables
        /// Byte<->layout-QChar map for `layout`'s text (spec §4.2, P2.1):
        /// built together with `layout` every rebuild, empty/default for
        /// tables (their per-cell layouts hold raw, unomitted cell text —
        /// P2.3's job). The one sanctioned second coordinate space; never
        /// crosses into View.cpp except through this member.
        ProjectionMap projection;
        BlockStyle style;
        quint64 seq      = 0;      //!< blockEditSequence when measured
        qreal   y        = 0;      //!< top of the block, document coords
        qreal   height   = 0;      //!< estimated, or exact once realized
        bool    realized = false;

        // ---- Table grid (T9, style.isTable only) --------------------------
        int tableCols = 0;
        std::vector<qreal> tableColWidths;    //!< size tableCols
        std::vector<qreal> tableRowHeights;   //!< size tableCells.size()/tableCols
        std::vector<TableCell> tableCells;    //!< row-major, size rows*tableCols
    };

    /// Text column width available to layouts. A change invalidates every
    /// realized layout (wrapping depends on it) but keeps the order.
    void setTextWidth(qreal width);
    qreal textWidth() const { return m_textWidth; }

    /// Reconcile with the document: adopt the current block order, drop
    /// layouts whose (id, seq) key moved, re-measure estimates for those.
    /// Cheap enough to call on every d2DocumentChanged.
    void sync(const MarkoffDocument &doc, const Theme &theme);

    /// Build real layouts for every block intersecting [top, bottom) in
    /// document coordinates. Returns true if anything was realized (i.e.
    /// heights moved and the caller should refresh its scroll range).
    bool realizeRange(const MarkoffDocument &doc, const Theme &theme,
                      qreal top, qreal bottom);

    /// Tell the cache where the caret is, for inline delimiter visibility
    /// (spec T7/§4.2). Rebuilds the entries that can have changed: the
    /// caret's old block (if it moved to a different block) and its current
    /// block — not a full restyle pass. A caret move that changes delimiter
    /// visibility changes the LAYOUT TEXT (omission, not just formats), so
    /// this is a full per-block rebuild (projection + layout text + formats
    /// + lines), not merely a setFormats() call. A block whose layout is
    /// not yet realized just gets correct text/formats at realize() time.
    void setCaret(const MarkoffDocument &doc, const Theme &theme,
                  BlockId block, int byteOffset);

    /// Set/replace the IME preedit area (T8, exit E6): splices `text` into
    /// `block`'s layout at `byteOffset` (this block's own coordinate space,
    /// C4 — the layout-QChar position it needs is resolved internally via
    /// the entry's projection, once rebuilt) for line-breaking and painting
    /// only, via QTextLayout::setPreeditArea — never touches blockText().
    /// Rebuilds the block's layout the same way setCaret() does, so preedit
    /// text wraps and paints exactly like typed text would, and any
    /// currently-hidden delimiters stay hidden around it. Base inline-format
    /// ranges landing at or after the preedit's layout position are shifted
    /// by `text.length()` so they keep tracking their real characters once
    /// the preedit splices in.
    void setPreedit(const MarkoffDocument &doc, const Theme &theme,
                    BlockId block, int byteOffset, const QString &text);
    /// Clear any active preedit area. No-op if none is set.
    void clearPreedit(const MarkoffDocument &doc, const Theme &theme);

    void clear();

    const std::vector<Entry> &entries() const { return m_entries; }
    qreal totalHeight() const { return m_totalHeight; }
    int   realizedCount() const;

    /// Index of the block containing document-y, or the nearest one when y
    /// falls in a margin. -1 only when the document is empty.
    int indexAtY(qreal y) const;
    int indexOf(BlockId id) const;

private:
    void  recomputePositions();
    qreal estimateHeight(const MarkoffDocument &doc, const Entry &e) const;
    void  realize(const MarkoffDocument &doc, const Theme &theme, Entry &e);
    /// Table variant of realize() (T9): builds the per-cell grid instead of
    /// a single block-wide layout. Dispatches on e.style.isTable.
    void  realizeTable(const MarkoffDocument &doc, const Theme &theme, Entry &e);
    /// Full per-block rebuild triggered by a caret/preedit change (spec
    /// §4.2): no-op if the entry isn't realized yet (realize() gets it
    /// right on its own first pass instead — see rebuildInline()).
    void  restyleInline(const MarkoffDocument &doc, const Theme &theme, Entry &e) const;
    /// The actual rebuild: projection (omission + '\n' substitution) +
    /// fresh QTextLayout text + formats + the createLine pass. Called
    /// unconditionally by realize() (first build) and by restyleInline()
    /// (already-realized entries, guarded there so an unrealized entry
    /// isn't force-built early).
    void  rebuildInline(const MarkoffDocument &doc, const Theme &theme, Entry &e) const;

    std::vector<Entry>  m_entries;
    QHash<BlockId, int> m_index;        //!< id → position in m_entries
    qreal   m_textWidth      = 0;
    qreal   m_totalHeight    = 0;
    quint64 m_structuralSeq  = 0;
    BlockId m_caretBlock;                //!< null = no caret tracked
    int     m_caretByte      = -1;
    BlockId m_preeditBlock;              //!< null = no preedit tracked
    int     m_preeditByte    = -1;       //!< this block's byte space (C4)
    QString m_preeditText;
};

}  // namespace Markoff::Canvas
