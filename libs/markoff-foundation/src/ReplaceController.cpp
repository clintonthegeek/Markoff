// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/ReplaceController.h>

#include <algorithm>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/SearchEngine.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/UndoLog.h>

namespace Markoff {

ReplaceController::ReplaceController(QObject *parent) : QObject(parent) {}
ReplaceController::~ReplaceController() = default;

std::optional<CollabText::Crdt::Operation>
ReplaceController::replaceCurrent(MarkoffDocument *doc, Session *sess,
                                   const QString &replacement)
{
    if (!doc || !sess) return std::nullopt;
    const Selection p = sess->primarySelection();
    const quint32 a = doc->resolveTextAnchor(p.anchor);
    const quint32 b = doc->resolveTextAnchor(p.active);
    if (a == b) return std::nullopt;

    MarkoffEdit r;
    r.oldStart = std::min(a, b);
    r.oldEnd   = std::max(a, b);
    r.newText  = replacement.toUtf8();
    const auto op = doc->applyLocalEdit({ r });

    SearchEngine().findNext(doc, sess);
    return op;
}

ReplaceController::ReplaceAllResult
ReplaceController::replaceAll(MarkoffDocument *doc, Session *sess,
                               const QString &replacement)
{
    ReplaceAllResult res;
    if (!doc || !sess) return res;

    QList<MarkoffEdit> edits;
    for (const Selection &x : sess->secondarySelections()) {
        if (x.kind != Selection::Kind::SearchMatch) continue;
        const quint32 a = doc->resolveTextAnchor(x.anchor);
        const quint32 b = doc->resolveTextAnchor(x.active);
        MarkoffEdit r;
        r.oldStart = std::min(a, b);
        r.oldEnd   = std::max(a, b);
        r.newText  = replacement.toUtf8();
        edits << r;
    }
    if (edits.isEmpty()) return res;
    std::sort(edits.begin(), edits.end(),
              [](const MarkoffEdit &a, const MarkoffEdit &b) {
                  return a.oldStart < b.oldStart;
              });
    res.op = doc->applyLocalEdit(edits);
    res.count = edits.size();
    SearchEngine().clearMatches(sess);
    return res;
}

// D2: single-block replacement using d2ApplyBufferEdit.
bool ReplaceController::replaceInBlock(MarkoffDocument &doc,
                                        BlockId blockId,
                                        uint32_t byteStart,
                                        uint32_t byteLen,
                                        const QByteArray &replacement)
{
    if (blockId.isNull()) return false;
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(blockId, byteStart, byteLen, replacement, t);
    return true;
}

}  // namespace Markoff
