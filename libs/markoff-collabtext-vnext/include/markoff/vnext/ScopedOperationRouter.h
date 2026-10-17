// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/RouterBudget.h>

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace Markoff::VNext {

class ProviderBackedDocumentState;

enum class RouteState {
    applied,
    duplicate,
    deferred,
    held_for_unknown_stream,
    backpressured,
    rejected,
};

struct RouteResult {
    RouteState state = RouteState::rejected;
    std::optional<CollabText::Error> error;
};

struct CompletedRemoteOperation {
    RemoteOperation operation;
    CollabText::ApplyOutcomeKind outcome;
};

struct QuarantinedRemoteOperation {
    RemoteOperation operation;
    CollabText::Error reason;
};

/// Bounded owner-thread router for one document epoch's text streams. It keeps
/// the complete caller envelope while a stream is unknown or an operation is
/// causally deferred. Nothing is evicted to make room; callers retain and retry
/// an input reported as backpressured. Rejected envelopes stay quarantined.
/// Rejected apply outcomes stay quarantined here; validation failures remain
/// caller-owned with their returned error. Registered streams are non-owning;
/// unregister each one before destroying it.
class ScopedOperationRouter final {
public:
    explicit ScopedOperationRouter(std::string document, uint64_t epoch,
                                    RouterBudget budget = {});

    /// Borrow the lifecycle owner so duplicate checks always read its current
    /// accepted projection after publication/recovery replaces that projection.
    /// The owner must outlive this router and both are used on its owner thread.
    void setDocumentState(ProviderBackedDocumentState* state) noexcept
    { m_documentState = state; }

    [[nodiscard]] CollabText::Result<std::vector<CompletedRemoteOperation>>
    registerStream(ScopedTextStream& stream);
    void unregisterStream(ScopedTextStream& stream) noexcept;

    /// The completion vector includes this operation and any older deferred
    /// operations that became applied/duplicate while it was offered. It is
    /// caller-owned and lets the host advance only the matching durable
    /// receipts after its recovery policy is satisfied.
    [[nodiscard]] RouteResult offer(
        const RemoteOperation& operation,
        std::vector<CompletedRemoteOperation>& completed);

    /// Rejected envelopes remain charged until the host has durably recorded
    /// their original bytes and reason, then acknowledges that exact envelope.
    [[nodiscard]] std::vector<QuarantinedRemoteOperation> quarantined() const;
    /// Rehydrate this router's text records from one provider recovery snapshot.
    /// The entire matching batch is restored or left with the caller on error.
    [[nodiscard]] CollabText::Result<size_t> restoreQuarantine(
        const std::vector<QByteArray>& records);
    [[nodiscard]] CollabText::Result<void> acknowledgeQuarantine(
        const RemoteOperation& exactEnvelope);

    [[nodiscard]] size_t heldOperationCount() const noexcept;
    [[nodiscard]] size_t heldByteCount() const noexcept;

private:
    enum class HeldState { unknown_stream, deferred, rejected };
    struct Held {
        RemoteOperation operation;
        HeldState state;
        size_t chargedBytes;
        std::optional<CollabText::Error> error;
    };

    [[nodiscard]] CollabText::Result<void> validate(
        const RemoteOperation& operation) const;
    [[nodiscard]] size_t charge(const RemoteOperation& operation) const noexcept;
    [[nodiscard]] bool sameEnvelope(const RemoteOperation& left,
                                   const RemoteOperation& right) const noexcept;
    [[nodiscard]] bool hasCapacity(size_t bytes) const noexcept;
    [[nodiscard]] ScopedTextStream* findStream(
        const CollabText::PositionScope& scope) const noexcept;
    [[nodiscard]] RouteResult applyKnown(
        const RemoteOperation& operation, ScopedTextStream& stream,
        std::vector<CompletedRemoteOperation>& completed);
    void retryDeferred(const CollabText::PositionScope& scope,
                       ScopedTextStream& stream,
                       std::vector<CompletedRemoteOperation>& completed);

    std::string m_document;
    uint64_t m_epoch;
    RouterBudget m_budget;
    ProviderBackedDocumentState* m_documentState = nullptr;
    std::vector<ScopedTextStream*> m_streams;
    std::deque<Held> m_held;
    size_t m_heldBytes = 0;
};

} // namespace Markoff::VNext
