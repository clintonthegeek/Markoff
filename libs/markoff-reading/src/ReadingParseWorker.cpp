// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingParseWorker.h"

#include "corbomite/readingview/ReadingPipeline.h"

#include <QMetaObject>
#include <QThread>

namespace Corbomite::ReadingView {

ReadingParseWorker::ReadingParseWorker(QObject *parent)
    : QObject(parent)
{
    // Register the section-vector type so queued connections can marshal it
    // through the meta-object system. Safe to call repeatedly; idempotent.
    qRegisterMetaType<QVector<std::shared_ptr<ReadingSection>>>(
        "QVector<std::shared_ptr<Corbomite::ReadingView::ReadingSection>>");
    qRegisterMetaType<QVector<std::shared_ptr<ReadingSection>>>(
        "QVector<std::shared_ptr<ReadingSection>>");
    qRegisterMetaType<quint64>("quint64");

    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("ReadingParseWorker"));
    m_thread->start();

    // Move *this* onto the worker thread so the QMetaObject::invokeMethod
    // QueuedConnection dispatches the slot on the worker thread. The thread
    // itself still lives on the parent thread — that is correct for
    // QThread.
    moveToThread(m_thread);

    // Create the worker-thread-resident pipeline on the worker thread using
    // a blocking queued invoke so construction is synchronous from the
    // caller's perspective and we don't race with the first parseAsync.
    QMetaObject::invokeMethod(
        this,
        [this] {
            // `this` is now affine to m_thread; the pipeline parented to
            // `this` inherits the same affinity.
            m_workerPipeline = new ReadingPipeline(this);
        },
        Qt::BlockingQueuedConnection);
}

ReadingParseWorker::~ReadingParseWorker()
{
    if (m_thread) {
        // Ensure the pipeline is destroyed on the worker thread before the
        // event loop exits (otherwise QObject affinity assertions can fire
        // in debug Qt builds when ~QObject runs on the main thread).
        QMetaObject::invokeMethod(
            this,
            [this] {
                delete m_workerPipeline;
                m_workerPipeline = nullptr;
            },
            Qt::BlockingQueuedConnection);

        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

quint64 ReadingParseWorker::bumpRequestId()
{
    // Post-increment so the first call returns 1, not 0 — callers can use
    // 0 as a sentinel "no request in flight".
    return m_latestRequestId.fetchAndAddOrdered(1) + 1;
}

void ReadingParseWorker::parseAsync(const QString &markdown, quint64 requestId)
{
    // Take our own deep copy to insulate the worker from caller-side
    // modification. QString is implicitly shared; `.detach()` forces a
    // real copy so the worker's view is stable.
    QString md = markdown;
    md.detach();

    QMetaObject::invokeMethod(
        this,
        "runParse",
        Qt::QueuedConnection,
        Q_ARG(QString, md),
        Q_ARG(quint64, requestId));
}

void ReadingParseWorker::runParse(QString markdown, quint64 requestId)
{
    // Coalescing: if a newer parseAsync has been posted while this one was
    // queued, drop our result before doing work — the newer request is
    // what the user cares about.
    if (requestId != m_latestRequestId.loadAcquire())
        return;

    if (!m_workerPipeline) {
        // Defensive — ctor should have installed this. Bail cleanly.
        return;
    }

    QVector<std::shared_ptr<ReadingSection>> sections =
        m_workerPipeline->splitIntoSections(markdown);

    // Second coalescing check: while we were parsing a newer request may
    // have arrived. If so, drop this emission.
    if (requestId != m_latestRequestId.loadAcquire())
        return;

    Q_EMIT parseFinished(requestId, sections);
}

QVector<std::shared_ptr<ReadingSection>>
ReadingParseWorker::parseSync(const QString &markdown)
{
    // Fresh pipeline on the caller's thread — avoids cross-thread QObject
    // affinity issues with `m_workerPipeline`. ReadingPipeline is stateless
    // aside from its QObject parent, so construction cost is negligible.
    ReadingPipeline pipeline;
    return pipeline.splitIntoSections(markdown);
}

} // namespace Corbomite::ReadingView
