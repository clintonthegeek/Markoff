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

namespace Markoff::Foundation {

struct ParsePool::Private {
    QThread          *thread = nullptr;
    ParsePoolWorker  *worker = nullptr;
    mutable QMutex    mutex;
    quint64           generation = 0;  // bumped on each schedule()
    quint64           inFlight   = 0;  // last-scheduled gen; 0 == resolved
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
        {
            QMutexLocker lk(&d->mutex);
            if (gen != d->generation) {
                // Superseded — drop the result.
                delete parsed;
                return;
            }
            // Most-recent generation has now resolved.
            d->inFlight = 0;
        }
        Q_EMIT parseReady(static_cast<const Markoff::Document *>(parsed));
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
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        d->inFlight = gen;
    }
    ParsePoolWorker *worker = d->worker;
    QMetaObject::invokeMethod(worker,
        [worker, b = std::move(utf8), gen]() mutable {
            worker->parseSnapshot(std::move(b), gen);
        },
        Qt::QueuedConnection);
}

bool ParsePool::isPending() const
{
    QMutexLocker lk(&d->mutex);
    return d->inFlight > 0 && d->inFlight == d->generation;
}

}  // namespace Markoff::Foundation
