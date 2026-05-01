// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QHash>
#include <QList>
#include <QObject>

#include <markoff-foundation/BlockAnchor.h>

#include <markoff/view/qml/ProjectionItem.h>

namespace Markoff::View::Qml {

class EditorBackend;
class LiveBlockModel;

/// `LiveProjectionLayer` — owner of the gap between the editing model and the
/// (lossy) source projection. See `docs/specs/2026-05-01-live-projection-layer.md`.
///
/// Stage-1: skeleton only. Storage + pass-through getters + no-op reconcile
/// slots. Producers (`InlineFormatHighlighter`,
/// `LiveSpeculativeFenceController`, the future empty-paragraph hole) wire in
/// during Stages 2-4 of the plan.
///
/// Lifecycle: one instance per `LiveListModelBinding` (spec §5 invariant 14).
/// Owned by the binding; lifetime equals the binding's. No globals.
class LiveProjectionLayer : public QObject {
    Q_OBJECT

public:
    explicit LiveProjectionLayer(QObject *parent = nullptr);
    ~LiveProjectionLayer() override;

    void setEditorBackend(EditorBackend *backend);
    void setBlockModel(LiveBlockModel *model);

    EditorBackend *editorBackend() const;
    LiveBlockModel *blockModel() const;

    // ---- v1 block-hole API (IME-preedit pattern). -------------------------
    //
    // Creates the pending hole and returns its newly-assigned id. Callers
    // pass `hole` with `id == 0`. v1 invariant: at most one block hole is
    // pending at a time. If a hole is already pending when this is called,
    // the layer first commits-or-abandons it (commit if non-empty
    // `bufferText`, abandon otherwise), then creates the new one.
    quint64 createBlockHole(BlockHole hole);

    /// Update the pending hole's local buffer. `holeId` must equal the
    /// layer's current pending hole id; otherwise a no-op (the hole was
    /// already committed or dropped — the caller raced).
    Q_INVOKABLE void setBlockHoleBuffer(quint64 holeId, const QString &bufferText);

    /// Commit the pending hole identified by `holeId`. Order:
    ///   1. Emit `aboutToCommit(holeId)` synchronously so the active
    ///      delegate finalizes any pending Qt IME preedit before the
    ///      buffer is read (delegate-side wiring is T21; the signal
    ///      exists now).
    ///   2. Snapshot `bufferText`.
    ///   3. Tell the model to remove the hole row.
    ///   4. Build a single `MarkoffEdit{ oldStart=reifyOffset,
    ///      oldEnd=reifyOffset, newText="\n\n"+buffer }` and call
    ///      `m_backend->document()->applyLocalEdit({edit})`.
    ///   5. Install a one-shot listener on the model's `rowsInserted`
    ///      signal so that when a new row appears at the expected
    ///      post-commit `viewRow`, the layer emits
    ///      `holeReified(viewRow, qtPos)` and disconnects. `qtPos` is
    ///      the snapshotted buffer length in UTF-16 code units.
    /// No-op if `holeId` does not match the pending hole.
    Q_INVOKABLE void commitBlockHole(quint64 holeId);

    /// Drop the pending hole identified by `holeId` without any source
    /// mutation. Emits `holeDropped(viewRow)`. No-op if `holeId` does not
    /// match.
    Q_INVOKABLE void dropBlockHole(quint64 holeId);

    /// At most one hole exists; commits it if `bufferText` is non-empty,
    /// drops it otherwise. Called by the save path (T23) and by
    /// `createBlockHole` when displacing a prior pending hole.
    Q_INVOKABLE void commitAllPendingHoles();

    Q_INVOKABLE bool hasPendingBlockHole() const { return m_pendingHole.has_value(); }
    Q_INVOKABLE quint64 pendingBlockHoleId() const;
    Q_INVOKABLE QString pendingBlockHoleBuffer() const;

    // ---- Parse-round-trip orchestration (called by LiveListModelBinding). ---
    //
    // During a parse round-trip, the binding detaches the pending hole (if
    // any), calls `model->applyOps`, and then either re-attaches the hole if
    // its anchor row is still valid, or abandons it otherwise. The detach
    // removes the model row so `applyOps` operates on the parsed-rows
    // underlay only — the hole is transparent to the diff machinery.
    //
    // Not Q_INVOKABLE: these are an internal seam for the binding, not a QML
    // API.

    /// Detach the pending hole during a parse round-trip. Removes the model
    /// row as a side effect; the layer's internal hole state is cleared.
    /// Pre: `hasPendingBlockHole() == true`. Returns the snapshot for the
    /// caller to either reattach or abandon.
    std::optional<BlockHole> detachPendingHoleForReparse();

