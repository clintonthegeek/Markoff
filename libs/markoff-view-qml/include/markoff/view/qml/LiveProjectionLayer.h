// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/MarkoffDocument.h>

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

    // Hole creation hooks — called by structural-key handlers (Stage 4+).
    /// Creates a block hole, assigning it a stable id (returned), inserting
    /// the hole row into the model after `hole.afterParsedRow`, and starting
    /// the abandonment idle timer. The returned id is also written into the
    /// passed-in `BlockHole`'s `id` field via the side-effecting overload below.
    quint64 createBlockHole(BlockHole hole);
    void createInlineHole(const InlineHole &hole);

    /// Drop the block hole with the given id. No CRDT edit. Removes the hole
    /// row from the model. Called on focus-out, idle, backspace-in-empty-hole,
    /// and undo-of-paired-edit.
    Q_INVOKABLE void dropBlockHole(quint64 holeId);

    /// Spec §6 case 1: Ctrl+Z while a hole is unreified drops the hole AND
    /// triggers the CRDT undo for the paired `\n\n` insert, in one user-visible
    /// step. If no holes exist, behaves as a normal undo. The
    /// `EditorBackend::undo()` path is the fallback for non-hole undo cases.
    Q_INVOKABLE void undoWithHoles();

    /// Restart the per-hole abandonment idle timer (called by the delegate on
    /// keystroke into a hole row, before reify, to debounce abandonment).
    Q_INVOKABLE void restartHoleIdleTimer(quint64 holeId);

    /// Reify the block hole with the given id by inserting `text` at the
    /// hole's `reifyByteOffset`. Drops the hole row from the model SYNCHRONOUSLY
    /// before invoking `applyLocalEdit`, so the next parse arrives to a model
    /// without the hole and produces a real block at the same view-row index.
    /// Returns true if the hole was reified (false if no such hole).
    Q_INVOKABLE bool reifyBlockHole(quint64 holeId, const QString &text);

    /// Returns true iff there exists at least one block hole.
    Q_INVOKABLE bool hasBlockHoles() const { return !m_blockHoles.isEmpty(); }

    /// Lookup helpers used by the structural-key handler / delegate.
    bool         hasBlockHoleAfterParsedRow(int parsedRow) const;
    Q_INVOKABLE quint64 blockHoleIdAt(int viewRow) const;
    Q_INVOKABLE int     viewRowForBlockHoleId(quint64 holeId) const;
    quint32      pairedSourceEditByteCountForHoleId(quint64 holeId) const;
    BlockHole    blockHoleById(quint64 holeId) const;

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
    int blockHoleCount() const;
    int inlineHoleCount() const;
    int inlinePredictionCount() const;
    int blockKindPredictionCount() const;

Q_SIGNALS:
    /// Emitted when the layer's projection set changes in a way the model
    /// should reflect. Stage-1 is a no-op; later stages emit on reconcile.
    void rowsChanged(int firstRow, int lastRow);

    /// A hole has been reified into a real CRDT edit. The listening view uses
    /// this to route focus into the now-real block once the parse round-trip
    /// produces it. `viewRow` is the model row the hole occupied (which is
    /// also the view row at which the new real block will materialise — spec
    /// §3.3 reification semantics: dropping the hole synchronously before
    /// applyLocalEdit means the new real block lands at the same view index).
    /// `qtPos` is where the cursor should land in the new block (== UTF-16
    /// length of the typed character that triggered reification).
    void holeReified(int viewRow, int qtPos);

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
    /// Idle-timer fire → drop the hole. v0 hard-codes 30s; spec §9 flags this
    /// for revisit after dogfooding.
    void onIdleTimerFired(quint64 holeId);

    QList<BlockHole>                m_blockHoles;
    QList<InlineHole>               m_inlineHoles;
    QHash<int, QList<InlinePrediction>> m_inlinePredictions;     // keyed by row
    QHash<int, BlockKindPrediction> m_blockKindPredictions;      // keyed by row

    /// Per-hole abandonment timer (30s, restarted on each keystroke into the
    /// hole). Owned by the layer; deleted when the hole is dropped/reified.
    QHash<quint64, QTimer *>        m_idleTimers;

    /// Monotonic id generator for block holes.
    quint64                         m_nextHoleId = 1;

    EditorBackend  *m_backend = nullptr;
    LiveBlockModel *m_model = nullptr;
    QPointer<Markoff::MarkoffDocument> m_observedDocument;
};

}  // namespace Markoff::View::Qml
