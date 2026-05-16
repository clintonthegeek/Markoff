// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindDispatch.h"

namespace Markoff::Live {

QString delegateClassFor(const QString &kind)
{
    // FALSIFIABILITY PROOF — REVERTS NEXT.
    // Each kind in its own bucket means the diff treats kind changes as
    // class changes, i.e. Delete+Insert. The §6.1 invariant test must
    // FAIL under this stub (TextEdit pointer changes again).
    return kind.isEmpty() ? QStringLiteral("text-inline") : kind;
}

}  // namespace Markoff::Live
