// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <crdt/Anchor.h>
#include <markoff-foundation/TextAnchor.h>

namespace Markoff::Detail {

/// CollabText::Crdt::Anchor → Markoff::TextAnchor.
TextAnchor toTextAnchor(const CollabText::Crdt::Anchor &a) noexcept;

/// Markoff::TextAnchor → CollabText::Crdt::Anchor.
CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept;

}  // namespace Markoff::Detail
