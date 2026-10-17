// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/OwnerHistoryCodec.h>

#include <vector>

namespace Markoff::VNext {

struct OperationReservationBinding {
    size_t operationIndex = 0;
    std::vector<ScopedReservation> reservations;
};

/// Builds the storage transaction for one already-staged Markoff action,
/// binding shared action bytes, identity claims, reservation intervals,
/// accepted revision and owner-local history in one checked helper.
class ActionTransactionBuilder final {
public:
    [[nodiscard]] static CollabText::Result<PreparedStorageTransaction> build(
        DocumentStorageSession& session,
        const ActionBundleCodec::Bundle& bundle,
        const OwnerHistorySnapshot& ownerHistory,
        const std::vector<OperationReservationBinding>& operationReservations);
};

} // namespace Markoff::VNext
