// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <QAbstractScrollArea>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QUrl>
#include <QVariant>

#include <markoff/core/BlockId.h>
#include <markoff/core/FoldRef.h>
#include <markoff/core/FormatOps.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Theme.h>

#include <markoff/canvas/MediaSeams.h>
#include <markoff/canvas/TableTypes.h>

class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QInputMethodEvent;
class QMenu;
class QMimeData;
class QPainter;
class QTextLayout;

namespace Markoff {
class MarkoffDocument;
class LinkService;
class EmbedRegistry;
class Session;
}

namespace Markoff::Canvas {

class BlockLayoutCache;
class CanvasActionController;

/// The view's caret: which block, and a UTF-8 byte offset into that
/// block's buffer (spec §4, C4 — one coordinate space, per block; never a
/// cross-block byte sum). The document is the only authority over text;
/// this is the only piece of state the view is allowed to own that the
/// document doesn't already know about.
struct CanvasCursor {
    BlockId block;
    int byteOffset = 0;

    bool operator==(const CanvasCursor &o) const noexcept
    {
        return block == o.block && byteOffset == o.byteOffset;
    }
};

/// A find-match highlight to paint (contract-v2 P3.4). View-agnostic on
/// purpose — this type deliberately does not know about
/// `Markoff::FindController`; `EditorWidget::attachFindController`
/// translates `FindController::Match` (+ current-index) into a list of
/// these. Byte range is block-relative, same convention as everywhere
/// else in this leaf (C4).
struct FindHighlight {
    BlockId block;
    int byteOffset = 0;
    int byteLength = 0;
    /// Distinct visual weight from the other matches (Theme::Slot
    /// SearchActiveMatchBackground vs SearchMatchBackground) — the
    /// currently-selected match under FindController::currentMatchIndex.
    bool isCurrent = false;
};

/// A remote participant's cursor/selection to paint (P6.2, spec §5
/// collaboration surface). Thin wrapper around `Markoff::Selection` rather
/// than a separate flat `{Selection, displayName, QColor}` triple: Selection
/// already carries `participantLabel`/`presenceColor`/`kind` (Selection.h),
/// purpose-built for `Kind::Presence`, so a second copy of the same three
/// fields would just be a second, potentially-conflicting source of truth
/// for the exact same state (findings log, P6.2). `selection.kind` is
/// expected to be `Selection::Kind::Presence`; `View` does not assume that
/// from list membership — it checks the kind at paint time (F1a: multi-
/// cursor readiness), so a later local multi-cursor list feeding
/// `Selection::Kind::Secondary` entries through the same mechanism reuses
/// this exact paint path instead of growing a second one.
struct RemotePresence {
    Markoff::Selection selection;
};

/// A resolved bracket pair around the caret (P7.2f, F1 #10). Both offsets
/// are block-relative UTF-8 byte offsets into `block`'s buffer (C4 — scoped
/// to one block, never a cross-block search; see
/// `View::bracketMatchAtCaret()`'s doc comment for why an unmatched or
/// cross-block case never produces one of these). `openByte`/`closeByte`
/// each name the bracket character's own single byte, not a range.
struct BracketMatch {
    BlockId block;
    int openByte = -1;
    int closeByte = -1;
};

/// Content column width policy (contract-v2 P4.5, spec §5.2 "Word wrap"):
/// either the full viewport width, or a fixed readable-line-length column
/// centered in the viewport (Obsidian "readable line length" toggle).
/// `View::layoutWidthFor()` is the one place this turns into an actual
/// pixel width; `pageMargin()`/`textWidth()` both derive from it, so
/// everything keyed off page margin (caret, hit-testing, paint,
/// block/table rects) centers along with the text automatically.
struct ContentWidthPolicy {
    enum Kind { FullWidth, FixedColumn };
    Kind kind = FullWidth;
    /// Only consulted when `kind == FixedColumn`. Obsidian's own default
    /// calibration (spec §5.3, F1 audit): `--file-line-width: 700px`.
    qreal fixedColumnWidth = 700.0;

    static ContentWidthPolicy fullWidth() { return ContentWidthPolicy{FullWidth, 0.0}; }
    static ContentWidthPolicy fixedColumn(qreal px)
    {
        return ContentWidthPolicy{FixedColumn, px};
    }
};

/**
 * Projection view leaf: renders a MarkoffDocument directly, one
 * QTextLayout per block, with its own input pipeline.
 *
 * Authority model (spec §2): the document wins totally. This widget
 * holds no editable text state — every layout it builds is a derived
 * cache keyed by (BlockId, blockEditSequence), rebuilt from
 * blockText(), never patched in place.
 *
 * T1: read-only. Lazy layout, per-kind presentation, wheel/keyboard
 * scrolling.
 * T2: caret, mouse hit-testing, printable-key typing, in-block
 * Backspace/Delete, arrow-key motion. Structural keys (Enter split,
 * boundary Backspace/Delete merge) arrive in T3. Undo/redo (T4) reuse
 * the T2 clamp — there is no separate caret-restoration mechanism.
 * T5: selection — an optional anchor `CanvasCursor` alongside the caret.
 * Drag/shift-extend moves the caret while the anchor holds; Ctrl+C/X join
 * selected blocks' selected byte ranges onto the clipboard; a mutating key
 * on a non-empty selection collapses it first (per-block deletes + a
 * structural merge of the two boundary blocks, one transaction).
 * T6: kind transitions. The caret's block is checked against its inferred
 * kind after a document change; a Paragraph whose text now matches e.g. an
 * ATX heading prefix is promoted via `d2SetBlockKind` with the buffer left
 * untouched (see `promoteCaretBlockKind`).
 * T9: minimal table. A Table block's entry has no single layout; instead
 * BlockLayoutCache builds a grid of per-cell `QTextLayout`s (own coordinate
 * space unchanged — cell content byte ranges are still block-relative
 * offsets, so the caret/editing paths below need no table-specific
 * branches beyond hit-testing and painting).
 * T8: IME composition. `inputMethodEvent` mirrors
 * `QWidgetTextControlPrivate::inputMethodEvent`'s ordering: replacement +
 * commit land as one `d2ApplyBufferEdit` at the caret; the (possibly new)
 * preedit string is spliced into the caret block's `QTextLayout` via
 * `setPreeditArea` — never the document — until it commits or cancels.
 */
class View : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit View(QWidget *parent = nullptr);
    ~View() override;

    /// Non-owning. Null is legal (an unbound view paints an empty page).
    void setDocument(MarkoffDocument *doc);
    MarkoffDocument *document() const;

    /// P6.0: non-owning reference to the current document's per-view
    /// Session (owned by `EditorWidget`, threaded in right after it calls
    /// `doc->createSession()`; cleared to null wherever the owner nulls or
    /// destroys its session). When set, `toggleFold()` additionally
    /// writes every fold through to `session->toggleFold()` — so an
    /// external reader of `session->foldedRegions()` sees real state — on
    /// top of its unconditional local `m_foldedHeads` update (see
    /// `toggleFold()`'s doc comment for why a *generic* reverse
    /// resolution of Session state back into `m_foldedHeads` is not
    /// attempted: it is unsound under D2's per-block CRDT buffers, a
    /// P6.0 finding). Null is legal — every existing direct-`View` test
    /// that never attaches a Session keeps working unchanged.
    void setSession(Session *session);
    Session *session() const;

    void setTheme(const Theme &theme);
    const Theme &theme() const;

    /// Contract-v2 (P3.5): multiplies every block's font pixel size on top
    /// of the theme's own sizing (`BlockPresentation::fontForSlot`).
    /// Invalidates every cache entry's style AND layout (width/height both
    /// depend on the font) — a full re-style + re-measure, same shape as
    /// `setTheme()` — but re-anchors the scroll position to the block that
    /// was at the top of the viewport before the call, not the raw pixel
    /// offset (every block's height just changed, so the old offset means
    /// nothing). No-op if `scale` is unchanged (fuzzy-compared).
    void setFontScale(qreal scale);
    qreal fontScale() const { return m_fontScale; }

    /// Contract-v2 (P4.5, spec §5.2 "Word wrap"): sets the content column
    /// width policy. Reflows the same way a resize does — realized entries
    /// get exact relayout at the new width, off-screen entries fall back
    /// to estimates until scrolled into view (`ensureLayoutForViewport`'s
    /// existing lazy-realize path, unchanged) — and re-anchors the scroll
    /// position to whichever block was at the top of the viewport before
    /// the call (same mechanism `resizeEvent` uses), so toggling the
    /// policy live doesn't fling the viewport to an unrelated block.
    /// No-op if the policy is unchanged.
    void setContentWidthPolicy(ContentWidthPolicy policy);
    ContentWidthPolicy contentWidthPolicy() const { return m_contentWidthPolicy; }

    // ---- Image / Mermaid / Embed seams (P5.4) ----------------------------
    // All three follow the "reference, not owner" convention setLinkService/
    // setActionController already use in this file: the consumer keeps the
    // callback/renderer/registry alive; this view only reads through it at
    // rebuild time. Each setter forces every already-realized entry to
    // re-realize (BlockLayoutCache::invalidateRealizedLayouts), so setting
    // one after the document is already loaded still takes effect on the
    // next paint.

    /// Image blocks (`![alt](src)`) paint `lookup`'s result, or a
    /// placeholder box on a miss/unset lookup. See `ImageResourceLookup`'s
    /// own doc comment (MediaSeams.h).
    void setImageResourceLookup(ImageResourceLookup lookup);
    /// Fenced code blocks whose info-string language is "mermaid" paint
    /// `renderer`'s pixmap while the caret is outside the block, source
    /// while inside. `nullptr` (the default) means mermaid blocks always
    /// show as plain code (P4.6 monospace styling), never a placeholder.
    void setMermaidRenderer(MermaidRenderer *renderer);
    /// Obsidian-style block embeds (`![[target]]`) consult `registry` for
    /// `hasExtension()` only (this view never mounts a real
    /// `MarkdownRenderChild` — always placeholder-rendered, plan P5.4).
    /// `nullptr` (the default) renders every embed as an "unregistered"
    /// placeholder.
    void setEmbedRegistry(Markoff::EmbedRegistry *registry);

    /// Read-only gate (contract-v2 P3.3, spec §4.2): the single authority
    /// every mutation-ingress path below checks (mirrors the live leaf's
    /// `binding()->readOnly` six-gate table, this file's CLAUDE.md
    /// cheat-sheet reference). Navigation, selection, copy, and find are
    /// unaffected — only paths that call `d2ApplyBufferEdit`/
    /// `d2SetBlockKind`/`undoD2`/`redoD2` or otherwise mutate the document
    /// gate on this.
    void setReadOnly(bool ro);
    bool isReadOnly() const;

    /// Caret rectangle in THIS widget's (View's) coordinate system — i.e.
    /// viewport-local pixels translated into View's own frame, matching
    /// `QWidget::inputMethodQuery(Qt::ImCursorRectangle)`'s convention.
    /// `Markoff::Canvas::EditorWidget::caretRect()` is a thin pass-through
    /// (View fills EditorWidget's client area, spec §4.1 composition).
    /// Invalid QRect if there is no document, no caret, or the caret's
    /// block is not currently realized. Table-cell carets (T9) are not yet
    /// special-cased here — same known gap as `inputMethodQuery`'s
    /// `Qt::ImCursorRectangle` case, which this shares its math with.
    QRect caretRect() const;

    // ---- Inspection surface -------------------------------------------
    // Read-only views onto the derived cache, for tests and the manual
    // harness. Nothing here is authority; it all recomputes from the
    // document. Kept on the widget rather than exposing BlockLayoutCache
    // so the cache stays a private implementation detail.

