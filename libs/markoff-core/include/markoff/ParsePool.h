// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;
class Document;  // markoff-parser

/// Single-worker-thread async parse queue.
///
/// Callers post markdown snapshots via postJob(). The pool parses each
/// snapshot on a dedicated worker thread using Document::fromMarkdown()
/// and marshals results back via jobCompleted() on the pool's thread.
///
/// A per-sender generation counter allows newer posts to supersede older
/// pending runs: when a job completes, the pool checks whether the
/// generation still matches; stale results (superseded or cancelled) are
/// silently dropped and their Document* is deleted.
///
/// MarkoffDocument dtor MUST call cancelJobsFor(this) before the object
/// is destroyed to prevent use-after-free: if a job completes after the
/// sender is gone and cancelJobsFor was not called, jobCompleted would be
/// emitted with a dangling sender pointer.
class MARKOFF_CORE_EXPORT ParsePool : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ParsePool)
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    /// Post a parse job for sender. Any prior pending job for the same sender
    /// is superseded; only the latest snapshot will produce a jobCompleted.
    void postJob(MarkoffDocument *sender, QString snapshot);

    /// Remove sender from the generation map. Any in-flight or pending parse
    /// for this sender will be silently dropped on completion.
    ///
    /// MarkoffDocument dtor MUST call this to prevent use-after-free on
    /// jobCompleted.
    void cancelJobsFor(MarkoffDocument *sender);

Q_SIGNALS:
    /// Emitted on the pool's thread (i.e. the thread that owns ParsePool)
    /// when a non-superseded parse completes. The caller takes ownership of
    /// \a parsed; it is never null.
    void jobCompleted(Markoff::MarkoffDocument *sender, Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Markoff
