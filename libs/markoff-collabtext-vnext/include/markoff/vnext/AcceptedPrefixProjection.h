// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/ScopedIdListStream.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/OwnerHistoryCodec.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Markoff::VNext {

class ProviderBackedDocumentState;

/// One exact operation and its accepted MAB envelope fields, returned on
/// demand from the provider's retained accepted record.
struct AcceptedActionOperation {
    ActionOperation operation;
    uint64_t deliverySequence = 0;
    QByteArray actionId;
    uint32_t operationIndex = 0;
    uint32_t operationCount = 0;
    AcceptedRevision priorRevision;
    AcceptedRevision committedRevision;
};

/// Fresh candidate-engine projections rebuilt from one complete provider
/// snapshot. Construction fails atomically: callers receive no partially
/// replayed projection if any accepted record or gap is invalid/deferred.
/// Owner-local undo history restoration remains a separate step.
class AcceptedPrefixProjection final {
public:
    [[nodiscard]] static CollabText::Result<std::unique_ptr<AcceptedPrefixProjection>>
    replay(DocumentStorageSession& session, const StorageRecoveryState& recovery);

    [[nodiscard]] const AcceptedRevision& acceptedRevision() const noexcept
    { return m_acceptedRevision; }
    [[nodiscard]] const std::optional<OwnerHistorySnapshot>& ownerHistory() const noexcept
    { return m_ownerHistory; }
    [[nodiscard]] const ScopedIdListStream* idList() const noexcept
    { return m_idList.get(); }
    /// Mutable stream handles are exposed only for the staged action owned by
    /// ProviderBackedDocumentState. Publication/recovery refreshes existing
    /// wrappers in place so registered routers retain valid addresses.
    [[nodiscard]] ScopedIdListStream* mutableIdList() noexcept
    { return m_idList.get(); }
    [[nodiscard]] ScopedTextStream* mutableTextStream(std::string_view stream) noexcept;
    [[nodiscard]] ReplicatedMapStream* mutableMapStream(std::string_view map) noexcept;
    [[nodiscard]] CollabText::Result<ScopedIdListStream*> ensureIdList(
        DocumentStorageSession& session);
    [[nodiscard]] CollabText::Result<ScopedTextStream*> ensureTextStream(
        DocumentStorageSession& session, const BlockIdentity& block);
    [[nodiscard]] CollabText::Result<ReplicatedMapStream*> ensureMapStream(
        DocumentStorageSession& session, const MapScope& scope);
    /// Refresh this projection from a complete replay while retaining every
    /// existing stream object's address. Routers and Markoff view adapters may
    /// borrow those objects across publication/recovery; their engine state is
    /// replaced in place and new routes are added.
    [[nodiscard]] CollabText::Result<void> refreshFrom(
        DocumentStorageSession& session, AcceptedPrefixProjection& replayed);
    [[nodiscard]] const ScopedTextStream* textStream(std::string_view stream) const noexcept;
    [[nodiscard]] const ReplicatedMapStream* mapStream(std::string_view map) const noexcept;
    /// Exact accepted-record lookup for a scoped producer identity. It scans
    /// the accepted records retained by this projection; QByteArray copies
    /// share their immutable provider-record storage. Duplicate handling can
    /// compare payload and MAB envelope fields rather than trusting Core's
    /// duplicate outcome as byte equality.
    [[nodiscard]] CollabText::Result<std::optional<AcceptedActionOperation>>
    findAcceptedOperation(std::string_view stream,
        const CollabText::ActorId& actor, uint64_t counter) const;

private:
    friend class ProviderBackedDocumentState;

    /// Advance metadata after a local action was already applied to this
    /// tentative projection and the provider accepted that exact next record.
    /// This deliberately does not apply operations; callers must prove the
    /// provider prefix is the prior prefix plus this one staged MAB.
    [[nodiscard]] CollabText::Result<void> acceptStagedLocalCommit(
        const AcceptedStorageRecord& record, const AcceptedRevision& acceptedRevision,
        OwnerHistorySnapshot ownerHistory);

    explicit AcceptedPrefixProjection(AcceptedRevision revision,
                                      std::string document, uint64_t epoch)
        : m_acceptedRevision(std::move(revision))
        , m_document(std::move(document))
        , m_epoch(epoch)
    {}

    [[nodiscard]] CollabText::Result<void> replayRecord(
        DocumentStorageSession& session, const AcceptedStorageRecord& record);

    AcceptedRevision m_acceptedRevision;
    std::string m_document;
    uint64_t m_epoch = 0;
    std::unique_ptr<ScopedIdListStream> m_idList;
    std::map<std::string, std::unique_ptr<ScopedTextStream>> m_textStreams;
    std::map<std::string, std::unique_ptr<ReplicatedMapStream>> m_mapStreams;
    std::vector<AcceptedStorageRecord> m_acceptedRecords;
    std::optional<OwnerHistorySnapshot> m_ownerHistory;
};

} // namespace Markoff::VNext