    int   blockCount() const;
    int   realizedBlockCount() const;
    qreal documentHeight() const;

    /// Document-order index of `id`, or -1 if it is not (or no longer) in
    /// the current document. Contract-v2 seam (P3.6): the "block index"
    /// half of the ephemeral-state schema — an index survives a
    /// detach/reattach cycle in a way a raw `BlockId` does not (a
    /// reloaded/reattached document mints fresh ids), so
    /// `EditorWidget::saveEphemeralState()` persists indices, not ids.
    int blockIndexOf(BlockId id) const;
    /// Inverse of `blockIndexOf()`: the block at document-order `index`, or
    /// a null `BlockId` if out of range for the current document.
    BlockId blockIdAt(int index) const;

    /// Block bounds in document coordinates (y=0 is the document top, not
    /// the viewport top). Null rect if the id is not in the document.
    QRectF blockRect(BlockId id) const;

    /// Ensures `id`'s cache entry has a realized `QTextLayout` (G1 a11y
    /// A2.3, spec §5): calls the cache's existing `realizeRange()` but
    /// bounded to exactly this one entry's y-span, never the viewport ±
    /// page-height range the scroll-driven realize pass uses — a
    /// geometry query realizes the ONE block it asked about, nothing else.
    /// Returns false (no-op) if `id` is not in the current document.
    /// `characterRectInViewport()`/`offsetAtPoint`-style callers must call
    /// this first; nothing else in this file forces realization on their
    /// behalf, same as `blockRect()` et al. staying silent (null rect) on
    /// an unrealized entry.
    bool ensureBlockRealized(BlockId id);

    /// Viewport-local rect of the character at `byteOffset` in `id`'s
    /// realized layout (G1 a11y A2.3) — same math as `caretRectInViewport`
    /// generalized to an arbitrary block/offset instead of only the
    /// current caret, and a full character-advance width instead of a 1px
    /// caret line. Null rect if `id` isn't realized (call
    /// `ensureBlockRealized()` first) or `byteOffset` doesn't resolve to a
    /// valid line. Test/inspection surface only — nothing here is
    /// authority.
    QRect characterRectInViewport(BlockId id, int byteOffset) const;

    /// The visual (wrapped) line's byte range `[start, end)` in `id`'s own
    /// buffer, for the line containing `byteOffset` in its realized layout
    /// (G1 a11y A2.3). Deliberately NOT the same thing as
    /// `QAccessibleTextInterface`'s own default `LineBoundary` handling
    /// (literal `\n`-splitting the plain string) — a wrapped paragraph has
    /// multiple visual lines with no embedded `\n` at all, so that default
    /// would report the whole block as one "line". `{-1, -1}` if `id` isn't
    /// realized (call `ensureBlockRealized()` first) or `byteOffset` is out
    /// of range. Test/inspection surface only — nothing here is authority.
    std::pair<int, int> lineByteRangeAt(BlockId id, int byteOffset) const;

    /// The byte offset in `id`'s own buffer nearest `viewportPos` within
    /// its realized layout (G1 a11y A2.3) — same per-block line/column
    /// math `hitTest()` (private, whole-document Y-dispatch) uses
    /// internally, but scoped to one already-known block rather than
    /// searching the whole document by Y. -1 if `id` isn't realized (call
    /// `ensureBlockRealized()` first). Does not itself check whether
    /// `viewportPos` actually falls within `id`'s vertical extent — callers
    /// that need that (e.g. rejecting a point that belongs to some OTHER
    /// block) gate on `blockRect()`/`rect()` first, same as this class's
    /// other geometry accessors leave range-membership to the caller.
    int byteOffsetAtPoint(BlockId id, const QPoint &viewportPos) const;

    /// Table cell bounds in document coordinates (T9, exit E8), or a null
    /// rect if `id` is not a realized Table block or (row, col) is out of
    /// range. row 0 is the header row. Test/inspection surface only —
    /// nothing here is authority.
    QRectF tableCellRect(BlockId id, int row, int col) const;

    /// Task-checkbox glyph bounds in document coordinates (P4.7), or a null
    /// rect if `id` isn't a task-list ListItem. Same rect `paintEvent`
    /// draws and `taskCheckboxAt`'s click hit-test checks (via the shared
    /// `taskCheckboxRect` geometry function in View.cpp) — test/inspection
    /// surface only, same convention as `blockRect`/`tableCellRect`.
    QRectF taskCheckboxRectFor(BlockId id) const;

    /// Paints since construction. Cheap paint-counter for tests that need
    /// to know a repaint actually happened.
    quint64 paintCount() const;

    /// Whether the byte at `byteOffset` in block `id` currently falls
    /// inside a hidden delimiter run (spec §4.2, P2.1): such a run is
    /// OMITTED from the block's layout text entirely, not merely painted
    /// invisible, so this re-evaluates the same reveal predicate the
    /// projection was built from rather than inspecting a format range.
    /// False if the block isn't realized or the offset isn't inside a
    /// currently-hidden run. Test/inspection surface only — nothing here
    /// is authority.
    bool isDelimiterHiddenAt(BlockId id, int byteOffset) const;

    /// The foreground color painted at `byteOffset` in block `id`'s realized
    /// layout (P4.6, code-block token coloring), read directly off the
    /// layout's own `formats()` — the same list `BlockLayoutCache::
    /// rebuildInline`'s one `setFormats()` call installed, not a
    /// recomputation. An invalid QColor means no format range covers that
    /// position (plain BlockPresentation styling applies instead) or the
    /// block isn't realized. Test/inspection surface only — nothing here is
    /// authority.
    QColor codeTokenColorAt(BlockId id, int byteOffset) const;

    /// The realized layout's first line's natural (unwrapped) text width in
    /// DIPs, or -1 if `id` isn't realized (spec §4.2 P2.1 exit criterion:
    /// this is real reflow, not a cosmetic recolor — hiding a delimiter run
    /// narrows what the layout actually measures). Test/inspection surface
    /// only — nothing here is authority.
    qreal lineNaturalWidth(BlockId id) const;

    /// Display Math (P5.3): whether `id`'s realized entry currently has a
    /// jkqtmathtext pixmap standing in for its text layout in paintEvent
    /// — true exactly when it's a display-math block (DisplayMode attr),
    /// jkqtmathtext parsed its LaTeX successfully, AND the caret is not in
    /// this block (source not being revealed). False for an unrealized
    /// block, an inline-math block, a non-math block, a parse failure, or
    /// while the caret is inside the block revealing raw source. Test/
    /// inspection surface only — nothing here is authority.
    bool isMathPixmapActive(BlockId id) const;

    /// P5.4: whether `id`'s realized entry currently has a consumer-
    /// resolved image pixmap standing in for its text layout — true
    /// exactly when it's a standard-image-form Image block AND the
    /// injected `ImageResourceLookup` returned a non-null pixmap for its
    /// target. False for an unrealized block, a non-image block, an
    /// embed-form Image block, or a lookup miss/unset lookup (in which
    /// case `isImagePlaceholderActive` is true instead). Test/inspection
    /// surface only — nothing here is authority.
    bool isImagePixmapActive(BlockId id) const;
    /// P5.4: whether `id`'s realized entry is a standard-image-form Image
    /// block currently painting the placeholder box (no pixmap resolved).
    /// Test/inspection surface only.
    bool isImagePlaceholderActive(BlockId id) const;
    /// P5.4: whether `id`'s realized entry currently has an injected
    /// `MermaidRenderer`'s pixmap standing in for its text layout — true
    /// exactly when it's a fenced code block whose language is "mermaid",
    /// a renderer is set, the render succeeded, AND the caret is not in
    /// this block. Test/inspection surface only.
    bool isMermaidPixmapActive(BlockId id) const;
    /// P5.4: whether `id` is an embed-form Image block (`![[target]]`) —
    /// always placeholder-painted this task, so this is equivalent to
    /// "is this block an embed" rather than a pixmap-active check. Test/
    /// inspection surface only.
    bool isEmbedPlaceholderActive(BlockId id) const;
    /// P5.4: the placeholder/target label text `paintEvent` would draw for
    /// `id`'s Image-block entry (standard image OR embed form) — for an
    /// embed, this is where `EmbedRegistry::hasExtension()`'s answer shows
    /// up (a different label string for "registered" vs "no factory").
    /// Empty for a non-image entry or an unrealized block. Test/inspection
    /// surface only — nothing here is authority.
    QString mediaLabelFor(BlockId id) const;

    /// P5.5: whether `id`'s realized entry is a callout blockquote
    /// (`> [!type]`, `CalloutBlocks::parseCallout`) — carries the typed
    /// icon+label header band, distinct from a plain blockquote. Test/
    /// inspection surface only.
    bool isCalloutBlock(BlockId id) const;
    /// P5.5: whether `id`'s realized entry is a footnote-definition
    /// Paragraph (`[^label]: ...`, `FootnoteDefBlocks::parseFootnoteDef`)
    /// — carries the back-reference marker/color/italic presentation,
    /// distinct from a plain paragraph. Test/inspection surface only.
    bool isFootnoteDefBlock(BlockId id) const;

    // ---- Folding (P5.6) ---------------------------------------------------
    // Heading sections, long lists (>= Detail::kLongListFoldThreshold
    // top-level items), and callouts (P5.5's typed blockquote header) are
    // foldable — see `Folding::resolveFoldable`'s own doc comment for the
    // exact per-kind body rules. Fold STATE (which head blocks are
    // currently toggled) lives here as a `BlockId` set (`m_foldedHeads`);
    // fold SHAPE (what body a given head's fold would hide) is always
    // re-derived fresh from current document structure, never cached
    // (spec §2/§3: the document is the one authority) — an edit that
    // shrinks/removes a fold head's body is picked up on the very next
    // `onDocumentChanged()`, no separate invalidation path needed.

    /// What foldable unit (if any) `id` currently heads, purely a function
    /// of document structure — independent of whether it's actually folded
    /// right now. False for a block that isn't a heading/callout/long-list
    /// head, or one of those shapes with nothing to hide (e.g. the last
    /// heading in the document, with no body after it).
    bool isBlockFoldable(BlockId id) const;
    /// True iff `id` is a fold head whose fold is currently ON. False for
    /// a block that isn't foldable at all, and for a block that's merely
    /// HIDDEN inside someone else's active fold (see `isBlockHidden`).
    bool isBlockFolded(BlockId id) const;
    /// True iff `id` is currently invisible because it falls inside some
    /// OTHER block's active fold body. A folded head is never "hidden" by
    /// its own fold (it's still the visible affordance row) — this and
    /// `isBlockFolded` are deliberately not each other's negation.
    bool isBlockHidden(BlockId id) const;
    /// Toggles `id`'s fold state. No-op if `id` isn't currently foldable
    /// (re-derived fresh, not read off stale state). Turning a fold ON
    /// while the caret sits inside the body it just hid moves the caret to
    /// `id` byte 0 first — through the same `setCaret` chokepoint every
    /// other caret-changing path in this file uses — so the caret is never
    /// left referencing now-invisible content (mirrors `clampCaret`'s
    /// "never strand the caret" rule for structural edits).
    void toggleFold(BlockId id);
    /// Fold-affordance glyph bounds in document coordinates, or a null
    /// rect if `id` isn't realized or isn't currently foldable. Same rect
    /// `paintEvent` draws and the click hit-test checks — test/inspection
    /// surface only, same convention as `taskCheckboxRectFor`.
    QRectF foldAffordanceRectFor(BlockId id) const;

