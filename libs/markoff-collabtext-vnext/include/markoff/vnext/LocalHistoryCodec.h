// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/Anchor.h>
#include <collabtext/ActorId.h>
#include <collabtext/LocalHistory.h>

#include <QByteArray>

namespace Markoff::VNext {

struct LocalHistoryRecord {
    CollabText::PositionScope scope;
    CollabText::ActorId actor;
    CollabText::Crdt::LocalHistorySnapshot snapshot;
};

/// Versioned, bounded, layout-independent codec for actor-local engine undo
/// metadata. The host stores this in private device state and restores it only
/// after replaying the matching shared operation prefix.
class LocalHistoryCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const LocalHistoryRecord& record);
    [[nodiscard]] static CollabText::Result<LocalHistoryRecord> decode(
        const QByteArray& bytes, const CollabText::PositionScope& expectedScope,
        const CollabText::ActorId& expectedActor);
};

} // namespace Markoff::VNext
