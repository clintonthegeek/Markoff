// SPDX-License-Identifier: GPL-3.0-or-later
#include "Helpers.h"

#include <algorithm>

#include <markoff-foundation/Cmd.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

namespace Markoff::Cmd::Detail {

std::pair<quint32, quint32>
selectionByteRange(const MarkoffDocument &doc, const Selection &sel)
{
    const auto a = doc.resolveTextAnchor(sel.anchor);
    const auto b = doc.resolveTextAnchor(sel.active);
    return a <= b ? std::pair{a, b} : std::pair{b, a};
}

}  // namespace Markoff::Cmd::Detail

namespace Markoff::Cmd {

void applyToAllPrimaryAndSecondaries(MarkoffDocument &doc, Session &session,
                                      const EditsFn &fn)
{
    if (!fn) return;
    QList<MarkoffEdit> all;
    all << fn(doc, session.primarySelection());
    for (const Selection &s : session.secondarySelections()) {
        if (s.kind != Selection::Kind::Secondary) continue;
        all << fn(doc, s);
    }
    if (all.isEmpty()) return;
    // The edits per-selection are produced in OLD-text byte coordinates
    // independently. Sort by oldStart ascending to satisfy applyLocalEdit's
    // ordering precondition.
    std::sort(all.begin(), all.end(),
              [](const MarkoffEdit &a, const MarkoffEdit &b) {
                  return a.oldStart < b.oldStart;
              });
    doc.applyLocalEdit(all);
}

}  // namespace Markoff::Cmd