    /// Ephemeral-state seam (P5.6, fills the P3.6 schema's empty "folds"
    /// key): document-order indices of every currently-folded head, in
    /// `m_cache` entry order. Mirrors `blockIndexOf`'s "an index survives
    /// detach/reattach, a raw BlockId doesn't" reasoning —
    /// `EditorWidget::saveEphemeralState` persists these, not BlockIds.
    QList<int> foldedHeadIndices() const;
    /// Inverse of `foldedHeadIndices()`: replaces the current folded-head
    /// set with whichever of `indices` still resolves (in the CURRENT
    /// document) to a block that is actually foldable — a stale/foreign
    /// index, or one landing on a block that no longer heads a foldable
    /// unit, is silently dropped rather than treated as a hard failure,
    /// same "restore whatever you can" rule the rest of ephemeral-state
    /// restore already follows (`EditorWidget::restoreEphemeralState`'s
    /// class doc).
    void setFoldedHeadIndices(const QList<int> &indices);

    /// Whether an IME composition is in progress (T8, exit E6): a non-empty
    /// preedit string is currently spliced into the caret block's layout.
    /// Mirrors QWidget::inputMethodComposing's role in the old leaves —
    /// inspection only, driven entirely by inputMethodEvent().
    bool isComposing() const;
    /// The current preedit string, or an empty string if not composing.
    /// Inspection surface for tests; the layout (via
    /// QTextLayout::preeditAreaText()) is the actual authority painted from.
    QString preeditText() const;

    // ---- Caret ----------------------------------------------------------
    // Inspection for tests; the caret is edited only through real events
    // (mouse press, key press) on the production widget, never set
    // directly (invariant 5).

    BlockId caretBlock() const;
    int     caretByteOffset() const;

    /// If the caret's block is a realized Table (T9), the (row, col) cell
    /// its byte offset falls in — row 0 is the header row, same convention
    /// as `tableCellRect()`. `std::nullopt` if the caret isn't in a table
    /// block, or the table isn't realized yet (row/col derivation needs
    /// `tableCells`, built only at realize time). Contract-v2 seam (P3.5):
    /// `EditorWidget::recomputeContext()` calls this to fill
    /// `EditorContext::tableRow`/`tableCol` — reuses the same row-major
    /// cell-index sequence plan P2.3 built for table selection
    /// (`cellIndexNear` in View.cpp), not a second table-position scheme.
    std::optional<std::pair<int, int>> caretTableCell() const;

    /// The caret's row/col plus its table's shape and the caret column's
    /// current alignment (contract-v2 P5.2). `nullopt` under exactly the
    /// same conditions as `caretTableCell()` (this wraps it and adds a
    /// fresh `TableGeometry` re-parse for the alignment/column-count
    /// fields the cache alone doesn't carry). `CanvasActionController`'s
    /// enabled/checked-state wiring and `buildContextMenu`'s table section
    /// both read this instead of re-deriving row/col/cols separately.
    struct TableCellContext {
        int row = 0;
        int col = 0;
        int rowCount = 0;
        int colCount = 0;
        TableAlign columnAlign = TableAlign::None;
    };
    std::optional<TableCellContext> caretTableContext() const;

    // ---- Table row/col ops + alignment (contract-v2 P5.2) -----------------
    // Byte edits to the table block's own buffer via TableGeometry, each one
    // UndoLog::Transaction (mirrors P5.1's appendTableRow). Act on the
    // caret's current table cell (caretTableContext()); no-op while
    // read-only, with no document, or when the caret isn't in a table.
    // Guard rules matching GFM/Obsidian sense: the header row (row 0) can't
    // be deleted or have a row inserted above it; the table's last surviving
    // column can't be deleted.

    void insertTableRowAbove();
    void insertTableRowBelow();
    void deleteTableRow();
    void insertTableColumnLeft();
    void insertTableColumnRight();
    void deleteTableColumn();
    /// Tri-state alignment write: rewrites the caret column's delimiter-row
    /// cell content in place (the surrounding pipes are untouched).
    void setTableColumnAlignment(TableAlign align);
    /// `ActionId::InsertTable`: inserts a new 2x2 table block right after
    /// the caret's current block and lands the caret in its first cell. The
    /// one table op here that is NOT an edit to an existing table's buffer
    /// — there may be no table yet. No-op while read-only or with no
    /// document/caret.
    void insertTable();

    /// Programmatic caret placement (contract-v2 EditorWidget seam, P3.1):
    /// the one sanctioned exception to "the caret is edited only through
    /// real events" above — `Markoff::Canvas::EditorWidget::setCursorPosition`
    /// routes through here. `block` is clamped to the last surviving block
    /// (mirroring `clampCaret`'s "nearest surviving block" rule) if it is
    /// unknown to the cache; `byteOffset` is clamped to the target block's
    /// length. Clears any active selection anchor, same as a real click.
    /// Synchronous, no queued/deferred application (C2) — the caller sees
    /// the new caret the moment this returns.
    void setCaretPosition(BlockId block, int byteOffset);

    // ---- Scroll anchor (contract-v2 P3.6) --------------------------------

    /// The block currently at the top of the viewport, and how far
    /// scrolled into it: 0.0 = the block's own top edge sits exactly at
    /// the viewport top, approaching 1.0 near the block's bottom edge.
    /// Generalizes `setFontScale()`'s P3.5 "top visible block" re-anchor
    /// concept (which snaps to a block's top edge only) with a
    /// within-block fraction, so `EditorWidget::saveEphemeralState()` can
    /// restore scroll position more precisely than "block N's top edge".
    /// `{-1, 0.0f}` if there is nothing to anchor to (no document, or no
    /// entries yet).
    std::pair<int, float> scrollAnchor() const;
    /// Inverse of `scrollAnchor()`: scrolls so `blockIndex`'s top edge
    /// sits `fraction` (clamped to `[0, 1]`) of the block's height below
    /// the viewport top. `blockIndex` is clamped to `[0, blockCount() - 1]`
    /// — restoring against a document shorter than the one the state was
    /// captured from lands on the last block rather than doing nothing.
    /// No-op if the current document has no blocks at all.
    void setScrollAnchor(int blockIndex, float fraction);

    // ---- Selection (T5) --------------------------------------------------
    // Inspection for tests; edited only through real events (mouse drag,
    // Shift+move, Ctrl+A), same rule as the caret.

    bool    hasSelection() const;
    BlockId selectionAnchorBlock() const;
    int     selectionAnchorByteOffset() const;

    // ---- Find highlights (contract-v2 P3.4) ------------------------------

    /// Replace all find-match highlights to paint. Draw-time
    /// `QTextLayout::FormatRange` only, added in `paintEvent` alongside the
    /// selection range built there — never a `QTextCharFormat`/`setFormats`
    /// mutation of the layout itself (spec §3: no second per-character
    /// format store; the layout stays a pure derived cache). Passing an
    /// empty list clears all highlight paint state, which is exactly what
    /// `EditorWidget::detachFindController` does. Table-cell matches (T9)
    /// are a known gap shared with selection's own table limitation — not
    /// special-cased here; `paintTable` does not consult this list.
    void setFindHighlights(const QList<FindHighlight> &highlights);

    /// Test/inspection surface only — nothing here is authority (same rule
    /// as the rest of this section). The highlights currently set for
    /// `id`, in the order last passed to `setFindHighlights`.
    QList<FindHighlight> findHighlightsForBlock(BlockId id) const;

    // ---- Selection-occurrence highlights (P7.2e, F1 #7) -------------------

    /// Test/inspection surface only — nothing here is authority (same rule
    /// as `findHighlightsForBlock`). The block-relative byte ranges of
    /// OTHER occurrences of the current selection's text currently
    /// highlighted in `id` — recomputed automatically by
    /// `recomputeOccurrenceHighlights()` (called from
    /// `pushSelectionToSession()`, the existing selection-change
    /// chokepoint); there is no public setter, unlike `setFindHighlights`,
    /// because this feature is entirely view-local/selection-derived
    /// rather than something an external controller (`FindController`)
    /// drives. Never includes the active selection's own span.
    QList<std::pair<int, int>> occurrenceHighlightsForBlock(BlockId id) const;

    // ---- Remote presence (contract-v2 P6.2) ------------------------------

    /// Replace all remote-participant presences to paint: caret bar + name
    /// flag (drawn by hand — `QTextLayout::drawCursor` only draws the LOCAL
    /// caret, in the theme's own color, and takes no per-call color) plus a
    /// selection tint in the participant's own color, all draw-time
    /// `QTextLayout::FormatRange`s built fresh in `paintEvent` alongside the
    /// local selection/find-highlight ranges (spec §3: the layout stays a
    /// pure derived cache, never a `setFormats()` mutation). Resolved from
    /// each `Selection`'s `TextAnchor`s against the CURRENT document on
    /// every paint — never cached CanvasCursor state — so an anchor whose
    /// block no longer exists (or was never realized) just fails to resolve
    /// and is silently skipped; stale/departed-participant filtering
    /// (collabtext `PresenceManager::is_live`) is the consumer's job, not
    /// this leaf's. Passing an empty list clears all presence paint state.
    void setRemotePresences(const QList<RemotePresence> &presences);

    /// Test/inspection surface only — nothing here is authority (same rule
    /// as `findHighlightsForBlock`). The subset of the last-set presences
    /// (`Selection::Kind::Presence` only) whose resolved anchor/active range
    /// touches block `id`, in the order last passed to `setRemotePresences`.
    /// A presence whose anchor(s) don't currently resolve (block gone/never
    /// realized) is omitted, same as it would be at paint time.
    QList<RemotePresence> remotePresencesForBlock(BlockId id) const;

    // ---- Scroll past end, empty-doc placeholder, bracket-match, drop-
    // cursor (P7.2f, F1 #8/#10) ---------------------------------------------

    /// True with no document, no blocks, or exactly one block that is both
    /// `BlockKind::Paragraph` and empty (`blockText().isEmpty()`) — the
    /// shape a brand-new document loads as. CM `view/src/placeholder.ts`
    /// reference: its `decorations` getter gates purely on
    /// `view.state.doc.length`, nothing else — no focus check anywhere in
    /// that source (verified by reading it, not assumed) — so
    /// `paintEvent`'s placeholder paint is gated on this alone, independent
    /// of `m_hasFocus`. That is a deliberate divergence from this same
    /// file's title-band placeholder (`paintTitle`'s `m_titleCaretActive`
    /// gate), which IS focus-gated — different feature, not a copy of it.
    bool isDocumentEmpty() const;

    /// Test/inspection surface only (same "nothing here is authority" rule
    /// as `findHighlightsForBlock`/`remotePresencesForBlock`). Resolves the
    /// bracket pair around the current caret, or `{}` if the caret is not
    /// collapsed (an active selection), sits in no block, or is not
    /// immediately adjacent to a bracket with a findable match. CM
    /// reference: `language/src/matchbrackets.ts` `matchBrackets` — checks
    /// the byte immediately before the caret first, then immediately after;
    /// scans for the matching bracket accounting for nesting depth. Scoped
    /// to `()`/`[]` only (the plan's named pairs, not CM's `{}` default) and
    /// to ONE block (C4) — a caret next to a bracket with no findable match
    /// within that same block's text (unbalanced, or the match would be in
    /// a different block — impossible here since a block's own text can
    /// never contain another block's bytes) resolves to `{}` rather than a
    /// half-filled result; this leaf does not paint CM's own "non-matching"
    /// visual style, logged simplification.
    std::optional<BracketMatch> bracketMatchAtCaret() const;

