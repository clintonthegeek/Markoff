// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>

#include <memory>

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::Parse::Detail {

/// Background Markdown parser, single-owner: each MarkoffDocument holds one
/// ParsePool by value. Snapshots are scheduled via schedule(); the worker
/// thread parses them and emits parseReady() on the pool's thread. The
/// receiver of parseReady() takes ownership of the parsed Document and is
/// responsible for deleting it.
///
/// Coalescing: requests are coalesced at the queue. At most one parse runs
/// on the worker at a time; subsequent schedule() calls during an in-flight
/// parse only update the "pending" snapshot, which is dispatched when the
/// current parse finishes. Stale results (whose generation has been
/// superseded by a pending snapshot) are dropped silently inside the pool.
class ParsePool : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ParsePool)
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    void schedule(QByteArray utf8);   ///< coalesces; runs on worker thread
    bool isPending() const;

Q_SIGNALS:
    void parseReady(const Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Parse::Detail
