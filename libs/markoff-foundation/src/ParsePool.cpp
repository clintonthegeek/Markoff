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

namespace {
enum class ParseKind { Incremental, Reset };
}

struct ParsePool::Private {
    QThread          *thread = nullptr;
    ParsePoolWorker  *worker = nullptr;
    mutable QMutex    mutex;
    quint64           generation = 0;     // bumped on each schedule*()
    bool              workerBusy = false; // a parse is currently running on the worker
    QByteArray        pending;            // latest snapshot waiting to be dispatched
    quint64           pendingGen = 0;     // generation tag for pending snapshot
    ParseKind         pendingKind = ParseKind::Incremental;
};

namespace {
/// Dispatch a parse to the worker, picking the slot that matches `kind`.
void dispatch(ParsePoolWorker *worker, QByteArray utf8, quint64 gen, ParseKind kind)
{
    if (kind == ParseKind::Reset) {
        QMetaObject::invokeMethod(worker,
            [worker, b = std::move(utf8), gen]() mutable {
                worker->parseReset(std::move(b), gen);
            },
            Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(worker,
            [worker, b = std::move(utf8), gen]() mutable {
                worker->parseSnapshot(std::move(b), gen);
            },
            Qt::QueuedConnection);
    }
}
}  // namespace

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
        ParseKind  nextKind = ParseKind::Incremental;
        {
            QMutexLocker lk(&d->mutex);
            if (gen != d->generation) {
                // This result is for a snapshot that has been superseded.
                delete parsed;
                parsed = nullptr;
            }
            if (!d->pending.isEmpty() || d->pendingGen != 0) {
                nextSnapshot = std::move(d->pending);
                nextGen      = d->pendingGen;
                nextKind     = d->pendingKind;
                d->pending.clear();
                d->pendingGen  = 0;
                d->pendingKind = ParseKind::Incremental;
                // worker stays busy
            } else {
                d->workerBusy = false;
            }
        }
        if (parsed) Q_EMIT parseReady(static_cast<const Markoff::Document *>(parsed));
        if (nextGen != 0)
            dispatch(d->worker, std::move(nextSnapshot), nextGen, nextKind);
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
    quint64   gen;
    ParseKind kind = ParseKind::Incremental;
    bool      dispatchNow = false;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        if (d->workerBusy) {
            // A parse is in flight. Replace pending utf8 (drops any prior
            // pending — that's the coalesce). Reset kind sticks: an
            // incremental schedule does NOT downgrade a pending Reset; the
            // reset must still be honored.
            d->pending    = std::move(utf8);
            d->pendingGen = gen;
            // pendingKind unchanged (Reset stays Reset; Incremental stays
            // Incremental).
        } else {
            d->workerBusy = true;
            dispatchNow   = true;
        }
    }
    if (dispatchNow) {
        dispatch(d->worker, std::move(utf8), gen, kind);
    }
}

void ParsePool::scheduleReset(QByteArray utf8)
{
    quint64 gen;
    bool    dispatchNow = false;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        if (d->workerBusy) {
            // Reset always wins over a pending incremental update.
            d->pending     = std::move(utf8);
            d->pendingGen  = gen;
            d->pendingKind = ParseKind::Reset;
        } else {
            d->workerBusy = true;
            dispatchNow   = true;
        }
    }
    if (dispatchNow) {
        dispatch(d->worker, std::move(utf8), gen, ParseKind::Reset);
    }
}

bool ParsePool::isPending() const
{
    QMutexLocker lk(&d->mutex);
    return d->workerBusy || !d->pending.isEmpty();
}

}  // namespace Markoff::Parse::Detail
