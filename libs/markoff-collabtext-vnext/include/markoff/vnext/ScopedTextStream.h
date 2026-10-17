// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/ActorId.h>
#include <collabtext/Anchor.h>
#include <collabtext/ApplyOutcome.h>
#include <collabtext/CrdtEngine.h>
#include <collabtext/LocalHistory.h>

#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <QByteArray>

#include <memory>
#include <optional>
#include <vector>

namespace Markoff::VNext {

/// One operation ready for Markoff's host-owned durable action bundle.
/// Returned operations are the sole publication source for this adapter. If
/// `encodable()` is false, the raw operation remains available, but it cannot
/// be durably published through the current byte format; the caller must retain
/// it and recover the projection before accepting further edits.
struct LocalOperation {
    CollabText::PositionScope scope;
    CollabText::Crdt::Identity identity;
    CollabText::Crdt::Operation operation;
    QByteArray bytes;
    std::optional<CollabText::Error> encoding_error;
    /// Exact provider ranges allocated while producing this operation.
    std::vector<ScopedReservation> reservations;

    bool encodable() const noexcept { return !encoding_error.has_value(); }
};

/// Route fields are checked against both this stream and the decoded operation
/// identity before an operation reaches the engine.
struct RemoteOperation {
    CollabText::PositionScope scope;
    CollabText::Crdt::Identity identity;
    QByteArray bytes;
    uint64_t delivery_sequence = 0;
    QByteArray action_id;
    uint32_t bundle_index = 0;
    uint32_t bundle_operation_count = 1;
};

/// Markoff byte-range adapter over one explicitly scoped CollabText text stream.
/// The caller supplies durable private writer authority and controls
/// publication, transactions and recovery.
class ScopedTextStream final {
public:
    /// Create using the already-open document provider session. The session
    /// owns scoped allocation and must outlive this stream. The public route
    /// remains the original CollabText PositionScope.
    static CollabText::Result<std::unique_ptr<ScopedTextStream>> create(
        DocumentStorageSession& session,
        CollabText::PositionScope scope);

    ~ScopedTextStream();
    ScopedTextStream(const ScopedTextStream&) = delete;
    ScopedTextStream& operator=(const ScopedTextStream&) = delete;

    QByteArray text() const;

    /// Replace a half-open range in UTF-8 byte coordinates, as used by
    /// MarkoffDocument. Split scalars, malformed text, and invalid ranges are
    /// rejected before mutation. CollabText receives one UTF-16 replacement.
    [[nodiscard]] CollabText::Result<LocalOperation> replaceUtf8(
        qsizetype byteStart, qsizetype byteEnd, const QByteArray& replacement);

    /// Seed a fresh stream for the document's one durable initial_import
    /// bundle. A nonempty seed returns its operation; an empty seed returns
    /// nullopt and still enters the pending state. While pending, local history
    /// snapshots are empty and mutations are blocked. Call commitInitialImport()
    /// only after the host durably appends that bundle.
    [[nodiscard]] CollabText::Result<std::optional<LocalOperation>> seedInitialImport(
        const QByteArray& initialUtf8);
    [[nodiscard]] CollabText::Result<void> commitInitialImport();

    [[nodiscard]] CollabText::Result<QByteArray> boundaryTokenAt(
        uint32_t utf8ByteOffset, CollabText::Affinity affinity) const;
    [[nodiscard]] CollabText::Result<CollabText::ResolvedTextPosition> resolveBoundary(
        const QByteArray& tokenBytes) const;

    [[nodiscard]] CollabText::Result<std::optional<LocalOperation>> undo();
    [[nodiscard]] CollabText::Result<std::optional<LocalOperation>> redo();
    [[nodiscard]] CollabText::Result<std::optional<LocalOperation>> undoGroup(
        const CollabText::Crdt::Identity& groupId);
    [[nodiscard]] CollabText::Result<std::optional<LocalOperation>> redoGroup(
        const CollabText::Crdt::Identity& groupId);

    [[nodiscard]] CollabText::Crdt::LocalHistorySnapshot localHistorySnapshot() const;
    [[nodiscard]] CollabText::Result<void> restoreLocalHistory(
        const CollabText::Crdt::LocalHistorySnapshot& snapshot);
    [[nodiscard]] const CollabText::ActorId& actor() const noexcept;

    /// Applies only after scope and operation identity validation. Durable
    /// receipt advancement belongs to the caller and must wait for its log.
    [[nodiscard]] CollabText::Result<CollabText::ApplyOutcome> applyRemote(
        const RemoteOperation& operation);
    /// Apply MTG/1 only after its exact record is durably accepted. The prior
    /// token must be the accepted revision immediately before this gap.
    [[nodiscard]] CollabText::Result<CollabText::ApplyOutcome>
    applyCommittedCounterGap(const QByteArray& scopedRecord,
                             const QByteArray& expectedAcceptedPrior);

    [[nodiscard]] const CollabText::PositionScope& scope() const noexcept;

private:
    friend class AcceptedPrefixProjection;
    [[nodiscard]] CollabText::Result<void> replaceStateFrom(ScopedTextStream& replayed);

    class Allocator;
    ScopedTextStream(CollabText::PositionScope scope,
                     std::shared_ptr<Allocator> allocator,
                     std::unique_ptr<CollabText::CrdtEngine> engine);

    [[nodiscard]] LocalOperation encodeLocal(
        CollabText::Crdt::Operation operation);

    CollabText::PositionScope m_scope;
    std::shared_ptr<Allocator> m_allocator;
    std::unique_ptr<CollabText::CrdtEngine> m_engine;
    bool m_touched = false;
    bool m_initialImportPending = false;
};

} // namespace Markoff::VNext
