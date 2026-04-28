// SPDX-License-Identifier: GPL-3.0-or-later
#include "Helpers.h"

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>

namespace Markoff::Cmd::Detail {

std::pair<quint32, quint32>
selectionByteRange(const MarkoffDocument &doc, const Selection &sel)
{
    const auto a = doc.resolveAnchor(sel.anchor);
    const auto b = doc.resolveAnchor(sel.active);
    return a <= b ? std::pair{a, b} : std::pair{b, a};
}

}  // namespace Markoff::Cmd::Detail
