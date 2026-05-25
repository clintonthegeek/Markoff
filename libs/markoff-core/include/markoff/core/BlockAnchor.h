// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockId.h>
#include <QList>
#include <QMetaType>

namespace Markoff {
// Compatibility alias — new code should use BlockId directly.
using BlockAnchor = BlockId;
}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::BlockId)
Q_DECLARE_METATYPE(QList<Markoff::BlockId>)
