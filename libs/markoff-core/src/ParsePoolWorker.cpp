// SPDX-License-Identifier: GPL-3.0-or-later
#include "ParsePoolWorker.h"

#include <markoff-parser/Document.h>

namespace Markoff {

void ParsePoolWorker::parseSnapshot(MarkoffDocument *sender,
                                    QString snapshot,
                                    quint64 generation)
{
    // Runs on the worker thread. Document::fromMarkdown() is the
    // MarkoffParser entry point: takes a QString, returns unique_ptr<Document>.
    // We release ownership to the raw pointer; ParsePool either forwards
    // (caller owns) or deletes (stale) the result.
    std::unique_ptr<Document> doc = Document::fromMarkdown(snapshot);
    Document *raw = doc.release();
    emit parsed(sender, raw, generation);
}

} // namespace Markoff
