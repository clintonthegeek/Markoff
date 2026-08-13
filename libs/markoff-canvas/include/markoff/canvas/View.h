// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include <QAbstractScrollArea>
#include <QRectF>

#include <markoff/core/BlockId.h>
#include <markoff/core/Theme.h>

namespace Markoff {
class MarkoffDocument;
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

    // ---- Inspection surface -------------------------------------------
    // Read-only views onto the derived cache, for tests and the manual
    // harness. Nothing here is authority; it all recomputes from the
    // document. Kept on the widget rather than exposing BlockLayoutCache
    // so the cache stays a private implementation detail.

    int   blockCount() const;
    int   realizedBlockCount() const;
    qreal documentHeight() const;

    /// Block bounds in document coordinates (y=0 is the document top, not
    /// the viewport top). Null rect if the id is not in the document.
    QRectF blockRect(BlockId id) const;

    /// Paints since construction. Cheap paint-counter for tests that need
    /// to know a repaint actually happened.
    quint64 paintCount() const;

    // ---- Caret ----------------------------------------------------------
    // Inspection for tests; the caret is edited only through real events
    // (mouse press, key press) on the production widget, never set
    // directly (invariant 5).

    BlockId caretBlock() const;
    int     caretByteOffset() const;

    // ---- Selection (T5) --------------------------------------------------
    // Inspection for tests; edited only through real events (mouse drag,
    // Shift+move, Ctrl+A), same rule as the caret.

    bool    hasSelection() const;
    BlockId selectionAnchorBlock() const;
    int     selectionAnchorByteOffset() const;

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

    MarkoffDocument *m_doc = nullptr;
    Theme m_theme;
    std::unique_ptr<BlockLayoutCache> m_cache;
    quint64 m_paintCount = 0;
    CanvasCursor m_caret;
    std::optional<CanvasCursor> m_selectionAnchor;
    bool m_hasFocus = false;
};

}  // namespace Markoff::Canvas
