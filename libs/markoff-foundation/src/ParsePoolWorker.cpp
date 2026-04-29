// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

#include <memory>

namespace Markoff::Parse::Detail {

void ParsePoolWorker::parseSnapshot(QByteArray utf8, quint64 generation)
{
    // Runs on the worker thread. Apply an incremental edit against the
    // session's prior tree (the session falls back to a full parse when
    // it has no prior state), then snapshot a fresh Document.
    m_session.applyEdit(QString::fromUtf8(utf8));
    std::unique_ptr<Markoff::Document> doc = m_session.snapshot();
    Q_EMIT parsed(doc.release(), generation);
}

void ParsePoolWorker::parseReset(QByteArray utf8, quint64 generation)
{
    m_session.reset(QString::fromUtf8(utf8));
    std::unique_ptr<Markoff::Document> doc = m_session.snapshot();
    Q_EMIT parsed(doc.release(), generation);
}

}  // namespace Markoff::Parse::Detail
