// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/AcceptedPrefixProjection.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>
#include <markoff/vnext/ActionStager.h>

#include <thread>
#include <functional>
#include <optional>

namespace Markoff::VNext {

enum class ProviderDocumentState {
    ready,
    awaitingInitialImport,
    initialImportAuthorized,
    recoveryRequired,
};

/// Owns the provider session and accepted projection for one document scope.
/// Construction is fallible and replay completes before the projection is
/// exposed. This is the lifecycle boundary used by the production document
/// port; it does not create a numeric or ephemeral fallback on open failure.
/// Every access, including read-only accessors and use of the returned storage
/// reference, is confined to the thread that opens the object. Lifecycle
/// mutations additionally reject off-thread and reentrant calls with typed
/// errors. Returned references are borrowed and must not outlive this owner.
class ProviderBackedDocumentState final {
public:
    using LocalActionCommittedFn = std::function<void(const QByteArray&, const QByteArray&)>;
    using StateChangedFn = std::function<void()>;
    [[nodiscard]] static CollabText::Result<std::unique_ptr<ProviderBackedDocumentState>>
    open(DocumentStorageProvider& provider, const DocumentStorageScope& scope,
         StorageMode mode);

    [[nodiscard]] ProviderDocumentState state() const noexcept { return m_state; }
    [[nodiscard]] bool canStartOrdinaryAction() const noexcept
    { return m_state == ProviderDocumentState::ready; }
    /// Called after local MAB bytes are durably accepted and the projection refreshes.
    void setOnLocalActionCommitted(LocalActionCommittedFn callback)
    { m_onLocalActionCommitted = std::move(callback); }
    /// Called synchronously whenever the public lifecycle state changes.
    void setOnStateChanged(StateChangedFn callback)
    { m_onStateChanged = std::move(callback); }
    /// Owner-thread-only borrowed session; invalidated with this lifecycle owner.
    [[nodiscard]] DocumentStorageSession& storage() noexcept { return *m_storage; }
    [[nodiscard]] const DocumentStorageSession& storage() const noexcept { return *m_storage; }
    [[nodiscard]] const StorageRecoveryState& recovery() const noexcept { return m_recovery; }
    [[nodiscard]] const AcceptedPrefixProjection& projection() const noexcept
    { return *m_projection; }

    /// Monotonic counter bumped whenever the visible block set may change:
    /// any committed action carrying an idList operation, every remote
    /// acceptance, and every recovery rebuild. Pure text/map actions leave it
    /// alone. Consumers may cache the visible block set keyed on this value.
    [[nodiscard]] uint64_t visibilityGeneration() const noexcept
    { return m_visibilityGeneration; }

    /// Next delivery sequence for a locally staged MAB:
    /// max(accepted MAB/3 deliverySequence) + 1 over the accepted prefix.
    /// Cached against the accepted-record count — a local commit advances the
    /// cache in memory, and any other prefix growth (remote acceptance,
    /// recovery rebuild) falls back to one rescan over the accepted records.
    [[nodiscard]] CollabText::Result<uint64_t> nextDeliverySequence();

    /// Borrow the owner-held tentative stream for one local action. These
    /// handles are owner-thread-only and must not be used to begin another
    /// action before the current one is resolved. Publication/recovery refreshes
    /// existing stream objects in place; every pointer expires with this owner.
    [[nodiscard]] CollabText::Result<ScopedIdListStream*> stagingIdList();
    [[nodiscard]] CollabText::Result<ScopedTextStream*> stagingTextStream(
        const BlockIdentity& block);
    [[nodiscard]] CollabText::Result<ReplicatedMapStream*> stagingMapStream(
        const MapScope& scope);

    /// Persist the exact user-authorized initial import before any operation
    /// identity can be reserved. The provider enforces idempotence and scope.
    [[nodiscard]] CollabText::Result<void> authorizeInitialImport(
        const PendingImportIntent& intent);

    /// Publish one staged MAB through the coordinator that recovered this
    /// document, then refresh the accepted projection and readiness. Only an
    /// authorized initial import or a ready document may publish an action.
    [[nodiscard]] ActionPublicationResult publishAction(
        const PreparedStorageTransaction& transaction);
    /// Accept and replay one complete, Markoff-validated remote MAB before the
    /// caller emits its transport receipt. Local owner history is unchanged.
    [[nodiscard]] ActionPublicationResult acceptRemoteAction(
        const PreparedStorageTransaction& transaction);
    /// Build the exact prepared transaction from staged operations and their
    /// intended owner history, then publish it through this same lifecycle
    /// coordinator. A construction refusal also rebuilds the accepted view.
    [[nodiscard]] ActionPublicationResult publishStagedAction(
        const ActionStager& stager, const OwnerHistorySnapshot& ownerHistory);

    /// Resolve an action whose publication outcome needs recovery. Proven
    /// absence rebuilds the accepted projection before any reserved gap can be
    /// published; a committed inspection refreshes the projection after owner
    /// history is finished. The owner retains the same coordinator throughout.
    [[nodiscard]] ActionPublicationResult recoverActionAndRebuild(
        const PreparedStorageTransaction& transaction);

    /// Publish one terminal coverage action through the same coordinator that
    /// recovered this document, then refresh the accepted projection/readiness.
    [[nodiscard]] ActionPublicationResult publishGap(
        const PreparedStorageTransaction& transaction);

private:
    void setState(ProviderDocumentState state);
    [[nodiscard]] CollabText::Result<void> restoreAcceptedProjection();
    [[nodiscard]] ActionPublicationResult publishAction(
        const PreparedStorageTransaction& transaction,
        const OwnerHistorySnapshot* stagedOwnerHistory,
        const uint64_t* stagedDeliverySequence = nullptr,
        bool stagedTouchesIdList = false);

    /// Delivery sequence of a fast-path local commit: trusted only when the
    /// exact-prefix fast path verified that the committed record is the sole
    /// append (any full-replay refresh leaves the cache cold for one rescan).
    struct DeliverySequenceCache {
        size_t acceptedRecordCount = 0;
        uint64_t next = 1;
    };

    ProviderBackedDocumentState(std::unique_ptr<DocumentStorageSession> storage,
        std::unique_ptr<ActionPublicationCoordinator> coordinator,
        StorageRecoveryState recovery,
        std::unique_ptr<AcceptedPrefixProjection> projection,
        ProviderDocumentState state);

    std::unique_ptr<DocumentStorageSession> m_storage;
    std::unique_ptr<ActionPublicationCoordinator> m_coordinator;
    StorageRecoveryState m_recovery;
    std::unique_ptr<AcceptedPrefixProjection> m_projection;
    ProviderDocumentState m_state;
    std::thread::id m_ownerThread;
    bool m_callActive = false;
    std::optional<DeliverySequenceCache> m_deliverySequenceCache;
    uint64_t m_visibilityGeneration = 0;
    LocalActionCommittedFn m_onLocalActionCommitted;
    StateChangedFn m_onStateChanged;
};

} // namespace Markoff::VNext
