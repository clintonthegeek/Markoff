// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveProjectionLayer.h>

namespace Markoff::View::Qml {

LiveProjectionLayer::LiveProjectionLayer(QObject *parent)
    : QObject(parent)
{}

LiveProjectionLayer::~LiveProjectionLayer() = default;

void LiveProjectionLayer::setEditorBackend(EditorBackend *backend)
{
    m_backend = backend;
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
    // Stage-1: no-op seam. Stage 2/3 fill in:
    //   - drop inline predictions whose ranges the parser now confirms
    //   - drop block-kind predictions whose kind the parser now matches
    //   - snap contradicted predictions away (model emits dataChanged via the
    //     blockModel/binding path)
    //   - drop holes whose origin anchor has been invalidated
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
