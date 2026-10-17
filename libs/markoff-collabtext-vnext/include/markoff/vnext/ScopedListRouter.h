// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/RouterBudget.h>
#include <markoff/vnext/ScopedIdListStream.h>

#include <deque>
#include <optional>
#include <vector>

namespace Markoff::VNext {

class ProviderBackedDocumentState;

struct RoutedListOperation {
    RemoteListOperation operation;
    uint64_t deliverySequence = 0;
    QByteArray actionId;
    uint32_t bundleIndex = 0;
    uint32_t bundleOperationCount = 1;
};

enum class ListRouteState {
    applied,
    duplicate,
    deferred,
    heldForUnknownStream,
    backpressured,
    rejected,
};

struct ListRouteResult {
    ListRouteState state = ListRouteState::rejected;
    std::optional<CollabText::Error> error;
};

struct CompletedListOperation {
    RoutedListOperation operation;
    CollabText::ApplyOutcomeKind outcome;
};

struct QuarantinedListOperation {
    RoutedListOperation operation;
    CollabText::Error reason;
};

/// Bounded owner-thread router for the document's structural order stream.
/// Unknown and deferred operations retain the complete delivery envelope;
/// rejected bytes stay charged until the host durably records and acknowledges
/// that exact envelope. Backpressured input remains caller-owned.
class ScopedListRouter final {
public:
    explicit ScopedListRouter(std::string document, uint64_t epoch,
                              RouterBudget budget = {});
    /// Borrow the lifecycle owner so duplicate checks always read its current
    /// accepted projection after publication/recovery replaces that projection.
    /// The owner must outlive this router and both are used on its owner thread.
    void setDocumentState(ProviderBackedDocumentState* state) noexcept
    { m_documentState = state; }

    [[nodiscard]] CollabText::Result<std::vector<CompletedListOperation>>
    registerStream(ScopedIdListStream& stream);
    void unregisterStream(ScopedIdListStream& stream) noexcept;

    [[nodiscard]] ListRouteResult offer(
        const RoutedListOperation& operation,
        std::vector<CompletedListOperation>& completed);

    [[nodiscard]] std::vector<QuarantinedListOperation> quarantined() const;
    [[nodiscard]] CollabText::Result<size_t> restoreQuarantine(
        const std::vector<QByteArray>& records);
    [[nodiscard]] CollabText::Result<void> acknowledgeQuarantine(
        const RoutedListOperation& exactEnvelope);
    [[nodiscard]] size_t heldOperationCount() const noexcept;
    [[nodiscard]] size_t heldByteCount() const noexcept;

private:
    enum class HeldState { unknown_stream, deferred, rejected };
    struct Held {
        RoutedListOperation operation;
        HeldState state;
        size_t chargedBytes;
        std::optional<CollabText::Error> error;
    };

    [[nodiscard]] CollabText::Result<void> validate(
        const RoutedListOperation& operation) const;
    [[nodiscard]] size_t charge(const RoutedListOperation& operation) const noexcept;
    [[nodiscard]] bool hasCapacity(size_t bytes) const noexcept;
    [[nodiscard]] bool sameEnvelope(const RoutedListOperation& left,
                                   const RoutedListOperation& right) const noexcept;
    [[nodiscard]] ScopedIdListStream* findStream(
        const CollabText::PositionScope& scope) const noexcept;
    [[nodiscard]] ListRouteResult applyKnown(
        const RoutedListOperation& operation, ScopedIdListStream& stream,
        std::vector<CompletedListOperation>& completed);
    void retryDeferred(const CollabText::PositionScope& scope,
                       ScopedIdListStream& stream,
                       std::vector<CompletedListOperation>& completed);

    std::string m_document;
    uint64_t m_epoch;
    RouterBudget m_budget;
    ProviderBackedDocumentState* m_documentState = nullptr;
    std::vector<ScopedIdListStream*> m_streams;
    std::deque<Held> m_held;
    size_t m_heldBytes = 0;
};

} // namespace Markoff::VNext
