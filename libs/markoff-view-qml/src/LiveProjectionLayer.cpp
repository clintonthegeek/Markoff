// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveProjectionLayer.h>

#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>

#include <markoff-foundation/MarkoffEdit.h>

namespace Markoff::View::Qml {

namespace {
constexpr int kHoleIdleTimeoutMs = 30 * 1000;  // spec §3.3 / §9
}

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
        QObject::disconnect(m_backend, &EditorBackend::documentChanged,
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
        // Track the document so we can resolve `m_observedDocument` when
        // reifying. The undo-pairing path (spec §6 case 1) is invoked
        // explicitly via `undoWithHoles()` rather than observed via
        // `contentsChanged`, because `contentsChanged` fires for every edit
        // (typing, undo, redo, remote ops) and disambiguating the undo case
        // from forward typing requires extra bookkeeping that is not worth
        // it for v0. See spec §9 — the simpler-of-two-options choice.
        auto rewireDocument = [this]() {
            m_observedDocument = m_backend ? m_backend->document() : nullptr;
        };
        rewireDocument();
        QObject::connect(m_backend, &EditorBackend::documentChanged,
                         this, rewireDocument);
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
    // Multiple-stacked-Enter coalesce (spec §9): if a hole already exists for
    // the same parent parsed-row, second Enter is a no-op.
    for (const BlockHole &existing : m_blockHoles) {
        if (existing.afterParsedRow == hole.afterParsedRow) {
            return existing.id;  // second Enter is a no-op; reuse existing id
        }
    }

    if (hole.id == 0) hole.id = m_nextHoleId++;
    m_blockHoles.append(hole);

    if (m_model) {
        m_model->insertHole(hole.id, hole.afterParsedRow,
                            hole.kind.isEmpty()
                                ? QStringLiteral("paragraph") : hole.kind);
    }

    // Idle abandonment timer (30s, hard-coded for v0; spec §3.3 / §9).
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kHoleIdleTimeoutMs);
    const quint64 hid = hole.id;
    QObject::connect(timer, &QTimer::timeout, this,
                     [this, hid]() { onIdleTimerFired(hid); });
    timer->start();
    m_idleTimers.insert(hid, timer);

    return hole.id;
}

void LiveProjectionLayer::dropBlockHole(quint64 holeId)
{
    int idx = -1;
    for (int i = 0; i < m_blockHoles.size(); ++i) {
        if (m_blockHoles[i].id == holeId) { idx = i; break; }
    }
    if (idx < 0) return;

    if (auto *t = m_idleTimers.take(holeId)) {
        t->stop();
        t->deleteLater();
    }
    m_blockHoles.removeAt(idx);
    if (m_model) m_model->removeHole(holeId);
}

bool LiveProjectionLayer::reifyBlockHole(quint64 holeId, const QString &text)
{
    int idx = -1;
    for (int i = 0; i < m_blockHoles.size(); ++i) {
        if (m_blockHoles[i].id == holeId) { idx = i; break; }
    }
    if (idx < 0) return false;
    const BlockHole hole = m_blockHoles[idx];

    // Capture the hole's view-row BEFORE dropping it, so we can tell the view
    // where to route focus once the parse round-trip materialises the real
    // block.
    const int reifiedViewRow = viewRowForBlockHoleId(holeId);

    // SYNCHRONOUSLY drop the hole BEFORE applyLocalEdit, so the next parse
    // arrives to a model without the synthetic row and produces the real
    // block at the same view index (spec §3.3 reification semantics).
    dropBlockHole(holeId);

    if (m_observedDocument && !text.isEmpty()) {
        Markoff::MarkoffEdit ed;
        ed.oldStart = hole.reifyByteOffset;
        ed.oldEnd   = hole.reifyByteOffset;
        ed.newText  = text.toUtf8();
        m_observedDocument->applyLocalEdit({ ed });
    }

    // Stage 4 follow-up: surface the reify to the view so it can route focus
    // into the now-real block once parse arrives. The view tracks pending
    // reify-focus state and applies on the next listView.count restoration.
    if (reifiedViewRow >= 0) {
        Q_EMIT holeReified(reifiedViewRow, static_cast<int>(text.length()));
    }
    return true;
}

void LiveProjectionLayer::undoWithHoles()
{
    // Spec §6 case 1: drop unreified holes first, then run the CRDT undo
    // (which rolls back the paired `\n\n` insert). One user-visible step.
    const bool hadHole = !m_blockHoles.isEmpty();
    while (!m_blockHoles.isEmpty()) {
        dropBlockHole(m_blockHoles.first().id);
    }
    if (hadHole && m_observedDocument) {
        m_observedDocument->undo();
    } else if (m_observedDocument) {
        // No holes — normal undo.
        m_observedDocument->undo();
    }
}

void LiveProjectionLayer::restartHoleIdleTimer(quint64 holeId)
{
    auto it = m_idleTimers.find(holeId);
    if (it == m_idleTimers.end()) return;
    it.value()->start();  // restart with same interval
}

void LiveProjectionLayer::onIdleTimerFired(quint64 holeId)
{
    dropBlockHole(holeId);
}

bool LiveProjectionLayer::hasBlockHoleAfterParsedRow(int parsedRow) const
{
    for (const BlockHole &h : m_blockHoles) {
        if (h.afterParsedRow == parsedRow) return true;
    }
    return false;
}

quint64 LiveProjectionLayer::blockHoleIdAt(int viewRow) const
{
    if (!m_model) return 0;
    return m_model->holeIdAt(viewRow);
}

int LiveProjectionLayer::viewRowForBlockHoleId(quint64 holeId) const
{
    if (!m_model) return -1;
    const int total = m_model->rowCount();
    for (int r = 0; r < total; ++r) {
        if (m_model->holeIdAt(r) == holeId) return r;
    }
    return -1;
}

quint32 LiveProjectionLayer::pairedSourceEditByteCountForHoleId(quint64 holeId) const
{
    for (const BlockHole &h : m_blockHoles) {
        if (h.id == holeId) return h.pairedSourceEditByteCount;
    }
    return 0;
}

BlockHole LiveProjectionLayer::blockHoleById(quint64 holeId) const
{
    for (const BlockHole &h : m_blockHoles) {
        if (h.id == holeId) return h;
    }
    return {};
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
    // Stage 4: the model is the source of truth for the row↔hole mapping.
    // The Stage 1 placeholder (`return !m_blockHoles.isEmpty()`) is
    // retired here.
    if (m_model) return m_model->isHoleRow(row);
    // Fallback when no model is wired (e.g. unit tests that exercise the
    // layer in isolation): treat any registered hole as if it occupied an
    // unspecified row, so the existing skeleton tests stay green.
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
