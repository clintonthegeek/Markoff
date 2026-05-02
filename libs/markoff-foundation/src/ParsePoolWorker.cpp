// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

#include <memory>

namespace Markoff::Parse::Detail {

void ParsePoolWorker::parseSnapshot(QByteArray utf8, quint64 generation, quint64 inputEditSeq)
{
    // Runs on the worker thread. Apply an incremental edit against the
    // session's prior tree (the session falls back to a full parse when
    // it has no prior state), then snapshot a fresh Document.
    auto *taps = m_taps.load(std::memory_order_acquire);
    if (taps) taps->tWorkerEntryNs.store(Markoff::Render::nowNs(),
                                         std::memory_order_release);
    m_session.applyEdit(QString::fromUtf8(utf8));
    std::unique_ptr<Markoff::Document> doc = m_session.snapshot();
    if (taps) taps->tWorkerEmitNs.store(Markoff::Render::nowNs(),
                                        std::memory_order_release);
    Q_EMIT parsed(doc.release(), generation, inputEditSeq);
}

void ParsePoolWorker::parseReset(QByteArray utf8, quint64 generation, quint64 inputEditSeq)
{
    auto *taps = m_taps.load(std::memory_order_acquire);
    if (taps) taps->tWorkerEntryNs.store(Markoff::Render::nowNs(),
                                         std::memory_order_release);
    m_session.reset(QString::fromUtf8(utf8));
    std::unique_ptr<Markoff::Document> doc = m_session.snapshot();
    if (taps) taps->tWorkerEmitNs.store(Markoff::Render::nowNs(),
                                        std::memory_order_release);
    Q_EMIT parsed(doc.release(), generation, inputEditSeq);
}

}  // namespace Markoff::Parse::Detail
