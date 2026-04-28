// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

#include <memory>

namespace Markoff::Foundation {

void ParsePoolWorker::parseSnapshot(QByteArray utf8, quint64 generation)
{
    // Runs on the worker thread. Document::fromMarkdown() takes a QString
    // and returns unique_ptr<Markoff::Document>. We release ownership to
    // the raw pointer; ParsePool either forwards it (caller owns) or
    // deletes it (stale).
    std::unique_ptr<Markoff::Document> doc =
        Markoff::Document::fromMarkdown(QString::fromUtf8(utf8));
    Q_EMIT parsed(doc.release(), generation);
}

}  // namespace Markoff::Foundation