    /// Test/inspection surface only. The hit-tested caret the drop-cursor
    /// indicator is currently painted at, or `{}` between drags / after
    /// `dragLeaveEvent`/`dropEvent`. See `m_dropCursorPos`'s doc comment.
    std::optional<CanvasCursor> dropCursorPosition() const;

    // ---- Links (contract-v2 P4.2) ---------------------------------------

    /// Consumer-owned link resolution/activation authority (spec §5.2).
    /// Not owned; nullptr (the default) disables link click/hover handling
    /// entirely — hit-tested link/wikilink/tag spans are simply never
    /// looked up against a service. Mirrors
    /// `Markoff::Live::LiveListModelBinding::setLinkService` /
    /// `Markoff::Styled::LinkInteraction::setLinkService`: this view calls
    /// `LinkService::activate`/`notifyHover`/`notifyHoverLeft` directly and
    /// emits no wrapper signals of its own — consumers connect to
    /// `Markoff::LinkService::linkActivated`/`linkHovered`/`linkHoverLeft`.
    void setLinkService(Markoff::LinkService *service);
    /// Forwarded into every `LinkActivation::fromContext` (e.g. the current
    /// note's path, for relative wikilink resolution). Mirrors live/styled's
    /// `setFromContext`.
    void setFromContext(const QString &context);

    // ---- Format verbs (contract-v2 P4.3) ---------------------------------
    // Each op is a thin per-block driver over the new
    // `<markoff/core/FormatOps.h>` per-block overloads (C4: no flat/global
    // byte offsets). No-op while read-only or with no document/caret. A
    // caret-only call (no selection) inserts a paired-marker template and
    // parks the caret inside it, matching the old leaves' behavior; a
    // selection spanning multiple blocks applies the op independently to
    // each covered block's own local byte range — never a cross-block byte
    // sum — and collapses the selection to the trailing edge afterward
    // (mirrors `FormatOps::wrapToggle`'s own multi-slice tail behavior).

    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleInlineCode();
    /// Selection: wraps `[selection](url)` and leaves `url` selected for
    /// easy replace. No selection: inserts `[](url)` and parks the caret
    /// between `[]`.
    void insertLink();
    /// Acts on the caret's own block only (headings are never multi-block).
    /// `level` 0 demotes to a plain paragraph (strips the ATX prefix);
    /// 1..6 sets/replaces it. Out-of-range levels are a no-op.
    void setHeadingLevel(int level);

    // ---- Context menu (contract-v2 P4.4) ---------------------------------

    /// Consumer-owned format-verb actions the context menu's format section
    /// is built from (`buildContextMenu`). Not owned; nullptr (the default)
    /// omits the format section entirely rather than adding disabled/dead
    /// items — same "reference, not owner" role `setLinkService` plays.
    /// `EditorWidget` wires this to its own `CanvasActionController` right
    /// after construction.
    void setActionController(CanvasActionController *controller);

    // ---- Inline title (contract-v2 P4.9, spec §5.2) ----------------------
    // Optional leading title band (Obsidian's `.inline-title`): the file
    // basename, editable, NOT a document block. Off by default. Rendered as
    // a leading non-block entry in the y-layout, ahead of block 0, sharing
    // the content column (P4.5's `layoutWidthFor()`/`pageMargin()`) — see
    // `titleBandHeight()`'s doc comment for how every block-y consumer in
    // this file accounts for the extra offset. Deliberately owns no
    // BlockId, no byte-offset coordinate, and is never added to
    // `BlockLayoutCache`'s entries: `cursorPosition()` (EditorWidget, which
    // walks `doc->iterateBlocks()`), find (`FindController`, document-only),
    // selection-copy (`selectedText()`, cache-entries-only) and
    // serialization (core, blocks-only) exclude it BY CONSTRUCTION — there
    // is no second index space to gate (C4 stays about document content
    // only).

    /// Sets the title text shown in the band. The PROGRAMMATIC setter (a
    /// consumer syncing after an external rename, or the initial file name)
    /// — does not emit `titleEdited` (that signal is reserved for edits the
    /// user made through the band itself, same "the view never echoes back
    /// what the consumer just pushed" shape as `setTheme`/`setReadOnly`).
    /// No-op if unchanged.
    void setInlineTitle(const QString &title);
    QString inlineTitle() const { return m_inlineTitle; }

    /// Shows/hides the band. Off by default (spec §5.2). Reflows the
    /// document below it (title band height counts toward
    /// `documentHeight()`/the scroll range) the same way a content-width
    /// policy change does. No-op if unchanged.
    void setInlineTitleVisible(bool visible);
    bool inlineTitleVisible() const { return m_inlineTitleVisible; }

    // ---- Accessible document name (G1 spec §9 Q2) -------------------------
    // The ONE deliberate exception to the a11y arc's "no new public API"
    // non-goal: only the embedder knows what the open document is actually
    // called (file name, note title, …), and the a11y container's
    // `QAccessible::Name` needs *something* to say. Purely a name-resolution
    // input — not rendered anywhere in the view, not persisted, not the same
    // thing as the inline title band above (that IS rendered). Resolution
    // order, implemented in `Detail::CanvasAccessible::text()`:
    // `accessibleDocumentName()` -> `inlineTitle()` -> a generic fallback.
    void setAccessibleDocumentName(const QString &name);
    QString accessibleDocumentName() const { return m_accessibleDocumentName; }

signals:
    /// Fired only on a user edit made directly in the title band (typing,
    /// Backspace/Delete) — never from `setInlineTitle`. The consumer turns
    /// this into a file rename (spec §5.2); this view has no rename
    /// authority of its own.
    void titleEdited(const QString &title);

    /// Fired from `ensureCaretVisible()` (P3.2) — the file's single
    /// chokepoint every caret-changing code path already calls afterward
    /// (T2/T7 comment on that function), now also called from
    /// `onDocumentChanged()` so a document-driven clamp (remote edit,
    /// undo/redo landing on a different block) notifies too. Unconditional
    /// — `caretBlock()`/`caretByteOffset()` may not have actually changed
    /// from the caller's point of view (e.g. re-realizing the same block);
    /// `EditorWidget` does the real change-gating in `CursorPos` space, the
    /// coordinate space its own `cursorPositionChanged` contract is in.
    void caretChanged();

    /// Fired from `setReadOnly()` on an actual change (gated, unlike
    /// `caretChanged()`). Contract-v2 seam (P4.3): the read-only half of
    /// `CanvasActionController::updateEnabledStates`'s enabled-state
    /// wiring — the other half rides `caretChanged`/the document's
    /// `d2DocumentChanged`, neither of which fires on a read-only flip by
    /// itself.
    void readOnlyChanged(bool ro);

    /// Fired when a drop carries one or more local-file URLs (P7.2, spec
    /// "drag-drop"). This view has no opinion on embed-vs-link — that is
    /// explicitly the consumer's decision (Corbomite) — so the payload is
    /// just the dropped URLs and the viewport-local drop position; nothing
    /// is inserted into the document by this leaf on a file drop. Text
    /// drops (`text/plain`/`text/markdown`) are handled entirely inside
    /// `dropEvent` instead and never reach this signal.
    void fileDropped(const QList<QUrl> &urls, const QPoint &viewportPos);

    /// Ctrl+Scroll-to-zoom (P7.3/[cluster-k] P3): `View` has no `fontScale`
    /// authority of its own — that's owned by the composed `MarkdownView`
    /// wrapper (`EditorWidget`) per contract-v2, the same seam
    /// MainWindow's View-menu Zoom In/Out actions and the QML leaf's
    /// window-level Ctrl+=/Ctrl+- shortcuts already drive via
    /// `setFontScale(fontScale() * kZoomStep)`. `wheelEvent` can't just
    /// mutate `m_fontScale` in place the way `setFontScale()` does — doing
    /// so would leave the wrapper's own clamped `fontScale()` state stale,
    /// so the NEXT menu/keyboard zoom would jump from the wrong baseline.
    /// It asks upward instead; `steps` is `+1`/`-1` (the sign of
    /// `QWheelEvent::angleDelta().y()`), never a raw pixel/angle delta —
    /// one wheel "notch" is one zoom step, matching every other zoom
    /// trigger's discrete-step shape.
    void fontScaleStepRequested(int steps);

