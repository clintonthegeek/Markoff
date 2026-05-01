// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveSpeculativeFenceController.h>

#include <markoff/view/qml/BlockKind.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/ProjectionItem.h>

namespace Markoff::View::Qml {

LiveSpeculativeFenceController::LiveSpeculativeFenceController(QObject *parent)
    : QObject(parent)
{}

LiveBlockModel *LiveSpeculativeFenceController::model() const { return m_model; }

void LiveSpeculativeFenceController::setModel(LiveBlockModel *m)
{
    if (m_model == m) return;
    m_model = m;
    Q_EMIT modelChanged();
}

LiveProjectionLayer *LiveSpeculativeFenceController::projectionLayer() const { return m_layer; }

void LiveSpeculativeFenceController::setProjectionLayer(LiveProjectionLayer *layer)
{
    if (m_layer == layer) return;
    m_layer = layer;
    Q_EMIT projectionLayerChanged();
}

void LiveSpeculativeFenceController::onEditApplied(const Markoff::BlockAnchor &/*anchor*/,
                                                   int row,
                                                   const QString &postText)
{
    // Stage-2 contract: this controller is a *producer*. All kind mutations on
    // the model flow through `LiveProjectionLayer`. Without a layer we have
    // nowhere to register the prediction, so we no-op rather than dual-route.
    if (!m_layer) return;
    if (!m_model) return;
    if (row < 0 || row >= m_model->rowCount()) return;

    const QString currentKind = m_model->data(
        m_model->index(row, 0),
        LiveBlockModel::KindRole).toString();

    if (currentKind != QStringLiteral("paragraph")) {
        // Not a paragraph — drop any stale prediction on this row. The layer
        // is the single owner of the kind-revert mutation.
        if (m_model->isSpeculative(row))
            m_layer->dropBlockKindPrediction(row);
        return;
    }

    // TODO: The spec (step 8.2) also requires "no prior unclosed fence in the doc"
    // before speculating. This guard is not implemented: we check only the block's own
    // text. False positives (paragraph inside an already-open fence) are always
    // corrected when the next parse arrives via LiveBlockModel::applyOps clearing all
    // speculative state. The transient visual artifact is minor and deferred.
    if (isFenceOpener(postText)) {
        if (!m_model->isSpeculative(row)) {
            BlockKindPrediction p;
            p.row             = row;
            p.originalKind    = currentKind;
            p.speculativeKind = BlockKind::CodeBlock;
            m_layer->createBlockKindPrediction(p);
        }
    } else {
        // Fence no longer present — drop the prediction.
        if (m_model->isSpeculative(row))
            m_layer->dropBlockKindPrediction(row);
    }
}

bool LiveSpeculativeFenceController::isFenceOpener(const QString &text)
{
    // A fence opener: text starts with ``` or ~~~ (optionally followed by a lang tag).
    return text.startsWith(QStringLiteral("```"))
        || text.startsWith(QStringLiteral("~~~"));
}

}  // namespace Markoff::View::Qml
