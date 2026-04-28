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
/// Coalescing: only the most-recently-scheduled snapshot's result is
/// surfaced. Earlier results that complete after a newer schedule() call
/// are silently dropped (deleted) inside the pool.
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