    /// Ctrl+0 reset-zoom (follow-up to the Ctrl+Scroll/[cluster-k] P3
    /// fix): same ask-upward shape as `fontScaleStepRequested` and for the
    /// same reason (no local `fontScale` authority to reset in place) —
    /// kept as its own signal rather than overloading
    /// `fontScaleStepRequested(0)` so a `steps == 0` call can never be
    /// mistaken for "reset" by a future connection.
    void fontScaleResetRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;
    /// QWidget::event() intercepts a plain Tab/Backtab QKeyEvent and calls
    /// focusNextPrevChild() BEFORE keyPressEvent() ever runs — so without
    /// this override, keyPressEvent's own Tab handling (literal tab
    /// insertion / list indent / table-cell nav) is unreachable whenever
    /// there's another widget to focus, e.g. the toolbar. Always returning
    /// false here is the standard "this widget owns Tab" technique (same
    /// effect as QPlainTextEdit::setTabChangesFocus(false)), so control
    /// always reaches keyPressEvent instead.
    bool focusNextPrevChild(bool next) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    /// Word-select (P7.3/[cluster-k] P3): a hit on an actual word selects
    /// its `QTextBoundaryFinder::Word` range (see `wordRangeAt` at the top
    /// of View.cpp — the same boundary-finder idiom `moveCaretByWord`
    /// already commits to, not a hand-rolled char-class scan); a hit on
    /// whitespace/an empty region just places the caret, matching a plain
    /// click. Either way it arms the triple-click detector
    /// (`m_pendingTripleClickBlock`'s own doc comment) — Qt has no native
    /// triple-click event, so `mousePressEvent` is what actually turns the
    /// THIRD physical click into a paragraph-select.
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /// Ctrl+Scroll-to-zoom (P7.3/[cluster-k] P3): Ctrl held routes through
    /// `fontScaleStepRequested` (see its own doc comment for why this view
    /// can't just call `setFontScale` on itself) and consumes the event;
    /// otherwise falls through to `QAbstractScrollArea`'s normal wheel
    /// scrolling, unchanged.
    void wheelEvent(QWheelEvent *event) override;
    /// P7.2: accepts a drag iff it carries `text/plain`, `text/markdown`,
    /// or local file URLs — same acceptance test `dropEvent` re-checks (Qt
    /// re-queries accept per drag-move, so there is no cached "will
    /// accept" flag to keep in sync).
    void dragEnterEvent(QDragEnterEvent *event) override;
    /// P7.2f (F1 #10): also updates `m_dropCursorPos` to the hit-tested
    /// caret under the pointer and repaints, so the drop-cursor indicator
    /// tracks every move — CM reference `view/src/dropcursor.ts`
    /// (`dragover` re-measures on every event; there is no cheaper "did it
    /// actually move" gate in that source either).
    void dragMoveEvent(QDragMoveEvent *event) override;
    /// P7.2f (F1 #10): clears `m_dropCursorPos` and repaints — CM's own
    /// `dragleave` handler does the same (`dropcursor.ts`). `QAbstractScrollArea`
    /// does not synthesize this from `dragMoveEvent` stopping; Qt fires it
    /// independently when the pointer leaves the widget or the drag is
    /// cancelled without a drop.
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    /// Text drops route the dropped string through the same `insertText`
    /// helper `paste()` uses (line-split + `tryStructuralKey`/
    /// `insertPrintable`), after moving the caret to the drop point via
    /// `hitTest`. File drops (local `QUrl`s) never touch the document —
    /// they surface as `fileDropped` for the consumer to decide
    /// embed-vs-link (spec: this leaf has no such authority).
    void dropEvent(QDropEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    /// Builds the standard edit context menu directly onto `menu`: cut/
    /// copy/paste/select-all, a separator, the format-verb section (from
    /// `setActionController`'s controller, if one is attached — omitted
    /// entirely otherwise), and "Copy Link Target" when the triggering
    /// right-click landed on a link/wikilink/tag span (spec §5.2 "Context
    /// menu": standard edit menu, extensible by consumer). Protected
    /// virtual so a consumer subclass can override to call the base
    /// implementation and then append its own items — the sanctioned
    /// extension seam this task names. Populates `menu` in place rather
    /// than returning one so an override can insert items anywhere in the
    /// sequence, not just append.
    virtual void buildContextMenu(QMenu &menu);
    /// Viewport `QEvent::Leave` only reaches this widget via an event
    /// filter — `QAbstractScrollArea::viewportEvent` does not forward
    /// Enter/Leave the way it forwards mouse-button/move events (same
    /// reason styled's `LinkInteraction` installs one). Used solely to
    /// close out an in-progress link hover when the pointer leaves the
    /// viewport without a `mouseMoveEvent` ever reporting "no link here".
    bool eventFilter(QObject *obj, QEvent *event) override;
    /// Builds the `QMimeData` a text drag-out (`mouseMoveEvent`) carries:
    /// `text/plain` (via `QMimeData::setText`) and `text/markdown` set to
    /// the same bytes — both views of one `selectedText()` snapshot, not
    /// two different serializations. Caller owns the returned object;
    /// `nullptr` if there is no selection. Protected (not private) so it
    /// is independently testable without driving a real, blocking
    /// `QDrag::exec()` — same "expose the seam, don't fake the modal loop"
    /// reasoning `buildContextMenu` already follows for the context-menu
    /// tests.
    QMimeData *createMimeDataFromSelection() const;

private:
    void onDocumentChanged();
    /// Realize whatever the current viewport needs and refresh the scroll
    /// range. Safe to call repeatedly; realization is monotonic.
    void ensureLayoutForViewport();
    void updateScrollRange();
    qreal pageMargin() const;
    qreal textWidth() const;
    /// The one place a viewport width turns into an actual content-column
    /// pixel width (P4.5's named falsification target): `FullWidth` is the
    /// full available width (viewport minus the two page margins);
    /// `FixedColumn` is the smaller of that and the policy's fixed width,
    /// so a viewport narrower than the fixed column still shrinks it
    /// rather than overflowing. `pageMargin()` centers whatever's left
    /// over evenly on both sides.
    qreal layoutWidthFor(qreal viewportWidth) const;
    /// Shared by `resizeEvent()` and `setContentWidthPolicy()`: re-derives
    /// layout for the current viewport/content width while keeping the
    /// block that was at the top of the viewport pinned there (the same
    /// anchor `scrollAnchor()`/`setScrollAnchor()` expose for P3.6's
    /// ephemeral-state round-trip) — a bare `ensureLayoutForViewport()`
    /// call alone would leave the scroll position at its old raw pixel
    /// value, which means something different once every visible block's
    /// height has reflowed under the new width.
    void reflowKeepingScrollAnchor();

    // ---- Caret / editing (T2) -------------------------------------------

    /// Block + byte offset under a viewport-coordinate point. Null block
    /// if the document is empty.
    CanvasCursor hitTest(const QPoint &viewportPos) const;
    /// hitTest()'s Table-block branch (T9): locates the (row, col) cell
    /// under the point in the cache entry at `entryIndex`, then hit-tests
    /// within that cell's own layout. Takes an index rather than the cache
    /// Entry type itself so this header doesn't need BlockLayoutCache.h
    /// (private to src/).
    CanvasCursor hitTestTable(int entryIndex, const QPoint &viewportPos,
                              qreal scrollY) const;
    /// paintEvent()'s Table-block branch (T9): draws the grid lines and
    /// each cell's layout, plus the caret if it is inside this table.
    /// `blockTop`/`contentX` are viewport-relative, same convention as
    /// paintEvent's own locals.
    void paintTable(QPainter &p, int entryIndex, qreal blockTop, qreal contentX) const;
    void setCaret(const CanvasCursor &caret);
    /// Session caret authority closure (P6.1, guide §B): pushes the
    /// current `m_caret`/`m_selectionAnchor` to the attached Session as
    /// an anchor-typed `Markoff::Selection` — the view-agnostic form
    /// `Session::primarySelection()` exposes to external readers (a
    /// remote-presence consumer, P6.2's paint path, or this task's own
    /// falsification test). No-op if no Session is attached (bare `View`
    /// in tests/spike-era consumers that never call `setSession`). Called
    /// from `setCaret()` (the caret-move chokepoint) and from every
    /// direct `m_selectionAnchor` mutation site that doesn't already
    /// funnel through `setCaret()` in the same operation — see the
    /// plan's P6.1 findings-log entry for the full call-site audit
    /// (arrow-key motion, deleteCluster, insertPrintable, and
    /// tryStructuralKey turned out NOT to funnel through `setCaret()`
    /// either, contrary to that method's own doc comment — pushed from
    /// directly).
    void pushSelectionToSession();
    /// P7.2e (F1 #7): rebuilds `m_occurrenceHighlightsByBlock` from the
    /// current selection's text — every OTHER occurrence of that exact
    /// text in the realized entries (`m_cache->entries()`; matches
    /// `setFindHighlights`'/other paint-time features' own realized-only
    /// scoping, spec §4/§5.3 note), case-sensitive, whole-word NOT
    /// required (CM `highlightSelectionMatches` defaults), skipping
    /// selections shorter than `kMinOccurrenceSelectionLength` or that are
    /// whitespace-only. Called from `pushSelectionToSession()` — the
    /// existing chokepoint every selection-changing call site in this file
    /// already funnels through — unconditionally, ahead of that function's
    /// own `m_session`-gated early return, so occurrence highlights update
    /// even in bare-`View` (no-Session) test/spike-era consumers.
    void recomputeOccurrenceHighlights();
    void moveCaretHorizontally(bool forward);
    void moveCaretVertically(bool forward);
    /// moveCaretVertically()'s Table-block branch (P5.1): Up/Down inside a
    /// table walks line-in-cell, then row-in-column (same column), then
    /// falls through to the generic cross-block exit once the caret is off
    /// the table's top/bottom row entirely. `idx` is the caret's entry
    /// index in the cache, same convention as hitTestTable/paintTable.
    void moveCaretVerticallyInTable(int idx, bool forward);
    /// Folding (P5.6): the next entry index in `forward`/backward document
    /// order that is NOT `Entry::folded`, or -1 if there is none.
    /// `moveCaretHorizontally`/`moveCaretVertically` route their
    /// cross-block index step through here instead of a bare `idx +- 1` so
    /// caret motion steps OVER a folded range's now-invisible content
    /// rather than landing inside it.
    int nextVisibleEntryIndex(int idx, bool forward) const;
    void moveCaretToLineEdge(bool home);
    /// P7.2b (F1 #1): word-wise motion (Ctrl+Left/Right), same real/
    /// reveal-aware layout stepping and cross-block-landing convention as
    /// moveCaretHorizontally() above, just stepping by QTextBoundaryFinder
    /// word boundary instead of one grapheme cluster.
    void moveCaretByWord(bool forward);
    /// P7.2b (F1 #1): Ctrl+Home/End — caret to byte 0 of the first block /
    /// end-of-text of the last block (CodeMirror's cursorDocStart/End).
    void moveCaretToDocumentStart();
    void moveCaretToDocumentEnd();
    void insertPrintable(const QString &text);
    void deleteCluster(bool forward);
    /// P7.2b (F1 #1): Ctrl+Backspace/Delete — same shape as deleteCluster()
    /// above, word boundary instead of a single grapheme cluster; no-op at
    /// a block edge (tryStructuralKey() already owns the boundary-merge
    /// case regardless of the Ctrl modifier — StructuralKeyHandler doesn't
    /// key on modifiers).
    void deleteWordCluster(bool forward);
    /// Route a key through StructuralKeyHandler (Enter split, boundary
    /// Backspace/Delete merge, Tab/Shift+Tab list indent) before any other
    /// handling. Returns true if the handler owned the key — it has
    /// already applied the mutation and the caret has already been moved
    /// to `r.caretBlock`/`r.caretByteInBlock`.
    bool tryStructuralKey(QKeyEvent *event);
    /// Table Tab/Shift+Tab cell navigation (P5.1). False if the caret isn't
    /// inside a table (StructuralKeyHandler/the generic key switch handle
    /// Tab everywhere else); true means this call owned the key and has
    /// already moved the caret (and possibly mutated the document — see
    /// appendTableRow()).
    bool tryTableTab(bool shift);
    /// Appends one empty row (P5.1's "last cell Tab appends a row",
    /// Obsidian behavior) to `block`'s buffer as a single d2ApplyBufferEdit
    /// transaction — `cols` empty cells, syntactically valid for
    /// TableGeometry's tokenizer.
    void appendTableRow(BlockId block, int cols);
    /// Shared by every P5.2 row/col/alignment op: re-parses `block` (via
    /// `TableGeometry`, already reflecting the just-applied edit — same
    /// "read back off the fresh buffer" discipline `appendTableRow()`
    /// established) and lands the caret at `row`/`col`'s cell start, both
    /// clamped into range. Generalizes `appendTableRow`'s own inline
    /// version of this because these ops (unlike a plain append) can shift
    /// bytes BEFORE the caret's own row — a bare byte-offset clamp isn't
    /// enough to keep the caret in the same logical cell.
    void repositionCaretInTable(BlockId block, int row, int col);
    /// Keep the caret referencing a block that still exists after a
    /// document change, biased toward the block's last known position
    /// (T2's version of the queue-#10 "never strand the caret" clamp;
    /// load-bearing again in T4).
    void clampCaret(int oldCaretIndexHint);
    void ensureCaretVisible();
    /// The scrollbar-clamp half of `ensureCaretVisible()`, split out
    /// (F3/find-next fix, punch-list [cluster-k]) so `ensureLayoutForViewport()`'s
    /// fixed-point loop can re-run it after `realizeRange()` turns estimated
    /// block heights into real ones, without also re-restyling the caret's
    /// delimiter visibility or re-emitting `caretChanged()` on every paint —
    /// see the call site's comment for the estimate-vs-real-height mechanism
    /// this closes.
    void scrollCaretIntoView();
    /// Shared math behind `caretRect()` and `inputMethodQuery`'s
    /// `Qt::ImCursorRectangle` case — viewport-local pixel rect of the
    /// caret in the realized entry's layout. Invalid QRect if unrealized.
    QRect caretRectInViewport() const;

    // ---- Kind transitions (T6) -------------------------------------------

    /// Caret's-block-only kind promotion (spec/plan T6, spike scope: no
    /// document-wide scan). Infers the kind from the caret block's current
    /// text and issues `d2SetBlockKind` on mismatch. Only promotes FROM
    /// Paragraph — a structural kind's buffer is content-only (ListItem,
    /// BlockQuote) or carries its own marker already (Heading, CodeBlock)
    /// and would otherwise re-infer Paragraph. Never strips the matched
    /// marker from the buffer (T1 finding, spec §9: a loaded Heading/
    /// CodeBlock keeps its ATX prefix/fence, so a typed one must too, or
    /// the two representations diverge).
    ///
    /// Kind-defining attrs (Heading level + form, Math display mode) are
    /// written in the same `UndoLog::Transaction` as the kind (P1.1).
    void promoteCaretBlockKind();

    /// Keeps an already-promoted Heading's `level` attr tracking its buffer
    /// (the first `#` promotes, so `##`…`######` never reach the promote
    /// path). Form-aware: ATX counts hashes, setext reads the underline.
    /// Only raises a level it can infer — demotion is the structural-key
    /// path's business, not this one's.
    void updateCaretHeadingLevel();

    /// Re-infers display-mode within an already-Math block (P5.3): the
    /// first '$' promotes to Math (inline) before a second '$' can ever
    /// reach the Paragraph-only promote path, so this is the only place a
    /// typed "$$…$$" can set DisplayMode true. Raise-only, same shape as
    /// updateCaretHeadingLevel.
    void updateCaretMathDisplayMode();

    // ---- Selection (T5) --------------------------------------------------

    /// Document-order comparison of two carets via the cache's block index.
    /// Ties (same block) compare by byte offset.
    bool caretLessThan(const CanvasCursor &a, const CanvasCursor &b) const;
    /// Anchor/caret in document order, {} if there is no selection (no
    /// anchor, or anchor == caret).
    std::optional<std::pair<CanvasCursor, CanvasCursor>> orderedSelection() const;
    /// The selected byte sub-range of `id`'s text, given the already-
    /// ordered selection endpoints. Empty range if `id` is not selected.
    std::pair<int, int> selectedByteRangeInBlock(
        BlockId id, const CanvasCursor &start, const CanvasCursor &end) const;
    QByteArray selectedText() const;
    /// Delete the selected range and merge its boundary blocks in one
    /// transaction (plan T5: per-block deletes + a structural-path merge,
    /// no cross-block byte math). Caret lands at the first corner; the
    /// selection is cleared. No-op if there is no selection.
    void collapseSelection();

    // ---- Remote presence (P6.2) -------------------------------------------

    /// Resolves a Presence Selection's TextAnchor against the CURRENT
    /// document: `{}` if the anchor is null or its origin block
    /// (`TextAnchor::block()`) is not a live cache entry (departed/gone
    /// content) — same "resolve, or nullopt" contract `onDocumentChanged`'s
    /// P6.1 workaround uses (`MarkoffDocument::blockAt` is dead code — see
    /// that finding), reused here rather than re-derived. Never cached;
    /// called fresh every paint.
    std::optional<CanvasCursor> resolvePresenceAnchor(const Markoff::TextAnchor &anchor) const;

    // ---- Bracket-match highlight (P7.2f, F1 #10) --------------------------

    /// Scans `block`'s own text (never a different block — C4) for the
    /// bracket matching the one at byte `pos`, honoring nesting depth.
    /// `pos` must itself be a bracket byte in `()`/`[]`; anything else
    /// returns `{}`. Direction is inferred from which half of the pair
    /// `pos` names: an opener scans forward, a closer scans backward — same
    /// shape as CM's `matchPlainBrackets`, just without a syntax tree to
    /// consult first (this leaf has none to offer it).
    std::optional<BracketMatch> findMatchingBracket(BlockId block, int pos) const;

    // ---- Cut/copy/paste/select-all (P4.4) --------------------------------
    // Shared by the keyboard shortcuts (keyPressEvent's existing
    // Ctrl+A/C/X, plus the Ctrl+V this task adds) and the context menu's
    // corresponding QActions — one implementation of each op, not a second
    // copy behind the menu.

    /// No-op while read-only or with no selection (Cut is disabled in its
    /// entirety while read-only, including the copy-to-clipboard half —
    /// mirrors Qt's own disabled-Cut-action convention).
    void cut();
    /// No-op with no selection. Unaffected by read-only (spec §4.2: copy
    /// keeps working).
    void copy();
    /// Inserts the system clipboard's text at the caret, replacing any
    /// active selection first. No-op while read-only, with no document/
    /// caret, or with empty clipboard text. Delegates the actual insertion
    /// to `insertText()` (P7.2 factor-out — see its doc comment).
    void paste();
    /// Selects the whole document (first block byte 0 through the last
    /// block's end). No-op with no document or no blocks.
    void selectAll();

    // ---- Editing-command floor (P7.2b, F1 #1) -----------------------------

    /// CodeMirror's selectLine (Alt+L). Selects the caret's entire block
    /// (byte 0 through its end), same shape as selectAll() scoped to one
    /// block. No-op with no document/caret.
    void selectLine();
    /// CodeMirror's deleteLine (Ctrl+Shift+K). Clears the caret's block's
    /// content — the block itself is NOT removed (see .cpp doc comment for
    /// why that reading was chosen over CodeMirror's own "remove the whole
    /// line" semantics). No-op with no document/caret or an already-empty
    /// block.
    void deleteLine();
    /// CodeMirror's moveLineUp/moveLineDown (Alt+Up/Alt+Down). Swaps the
    /// caret's block's content (text/kind/attrs) with its previous (`down`
    /// false) or next (`down` true) sibling in document order — a content
    /// swap between existing BlockIds, not a real IdList reorder (core has
    /// no reorder primitive; see .cpp doc comment). No-op at a document
    /// edge or with no document/caret.
    void moveLine(bool down);

    // ---- Auto-pairing / wrap-selection (P7.2c, F1 #4) --------------------
    // CodeMirror reference: autocomplete/src/closebrackets.ts
    // (insertBracket/deleteBracketPair/closeBracketsKeymap). Scope guard:
    // exactly the 5 pairs the plan names — (/), [/], "/", `/`, and the
    // Markdown-specific bold wrap **/** — no {}, no ''. Insertion always
    // routes through insertPrintable() (Cmd::insertCharacter underneath),
    // never a parallel bare-Transaction insert path: P7.2a's fix and the
    // out-of-band collab regression it exposed both live in that exact
    // machinery, and a second insertion path here would bypass undo-
    // coalescing and risk the same class of onCommit-drops-ops bug.
    // Deletion (tryDeleteFreshPair) is not subject to that constraint —
    // deleteCluster() itself already opens a bare UndoLog::Transaction for
    // deletes, unrelated to the coalescing machinery, and this mirrors it.

    /// One tracked "just auto-inserted" closer run: `byteOffset` is the
    /// closer's first byte in `block`, `length` its byte count (1 for the
    /// 4 single-char pairs, 2 for the bold `**` close — all 5 markers are
    /// ASCII, so byte count == QChar count throughout this section). See
    /// m_autoPairedClose's own doc comment for the view-state justification
    /// (spec §2) and the freshness rule this type exists to encode.
    struct AutoPairedCloser {
        BlockId block;
        int byteOffset = -1;
        int length = 0;
    };

    /// Intercepts the printable-key path for the 5 named pair triggers
    /// BEFORE the generic isPrintable dispatch reaches insertPrintable().
    /// Returns true (and has already applied whatever mutation/caret-move
    /// is appropriate) for: wrap-selection (a pair's open char typed with
    /// an active single-block selection), auto-pair (a pair's open char
    /// typed with no selection), and type-through (a pair's close char, or
    /// `*` completing bold, typed with the caret immediately before a
    /// tracked auto-inserted closer — see m_autoPairedClose). Returns false
    /// for every other key (including a bare, untracked `*` or a closer
    /// with nothing tracked at the caret), leaving the event to fall
    /// through to the ordinary printable path unchanged. No-op (returns
    /// false) while composing (IME preedit is not touched by this leaf's
    /// own bracket logic) or read-only (caller only reaches this after the
    /// read-only gate already passed, but composing is checked here since
    /// nothing upstream does).
    bool tryAutoPairOrWrap(QKeyEvent *event, const std::optional<AutoPairedCloser> &pendingClose);
    /// Backspace special case (point 3): if `pendingClose` names a closer
    /// run starting exactly at the caret's current position (i.e. the
    /// caret sits immediately between an open/close pair that the
    /// PRECEDING keystroke just auto-inserted — see m_autoPairedClose's
    /// doc comment for why "preceding keystroke" is the exact freshness
    /// test), deletes both the opener (immediately before the caret) and
    /// the closer (immediately after) as one transaction, caret landing
    /// where the opener's first byte was. Returns false (no-op) otherwise,
    /// leaving Backspace to the ordinary deleteCluster()/tryStructuralKey()
    /// handling.
    bool tryDeleteFreshPair(const std::optional<AutoPairedCloser> &pendingClose);
    /// Inserts `openStr` + `closeStr` at the caret via insertPrintable
    /// (ASCII-only pair markers, so byte length == QChar count), then walks
    /// the caret back to sit between them and records the closer's
    /// position in m_autoPairedClose.
    void performAutoPair(QLatin1String openStr, QLatin1String closeStr);
    /// Wraps the current single-block selection in `openStr`/`closeStr`
    /// (CodeMirror's own wrap behavior: the originally-selected text stays
    /// selected, now sitting between the two inserted marks). Caller
    /// (tryAutoPairOrWrap) has already confirmed the selection exists and
    /// does not cross a block boundary — multi-block wrap is out of scope
    /// this task (logged simplification; falls through to the ordinary
    /// collapse-and-replace behavior instead).
    void wrapSelectionInPair(QLatin1String openStr, QLatin1String closeStr);

    // ---- Drag-drop + middle-click paste (P7.2) ----------------------------
    // Text drag out reuses `selectedText()` (same bytes `copy()` already
    // puts on the clipboard); text drag in, file drop, and X11
    // primary-selection middle-click paste all reuse `insertText()` below
    // rather than growing a second text-insertion path per gesture.

    /// The shared body `paste()`, the drop handler, and middle-click paste
    /// all call: normalizes line endings, splits on '\n', and routes each
    /// line-break through `tryStructuralKey()` (the same path a real Enter
    /// keystroke uses) with each line's content going through
    /// `insertPrintable()`. Collapses any active selection first. No-op
    /// while read-only, with no document/caret, or with empty `text`.
    void insertText(const QString &text);
    /// Builds a `QDrag` from `createMimeDataFromSelection()` and execs it
    /// (`mouseMoveEvent`'s drag-threshold branch calls this once the press
    /// starts moving). Copy-only while read-only; Copy+Move once editable
    /// (`qwidgettextcontrol.cpp`'s `startDrag()` shape). Move decision
    /// (P7.2 finding, logged in the plan): a successful Move whose target
    /// is NOT this view's own viewport deletes the source selection —
    /// crossing to another view/application is a real move. A Move
    /// dropped back onto this SAME view's viewport is treated as a copy
    /// instead (the source selection is left alone): deleting it would
    /// require re-deriving the now-stale source byte range after the
    /// insert already landed at a different point in the same block/
    /// document, which is exactly the kind of cross-edit offset
    /// invalidation this leaf's byte-offset model (C4) does not attempt
    /// to solve for a single gesture — logged as a scoped limitation
    /// rather than built around.
    void startTextDrag();

    // ---- Format verbs (P4.3) ----------------------------------------------

    /// Shared driver behind `toggleBold`/`Italic`/`Strikethrough`/
    /// `InlineCode` and `insertLink`: dispatches to the caret-only,
    /// single-block-selection, or multi-block-selection case, invoking
    /// `applyOne` once per covered block with that block's own local byte
    /// range (never a cross-block sum). `applyOne` is
    /// `FormatOps::wrapToggleInBlock`/`insertLinkInBlock` bound to its
    /// extra argument (the delimiter, or nothing for the link op). A
    /// single-block result restores the returned selection/caret exactly;
    /// a multi-block result collapses to the last covered block's returned
    /// caret/selection end, mirroring `FormatOps::wrapToggle`'s own
    /// multi-slice tail behavior.
    using FormatOpFn = std::function<std::optional<Markoff::FormatOps::ByteRange>(
        BlockId, Markoff::FormatOps::ByteRange)>;
    void applyFormatOp(const FormatOpFn &applyOne);

    // ---- Links (P4.2) -----------------------------------------------------

    /// The link/wikilink/tag span (if any) covering `cursor`'s byte offset
    /// in its own block, resolved into a `LinkActivation`. `std::nullopt` if
    /// `cursor`'s block is unknown/null or no such span covers the offset —
    /// independent of whether a `LinkService` is set (callers decide what
    /// nullopt-vs-no-service means; `linkActivationAt` itself only answers
    /// "is there a link span here"). Reuses `hitTest`'s `CanvasCursor`
    /// output (block + byte offset) — the ONE hit-test path in this file —
    /// rather than a second span-lookup mechanism; both `mousePressEvent`'s
    /// activation gesture and `updateHover`'s tracking call through here.
    std::optional<Markoff::LinkActivation> linkActivationAt(
        const CanvasCursor &cursor, Qt::KeyboardModifiers mods) const;
    /// Mouse-move hover tracking (P4.2): re-hit-tests at `viewportPos`,
    /// diffs against the currently-hovered link's raw text, and only on an
    /// actual state TRANSITION (a different link, or link ↔ no-link) fires
    /// `LinkService::notifyHoverLeft`/`notifyHover` and touches the
    /// viewport cursor shape. Called on every plain (no-button) mouse move,
    /// but the transition-gating above means the `setCursor`/`unsetCursor`
    /// calls do not — the "styled smell"
    /// (`libs/markoff-styled/src/LinkInteraction.cpp:handleMove` sets
    /// `QCursor` unconditionally on every move within the same link) this
    /// task was explicitly told to avoid repeating.
    void updateHover(const QPoint &viewportPos, const QPoint &globalPos);
    /// Closes out an in-progress hover (link → none): fires
    /// `notifyHoverLeft` if one was active and restores the I-beam/arrow
    /// cursor. Called from the viewport-leave event filter and whenever
    /// `updateHover` finds no link under the pointer.
    void clearHover();

    // ---- Task-list checkboxes (P4.7) ---------------------------------------

    /// The task-item BlockId whose checkbox glyph covers `viewportPos`,
    /// if any. Reuses `hitTest`'s cache-entry y-lookup (`indexAtY`) rather
    /// than a second one, then checks the click point against the same
    /// glyph rect `paintEvent`'s task-checkbox branch draws (both go
    /// through `taskCheckboxRect` in View.cpp — one geometry, not two).
    /// `nullopt` for a miss, an unrealized entry (no font metrics without a
    /// layout — matches the marker-text decoration's own realized-only
    /// requirement), or a non-task ListItem/other kind.
    std::optional<BlockId> taskCheckboxAt(const QPoint &viewportPos) const;

    // ---- Folding hit-test (P5.6) -------------------------------------------

    /// The foldable head BlockId whose fold-affordance glyph covers
    /// `viewportPos`, if any. Same shape as `taskCheckboxAt`: realized-only,
    /// checks the click point against the identical rect `paintEvent`
    /// draws and `foldAffordanceRectFor` reports (via `foldAffordanceRect`
    /// in View.cpp — one geometry, not two). `nullopt` for a miss, an
    /// unrealized entry, or a block that isn't currently foldable.
    std::optional<BlockId> foldAffordanceAt(const QPoint &viewportPos) const;

    // ---- Inline title (P4.9) ----------------------------------------------

    /// The band's height in DIPs when visible, 0 when not (spec §5.2 "off
    /// by default"). Fixed regardless of title length/content (single-line,
    /// no-wrap band — a decide-yourself-and-log simplification; Obsidian
    /// itself wraps a very long title, which this leaf does not attempt).
    /// This is the ONE offset every y-consuming function in this file adds
    /// (converting a `BlockLayoutCache` y — which starts at 0 for block 0 —
    /// into document/viewport space) or subtracts (the reverse, converting
    /// a scroll/click position into the cache's own y-space before querying
    /// it) — `hitTest()`'s and `paintEvent()`'s own comments point back
    /// here. There is no second coordinate STORE, only this one derived
    /// offset applied at every read (same "derived, not stored" shape as
    /// `layoutWidthFor()`).
    qreal titleBandHeight() const;
    /// The (bold, upscaled Heading-role) font the title band paints and
    /// hit-tests with. Scales with `m_fontScale` like block presentation
    /// does, so "bigger text" affects the title band too.
    QFont titleFont() const;
    /// Shared by `paintTitle()` and `hitTestTitle()`: lays out `text` as a
    /// single no-wrap line at `textWidth()`, in `titleFont()`. Not cached —
    /// a title is short and this runs at most once per paint/click, not per
    /// keystroke of document typing.
    void layoutTitleLine(QTextLayout &layout, const QString &text) const;
    /// True if `viewportPos` falls inside the title band's vertical extent
    /// (any x — the whole band width is a click target, matching a normal
    /// line edit). When true and `outCharPos` is non-null, fills it with
    /// the UTF-16 char offset into `m_inlineTitle` nearest the point's x —
    /// the title's own tiny coordinate space (character offsets into a
    /// QString), deliberately NOT `CanvasCursor`/BlockId/byte-offset (C4 is
    /// about document content; the title has none).
    bool hitTestTitle(const QPoint &viewportPos, int *outCharPos) const;
    /// Key handling while `m_titleCaretActive` (P4.9): printable insert,
    /// Backspace/Delete, Left/Right/Home/End within the title text: Down/
    /// Enter exit to block 0 byte 0 (the caret seam spec §5.2 names);
    /// Escape/Up exit without moving the document caret (no block sits
    /// above the title for Up to enter — spec names only the Down
    /// direction). Every mutating branch emits `titleEdited`.
    void handleTitleKeyPress(QKeyEvent *event);
    /// The Down/Enter caret-seam target: leaves title-edit mode and places
    /// the real document caret at block 0 byte 0 via `setCaretPosition`
    /// (the same chokepoint every other programmatic caret placement uses).
    /// No-op (title-edit mode still clears) if the document has no blocks.
    void exitTitleEditingToBlockZero();
    /// `paintEvent()`'s title-band branch: draws the band's text (or an
    /// "Untitled" placeholder, dimmed, when `m_inlineTitle` is empty and
    /// not currently being edited) and — while focused and
    /// `m_titleCaretActive` — its own caret. Viewport-relative, same
    /// convention as the rest of `paintEvent`'s locals.
    void paintTitle(QPainter &p) const;

    // ---- Folding (P5.6) ----------------------------------------------
    // Fold SHAPE (what a head's body is) is `Folding::resolveFoldable`,
    // canvas-local, stateless. Fold STATE (which heads are currently
    // toggled) is `m_foldedHeads` below, the one piece of view-local state
    // this seam owns — same "leaf state the document doesn't know about"
    // exception `m_selectionAnchor`/preedit already are (spec §2/§3: the
    // document itself is untouched by folding, same as a selection).

    /// The union of every currently-folded head's body blocks, freshly
    /// re-derived from `Folding::resolveFoldable` (never cached across
    /// calls) — cheap relative to a keystroke, and re-deriving means an
    /// edit that shrinks/removes a fold head's body is picked up for free
    /// on the very next call, no separate invalidation path.
    QSet<BlockId> hiddenBlocksFromFolds() const;
    /// Pushes `hiddenBlocksFromFolds()` into `m_cache` (`BlockLayoutCache::
    /// setFoldedBlocks`). Called from `onDocumentChanged()` and
    /// `setFontScale()` — the two places `m_cache->sync()` runs and so
    /// resets every entry's `folded` flag to false — and from
    /// `toggleFold()` itself.
    void refreshFoldedBlocks();

    /// P6.0: builds the `Markoff::FoldRef` that identifies `id` as a fold
    /// head — `kind` (canvas's `LongList`/`Callout` both map to core's
    /// generic `FoldRef::Kind::Block`; `Heading` maps directly), `start`
    /// (the raw `CollabText::Crdt::Anchor` at `id`'s byte 0, via
    /// `MarkoffDocument::blockCrdtAnchorAt` — the D2-safe seam this task
    /// added to core), and `headingLevel` for the Heading case (read off
    /// the same `AttrNames::Level` attribute `Folding::resolveFoldable`
    /// already reads). `headingPath` is left empty — see the P6.0 findings
    /// log entry. Caller must already know `id` is foldable
    /// (`isBlockFoldable`); this does not itself check.
    Markoff::FoldRef foldRefFor(BlockId id) const;

    // ---- Frontmatter (P5.5) -------------------------------------------
    // Frontmatter is NOT a document block (markoff-core's
    // `Document::extract` splits it off `source` before the body is ever
    // parsed into blocks — `extracted.body` never contains it, so it has
    // no `BlockId`) — same "leading non-block y-layout entry" shape P4.9's
    // title band established, read-only, driven straight from `m_doc`
    // rather than duplicated into view-local state.
    //
    // Footnote DEFINITIONS turned out NOT to need this treatment (see
    // FootnoteDefBlocks.h's doc comment): unlike frontmatter,
    // `Document::extract` only *copies* footnote-def lines into its
    // `footnotes` list for numbering — it does not strip them from
    // `extracted.body`, so `[^1]: text` remains a completely ordinary
    // `BlockKind::Paragraph` block in `IdList`. Its back-reference styling
    // is therefore just another `BlockPresentation::presentationFor`
    // per-kind case (`FootnoteDefBlocks::parseFootnoteDef`), no second
    // y-layout entry needed.

    /// Height of the leading frontmatter band, 0 when the document has no
    /// frontmatter. Collapsed (default): one row per recognized `key:
    /// value` property (`FrontmatterBlock::parseFrontmatterProperties`), or
    /// a single "Properties" summary row if none parsed. Expanded
    /// (`m_frontmatterExpanded`, toggled by clicking the band): one row per
    /// line of the raw YAML — same "caret/click reveals source" shape the
    /// code-fence and math-block per-block reveal already use, adapted to a
    /// click since this band has no BlockId for a real caret to enter.
    qreal frontmatterBandHeight() const;
    /// `titleBandHeight() + frontmatterBandHeight()` — the offset every
    /// block-y consumer in this file now adds/subtracts (titleBandHeight()
    /// alone stayed reserved for the title band's OWN hit-test/paint, which
    /// occupies only its own sub-range at the very top).
    qreal leadingBandHeight() const;
    /// True if `viewportPos` falls inside the frontmatter band's vertical
    /// extent. No char-offset output (unlike `hitTestTitle`) — the band is
    /// read-only, a click anywhere in it just toggles `m_frontmatterExpanded`.
    bool hitTestFrontmatter(const QPoint &viewportPos) const;
    void paintFrontmatter(QPainter &p) const;

    MarkoffDocument *m_doc = nullptr;
    Theme m_theme;
    /// Font-scale multiplier (P3.5). Threaded into every `m_cache->sync()`
    /// call; see `setFontScale()`.
    qreal m_fontScale = 1.0;
    /// Content column width policy (P4.5). See `setContentWidthPolicy`'s
    /// doc comment; `layoutWidthFor()` is the sole consumer.
    ContentWidthPolicy m_contentWidthPolicy;
    std::unique_ptr<BlockLayoutCache> m_cache;
    quint64 m_paintCount = 0;
    CanvasCursor m_caret;
    std::optional<CanvasCursor> m_selectionAnchor;
    /// F3/find-next fix (punch-list [cluster-k]): set by `ensureCaretVisible()`
    /// whenever it scrolls the caret into view, since that scroll target can
    /// be based on an unrealized block's ESTIMATED y. Consumed by
    /// `ensureLayoutForViewport()`'s fixed-point loop, which re-clamps the
    /// scrollbar to the caret on each pass that realizes new layout until
    /// the caret's own entry is confirmed realized — closing the gap over a
    /// few paints for a deep jump. `setScrollAnchor()` clears it unconditionally
    /// so an explicit scroll restore (`EditorWidget::restoreEphemeralState()`'s
    /// documented "scroll always wins over the caret's auto-scroll" contract)
    /// is never fought on a later paint.
    bool m_caretVisibilityPending = false;
    bool m_hasFocus = false;
    /// Read-only gate authority (P3.3). See `setReadOnly`'s doc comment.
    bool m_readOnly = false;
    /// Find-match highlights (P3.4), grouped by block for O(1) lookup
    /// during `paintEvent`. Draw-time-only paint state — see
    /// `setFindHighlights`'s doc comment.
    QHash<BlockId, QList<FindHighlight>> m_findHighlightsByBlock;
    /// Selection-occurrence highlights (P7.2e, F1 #7), grouped by block for
    /// O(1) lookup during `paintEvent`, same convention as
    /// `m_findHighlightsByBlock`. Draw-time-only paint state, rebuilt by
    /// `recomputeOccurrenceHighlights()` every time the selection changes —
    /// never independently authoritative. Value is the block-relative
    /// `{byteOffset, byteLength}` pair; a bare pair (not a whole
    /// `FindHighlight`) because this feature has no "current" distinction
    /// to carry the way find-matches do.
    QHash<BlockId, QList<std::pair<int, int>>> m_occurrenceHighlightsByBlock;
    /// Remote-participant presences (P6.2). Kept as a flat list, NOT grouped
    /// by block the way `m_findHighlightsByBlock` is: a Presence Selection's
    /// range is named by two `TextAnchor`s, which resolve to a block only by
    /// re-consulting the document (`resolvePresenceAnchor`), so a per-block
    /// index built at `setRemotePresences` time would itself be exactly the
    /// kind of cached-resolved-position state the "never cached" rule (this
    /// struct's own doc comment) rules out. Draw-time-only paint state.
    QList<RemotePresence> m_remotePresences;

    /// Drop-cursor indicator target (P7.2f, F1 #10). View state the
    /// document has no concept of, same shape as `m_selectionAnchor`/
    /// `m_preeditText`/`m_foldedHeads` before it: it exists only while a
    /// drag is over this viewport. Set by `dragEnterEvent`/`dragMoveEvent`
    /// (hit-tested fresh on every move — CM's own `dropcursor.ts` re-
    /// measures on every `dragover` too, no caching), cleared by
    /// `dragLeaveEvent` and `dropEvent`. Draw-time-only paint state, read
    /// by `paintEvent` exactly like the remote-presence caret bar.
    std::optional<CanvasCursor> m_dropCursorPos;

    // ---- Folding (P5.6, retro-wired to Session in P6.0) ------------------
    /// Fold heads currently toggled ON. Remains the authority for the
    /// View's own writes (see `toggleFold()`'s doc comment for why a
    /// Session-state-derived cache is not attempted); when a Session is
    /// attached, every write here is mirrored to it via
    /// `Session::toggleFold()` so it stays a genuine second reader
    /// surface — see the private "Folding" section's doc comment for why
    /// fold SHAPE is never stored here.
    QSet<BlockId> m_foldedHeads;
    /// P6.0: non-owning, see `setSession()`.
    Session *m_session = nullptr;

    // ---- IME (T8) ---------------------------------------------------------
    // Composition state the document doesn't know about (same exception as
    // m_selectionAnchor, T5): a preedit string exists only in this leaf's
    // input pipeline and BlockLayoutCache's spliced-in layout until it is
    // committed, at which point it becomes a real d2ApplyBufferEdit and
    // this reverts to empty.
    QString m_preeditText;

    // ---- Auto-pairing / wrap-selection (P7.2c, F1 #4) ---------------------
    // View-state justification (spec §2, same shape as m_selectionAnchor/
    // m_preeditText/m_foldedHeads before it): the document has no concept
    // of "this pair was auto-inserted" — a `()` the user typed manually one
    // character at a time and a `()` this leaf auto-paired are byte-for-
    // byte identical in the buffer, so the distinction point 3 needs
    // (Backspace between a FRESH pair deletes both; between a manually-
    // typed one it does not) cannot be re-derived from document content at
    // all, the way fold SHAPE or delimiter-hiddenness can. The plan's F1
    // note offered two options: track it as view state, or derive it from
    // the undo entry. Derivation was rejected: UndoLog::Transaction/
    // UndoLog::Entry (core, unexported here) does not currently record
    // "this transaction was specifically an auto-pair insert" as a tagged
    // fact distinct from any other coalesced-printable transaction — adding
    // that tag would be a core change this task's scope guard rules out
    // ("markoff-core/ should NOT need changes"). View state it is.
    //
    // Freshness rule (deliberately narrow, logged simplification vs. real
    // CodeMirror's per-position StateField that survives cursor motion):
    // this holds the ONE most recently auto-inserted closer, valid for
    // exactly the next keystroke. keyPressEvent() snapshots-then-clears it
    // at the very top of every key event, so ANY intervening key — arrow
    // motion, another printable char, anything — invalidates it; only a
    // successful auto-pair-creating keystroke re-arms it. This trades
    // "survives caret round-trips away and back" (real CM) for "trivial to
    // reason about and impossible to leave stale across an edit elsewhere
    // in the block" — acceptable given the narrow, single-keystroke-window
    // behaviors (type-through, Backspace-deletes-both) this exists to
    // serve. mousePressEvent() also clears it defensively, though the
    // position check in tryDeleteFreshPair()/tryAutoPairOrWrap() (caret
    // must sit exactly at the tracked byte offset) already makes a stale
    // entry harmless on its own.
    std::optional<AutoPairedCloser> m_autoPairedClose;

    // ---- Links (P4.2) -------------------------------------------------
    // Consumer-owned; not linked, only observed via its own signals (see
    // setLinkService's doc comment). Same "reference, not owner" role as
    // m_findController plays in EditorWidget.
    Markoff::LinkService *m_linkService = nullptr;
    QString m_linkFromContext;
    /// Raw text of the currently-hovered link, empty if none — the hover
    /// state `updateHover`/`clearHover` diff against (mirrors styled's
    /// LinkInteraction::m_currentHoveredRawText).
    QString m_hoveredLinkRawText;
    /// Cursor-shape cache: whether the viewport's cursor is currently the
    /// pointing-hand override. Only a hover state TRANSITION flips this and
    /// calls setCursor/unsetCursor — see updateHover's doc comment.
    bool m_cursorIsPointingHand = false;

    // ---- Context menu (P4.4) ---------------------------------------------
    // Consumer-owned; not linked, only read from — same "reference, not
    // owner" role m_linkService plays.
    CanvasActionController *m_actionController = nullptr;
    /// The link/wikilink/tag span (if any) under the point the context menu
    /// was last invoked at — set by `contextMenuEvent` right before
    /// `buildContextMenu` runs, consulted by both `buildContextMenu` (to
    /// decide whether to add "Copy Link Target") and that item's own
    /// trigger handler. Independent of hover/caret state: a right-click can
    /// land on a link the pointer never hovered and the caret never
    /// touched.
    std::optional<Markoff::LinkActivation> m_contextMenuLink;

    // ---- Inline title (P4.9) -----------------------------------------
    // View-local widget state ONLY (spec §5.2: "not a document block") —
    // the document, Session, and UndoLog know nothing of this. Off by
    // default; not persisted here (EditorWidget's contract setter is the
    // consumer-facing surface, same "leaf state vs. contract state" split
    // readOnly/theme/fontScale already follow).
    QString m_inlineTitle;
    bool m_inlineTitleVisible = false;
    /// Accessible document name (G1 spec §9 Q2) — set-only-by-embedder input
    /// to `CanvasAccessible::text(QAccessible::Name)`'s resolution chain.
    /// Empty by default, same "off unless the consumer opts in" shape as
    /// the inline title above.
    QString m_accessibleDocumentName;
    /// Whether keyboard input is currently routed into the title band
    /// rather than the document (mouse click in the band, or Up/no-op from
    /// it, sets/clears this — see `handleTitleKeyPress`/`mousePressEvent`).
    /// `setCaret()` — the one chokepoint every real document caret move
    /// funnels through — clears it unconditionally, so any document-side
    /// caret placement always wins focus away from the title.
    bool m_titleCaretActive = false;
    /// UTF-16 char offset into `m_inlineTitle` — the title's own coordinate
    /// space (a plain QString offset, not a `CanvasCursor`/byte offset;
    /// there is no document buffer here for C4 to apply to).
    int m_titleCaretPos = 0;

    // ---- Frontmatter (P5.5) ------------------------------------------------
    /// Collapsed (properties header) vs. expanded (raw YAML) — the ONLY
    /// piece of view-local state this seam owns; the content itself is
    /// always read fresh from `m_doc->frontmatterValue("raw")`, never
    /// cached here (see the frontmatterBandHeight() doc comment).
    bool m_frontmatterExpanded = false;

    // ---- Drag-drop (P7.2) --------------------------------------------------
    /// Set by `mousePressEvent` when a plain (non-Shift) press lands INSIDE
    /// the existing selection: the gesture is ambiguous until the next
    /// move — either it stays under `QApplication::startDragDistance()`
    /// (a plain click that should reposition the caret and clear the
    /// selection, `qwidgettextcontrol.cpp`'s own `mightStartDrag`
    /// precedent) or it crosses the threshold and `mouseMoveEvent` starts
    /// a `QDrag`. Cleared on release and once a drag actually starts.
    bool m_dragPending = false;
    /// The press position that armed `m_dragPending` — the threshold
    /// origin `mouseMoveEvent` measures against, and the fallback caret
    /// target if the press turns out to be a plain click instead of a
    /// drag (see `m_dragPending`'s doc comment).
    QPoint m_dragPressPos;

    // ---- Double/triple-click select (P7.3, [cluster-k] P3) -----------------
    /// Wall-clock timestamp (`QDateTime::currentMSecsSinceEpoch()` — see
    /// `mousePressEvent`'s triple-click branch for why not
    /// `QMouseEvent::timestamp()`) of the last `mouseDoubleClickEvent`,
    /// and the block it landed on. Qt only ever synthesizes ONE
    /// `mouseDoubleClickEvent` per click-pair — its own double-click state
    /// machine resets immediately afterward, so the THIRD physical click
    /// arrives back at `mousePressEvent` as an ordinary press,
    /// indistinguishable from an unrelated first click unless something
    /// remembers the double-click that just happened. `mousePressEvent`
    /// compares its own press against these two fields — same block, and
    /// within `QApplication::doubleClickInterval()` — and promotes it to a
    /// whole-block (paragraph) selection when both hold. Reset to a null
    /// block once consumed, or once any OTHER press breaks the sequence,
    /// so a fourth click doesn't spuriously re-trigger.
    qint64 m_pendingTripleClickTimestamp = 0;
    BlockId m_pendingTripleClickBlock;
};

}  // namespace Markoff::Canvas
