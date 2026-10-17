// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/DocumentStorageProvider.h>

#include <functional>

namespace Markoff::VNext {

enum class ActionPublicationState {
    committed,
    committedHistoryPending,
    absent,
    uncertain,
    rejected,
};

struct ActionPublicationResult {
    ActionPublicationState state = ActionPublicationState::uncertain;
    std::optional<AcceptedRevision> committedRevision;
    std::optional<CollabText::Error> diagnostic;
};

/// Validate the provider's revision annotations on a complete accepted prefix.
/// Tokens are compared only for equality; each record must chain from the
/// previous committed prefix and the final token must equal the snapshot head.
[[nodiscard]] CollabText::Result<void> validateAcceptedPrefixRevisions(
    const StorageRecoveryState& recovery);

/// Owns the durable prepare/append/inspect/private-history boundary for one
/// already-staged MAB action. Absent transactions remain blocked until their
/// reservations have durable terminal-gap coverage.
class ActionPublicationCoordinator final {
public:
    explicit ActionPublicationCoordinator(DocumentStorageSession& session);

    [[nodiscard]] ActionPublicationResult publish(
        const PreparedStorageTransaction& transaction);
    /// Publish one durable MTG/1 or MMG/1 reservation-coverage event. This
    /// remains available while ordinary actions are blocked.
    [[nodiscard]] ActionPublicationResult publishGap(
        const PreparedStorageTransaction& transaction);
    /// Reconcile the exact prepared transaction after reopen or uncertainty.
    [[nodiscard]] ActionPublicationResult recover(
        const PreparedStorageTransaction& transaction);
    /// Reconcile an absent action, then require the host to replace tentative
    /// projections and replay this complete accepted prefix before gap writes.
    [[nodiscard]] ActionPublicationResult recoverAndRebuild(
        const PreparedStorageTransaction& transaction,
        const std::function<CollabText::Result<void>(const StorageRecoveryState&)>& rebuild);
    [[nodiscard]] bool blocked() const noexcept { return m_blocked; }

private:
    [[nodiscard]] ActionPublicationResult finishCommitted(
        const PreparedStorageTransaction& transaction,
        const AcceptedRevision& revision);
    [[nodiscard]] ActionPublicationResult finishGap(
        const PreparedStorageTransaction& transaction,
        const AcceptedRevision& revision);
    [[nodiscard]] CollabText::Result<void> validate(
        const PreparedStorageTransaction& transaction) const;
    [[nodiscard]] CollabText::Result<void> validateGap(
        const PreparedStorageTransaction& transaction) const;

    DocumentStorageSession* m_session;
    bool m_blocked = false;
    bool m_absenceNeedsRebuild = false;
    bool m_absenceRebuilt = false;
};

} // namespace Markoff::VNext
