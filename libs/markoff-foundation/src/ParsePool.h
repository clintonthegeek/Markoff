// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>

#include <memory>

#include <markoff-foundation/RenderPhases.h>

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::Parse::Detail {

/// Background Markdown parser, single-owner: each MarkoffDocument holds one
/// ParsePool by value. Snapshots are scheduled via schedule() (incremental
/// path) or scheduleReset() (full reparse, drops session state). The worker
/// thread parses them and emits parseReady() on the pool's thread. The
/// receiver of parseReady() takes ownership of the parsed Document and is
/// responsible for deleting it.
///
/// Coalescing: requests are coalesced at the queue. At most one parse runs
/// on the worker at a time; subsequent schedule() / scheduleReset() calls
/// during an in-flight parse only update the "pending" snapshot, which is
/// dispatched when the current parse finishes. Stale results (whose
/// generation has been superseded by a pending snapshot) are dropped
/// silently inside the pool.
///
/// Kind precedence: a pending Reset always wins over a pending incremental
/// update (since Reset must be honored to load the new buffer state). A
/// later incremental update can replace an earlier pending Reset *only* if
/// the underlying intent is the same — currently we treat them as distinct
/// streams: once a Reset is pending, subsequent schedule() calls upgrade
/// the pending utf8 but keep the Reset kind.
class ParsePool : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ParsePool)
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    /// Incremental-reparse request. Worker uses session's prior tree.
    void schedule(QByteArray utf8);

    /// Full-reset request: drops session state on the worker, then full
    /// parses `utf8`. Use for resetContent (file load, reload, revert).
    void scheduleReset(QByteArray utf8);

    bool isPending() const;

    /// Bench-only opt-in tap installation. When `taps` is non-null, the worker
    /// writes T_workerEntry / T_workerEmit on each parse, and the main-thread
    /// receiver lambda writes T_mainSlotEntry / T_modelDone. Production
    /// callers leave this null and pay zero. Caller owns the taps and is
    /// responsible for thread-safe lifetime (must outlive any parse that
    /// completes while installed).
    void setRenderPhaseTaps(Markoff::Render::RenderPhaseTaps *taps) noexcept;

Q_SIGNALS:
    void parseReady(const Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Parse::Detail
