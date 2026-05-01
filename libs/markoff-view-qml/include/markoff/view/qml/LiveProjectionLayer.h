// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

    // Hole creation hooks — called by structural-key handlers (Stage 4+).
    void createBlockHole(const BlockHole &hole);
    void createInlineHole(const InlineHole &hole);

    // Prediction creation hooks — called by InlineFormatHighlighter (Stage 3)
    // and LiveSpeculativeFenceController (Stage 2).
    void createInlinePrediction(const InlinePrediction &prediction);
    void createBlockKindPrediction(const BlockKindPrediction &prediction);

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

    /// A hole has been reified into a real CRDT edit at `newAnchor`. The
    /// listening view uses this to route focus into the now-real block.
    void holeReified(Markoff::BlockAnchor newAnchor);

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
    QList<BlockHole>                m_blockHoles;
    QList<InlineHole>               m_inlineHoles;
    QHash<int, QList<InlinePrediction>> m_inlinePredictions;     // keyed by row
    QHash<int, BlockKindPrediction> m_blockKindPredictions;      // keyed by row

    EditorBackend  *m_backend = nullptr;
    LiveBlockModel *m_model = nullptr;
};

}  // namespace Markoff::View::Qml
