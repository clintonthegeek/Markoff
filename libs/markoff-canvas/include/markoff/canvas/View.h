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
#include <QVariant>

#include <markoff/core/BlockId.h>
#include <markoff/core/FormatOps.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/core/Theme.h>

class QInputMethodEvent;
class QPainter;

namespace Markoff {
class MarkoffDocument;
class LinkService;
}

namespace Markoff::Canvas {

class BlockLayoutCache;

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

    /// Table cell bounds in document coordinates (T9, exit E8), or a null
    /// rect if `id` is not a realized Table block or (row, col) is out of
    /// range. row 0 is the header row. Test/inspection surface only —
    /// nothing here is authority.
    QRectF tableCellRect(BlockId id, int row, int col) const;

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

    /// The realized layout's first line's natural (unwrapped) text width in
    /// DIPs, or -1 if `id` isn't realized (spec §4.2 P2.1 exit criterion:
    /// this is real reflow, not a cosmetic recolor — hiding a delimiter run
    /// narrows what the layout actually measures). Test/inspection surface
    /// only — nothing here is authority.
    qreal lineNaturalWidth(BlockId id) const;

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

signals:
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    /// Viewport `QEvent::Leave` only reaches this widget via an event
    /// filter — `QAbstractScrollArea::viewportEvent` does not forward
    /// Enter/Leave the way it forwards mouse-button/move events (same
    /// reason styled's `LinkInteraction` installs one). Used solely to
    /// close out an in-progress link hover when the pointer leaves the
    /// viewport without a `mouseMoveEvent` ever reporting "no link here".
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onDocumentChanged();
    /// Realize whatever the current viewport needs and refresh the scroll
    /// range. Safe to call repeatedly; realization is monotonic.
    void ensureLayoutForViewport();
    void updateScrollRange();
    qreal pageMargin() const;
    qreal textWidth() const;

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
    void moveCaretHorizontally(bool forward);
    void moveCaretVertically(bool forward);
    void moveCaretToLineEdge(bool home);
    void insertPrintable(const QString &text);
    void deleteCluster(bool forward);
    /// Route a key through StructuralKeyHandler (Enter split, boundary
    /// Backspace/Delete merge, Tab/Shift+Tab list indent) before any other
    /// handling. Returns true if the handler owned the key — it has
    /// already applied the mutation and the caret has already been moved
    /// to `r.caretBlock`/`r.caretByteInBlock`.
    bool tryStructuralKey(QKeyEvent *event);
    /// Keep the caret referencing a block that still exists after a
    /// document change, biased toward the block's last known position
    /// (T2's version of the queue-#10 "never strand the caret" clamp;
    /// load-bearing again in T4).
    void clampCaret(int oldCaretIndexHint);
    void ensureCaretVisible();
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

    MarkoffDocument *m_doc = nullptr;
    Theme m_theme;
    /// Font-scale multiplier (P3.5). Threaded into every `m_cache->sync()`
    /// call; see `setFontScale()`.
    qreal m_fontScale = 1.0;
    std::unique_ptr<BlockLayoutCache> m_cache;
    quint64 m_paintCount = 0;
    CanvasCursor m_caret;
    std::optional<CanvasCursor> m_selectionAnchor;
    bool m_hasFocus = false;
    /// Read-only gate authority (P3.3). See `setReadOnly`'s doc comment.
    bool m_readOnly = false;
    /// Find-match highlights (P3.4), grouped by block for O(1) lookup
    /// during `paintEvent`. Draw-time-only paint state — see
    /// `setFindHighlights`'s doc comment.
    QHash<BlockId, QList<FindHighlight>> m_findHighlightsByBlock;
    // ---- IME (T8) ---------------------------------------------------------
    // Composition state the document doesn't know about (same exception as
    // m_selectionAnchor, T5): a preedit string exists only in this leaf's
    // input pipeline and BlockLayoutCache's spliced-in layout until it is
    // committed, at which point it becomes a real d2ApplyBufferEdit and
    // this reverts to empty.
    QString m_preeditText;

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
};

}  // namespace Markoff::Canvas
