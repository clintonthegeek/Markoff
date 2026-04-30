// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveListModelBinding.h>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionModel.h>
#include "BlockWalker.h"
#include "AstBlockDiff.h"

#include <markoff-parser/Document.h>

#include <QMetaObject>
#include <QRunnable>
#include <QSet>
#include <QThreadPool>

#include <atomic>

namespace Markoff::View::Qml {

struct LiveListModelBinding::Private {
    EditorBackend       *editorBackend = nullptr;
    LiveBlockModel      *model         = nullptr;
    LiveSelectionModel  *selection     = nullptr;
    QList<BlockKey>      lastKeys;

    /// Monotonic counter incremented on every dispatched walk. Worker threads
    /// capture the value at dispatch time and the main-thread post-back drops
    /// stale results when a newer walk has been dispatched in the meantime.
    /// std::atomic because the worker thread reads it during the post-back.
    std::atomic<quint64> walkGeneration{0};

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
    d->selection = new LiveSelectionModel(this);
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
LiveSelectionModel *LiveListModelBinding::selectionModel() const { return d->selection; }

void LiveListModelBinding::setEditorBackend(EditorBackend *eb)
{
    if (d->editorBackend == eb) return;
    if (d->editorBackend) {
        QObject::disconnect(d->editorBackend, nullptr, this, nullptr);
    }
    d->editorBackend = eb;
    if (d->editorBackend) {
        QObject::connect(d->editorBackend, &EditorBackend::parseUpdatedAt,
                         this, &LiveListModelBinding::onParseUpdatedAt);
    }
    Q_EMIT editorBackendChanged();
}

void LiveListModelBinding::onParseUpdatedAt(const Markoff::Document *parsed,
                                            CollabText::Crdt::Global /*atVersion*/)
{
    if (!parsed) return;

    // Capture before crossing the thread boundary. `sourceText()` returns a
    // QString (implicitly shared); the worker walks a private view of it.
    // We never deref `parsed` off-thread — its lifetime is owned by
    // MarkoffDocument's `latestParse` and may be replaced when the next parse
    // arrives.
    const QString source = parsed->sourceText();
    const quint64 myGen  = d->walkGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

    d->walkPool.start([this, source, myGen]() {
        QList<BlockRecord> records = BlockWalker::walk(source);

        // Post the result back to the binding's thread. If `this` is
        // destroyed before delivery, Qt drops the pending event during
        // QObject's destructor.
        QMetaObject::invokeMethod(this,
            [this, records = std::move(records), myGen]() mutable {
                // Drop stale results: a newer walk has been dispatched since.
                if (myGen != d->walkGeneration.load(std::memory_order_acquire)) return;

                QList<BlockKey> nextKeys;
                nextKeys.reserve(records.size());
                for (const auto &r : records) {
                    nextKeys.append(BlockKey { r.kind, r.source });
                }

                const QList<AstBlockDiff::Op> ops =
                    AstBlockDiff::diff(d->lastKeys, nextKeys);

                if (d->selection->hasSelection()) {
                    QSet<int> deletedPrevIndices;
                    for (const auto &op : ops) {
                        if (op.kind == AstBlockDiff::OpKind::Delete) {
                            deletedPrevIndices.insert(op.prevIndex);
                        }
                    }
                    const int aB   = d->selection->anchorBlock();
                    const int actB = d->selection->activeBlock();
                    if (deletedPrevIndices.contains(aB) ||
                        deletedPrevIndices.contains(actB)) {
                        d->selection->clear();
                    }
                }

                d->model->applyOps(ops, records);
                d->lastKeys = std::move(nextKeys);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace Markoff::View::Qml
