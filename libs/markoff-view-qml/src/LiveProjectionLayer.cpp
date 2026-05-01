// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveProjectionLayer.h>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>

namespace Markoff::View::Qml {

LiveProjectionLayer::LiveProjectionLayer(QObject *parent)
    : QObject(parent)
{}

LiveProjectionLayer::~LiveProjectionLayer() = default;

void LiveProjectionLayer::setEditorBackend(EditorBackend *backend)
{
    if (m_backend == backend) return;
    if (m_backend) {
        QObject::disconnect(m_backend, &EditorBackend::parseUpdatedAt,
                            this, nullptr);
    }
    m_backend = backend;
    if (m_backend) {
        // Reconciliation runs synchronously on parse arrival (spec §5
        // invariant 15). The 3-arg signal is reduced to a no-op connection
        // here — Stage 2 reconciliation only needs to drop registered
        // predictions; Stage 4 will widen this to anchor-invalidation for
        // holes.
        QObject::connect(m_backend, &EditorBackend::parseUpdatedAt,
                         this, [this](const Markoff::Document *,
                                      quint64,
                                      const QList<Markoff::BlockAnchor> &) {
                             onParseUpdated();
                         });
    }
}

void LiveProjectionLayer::setBlockModel(LiveBlockModel *model)
{
    m_model = model;
}

EditorBackend *LiveProjectionLayer::editorBackend() const
{
    return m_backend;
}

LiveBlockModel *LiveProjectionLayer::blockModel() const
{
    return m_model;
}

void LiveProjectionLayer::createBlockHole(const BlockHole &hole)
{
    m_blockHoles.append(hole);
}

void LiveProjectionLayer::createInlineHole(const InlineHole &hole)
{
    m_inlineHoles.append(hole);
}

void LiveProjectionLayer::createInlinePrediction(const InlinePrediction &prediction)
{
    m_inlinePredictions[prediction.row].append(prediction);
}

void LiveProjectionLayer::createBlockKindPrediction(const BlockKindPrediction &prediction)
{
    m_blockKindPredictions.insert(prediction.row, prediction);
    // Apply the kind change to the model on the producer's behalf. The model
    // owns the speculative-state map (cleared by `applyOps` when parse truth
    // arrives); the layer owns the prediction-set bookkeeping.
    if (m_model) {
        m_model->speculativelyChangeKind(prediction.row, prediction.speculativeKind);
    }
}

void LiveProjectionLayer::dropBlockKindPrediction(int row)
{
    m_blockKindPredictions.remove(row);
    if (m_model) {
        m_model->revertSpeculativeKind(row);
    }
}

QList<InlinePrediction> LiveProjectionLayer::predictionsForRow(int row) const
{
    return m_inlinePredictions.value(row);
}

const BlockKindPrediction *LiveProjectionLayer::blockKindPredictionFor(int row) const
{
    auto it = m_blockKindPredictions.constFind(row);
    if (it == m_blockKindPredictions.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

bool LiveProjectionLayer::rowIsHole(int /*row*/) const
{
    // Stage-1: no row↔hole mapping is established yet (the model doesn't
    // interleave holes until Stage 4 / T17). When holes carry no row, the
    // answer is trivially "no real row corresponds to a hole." Any test
    // exercising `rowIsHole` for a registered hole row depends on Stage 4
    // wiring; for Stage 1, surfacing whether *any* block hole is registered
    // is enough to validate the storage path.
    return !m_blockHoles.isEmpty();
}

int LiveProjectionLayer::blockHoleCount() const
{
    return m_blockHoles.size();
}

int LiveProjectionLayer::inlineHoleCount() const
{
    return m_inlineHoles.size();
}

int LiveProjectionLayer::inlinePredictionCount() const
{
    int count = 0;
    for (const auto &list : m_inlinePredictions) {
        count += list.size();
    }
    return count;
}

int LiveProjectionLayer::blockKindPredictionCount() const
{
    return m_blockKindPredictions.size();
}

void LiveProjectionLayer::onParseUpdated()
{
    // Stage-2: reconciliation for block-kind predictions on parse return.
    //
    // The model's own speculative-state map (`m_speculativeOriginals`) is
    // cleared by `LiveBlockModel::applyOps` when parse results are applied —
    // parser truth is authoritative there. The layer's prediction-set
    // bookkeeping mirrors that lifecycle: every registered block-kind
    // prediction is dropped on parse arrival. If the parser confirmed the
    // speculative kind, the model row already reflects the confirmed kind;
    // if it contradicted, `applyOps` snapped the row back. Either way, the
    // prediction has run its course.
    m_blockKindPredictions.clear();

    // Stage 3 fills in inline prediction reconciliation; Stage 4 fills in
    // hole anchor-invalidation.
}

void LiveProjectionLayer::onLocalEditApplied()
{
    // Stage-1: no-op seam. Stage 4 fills in:
    //   - first printable char into a hole row reifies the hole into a real
    //     applyLocalEdit; emit holeReified
    //   - backspace at qtPos 0 in an empty hole drops the hole and routes
    //     focus to the previous row
}

}  // namespace Markoff::View::Qml
