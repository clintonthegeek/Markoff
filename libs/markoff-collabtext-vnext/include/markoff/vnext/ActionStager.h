// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ActionTransactionBuilder.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/ScopedIdListStream.h>
#include <markoff/vnext/ScopedTextStream.h>

namespace Markoff::VNext {

/// Converts typed local stream results into one MAB action and preserves which
/// durable reservation ranges belong to each emitted operation.
class ActionStager final {
public:
    ActionStager(std::string document, uint64_t epoch,
                 uint64_t deliverySequence, QByteArray actionId,
                 ActionKind kind);

    [[nodiscard]] CollabText::Result<void> add(const LocalListOperation& operation);
    [[nodiscard]] CollabText::Result<void> add(const LocalOperation& operation);
    [[nodiscard]] CollabText::Result<void> add(const LocalMapOperation& operation);

    void setSourceSnapshot(QByteArray snapshot);
    [[nodiscard]] const ActionBundleCodec::Bundle& bundle() const noexcept
    { return m_bundle; }
    [[nodiscard]] const std::vector<OperationReservationBinding>&
    operationReservations() const noexcept { return m_operationReservations; }

private:
    [[nodiscard]] CollabText::Result<void> append(
        ActionOperation operation,
        std::vector<ScopedReservation> reservations);

    ActionBundleCodec::Bundle m_bundle;
    std::vector<OperationReservationBinding> m_operationReservations;
};

} // namespace Markoff::VNext
