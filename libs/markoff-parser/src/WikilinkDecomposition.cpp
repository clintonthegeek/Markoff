// SPDX-License-Identifier: GPL-3.0-or-later
#include "WikilinkDecomposition.h"

namespace Markoff::Detail {

Markoff::LinkTarget decomposeWikilinkInner(QStringView inner)
{
    Markoff::LinkTarget t;
    if (inner.isEmpty()) return t;

    // Split on first '|' → ref + alias.
    QStringView ref = inner;
    const auto pipeIdx = inner.indexOf(QLatin1Char('|'));
    if (pipeIdx >= 0) {
        ref = inner.left(pipeIdx);
        t.alias = inner.mid(pipeIdx + 1).toString();
    }

    // Split ref on first '#' → page + anchor.
    QStringView page = ref;
    QStringView anchor;
    const auto hashIdx = ref.indexOf(QLatin1Char('#'));
    if (hashIdx >= 0) {
        page = ref.left(hashIdx);
        anchor = ref.mid(hashIdx + 1);
    }
    t.page = page.toString();

    // Anchor: '^prefix' → blockRef; otherwise → section.
    if (!anchor.isEmpty()) {
        if (anchor.startsWith(QLatin1Char('^')))
            t.blockRef = anchor.mid(1).toString();
        else
            t.section = anchor.toString();
    }
    return t;
}

}  // namespace Markoff::Detail
