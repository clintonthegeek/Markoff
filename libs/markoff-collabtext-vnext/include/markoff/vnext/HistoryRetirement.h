// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/OwnerHistoryCodec.h>

namespace Markoff::VNext {

class AcceptedPrefixProjection;

/// Move oldest undoable actions out of the actor window and record those
/// groups on `history`'s committing action. Refuses without publishing when
/// the retirement record cannot be attached. Does not run for an empty
/// rollback snapshot.
[[nodiscard]] CollabText::Result<void> enforceActorUndoDepth(
    AcceptedPrefixProjection& projection, OwnerHistorySnapshot& history);

/// Drop pending engine retirement entries that this durable index already
/// names. Acknowledgement happens only after that index is committed.
[[nodiscard]] CollabText::Result<void> acknowledgeDurableRetirements(
    AcceptedPrefixProjection& projection, const OwnerHistorySnapshot& history);

} // namespace Markoff::VNext
