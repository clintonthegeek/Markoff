// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/BlockIdentity.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <collabtext/Anchor.h>
#include <collabtext/IdListEngine.h>
#include <collabtext/LocalHistory.h>

#include <QByteArray>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Markoff::VNext {

struct ListProjectionEntry {
    BlockIdentity identity;
    uint64_t applicationValue = 0;
};

struct LocalListOperation {
    CollabText::PositionScope scope;
    CollabText::Crdt::Identity identity;
    CollabText::Crdt::IdListOperation operation;
    QByteArray bytes;
    std::optional<BlockIdentity> insertedBlock;
    /// Exact provider ranges allocated while producing this operation.
    std::vector<ScopedReservation> reservations;
};

struct RemoteListOperation {
    CollabText::PositionScope scope;
    CollabText::Crdt::Identity identity;
    QByteArray bytes;
};

/// Structural list adapter for Markoff's block-order stream. Block identities
/// come from Core's full actor/counter element identities; caller values remain
/// application payload only and may repeat; they are never block identities.
class ScopedIdListStream final {
public:
    [[nodiscard]] static CollabText::Result<std::unique_ptr<ScopedIdListStream>> create(
        DocumentStorageSession& session, CollabText::PositionScope scope);

    ~ScopedIdListStream();
    ScopedIdListStream(const ScopedIdListStream&) = delete;
    ScopedIdListStream& operator=(const ScopedIdListStream&) = delete;

    [[nodiscard]] const CollabText::PositionScope& scope() const noexcept;
    [[nodiscard]] const CollabText::ActorId& actor() const noexcept;
    [[nodiscard]] CollabText::Result<LocalListOperation> insertAfter(
        std::optional<BlockIdentity> after, uint64_t applicationValue);
    [[nodiscard]] CollabText::Result<LocalListOperation> remove(
        const BlockIdentity& identity);
    [[nodiscard]] CollabText::Result<std::optional<LocalListOperation>> undo();
    [[nodiscard]] CollabText::Result<std::optional<LocalListOperation>> redo();
    [[nodiscard]] CollabText::Result<std::optional<LocalListOperation>> undoGroup(
        const CollabText::Crdt::Identity& groupId);
    [[nodiscard]] CollabText::Result<std::optional<LocalListOperation>> redoGroup(
        const CollabText::Crdt::Identity& groupId);
    [[nodiscard]] CollabText::Crdt::LocalHistorySnapshot localHistorySnapshot() const;
    [[nodiscard]] CollabText::Result<void> restoreLocalHistory(
        const CollabText::Crdt::LocalHistorySnapshot& snapshot);
    /// Move `count` oldest undo entries into the pending retirement queue,
    /// then restore the engine's group ceiling.
    [[nodiscard]] CollabText::Result<void> retireOldestUndoGroups(std::size_t count);
    [[nodiscard]] CollabText::Result<void> acknowledgeUndoRetirements(
        const std::vector<CollabText::Crdt::Identity>& groupIds);
    [[nodiscard]] CollabText::Result<CollabText::Crdt::Anchor> anchorOf(
        const BlockIdentity& identity,
        CollabText::Crdt::Bias bias = CollabText::Crdt::Bias::Left) const;
    [[nodiscard]] CollabText::Result<std::vector<ListProjectionEntry>> visibleEntries() const;
    /// Apply only an MTG record already durably authorized and committed by
    /// the host for this exact scope and accepted-prefix token.
    [[nodiscard]] CollabText::Result<CollabText::ApplyOutcome> applyCommittedCounterGap(
        const QByteArray& scopedRecord, const QByteArray& expectedAcceptedPrior);
    [[nodiscard]] CollabText::Result<CollabText::ApplyOutcome> applyRemote(
        const RemoteListOperation& operation);

private:
    friend class AcceptedPrefixProjection;
    [[nodiscard]] CollabText::Result<void> replaceStateFrom(ScopedIdListStream& replayed);

    class Allocator;
    ScopedIdListStream(DocumentStorageSession& session,
                       CollabText::PositionScope scope,
                       std::shared_ptr<Allocator> allocator,
                       std::unique_ptr<CollabText::IdListEngine> engine);
    [[nodiscard]] CollabText::Result<LocalListOperation> wrap(
        CollabText::Crdt::IdListOperation operation,
        std::optional<BlockIdentity> insertedBlock);

    DocumentStorageSession* m_session;
    CollabText::PositionScope m_scope;
    std::shared_ptr<Allocator> m_allocator;
    std::unique_ptr<CollabText::IdListEngine> m_engine;
};

} // namespace Markoff::VNext
