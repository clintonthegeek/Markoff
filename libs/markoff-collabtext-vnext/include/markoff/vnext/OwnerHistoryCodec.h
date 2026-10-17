// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/CrossStreamHistoryCodec.h>
#include <markoff/vnext/LocalHistoryCodec.h>

namespace Markoff::VNext {

/// Complete intended owner-private undo state for one prepared action. The
/// provider binds transactionId and acceptedPrior to its committed revision.
struct OwnerHistorySnapshot {
    std::string document;
    uint64_t epoch = 0;
    CollabText::ActorId owner;
    QByteArray transactionId;
    QByteArray acceptedPrior;
    std::vector<LocalHistoryRecord> streams;
    CrossStreamHistoryIndex actionIndex;
};

class OwnerHistoryCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const OwnerHistorySnapshot& snapshot);
    [[nodiscard]] static CollabText::Result<OwnerHistorySnapshot> decode(
        const QByteArray& bytes, const std::string& expectedDocument,
        uint64_t expectedEpoch, const CollabText::ActorId& expectedOwner,
        const QByteArray& expectedTransactionId,
        const QByteArray& expectedAcceptedPrior);
};

} // namespace Markoff::VNext
