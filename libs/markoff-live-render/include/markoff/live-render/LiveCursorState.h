// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Cursor.h>

#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <optional>
#include <qqmlintegration.h>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::LiveRender {

class BlockKindRegistry;
class LiveBlockModel;

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
/// the structural-key handler in R5: when a structural edit creates a new
/// row, the row doesn't exist in the model until the parse-back arrives.
/// The pending request is held; `rowsInserted` resolves it. Spec §5.3
/// step 6.
class MARKOFF_LIVE_RENDER_EXPORT LiveCursorState : public QObject {
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

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    QString cursorKind() const;
    int focusedAnchorRow() const;
    int focusedQtPos() const;

    /// Override the model whose `rowsInserted` resolves pending requests.
    /// Defaults to the inner LiveBlockModel passed to the constructor.
    /// Pending-request row indices are interpreted in this model's
    /// coordinate space. Retained as a public seam for future use; in the
    /// marker-paragraph regime the inner LiveBlockModel is the only model.
    void setSignalModel(QAbstractItemModel *signalModel);

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

    /// Byte-position-keyed pure-pending variant. Use when the structural
    /// edit shifts an existing block to a known POST-EDIT byte position
    /// (e.g. start-of-paragraph Enter inserts a marker before the user's
    /// content; the user's content's first byte is now at
    /// `currentBlockStart + markerBytes`). The pending request resolves on
    /// every `rowsInserted` event by asking the foundation document for
    /// each row's byte range and finding the row whose range contains
    /// `targetByte`. Robust against:
    ///   - anchor renumbering during AstBlockDiff collapse,
    ///   - off-by-one row-index drift across the parse-back diff,
    ///   - any other anchor-identity quirks the foundation may expose.
    /// Bug 3 v2 fix (Task 18 dogfood pass 3): the previous anchor-keyed
    /// path landed on the wrong paragraph in some mid-document cases.
    void requestTextCaretAtByte(Markoff::MarkoffDocument *document,
                                quint32 targetByte,
                                int qtPos);

    /// Called by LiveListModelBinding from onParseUpdated. Increments the
    /// pending request's parse-cycle counter; drops on the SECOND call
    /// after the request was recorded (i.e. drops at parseCyclesSeen >= 2).
    /// `parseSeq` is unused as a value (we only care about the count);
    /// keep the signature so the call-site is self-documenting.
    void noteParseArrived(quint64 parseSeq);

Q_SIGNALS:
    void cursorChanged();

private:
    bool validateVariant(const Cursor &c) const;
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void resolvePendingForRow(int row);
    void onAnchorRenumbered(int row,
                            Markoff::BlockAnchor oldAnchor,
                            Markoff::BlockAnchor newAnchor);

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;
    QAbstractItemModel      *m_signalModel = nullptr;

    struct PendingRow {
        int row;
        int qtPos;
        int parseCyclesSeen = 0;  // bumped on each noteParseArrived
        // If set, treat this pending request as anchor-keyed: ignore the
        // `row` field and resolve by searching the model for this
        // BlockAnchor on every rowsInserted event. Used by start-of-
        // paragraph Enter (marker insert before an existing block) where
        // the user's content's row index is not stable across the diff
        // but its BlockAnchor identity is.
        std::optional<Markoff::BlockAnchor> anchor;
        // If set, treat this pending request as byte-keyed: resolve by
        // asking the foundation document which row's byte range contains
        // `targetByte`. This is the most robust resolution path; used by
        // start-of-paragraph Enter after the previous anchor-keyed path
        // mis-resolved in some mid-document cases (Bug 3 v2).
        QPointer<Markoff::MarkoffDocument> byteDocument;
        std::optional<quint32>             targetByte;
    };
    std::optional<PendingRow> m_pendingRow;
    void resolvePendingForAnchor();
    void resolvePendingForByte();
};

}  // namespace Markoff::LiveRender
