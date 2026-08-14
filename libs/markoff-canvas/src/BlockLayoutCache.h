// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <vector>

#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QTextLayout>

#include <markoff/core/BlockId.h>
#include <markoff/core/Kf6SyntaxHighlightService.h>

#include <markoff/canvas/MediaSeams.h>

#include "BlockPresentation.h"
#include "ProjectionMap.h"

namespace Markoff {
class MarkoffDocument;
class Theme;
class EmbedRegistry;
}

namespace Markoff::Canvas {

/// Padding inside a table cell, on all four sides, in DIPs (T9). Shared
/// between BlockLayoutCache's geometry pass and View's paint/hit-test paths
/// so cell content lands at the same point it was measured against.
constexpr qreal kTableCellPadding = 4.0;

/// Fixed height of an Image/Embed placeholder box (P5.4), in DIPs — used
/// both for BlockLayoutCache's height computation and View::paintEvent's
/// box-drawing, so the two never disagree about how much space a
/// placeholder occupies.
constexpr qreal kMediaPlaceholderHeight = 80.0;

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

        /// Display Math (P5.3): a jkqtmathtext render of this block's bare
        /// LaTeX (style.isMathDisplay only), built by rebuildInline()
        /// whenever the caret is NOT in this block — i.e. exactly when
        /// paintEvent should paint it instead of `layout`. Null while the
        /// caret is in the block (source revealed, same trigger as the
        /// code-fence per-block reveal) or if jkqtmathtext failed to parse
        /// the source. `layout` itself is always built regardless — the
        /// pixmap only ever substitutes at PAINT time, so hit-testing/
        /// caret motion/selection keep working against real text
        /// unconditionally.
        QPixmap mathPixmap;

        /// Image resource-lookup result (P5.4, `style.isImageBlock` only):
        /// built by `rebuildInline()` from the injected
        /// `ImageResourceLookup`, keyed off the block's own parsed target
        /// (`MediaBlocks::parseImageBlock`). Null when no lookup is set,
        /// the lookup returned null (a miss), or this isn't an image
        /// block — `View::paintEvent` paints a placeholder box using
        /// `mediaLabel` in that case.
        QPixmap imagePixmap;

        /// Mermaid renderer result (P5.4, `style.isCodeBlock` + a
        /// "mermaid" fence language only): built the same way display
        /// Math's `mathPixmap` is — only while the caret is NOT in this
        /// block. Null when no renderer is injected, the block isn't a
        /// mermaid fence, the renderer returned null, or the caret is in
        /// the block (source revealed) — `View::paintEvent` falls back to
        /// the normal code-block text layout in every one of those cases
        /// (no placeholder box for mermaid, unlike Image/Embed: a missing
        /// renderer is exactly `CodeHighlighting`'s own "service miss
        /// renders plain monospace" precedent).
        QPixmap mermaidPixmap;

        /// Parsed target/label for a `style.isImageBlock` or
        /// `style.isEmbedBlock` entry (P5.4) —
        /// `MediaBlocks::parseImageBlock`'s target/altOrAlias (plus, for
        /// an embed, an `EmbedRegistry::hasExtension` consultation folded
        /// into the text), cached here so `View::paintEvent`'s
        /// placeholder-box label doesn't reparse the buffer every paint.
        /// Empty for every other entry.
        QString mediaLabel;

        quint64 seq      = 0;      //!< blockEditSequence when measured
        qreal   y        = 0;      //!< top of the block, document coords
        qreal   height   = 0;      //!< estimated, or exact once realized
        bool    realized = false;

        /// Folding (P5.6): true when this block is currently hidden by an
        /// active fold — its OWN `height` field is left untouched (still
        /// the real/estimated content height, so the entry stays fully
        /// findable/queryable: `blockRect`, `indexOf`, a realized layout
        /// for search/selection all keep working exactly as if it weren't
        /// folded) but `recomputePositions()` adds 0 for it instead of
        /// `height` — the entry is "skipped in y-layout" by contributing
        /// no space, never by being forced back to unrealized (an
        /// unrealized entry's `height` is `estimateHeight()`'s non-zero
        /// guess, which would defeat the whole point). `View` derives this
        /// set fresh from `Folding::resolveFoldable` + its own folded-head
        /// set every time it changes; nothing here decides fold policy.
        bool    folded   = false;

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
    /// Cheap enough to call on every d2DocumentChanged. `fontScale`
    /// (contract-v2 P3.5) is forwarded to `presentationFor()` for every
    /// entry this call (re)styles — callers that invalidate styling for a
    /// scale change (View::setFontScale, via `clear()`) get every entry
    /// restyled at the new scale on the very next call, same as a
    /// structural edit already forces via `restyleAll` below.
    void sync(const MarkoffDocument &doc, const Theme &theme, qreal fontScale = 1.0);

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

    /// P5.4 image seam. Not owned (callback semantics — same "reference,
    /// not owner" convention `m_syntaxHighlighter`'s concrete-service role
    /// already follows, just injectable now). Forces every already-
    /// realized entry to re-realize on the next `realizeRange()` call, so
    /// a lookup set after the document is already loaded takes effect on
    /// the next paint rather than only on the next structural edit.
    void setImageResourceLookup(ImageResourceLookup lookup);
    /// P5.4 mermaid seam. Not owned — see `MermaidRenderer`'s own doc
    /// comment. Same re-realize-on-set behavior as the image lookup above.
    void setMermaidRenderer(MermaidRenderer *renderer);
    /// P5.4 embed seam. Not owned. Same re-realize-on-set behavior.
    void setEmbedRegistry(Markoff::EmbedRegistry *registry);

    /// Folding (P5.6): marks every entry whose id is in `hidden` as
    /// `Entry::folded` (clearing it on every other entry) and recomputes
    /// y-positions so hidden entries contribute zero height. Cheap: no
    /// layout work, just the `folded` flags + the existing prefix-sum pass.
    /// Safe to call every time `View`'s folded-head set could have changed
    /// (fold toggle, or a document edit that could have moved a fold
    /// head's body) — `hidden` is always freshly derived, never patched
    /// incrementally.
    void setFoldedBlocks(const QSet<BlockId> &hidden);

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
    /// Drops every entry's realized layout (keeps the estimate/y-position
    /// otherwise), forcing a fresh `realize()`/`rebuildInline()` pass on
    /// the next `realizeRange()` — same invalidation `setTextWidth()` was
    /// already doing inline; factored out so the P5.4 seam setters below
    /// can reuse it without also forcing a width change.
    void invalidateRealizedLayouts();

    /// Code-block token colorer (P4.6): keyed per call by the fence's own
    /// info string (CodeHighlighting::parseCodeFence), not stored per
    /// instance here — this service object itself is stateless lookup
    /// (KSyntaxHighlighting::Repository), owned once and reused for every
    /// CodeBlock entry's rebuild. Canvas has no consumer-injected
    /// MarkoffServices wiring yet (nothing in the tree currently
    /// instantiates one — grep confirms it), so this leaf owns its own
    /// instance directly, same as it owns everything else in this cache.
    Kf6SyntaxHighlightService m_syntaxHighlighter;

    /// P5.4 seams — all "reference, not owner": the consumer keeps the
    /// renderer/registry alive, this cache only reads through them at
    /// rebuild time. See the setters' own doc comments.
    ImageResourceLookup    m_imageLookup;
    MermaidRenderer        *m_mermaidRenderer = nullptr;
    Markoff::EmbedRegistry *m_embedRegistry   = nullptr;

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
