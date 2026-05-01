// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveListModelBinding.h>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/LiveSelectionView.h>
#include "BlockWalker.h"
#include "AstBlockDiff.h"

#include <markoff-parser/Document.h>

#include <QList>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

#include <atomic>
#include <optional>

namespace Markoff::View::Qml {

struct LiveListModelBinding::Private {
    EditorBackend       *editorBackend = nullptr;
    LiveBlockModel      *model         = nullptr;
    LiveSelectionView   *selection     = nullptr;
    LiveProjectionLayer *projection    = nullptr;
    QList<BlockKey>      lastKeys;

    std::optional<Markoff::BlockAnchor> focusedAnchor;
    int focusedCursorPosition = 0;

    /// Monotonic counter incremented on every dispatched walk. Worker threads
    /// capture the value at dispatch time and the main-thread post-back drops
    /// stale results when a newer walk has been dispatched in the meantime.
    /// std::atomic because the worker thread reads it during the post-back.
    std::atomic<quint64> walkGeneration{0};

    /// The parse sequence number from the last accepted parse result.
    quint64 lastParseSequence{0};

    /// Private thread pool dedicated to BlockWalker dispatch. Owning it
    /// (rather than borrowing the global pool) lets the destructor block on
    /// `waitForDone()` so no worker is mid-`invokeMethod(this, ...)` when
    /// `*this` is being torn down. Race avoidance, not throughput.
    QThreadPool walkPool;
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model = new LiveBlockModel(this);
    d->selection = new LiveSelectionView(this);
    // Spec §5 invariant 14: one projection-layer instance per binding,
    // owned by the binding, lifetime equals the binding's.
    d->projection = new LiveProjectionLayer(this);
    d->projection->setBlockModel(d->model);
    // One worker is enough — stale-walk drop happens via walkGeneration; we
    // don't benefit from running multiple walks in parallel against the same
    // model.
    d->walkPool.setMaxThreadCount(1);
}

LiveListModelBinding::~LiveListModelBinding()
{
    // Drain in-flight walkers before the QObject base destructor runs;
    // otherwise a worker mid-invokeMethod against `this` races the teardown.
    d->walkPool.waitForDone();
}

EditorBackend *LiveListModelBinding::editorBackend() const { return d->editorBackend; }
LiveBlockModel *LiveListModelBinding::model() const { return d->model; }
LiveSelectionView *LiveListModelBinding::selectionModel() const { return d->selection; }
LiveProjectionLayer *LiveListModelBinding::projectionLayer() const { return d->projection; }

void LiveListModelBinding::setEditorBackend(EditorBackend *eb)
{
    if (d->editorBackend == eb) return;
    if (d->editorBackend) {
        QObject::disconnect(d->editorBackend, nullptr, this, nullptr);
    }
    d->editorBackend = eb;
    // Forward the backend to the projection layer so it can subscribe to
    // `parseUpdatedAt` itself — the layer owns its own reconciliation
    // connection (single subscriber per signal; spec §5 invariant 15).
    d->projection->setEditorBackend(eb);
    if (d->editorBackend) {
        QObject::connect(d->editorBackend, &EditorBackend::parseUpdatedAt,
                         this, &LiveListModelBinding::onParseUpdatedAt);
        QObject::connect(d->editorBackend, &EditorBackend::sessionChanged, this, [this]() {
            d->selection->setSession(d->editorBackend->session());
        });
        QObject::connect(d->editorBackend, &EditorBackend::documentChanged, this, [this]() {
            d->selection->setDocument(d->editorBackend->document());
        });
        d->selection->setDocument(d->editorBackend->document());
        d->selection->setSession(d->editorBackend->session());
    } else {
        d->selection->setDocument(nullptr);
        d->selection->setSession(nullptr);
    }
    Q_EMIT editorBackendChanged();
}

void LiveListModelBinding::onParseUpdatedAt(const Markoff::Document *parsed,
                                            quint64 parseSequence,
                                            const QList<Markoff::BlockAnchor> &blockAnchors)
{
    if (!parsed) return;

    // Capture before crossing the thread boundary. `sourceText()` returns a
    // QString (implicitly shared); the worker walks a private view of it.
    // We never deref `parsed` off-thread — its lifetime is owned by
    // MarkoffDocument's `latestParse` and may be replaced when the next parse
    // arrives.
    const QString source = parsed->sourceText();
    // blockAnchors is a QList (implicitly shared) — copy to own a named value for the lambda capture.
    const QList<Markoff::BlockAnchor> anchors = blockAnchors;
    const quint64 myGen  = d->walkGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

    d->walkPool.start([this, source, anchors, myGen, parseSequence]() {
        QList<BlockRecord> records = BlockWalker::walk(source);

        // Post the result back to the binding's thread. If `this` is
        // destroyed before delivery, Qt drops the pending event during
        // QObject's destructor.
        QMetaObject::invokeMethod(this,
            [this, records = std::move(records), anchors, myGen, parseSequence]() mutable {
                // Drop stale results: a newer walk has been dispatched since.
                if (myGen != d->walkGeneration.load(std::memory_order_acquire)) return;

                d->lastParseSequence = parseSequence;

                // Build keys from (kind, BlockAnchor) pairs. The anchors list
                // is aligned to the top-level block order from the parse; if
                // the walker produced more records than we have anchors (e.g.
                // BlockWalker adds synthetic sub-blocks), we fall back to a
                // default-constructed BlockAnchor so the list lengths stay in
                // sync with records.
                QList<BlockKey> nextKeys;
                nextKeys.reserve(records.size());
                for (qsizetype i = 0; i < records.size(); ++i) {
                    const Markoff::BlockAnchor anchor =
                        (i < anchors.size()) ? anchors[i] : Markoff::BlockAnchor{};
                    records[i].blockAnchor = anchor;
                    nextKeys.append(BlockKey { records[i].kind, anchor });
                }

                const QList<AstBlockDiff::Op> ops =
                    AstBlockDiff::diff(d->lastKeys, nextKeys);

                if (d->selection->hasSelection()) {
                    QList<Markoff::BlockAnchor> deletedBlockAnchors;
                    for (const auto &op : ops) {
                        if (op.kind == AstBlockDiff::OpKind::Delete &&
                            op.prevIndex >= 0 && op.prevIndex < d->lastKeys.size()) {
                            deletedBlockAnchors.append(d->lastKeys[op.prevIndex].anchor);
                        }
                    }
                    if (!deletedBlockAnchors.isEmpty())
                        d->selection->notifyBlocksRemoved(deletedBlockAnchors);
                }

                // Detect kind-change at the focused block: a Delete of the
                // focused block's anchor paired with an Insert at the same
                // model row position (nextIndex == deletedRow) means the block
                // changed kind in-place rather than being reshuffled.
                // NOTE: the BlockAnchor changes on prepend (e.g. "# " →
                // heading) because it is the CRDT anchor at the first byte.
                // Two-pass approach: first find the Delete for the focused
                // anchor, then separately scan for an Insert at that exact row.
                // This prevents an unrelated Insert that happens to be the last
                // one in the diff from matching against an earlier Delete.
                if (d->focusedAnchor.has_value()) {
                    const auto fa = d->focusedAnchor.value();
                    int deletedRow = -1;
                    for (const auto &op : ops) {
                        if (op.kind == AstBlockDiff::OpKind::Delete
                                && op.prevIndex >= 0
                                && op.prevIndex < d->lastKeys.size()
                                && d->lastKeys[op.prevIndex].anchor == fa) {
                            deletedRow = op.prevIndex;
                            break;
                        }
                    }
                    bool insertedAtSameRow = false;
                    if (deletedRow >= 0) {
                        for (const auto &op : ops) {
                            if (op.kind == AstBlockDiff::OpKind::Insert
                                    && op.nextIndex == deletedRow) {
                                insertedAtSameRow = true;
                                break;
                            }
                        }
                    }
                    // Same row deleted and re-inserted at that position → kind-change.
                    if (deletedRow >= 0 && insertedAtSameRow) {
                        const int savedPos = d->focusedCursorPosition;
                        const Markoff::BlockAnchor newAnchor = nextKeys[deletedRow].anchor;
                        // Update focused anchor to the new anchor so
                        // isFocusRestoreTarget works in delegates.
                        d->focusedAnchor = newAnchor;
                        QMetaObject::invokeMethod(this, [this, newAnchor, savedPos]() {
                            Q_EMIT focusRestoreRequested(newAnchor, savedPos);
                        }, Qt::QueuedConnection);
                    }
                }

                // Hole-aware applyOps: detach any pending hole so the diff
                // ops land on the parsed-rows underlay only, then reattach
                // (if anchor row still valid) or abandon.
                std::optional<BlockHole> heldHole;
                if (d->projection && d->projection->hasPendingBlockHole()) {
                    heldHole = d->projection->detachPendingHoleForReparse();
                }

                d->model->applyOps(ops, records);
                d->lastKeys = std::move(nextKeys);

                if (heldHole.has_value()) {
                    if (heldHole->afterParsedRow >= 0
                            && heldHole->afterParsedRow < records.size()) {
                        d->projection->reattachHoleAfterReparse(*heldHole);
                    } else if (heldHole->afterParsedRow == -1
                               && records.isEmpty()) {
                        // Empty-document case: hole anchors at row 0, no
                        // parsed rows. Still valid.
                        d->projection->reattachHoleAfterReparse(*heldHole);
                    } else {
                        d->projection->reattachHoleAfterReparseAbandon(*heldHole);
                    }
                }
            },
            Qt::QueuedConnection);
    });
}

void LiveListModelBinding::notifyFocused(const Markoff::BlockAnchor &anchor, int cursorPos)
{
    d->focusedAnchor = anchor;
    d->focusedCursorPosition = cursorPos;
}

void LiveListModelBinding::notifyFocusedCursorMoved(int cursorPos)
{
    d->focusedCursorPosition = cursorPos;
}

bool LiveListModelBinding::isFocusRestoreTarget(const Markoff::BlockAnchor &anchor) const
{
    return d->focusedAnchor.has_value() && d->focusedAnchor.value() == anchor;
}

void LiveListModelBinding::setRowComposing(int row, bool composing)
{
    d->model->setComposingRow(row, composing);
}

}  // namespace Markoff::View::Qml
