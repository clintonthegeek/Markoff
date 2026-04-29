// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParsePool.h"
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

#include <QMetaObject>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <utility>

namespace Markoff::Parse::Detail {

struct ParsePool::Private {
    QThread          *thread = nullptr;
    ParsePoolWorker  *worker = nullptr;
    mutable QMutex    mutex;
    quint64           generation = 0;     // bumped on each schedule()
    bool              workerBusy = false; // a parse is currently running on the worker
    QByteArray        pending;            // snapshot waiting to be dispatched after current parse
    quint64           pendingGen = 0;     // generation tag for pending snapshot
};

ParsePool::ParsePool(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // QSignalSpy and queued connections need this metatype registered.
    static const int sRegistered = []{
        qRegisterMetaType<const Markoff::Document *>("const Markoff::Document*");
        qRegisterMetaType<Markoff::Document *>("Markoff::Document*");
        return 0;
    }();
    Q_UNUSED(sRegistered);

    d->thread = new QThread(this);
    d->worker = new ParsePoolWorker();
    d->worker->moveToThread(d->thread);

    // Worker is owned by the thread; delete it when the thread finishes.
    connect(d->thread, &QThread::finished, d->worker, &QObject::deleteLater);

    // Results come back via QueuedConnection so the lambda runs on our thread.
    connect(d->worker, &ParsePoolWorker::parsed,
            this, [this](Markoff::Document *parsed, quint64 gen) {
        QByteArray nextSnapshot;
        quint64    nextGen = 0;
        {
            QMutexLocker lk(&d->mutex);
            if (gen != d->generation) {
                // This result is for a snapshot that has been superseded by
                // a newer pending one. Drop the result and dispatch the pending.
                delete parsed;
                parsed = nullptr;
            }
            // Drain pending if any.
            if (!d->pending.isEmpty() || d->pendingGen != 0) {
                nextSnapshot = std::move(d->pending);
                nextGen      = d->pendingGen;
                d->pending.clear();
                d->pendingGen = 0;
                // worker stays busy
            } else {
                d->workerBusy = false;
            }
        }
        if (parsed) Q_EMIT parseReady(static_cast<const Markoff::Document *>(parsed));
        if (nextGen != 0) {
            ParsePoolWorker *worker = d->worker;
            QMetaObject::invokeMethod(worker,
                [worker, b = std::move(nextSnapshot), nextGen]() mutable {
                    worker->parseSnapshot(std::move(b), nextGen);
                },
                Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);

    d->thread->start();
}

ParsePool::~ParsePool()
{
    d->thread->quit();
    d->thread->wait();
    // Worker is deleteLater'd on thread::finished (see constructor connect).
    // Any QueuedConnection-delivered lambdas from worker::parsed still in the
    // owner-thread event queue are safe: ~QObject (runs after this destructor
    // body) disconnects all incoming queued connections before the next
    // event-loop iteration can deliver them.
}

void ParsePool::schedule(QByteArray utf8)
{
    quint64 gen;
    bool    dispatchNow = false;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        if (d->workerBusy) {
            // A parse is already in flight. Replace pending with this newer
            // snapshot (drops any prior pending — that's the coalesce).
            d->pending    = std::move(utf8);
            d->pendingGen = gen;
        } else {
            d->workerBusy = true;
            dispatchNow   = true;
        }
    }
    if (dispatchNow) {
        ParsePoolWorker *worker = d->worker;
        QMetaObject::invokeMethod(worker,
            [worker, b = std::move(utf8), gen]() mutable {
                worker->parseSnapshot(std::move(b), gen);
            },
            Qt::QueuedConnection);
    }
}

bool ParsePool::isPending() const
{
    QMutexLocker lk(&d->mutex);
    return d->workerBusy || !d->pending.isEmpty();
}

}  // namespace Markoff::Parse::Detail
