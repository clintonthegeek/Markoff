// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <crdt/Anchor.h>
#include <markoff/core/TextAnchor.h>
#include <markoff/core/BlockId.h>

namespace Markoff::Detail {

/// CollabText::Crdt::Anchor + BlockId → Markoff::TextAnchor.
TextAnchor toTextAnchor(BlockId blockId, const CollabText::Crdt::Anchor &a) noexcept;

/// Markoff::TextAnchor → CollabText::Crdt::Anchor (drops BlockId).
CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept;

}  // namespace Markoff::Detail
