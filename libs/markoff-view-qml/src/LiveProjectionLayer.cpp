// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveProjectionLayer.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

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

quint64 LiveProjectionLayer::createBlockHole(BlockHole hole)
{
    // v1 invariant: at most one block hole pending. Displace any prior hole
    // first (commit if it has buffered text, drop if not).
    if (m_pendingHole.has_value()) {
        commitAllPendingHoles();
    }

    hole.id = m_nextHoleId++;
    m_pendingHole = hole;

    if (m_model) {
        m_model->insertHoleRow(hole.id, hole.kind, hole.bufferText, hole.afterParsedRow);
    }
    return hole.id;
}

void LiveProjectionLayer::setBlockHoleBuffer(quint64 holeId, const QString &bufferText)
{
    if (!m_pendingHole.has_value() || m_pendingHole->id != holeId) return;
    if (m_pendingHole->bufferText == bufferText) return;
    m_pendingHole->bufferText = bufferText;
    if (m_model) {
        m_model->setHoleBufferText(bufferText);
    }
    Q_EMIT bufferChanged(holeId);
}

void LiveProjectionLayer::commitBlockHole(quint64 holeId)
{
    if (!m_pendingHole.has_value() || m_pendingHole->id != holeId) return;

    // (1) Synchronously notify the active delegate so it can finalize any
    // in-flight Qt IME preedit. The delegate's slot may call
    // `setBlockHoleBuffer` re-entrantly; that's intentional.
    Q_EMIT aboutToCommit(holeId);

    // (2) Snapshot AFTER aboutToCommit so a final IME commit lands.
    if (!m_pendingHole.has_value() || m_pendingHole->id != holeId) {
        // Slot dropped/replaced the hole synchronously. Nothing to do.
        return;
    }
    const BlockHole snapshot = *m_pendingHole;
    const int expectedRow = snapshot.afterParsedRow + 1;
    const int qtPos = snapshot.bufferText.length();  // UTF-16 code units

    // (3) Remove the hole row BEFORE applying the local edit so the parse
    // round-trip's resulting `applyOps` sees a hole-free model (the model's
    // applyOps assert fires otherwise).
    m_pendingHole.reset();
    if (m_model) {
        m_model->removeHoleRow();
    }

    // (4) Apply the single CRDT edit that materializes the buffered text.
    if (m_backend && m_backend->document()) {
        Markoff::MarkoffEdit edit;
        edit.oldStart = snapshot.reifyOffset;
        edit.oldEnd   = snapshot.reifyOffset;
        QByteArray bytes;
        bytes.append("\n\n", 2);
        bytes.append(snapshot.bufferText.toUtf8());
        edit.newText = bytes;
        m_backend->document()->applyLocalEdit({ edit });
    }

    // (5) One-shot listener on the model's `rowsInserted`. Emit
    // `holeReified` once a parse-driven Insert lands a row that contains
    // (or is) the expected post-commit viewRow. The parser may insert
    // multiple rows in a single round-trip; we treat the first range that
    // covers `expectedRow` as the match (see fallback note in T19 plan).
    if (m_model) {
        // Disconnect any stale prior connection before installing a new one.
        if (m_pendingHoleReifiedConnection) {
            QObject::disconnect(m_pendingHoleReifiedConnection);
            m_pendingHoleReifiedConnection = {};
        }
        m_pendingHoleReifiedConnection = QObject::connect(
            m_model, &QAbstractItemModel::rowsInserted, this,
            [this, expectedRow, qtPos](const QModelIndex &, int first, int last) {
                if (first <= expectedRow && expectedRow <= last) {
                    Q_EMIT holeReified(expectedRow, qtPos);
                    if (m_pendingHoleReifiedConnection) {
                        QObject::disconnect(m_pendingHoleReifiedConnection);
                        m_pendingHoleReifiedConnection = {};
                    }
                }
            });
    }
}

void LiveProjectionLayer::dropBlockHole(quint64 holeId)
{
    if (!m_pendingHole.has_value() || m_pendingHole->id != holeId) return;
    const int viewRow = m_pendingHole->afterParsedRow + 1;
    m_pendingHole.reset();
    if (m_model) {
        m_model->removeHoleRow();
    }
    Q_EMIT holeDropped(viewRow);
}

void LiveProjectionLayer::commitAllPendingHoles()
{
    if (!m_pendingHole.has_value()) return;
    const quint64 id = m_pendingHole->id;
    if (m_pendingHole->bufferText.isEmpty()) {
        dropBlockHole(id);
    } else {
        commitBlockHole(id);
    }
}

quint64 LiveProjectionLayer::pendingBlockHoleId() const
{
    return m_pendingHole.has_value() ? m_pendingHole->id : 0;
}

QString LiveProjectionLayer::pendingBlockHoleBuffer() const
{
    return m_pendingHole.has_value() ? m_pendingHole->bufferText : QString();
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

void LiveProjectionLayer::clearInlinePredictionsForRow(int row)
{
    m_inlinePredictions.remove(row);
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

bool LiveProjectionLayer::rowIsHole(int row) const
{
    if (!m_pendingHole.has_value()) return false;
    if (m_model && m_model->hasHoleRow()) {
        return row == m_model->holeViewRow();
    }
    // No model wired yet (test convenience): treat any row as "is hole" if
    // a hole exists, mirroring the prior Stage-1 stub behavior.
    return true;
}

int LiveProjectionLayer::blockHoleCount() const
{
    return m_pendingHole.has_value() ? 1 : 0;
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

    // Stage-3: inline prediction reconciliation. Same wholesale-clear policy
    // mirrors the producer-side `setSource` recomputation: every keystroke
    // re-runs the highlighter's source-scan and re-publishes predictions, so
    // dropping the registry on parse arrival is safe — the next character
    // typed will republish anything still applicable. Per-row selective
    // reconciliation is a follow-on optimization.
    m_inlinePredictions.clear();

    // Stage 4 fills in hole anchor-invalidation.
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
