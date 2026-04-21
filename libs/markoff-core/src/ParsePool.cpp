// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/ParsePool.h>
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

#include <QThread>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace Markoff {

struct ParsePool::Private {
    QThread *thread = nullptr;
    ParsePoolWorker *worker = nullptr;
    QMutex mutex;
    QHash<MarkoffDocument *, quint64> generations;
};

ParsePool::ParsePool(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->thread = new QThread(this);
    d->worker = new ParsePoolWorker();
    d->worker->moveToThread(d->thread);

    // Worker is owned by the thread; delete it when the thread finishes.
    connect(d->thread, &QThread::finished, d->worker, &QObject::deleteLater);

    // Results come back via QueuedConnection so the lambda runs on our thread.
    connect(d->worker, &ParsePoolWorker::parsed,
            this, [this](MarkoffDocument *sender, Document *parsed, quint64 gen) {
        {
            QMutexLocker lk(&d->mutex);
            const auto it = d->generations.constFind(sender);
            if (it == d->generations.constEnd() || it.value() != gen) {
                // Superseded or cancelled — drop the result.
                delete parsed;
                return;
            }
        }
        emit jobCompleted(sender, parsed);
    }, Qt::QueuedConnection);

    d->thread->start();
}

ParsePool::~ParsePool()
{
    d->thread->quit();
    d->thread->wait();
    // Worker is deleteLater'd on thread::finished (see constructor connect).
    // Any QueuedConnection-delivered lambdas from worker::parsed still in the
    // main-thread event queue are safe: ~QObject (runs after this destructor body)
    // disconnects all incoming queued connections before the next event-loop
    // iteration can deliver them.
}

void ParsePool::postJob(MarkoffDocument *sender, QString snapshot)
{
    quint64 gen;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generations[sender];
    }
    ParsePoolWorker *worker = d->worker;
    QMetaObject::invokeMethod(worker,
        [worker, sender, snap = std::move(snapshot), gen]() mutable {
            worker->parseSnapshot(sender, std::move(snap), gen);
        },
        Qt::QueuedConnection);
}

void ParsePool::cancelJobsFor(MarkoffDocument *sender)
{
    QMutexLocker lk(&d->mutex);
    d->generations.remove(sender);
}

} // namespace Markoff
