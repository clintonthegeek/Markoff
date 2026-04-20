// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Phase 5 — off-main-thread parse driver. Notes ≥ kAsyncParseThresholdBytes
// (10240 bytes) are parsed on this worker; smaller notes can call `parseSync`
// on the main thread. The worker coalesces rapid requests: a new parseAsync
// bumps `m_latestRequestId`, and the worker slot drops results whose
// requestId no longer matches — avoids flicker when the user types rapidly.

#ifndef CORBOMITE_READINGVIEW_READINGPARSEWORKER_H
#define CORBOMITE_READINGVIEW_READINGPARSEWORKER_H

#include "corbomite/readingview/ReadingSection.h"

#include <QAtomicInteger>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>

class QThread;

namespace Corbomite::ReadingView {

class ReadingPipeline;

/// Async parse driver. Owns a single QThread that hosts a ReadingPipeline
/// instance and processes queued `parseAsync` requests off the main thread.
/// Synchronous callers should prefer `parseSync`, which constructs a fresh
/// pipeline on the calling thread — the worker's own pipeline is reserved
/// for its thread to avoid cross-thread QObject affinity issues.
class ReadingParseWorker : public QObject
{
    Q_OBJECT

public:
    explicit ReadingParseWorker(QObject *parent = nullptr);
    ~ReadingParseWorker() override;

    /// Async entry — fire-and-forget. Results arrive via `parseFinished`.
    /// The worker's slot deep-copies the markdown into its thread context
    /// (QString is implicitly shared but the worker takes its own copy to
    /// insulate against caller-side modification between post + dispatch).
    ///
    /// Coalescing: every call bumps `m_latestRequestId`. The worker slot
    /// checks `requestId == m_latestRequestId` before emitting `parseFinished`
    /// — stale results are dropped so rapid typing produces at most one
    /// finished signal per keystroke burst.
    void parseAsync(const QString &markdown, quint64 requestId);

    /// Allocate the next coalescing request id. Thread-safe — callers should
    /// pair this with `parseAsync` so the worker can drop stale results.
    quint64 bumpRequestId();

    /// Synchronous parse for sub-threshold notes. Runs on the caller's
    /// thread with a fresh ReadingPipeline so no cross-thread affinity is
    /// introduced. Safe to invoke from the UI thread.
    QVector<std::shared_ptr<ReadingSection>> parseSync(const QString &markdown);

Q_SIGNALS:
    void parseFinished(quint64 requestId,
                       QVector<std::shared_ptr<ReadingSection>> sections);

private:
    Q_INVOKABLE void runParse(QString markdown, quint64 requestId);

    QThread *m_thread = nullptr;
    // Pipeline lives on m_thread. Constructed in ctor via invokeMethod so
    // its QObject affinity matches the worker thread.
    ReadingPipeline *m_workerPipeline = nullptr;
    QAtomicInteger<quint64> m_latestRequestId{0};
};

} // namespace Corbomite::ReadingView

Q_DECLARE_METATYPE(QVector<std::shared_ptr<Corbomite::ReadingView::ReadingSection>>)

#endif // CORBOMITE_READINGVIEW_READINGPARSEWORKER_H
