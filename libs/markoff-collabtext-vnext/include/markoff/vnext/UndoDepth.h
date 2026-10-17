// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace Markoff::VNext {

/// Undoable `edit` and `replacement` actions kept for one actor. The action
/// that would make the set larger records retirement of the oldest one.
inline constexpr std::size_t kActorUndoDepth = 100;

/// Core's own group ceiling. Markoff does not use it as the undo policy.
/// One action may contain more groups than `kActorUndoDepth`.
inline constexpr std::size_t kEngineUndoGroupCeiling = 100000;

} // namespace Markoff::VNext
