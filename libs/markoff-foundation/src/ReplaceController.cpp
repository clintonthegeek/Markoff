// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/ReplaceController.h>

#include <algorithm>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/SearchEngine.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

namespace Markoff {

ReplaceController::ReplaceController(QObject *parent) : QObject(parent) {}
ReplaceController::~ReplaceController() = default;

std::optional<CollabText::Crdt::Operation>
ReplaceController::replaceCurrent(MarkoffDocument *doc, Session *sess,
                                   const QString &replacement)
{
    if (!doc || !sess) return std::nullopt;
    const Selection p = sess->primarySelection();
    const quint32 a = doc->resolveAnchor(p.anchor);
    const quint32 b = doc->resolveAnchor(p.active);
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
ReplaceController::replaceAll(MarkoffDocument *, Session *, const QString &)
{
    return {};   // filled in Task 42
}

}  // namespace Markoff
