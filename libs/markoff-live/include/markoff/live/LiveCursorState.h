// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/Cursor.h>

#include <QObject>
#include <QString>
#include <optional>
#include <qqmlintegration.h>

namespace Markoff::Live {

class BlockKindRegistry;
class LiveBlockModel;
class LiveListModelBinding;

/// Owns the single canonical cursor value for the live view. Validates
/// `request()` calls against the target block's `BlockKindDescriptor`
/// (so BlockSelected is refused on a paragraph, etc.). Emits
/// `cursorChanged()` only when the cursor actually changes. Spec §5.3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
///
/// `requestTextCaretAtRow` is the deterministic-pending variant used by
/// the structural-key handler. When the row already exists in the model
/// it resolves immediately; when a structural edit has not yet propagated
/// through the CRDT→model pipeline the request is held until
/// `rowsInserted` fires. Legitimate use requires that the row's TEXT is
/// already stable at the time of the call — do not use immediately after
/// a d2ApplyBufferEdit that changes the row content (use
/// requestTextCaretAtAnchor instead). Spec §5.3 step 6.
class MARKOFF_LIVE_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveCursorState is provided by LiveListModelBinding")

    Q_PROPERTY(QString cursorKind READ cursorKind NOTIFY cursorChanged)
    /// The inner-model row index for the active anchor-side TextCaret, else -1.
    /// QML uses this directly as the ListView row index to find the delegate
    /// that should receive keyboard focus after a structural edit (mid-block
    /// split, Backspace-merge, Delete-merge, marker insertion).
    Q_PROPERTY(int focusedAnchorRow READ focusedAnchorRow NOTIFY cursorChanged)
    /// The qtPos of the active TextCaret, else -1.
    Q_PROPERTY(int focusedQtPos READ focusedQtPos NOTIFY cursorChanged)
    /// Cross-block column-preservation state. Set by LiveNavigationController
    /// before each Up/Down cross; cleared on Left/Right or any non-vertical
    /// motion. -1.0 sentinel = unset. Spec §4.5 lifecycle rules.
    Q_PROPERTY(qreal desiredVisualX READ desiredVisualX
                                    WRITE setDesiredVisualX
                                    NOTIFY desiredVisualXChanged)
    /// Cross-block column-preservation visual-line hint. Set by
    /// `LiveNavigationController` before each Up/Down cross to indicate
    /// which visual line of the destination block the caret should land on
    /// (FirstLine for Up, LastLine for Down). Cleared back to None inside
    /// `resolvePendingForRow*` after the destination delegate has consumed
    /// it. Read by QML delegates' focusEditAt + onCursorChanged paths.
    Q_PROPERTY(VisualLineHint pendingVisualLineHint READ pendingVisualLineHint
                                                    NOTIFY visualLineHintChanged)

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             LiveListModelBinding    *binding,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    QString cursorKind() const;
    int focusedAnchorRow() const;
    int focusedQtPos() const;

    qreal desiredVisualX() const noexcept { return m_desiredVisualX; }
    void  setDesiredVisualX(qreal x);
    Q_INVOKABLE void clearDesiredVisualX();

    enum class VisualLineHint { None, FirstLine, LastLine };
    Q_ENUM(VisualLineHint)

    /// Like requestTextCaretAtRow, but qtPos is computed by the destination
    /// delegate from desiredVisualX projected onto the line indicated by hint.
    void requestTextCaretAtRowVisualX(int expectedRow, VisualLineHint hint);
    VisualLineHint pendingVisualLineHint() const noexcept { return m_pendingVlhint; }

    void request(const Cursor &newCursor);
    void clear();

    int rowForBlock(const Markoff::BlockAnchor &block) const;

    /// Request a TextCaret at `qtPos` of the row at `expectedRow` once it
    /// exists. If the row already exists, equivalent to constructing a
    /// TextCaret from `model->recordAt(expectedRow).blockAnchor` and
    /// calling `request()`. If the row does not yet exist (because a
    /// structural edit was applied and the parse-back hasn't created it),
    /// record the request and watch `model->rowsInserted` for resolution.
    /// Pending requests linger up to two parse cycles before being
    /// dropped (see spec §8.4). Spec §5.3 step 6.
    ///
    /// If a request is already pending, it is replaced (latest-request-wins).
    Q_INVOKABLE void requestTextCaretAtRow(int expectedRow, int qtPos);

    /// Pure-pending variant: never resolves immediately even when
    /// `expectedRow` already exists. Use when the structural edit will
    /// INSERT a new row at this index (mid-block split, hole commit). The
    /// pending request resolves on the next `rowsInserted` whose range
    /// covers `expectedRow`. Distinguishes "row will be born here" from
    /// "row already exists, just move the cursor" — the latter must use
    /// requestTextCaretAtRow above.
    Q_INVOKABLE void requestTextCaretAtNewRow(int expectedRow, int qtPos);

    /// One-way sync from QML `TextEdit::cursorPosition` → canonical
    /// `m_cursor`. Called by each text-bearing delegate's
    /// `onCursorPositionChanged` and by `LiveEditBinding::onContentsChange`
    /// after each buffer edit. Without this hook, `m_cursor` desyncs as
    /// the user types or moves within a block (because TextEdit handles
    /// within-block arrows and IME natively), and any subsequent kind
    /// transition or structural diff reads a stale qtPos and lands the
    /// caret in the wrong place. Idempotent if the cursor is already at
    /// `(anchor, qtPos)`. Deliberately does NOT reset `m_pendingRow` — a
    /// pending structural-key request must survive incidental TextEdit
    /// cursor moves until its structural signal arrives.
    Q_INVOKABLE void syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos);

    /// Anchor-keyed pure-pending variant. Use when the structural edit
    /// shifts an existing block (whose `BlockAnchor` we already know)
    /// rather than creating a brand-new block. The pending request
    /// resolves on the next `rowsInserted` event by searching the model
    /// for `expectedAnchor`'s row, NOT by indexing a row position.
    /// This is robust to any number of intervening Insert/Delete/Equal
    /// ops in the parse-back diff: the user's content's row index can
    /// shift unpredictably (anchor renumbering, multi-row diffs), but
    /// its BlockAnchor identity is stable. Bug 3 fix (Task 18 dogfood
    /// pass 2): start-of-paragraph Enter in mid-document context must
    /// land the cursor on the user's content, not on the row after it.
    void requestTextCaretAtAnchor(Markoff::BlockAnchor expectedAnchor, int qtPos);

Q_SIGNALS:
    void cursorChanged();
    void desiredVisualXChanged();
    void visualLineHintChanged();

private:
    bool validateVariant(const Cursor &c) const;
    void onStructuralRowsInserted(int first, int last);
    void onStructuralRowRemoved(int row);
    void resolvePendingForRow(int row);

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;
    LiveListModelBinding    *m_binding = nullptr;
    qreal                    m_desiredVisualX = -1.0;
    VisualLineHint           m_pendingVlhint  = VisualLineHint::None;

    struct PendingRow {
        int row;
        int qtPos;
        // If set, treat this pending request as anchor-keyed: ignore the
        // `row` field and resolve by searching the model for this
        // BlockAnchor on every structural signal event. Used by start-of-
        // paragraph Enter (marker insert before an existing block) where
        // the user's content's row index is not stable across the diff
        // but its BlockAnchor identity is.
        std::optional<Markoff::BlockAnchor> anchor;
    };
    std::optional<PendingRow> m_pendingRow;
    void resolvePendingForAnchor();
};

}  // namespace Markoff::Live
