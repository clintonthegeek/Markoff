// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/BlockIdentity.h>
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/CrossStreamHistoryCodec.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/MarkoffMapCodec.h>

#include <collabtext/ActorId.h>

#include <QByteArray>
#include <QHash>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Markoff::VNext {

struct MapScope {
    std::string document;
    std::string map;
    uint64_t epoch = 0;
    friend bool operator==(const MapScope&, const MapScope&) = default;
};

struct LocalMapOperation {
    MapScope scope;
    std::optional<BlockIdentity> targetBlock;
    MarkoffMapOperation operation;
    QByteArray bytes;
    /// Owner-local state captured before this operation; not part of MMP/1.
    bool priorValueExisted = false;
    QByteArray priorValue;
    ScopedReservation reservation;
};

struct MapProjectionEntry {
    std::optional<BlockIdentity> targetBlock;
    QByteArray key;
    QByteArray value;
};

/// Preserve the stream, target and checked operation identity when placing a
/// local map write into an MAB action. The enclosing bundle supplies document,
/// epoch, action and delivery metadata.
[[nodiscard]] CollabText::Result<ActionOperation> toActionOperation(
    const LocalMapOperation& operation);
/// Build the durable owner-history association needed to compensate this
/// operation after reopen. Map operations are singleton history groups.
[[nodiscard]] CollabText::Result<HistoryGroupAssociation> toHistoryAssociation(
    const LocalMapOperation& operation);

enum class MapApplyKind { applied, stale, duplicate };

/// Markoff-owned LWW map stream. It uses the document provider's durable
/// scoped counter reservation but does not depend on a CollabText primitive
/// or expose a Core implementation type. The returned MMP bytes are
/// the publication source; callers append them in an atomic MAB action before
/// accepting them as durable. Resolve uncertain appends by inspecting/replaying
/// that exact MAB action. A definitively failed append consumes its reserved
/// identity; the host must publish MMG/1 terminal interval evidence before
/// allowing further writes by this actor on the map stream, including after
/// restart. MMG is Markoff map metadata and does not invent a Core primitive.
class ReplicatedMapStream final {
public:
    /// Create using the already-open document provider session. The session
    /// owns scoped allocation and must outlive this stream.
    [[nodiscard]] static CollabText::Result<std::unique_ptr<ReplicatedMapStream>> create(
        DocumentStorageSession& session, MapScope scope);

    ~ReplicatedMapStream();
    ReplicatedMapStream(const ReplicatedMapStream&) = delete;
    ReplicatedMapStream& operator=(const ReplicatedMapStream&) = delete;

    [[nodiscard]] const MapScope& scope() const noexcept;
    [[nodiscard]] const CollabText::ActorId& actor() const noexcept;

    /// Call once after the host has replayed the complete accepted MMP prefix.
    /// Local writes are refused before this barrier so they cannot overwrite
    /// state that has not yet been reconstructed.
    [[nodiscard]] CollabText::Result<void> completeReplay();
    [[nodiscard]] std::optional<QByteArray> get(
        const QByteArray& key,
        std::optional<BlockIdentity> targetBlock = std::nullopt) const;
    [[nodiscard]] std::vector<MapProjectionEntry> values() const;

    [[nodiscard]] CollabText::Result<LocalMapOperation> set(
        const QByteArray& key, const QByteArray& value,
        std::optional<BlockIdentity> targetBlock = std::nullopt);
    [[nodiscard]] CollabText::Result<LocalMapOperation> remove(
        const QByteArray& key,
        std::optional<BlockIdentity> targetBlock = std::nullopt);
    /// Emit a fresh replicated inverse for the current LWW head of a local
    /// operation. Refuses to overwrite a later write to the same map key.
    [[nodiscard]] CollabText::Result<LocalMapOperation> compensate(
        const LocalMapOperation& operation);

    /// Apply a checked MMP/1 operation. A lower LWW stamp is accepted as stale
    /// for this projection but still advances the local observed counter.
    /// Global actor/counter byte-collision detection belongs to the durable
    /// host MAB identity index.
    [[nodiscard]] CollabText::Result<MapApplyKind> applyRemote(
        const QByteArray& operationBytes,
        std::optional<BlockIdentity> targetBlock = std::nullopt);
    /// Apply MMG/1 coverage only after its exact record is durably accepted.
    /// The prior token must be the accepted revision immediately before it.
    [[nodiscard]] CollabText::Result<void> applyCommittedCounterGap(
        const QByteArray& scopedRecord, const QByteArray& expectedAcceptedPrior);

private:
    friend class AcceptedPrefixProjection;
    [[nodiscard]] CollabText::Result<void> replaceStateFrom(ReplicatedMapStream& replayed);

    struct Entry {
        std::optional<BlockIdentity> targetBlock;
        QByteArray key;
        QByteArray value;
        CollabText::ActorId actor;
        uint64_t counter = 0;
        bool tombstone = false;
    };

    ReplicatedMapStream(DocumentStorageSession& session, MapScope scope);
    [[nodiscard]] CollabText::Result<LocalMapOperation> write(
        const QByteArray& key, const QByteArray& value, bool tombstone,
        std::optional<BlockIdentity> targetBlock);

    DocumentStorageSession* m_session;
    MapScope m_scope;
    QHash<QByteArray, Entry> m_entries;
    std::vector<MapCounterGap> m_committedGaps;
    bool m_replayComplete = false;
};

} // namespace Markoff::VNext
