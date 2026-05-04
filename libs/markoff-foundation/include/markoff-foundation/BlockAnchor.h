// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockId.h>
#include <QMetaType>

namespace Markoff {
// Compatibility alias — new code should use BlockId directly.
using BlockAnchor = BlockId;
}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::BlockId)
