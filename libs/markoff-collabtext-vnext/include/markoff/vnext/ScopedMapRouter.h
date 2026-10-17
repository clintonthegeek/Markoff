// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/RouterBudget.h>

#include <deque>
#include <optional>
#include <vector>

namespace Markoff::VNext {

class ProviderBackedDocumentState;

struct RemoteMapOperation {
    MapScope scope;
    std::optional<BlockIdentity> targetBlock;
    QByteArray bytes;
    uint64_t deliverySequence = 0;
    QByteArray actionId;
    uint32_t bundleIndex = 0;
    uint32_t bundleOperationCount = 1;
};

enum class MapRouteState {
    applied,
    stale,
    duplicate,
    heldForUnknownStream,
    backpressured,
    rejected,
};

struct MapRouteResult {
    MapRouteState state = MapRouteState::rejected;
    std::optional<CollabText::Error> error;
};

struct CompletedMapOperation {
    RemoteMapOperation operation;
    MapApplyKind outcome;
};

struct QuarantinedMapOperation {
    RemoteMapOperation operation;
    CollabText::Error reason;
};

/// Bounded owner-thread router for Markoff MMP streams. It retains original
/// envelopes for unknown maps and rejected map applications. Bundle-atomic
/// receipts and cross-bundle operation-identity uniqueness remain host duties.
class ScopedMapRouter final {
public:
    explicit ScopedMapRouter(std::string document, uint64_t epoch,
                             RouterBudget budget = {});
    /// Borrow the lifecycle owner so duplicate checks always read its current
    /// accepted projection after publication/recovery replaces that projection.
    /// The owner must outlive this router and both are used on its owner thread.
    void setDocumentState(ProviderBackedDocumentState* state) noexcept
    { m_documentState = state; }

    [[nodiscard]] CollabText::Result<std::vector<CompletedMapOperation>>
    registerStream(ReplicatedMapStream& stream);
    void unregisterStream(ReplicatedMapStream& stream) noexcept;

    [[nodiscard]] MapRouteResult offer(
        const RemoteMapOperation& operation,
        std::vector<CompletedMapOperation>& completed);

    [[nodiscard]] std::vector<QuarantinedMapOperation> quarantined() const;
    [[nodiscard]] CollabText::Result<size_t> restoreQuarantine(
        const std::vector<QByteArray>& records);
    [[nodiscard]] CollabText::Result<void> acknowledgeQuarantine(
        const RemoteMapOperation& exactEnvelope);
    [[nodiscard]] size_t heldOperationCount() const noexcept;
    [[nodiscard]] size_t heldByteCount() const noexcept;

private:
    enum class HeldState { unknown_stream, rejected };
    struct Held {
        RemoteMapOperation operation;
        HeldState state;
        size_t chargedBytes;
        std::optional<CollabText::Error> error;
    };

    [[nodiscard]] CollabText::Result<void> validate(
        const RemoteMapOperation& operation) const;
    [[nodiscard]] size_t charge(const RemoteMapOperation& operation) const noexcept;
    [[nodiscard]] bool hasCapacity(size_t bytes) const noexcept;
    [[nodiscard]] bool sameEnvelope(const RemoteMapOperation& left,
                                   const RemoteMapOperation& right) const noexcept;
    [[nodiscard]] ReplicatedMapStream* findStream(const MapScope& scope) const noexcept;
    [[nodiscard]] MapRouteResult applyKnown(
        const RemoteMapOperation& operation, ReplicatedMapStream& stream,
        std::vector<CompletedMapOperation>& completed);

    std::string m_document;
    uint64_t m_epoch;
    RouterBudget m_budget;
    ProviderBackedDocumentState* m_documentState = nullptr;
    std::vector<ReplicatedMapStream*> m_streams;
    std::deque<Held> m_held;
    size_t m_heldBytes = 0;
};

} // namespace Markoff::VNext