    /// Re-attach a previously-detached hole. Re-inserts the model row and
    /// restores the layer's internal hole state to the snapshot.
    /// Pre: `hasPendingBlockHole() == false`.
    void reattachHoleAfterReparse(const BlockHole &snapshot);

    /// Discard a previously-detached hole whose anchor row is no longer
    /// valid. Emits `holeDropped(snapshot.afterParsedRow + 1)`. Does not
    /// touch the model — the row was already removed by `detach`.
    void reattachHoleAfterReparseAbandon(const BlockHole &snapshot);

    // ---- Inline holes (unchanged Stage-1 stub). ---------------------------
    void createInlineHole(const InlineHole &hole);

    // Prediction creation hooks — called by InlineFormatHighlighter (Stage 3)
    // and LiveSpeculativeFenceController (Stage 2).
    void createInlinePrediction(const InlinePrediction &prediction);
    void createBlockKindPrediction(const BlockKindPrediction &prediction);

    /// Clear all inline predictions registered for `row`. Called by
    /// `InlineFormatHighlighter` before it re-publishes a fresh prediction
    /// set on each `setSource` (the highlighter's source-scan recomputes
    /// from scratch each time).
    void clearInlinePredictionsForRow(int row);

    /// Drop a previously-registered block-kind prediction for `row`. Reverts
    /// the model's speculative kind back to the parser-confirmed kind in the
    /// same call. Used by `LiveSpeculativeFenceController` when the trigger
    /// pattern (fence opener) is erased before parser confirmation arrives.
    void dropBlockKindPrediction(int row);

    // Lookups for consumers.
    QList<InlinePrediction> predictionsForRow(int row) const;
    /// Returns nullptr if no kind-prediction is registered for `row`. The
    /// pointer is owned by the layer and is invalidated by any subsequent
    /// `createBlockKindPrediction`/reconcile call for that row.
    const BlockKindPrediction *blockKindPredictionFor(int row) const;
    bool rowIsHole(int row) const;

    /// Total registered item counts — Stage-1 test convenience.
    /// `blockHoleCount` returns 0 or 1 under the v1 one-hole invariant.
    int blockHoleCount() const;
    int inlineHoleCount() const;
    int inlinePredictionCount() const;
    int blockKindPredictionCount() const;

Q_SIGNALS:
    /// Emitted when the layer's projection set changes in a way the model
    /// should reflect. Stage-1 is a no-op; later stages emit on reconcile.
    void rowsChanged(int firstRow, int lastRow);

    /// A pending hole has been reified into a real parsed block. Emitted
    /// from inside the one-shot `rowsInserted` listener installed by
    /// `commitBlockHole`. `viewRow` is the model row of the new real block;
    /// `qtPos` is the cursor offset (in UTF-16 code units) the focus router
    /// should place inside that row's TextEdit.
    void holeReified(int viewRow, int qtPos);

    /// Synchronous notification fired at the start of `commitBlockHole`
    /// before the buffer snapshot is read. T21 will wire the active
    /// delegate to commit any pending Qt IME preedit on this signal.
    void aboutToCommit(quint64 holeId);

    /// A pending hole was abandoned without source mutation. Carries the
    /// view row the hole occupied (for focus routing back to the prior
    /// block).
    void holeDropped(int previousViewRow);

    /// A pending hole was just created. Carries the view row the hole
    /// occupies so the QML side can route focus into the new delegate.
    /// Emitted at the end of `createBlockHole`, after the model row has
    /// been inserted.
    void holeCreated(int viewRow);

    /// Higher-level notification that the pending hole's buffer changed.
    /// The model already emits `dataChanged` on `TextRole`; this is for
    /// non-model consumers.
    void bufferChanged(quint64 holeId);

public Q_SLOTS:
    /// Reconcile predictions and holes against the latest parser output.
    /// Stage-1: no-op. Stage 2/3 fill in (drop confirmed predictions, snap
    /// back contradicted ones, drop holes whose anchors became invalid).
    void onParseUpdated();

    /// Reconcile holes against the latest applied local edit (e.g. a
    /// printable char into a hole reifies it; a backspace at qtPos 0 in an
    /// empty hole drops it). Stage-1: no-op.
    void onLocalEditApplied();

private:
    std::optional<BlockHole>        m_pendingHole;
    quint64                         m_nextHoleId = 1;
    QMetaObject::Connection         m_pendingHoleReifiedConnection;

    QList<InlineHole>               m_inlineHoles;
    QHash<int, QList<InlinePrediction>> m_inlinePredictions;     // keyed by row
    QHash<int, BlockKindPrediction> m_blockKindPredictions;      // keyed by row

    EditorBackend  *m_backend = nullptr;
    LiveBlockModel *m_model = nullptr;
};

}  // namespace Markoff::View::Qml
