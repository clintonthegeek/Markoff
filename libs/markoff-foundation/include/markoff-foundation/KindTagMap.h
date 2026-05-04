// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/CausalLwwMap.h>
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>

namespace Markoff {
using KindTagMap = CausalLwwMap<BlockId, BlockKind>;
}
