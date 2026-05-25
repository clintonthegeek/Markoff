// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CausalLwwMap.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>

namespace Markoff {
using KindTagMap = CausalLwwMap<BlockId, BlockKind>;
}  // namespace Markoff
