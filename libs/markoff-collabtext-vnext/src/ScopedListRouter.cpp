// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ScopedListRouter.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/QuarantineRecordCodec.h>

#include <collabtext/Serialization.h>
#include <markoff/core/TextUnits.h>
#include <markoff/vnext/StreamNames.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace Markoff::VNext {
namespace {

constexpr uint32_t kMaxBundleOperationCount = 65536;
constexpr size_t kMaxRouteStringBytes = 256;
constexpr size_t kMaxActionIdBytes = 256;

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return Markoff::TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool exactAcceptedEnvelope(const AcceptedActionOperation& original,
                           const RoutedListOperation& routed)
{
    const auto& operation = routed.operation;
    const auto& accepted = original.operation;
    return accepted.primitive == OperationPrimitive::idList
        && accepted.codec == OperationCodec::ctlo2
        && !accepted.targetBlock
        && accepted.stream == operation.scope.stream
        && accepted.producer == operation.identity.actor
        && accepted.counter == operation.identity.counter
        && accepted.payload == operation.bytes
        && original.deliverySequence == routed.deliverySequence
        && original.actionId == routed.actionId
        && original.operationIndex == routed.bundleIndex
        && original.operationCount == routed.bundleOperationCount;
}

} // namespace

ScopedListRouter::ScopedListRouter(std::string document, uint64_t epoch,
                                   RouterBudget budget)
    : m_document(std::move(document)), m_epoch(epoch), m_budget(budget) {}

CollabText::Result<void> ScopedListRouter::validate(
    const RoutedListOperation& routed) const
{
    using namespace CollabText;
    const auto& operation = routed.operation;
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "list router has an invalid document scope");
    if (operation.scope.document != m_document)
        return Error(ErrorCode::wrong_document,
                     "remote list operation has the wrong document key");
    if (operation.scope.stream != kOrderStreamName)
        return Error(ErrorCode::unknown_stream,
                     "remote list operation is not on the canonical order stream");
    if (operation.scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_epoch,
                     "remote list operation has the wrong epoch");
    if (operation.scope.primitive != Primitive::list)
        return Error(ErrorCode::wrong_primitive,
                     "list router received another primitive");
    if (routed.deliverySequence == 0 || routed.actionId.isEmpty()
        || static_cast<size_t>(routed.actionId.size()) > kMaxActionIdBytes)
        return Error(ErrorCode::malformed,
                     "remote list operation has invalid action metadata");
    if (routed.bundleOperationCount == 0
        || routed.bundleOperationCount > kMaxBundleOperationCount
        || routed.bundleIndex >= routed.bundleOperationCount)
        return Error(ErrorCode::malformed,
                     "invalid list action bundle operation index");
    if (!operation.identity.actor.valid() || operation.identity.counter == 0)
        return Error(ErrorCode::invalid_identity,
                     "remote list operation has an invalid identity");
    auto decoded = Crdt::decode_idlist_operation(std::string_view(
        operation.bytes.constData(), static_cast<size_t>(operation.bytes.size())));
    if (!decoded.ok()) return decoded.error();
    if (Crdt::idlist_op_identity(decoded.value()) != operation.identity)
        return Error(ErrorCode::actor_conflict,
                     "remote list envelope identity differs from its operation bytes");
    return {};
}

size_t ScopedListRouter::charge(const RoutedListOperation& routed) const noexcept
{
    const size_t fixed = sizeof(Held) + sizeof(uint64_t) + sizeof(uint32_t) * 2;
    const auto& operation = routed.operation;
    size_t total = fixed;
    for (const size_t part : {operation.scope.document.size(), operation.scope.stream.size(),
                              static_cast<size_t>(routed.actionId.size()),
                              static_cast<size_t>(operation.bytes.size())}) {
        if (part > std::numeric_limits<size_t>::max() - total)
            return std::numeric_limits<size_t>::max();
        total += part;
    }
    return total;
}

bool ScopedListRouter::hasCapacity(size_t bytes) const noexcept
{
    return m_held.size() < m_budget.maxHeldOperations
        && bytes <= m_budget.maxHeldBytes
        && m_heldBytes <= m_budget.maxHeldBytes - bytes;
}

bool ScopedListRouter::sameEnvelope(const RoutedListOperation& left,
                                    const RoutedListOperation& right) const noexcept
{
    return left.operation.scope == right.operation.scope
        && left.operation.identity == right.operation.identity
        && left.operation.bytes == right.operation.bytes
        && left.deliverySequence == right.deliverySequence
        && left.actionId == right.actionId
        && left.bundleIndex == right.bundleIndex
        && left.bundleOperationCount == right.bundleOperationCount;
}

ScopedIdListStream* ScopedListRouter::findStream(
    const CollabText::PositionScope& scope) const noexcept
{
    const auto found = std::find_if(m_streams.cbegin(), m_streams.cend(),
        [&scope](const ScopedIdListStream* stream) {
            return stream && stream->scope() == scope;
        });
    return found == m_streams.cend() ? nullptr : *found;
}

ListRouteResult ScopedListRouter::applyKnown(
    const RoutedListOperation& routed, ScopedIdListStream& stream,
    std::vector<CompletedListOperation>& completed)
{
    const auto result = stream.applyRemote(routed.operation);
    if (!result.ok()) return {ListRouteState::rejected, result.error()};
    switch (result.value().kind()) {
    case CollabText::ApplyOutcomeKind::applied:
        completed.push_back({routed, CollabText::ApplyOutcomeKind::applied});
        return {ListRouteState::applied, std::nullopt};
    case CollabText::ApplyOutcomeKind::duplicate: {
        if (m_documentState) {
            const auto& operation = routed.operation;
            auto accepted = m_documentState->projection().findAcceptedOperation(
                operation.scope.stream, operation.identity.actor,
                operation.identity.counter);
            if (!accepted.ok()) return {ListRouteState::rejected, accepted.error()};
            if (!accepted.value())
                return {ListRouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::history_unavailable,
                        "Core list duplicate has no matching accepted MAB identity")};
            if (!exactAcceptedEnvelope(*accepted.value(), routed))
                return {ListRouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::actor_conflict,
                        "accepted list identity arrived with different MAB envelope bytes")};
        }
        completed.push_back({routed, CollabText::ApplyOutcomeKind::duplicate});
        return {ListRouteState::duplicate, std::nullopt};
    }
    case CollabText::ApplyOutcomeKind::deferred:
        return {ListRouteState::deferred, std::nullopt};
    case CollabText::ApplyOutcomeKind::rejected:
        return {ListRouteState::rejected, result.value().error()};
    }
    return {ListRouteState::rejected,
            CollabText::Error(CollabText::ErrorCode::malformed,
                              "unknown list apply outcome")};
}

CollabText::Result<std::vector<CompletedListOperation>>
ScopedListRouter::registerStream(ScopedIdListStream& stream)
{
    using namespace CollabText;
    const auto& scope = stream.scope();
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "list router has an invalid document scope");
    if (scope.document != m_document)
        return Error(ErrorCode::wrong_document,
                     "registered list belongs to another document");
    if (scope.stream != kOrderStreamName)
        return Error(ErrorCode::unknown_stream,
                     "registered list is not the canonical order stream");
    if (scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_epoch,
                     "registered list belongs to another epoch");
    if (scope.primitive != Primitive::list)
        return Error(ErrorCode::wrong_primitive,
                     "registered stream is not a list");
    if (findStream(scope))
        return Error(ErrorCode::writer_conflict,
                     "a list stream with this scope is already registered");
    m_streams.push_back(&stream);

    std::vector<CompletedListOperation> completed;
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (it->state != HeldState::unknown_stream || it->operation.operation.scope != scope) {
            ++it;
            continue;
        }
        const auto result = applyKnown(it->operation, stream, completed);
        if (result.state == ListRouteState::applied
            || result.state == ListRouteState::duplicate) {
            m_heldBytes -= it->chargedBytes;
            it = m_held.erase(it);
        } else {
            it->state = result.state == ListRouteState::deferred
                ? HeldState::deferred : HeldState::rejected;
            it->error = result.error;
            ++it;
        }
    }
    retryDeferred(scope, stream, completed);
    return completed;
}

void ScopedListRouter::unregisterStream(ScopedIdListStream& stream) noexcept
{
    std::erase(m_streams, &stream);
}

ListRouteResult ScopedListRouter::offer(
    const RoutedListOperation& operation,
    std::vector<CompletedListOperation>& completed)
{
    auto valid = validate(operation);
    if (!valid.ok()) return {ListRouteState::rejected, valid.error()};
    for (const auto& held : m_held) {
        if (held.operation.operation.scope != operation.operation.scope
            || held.operation.operation.identity != operation.operation.identity)
            continue;
        if (sameEnvelope(held.operation, operation))
            return {held.state == HeldState::unknown_stream
                        ? ListRouteState::heldForUnknownStream
                        : held.state == HeldState::deferred
                            ? ListRouteState::deferred : ListRouteState::rejected,
                    held.error};
        if (m_documentState) {
            const auto& remote = operation.operation;
            auto accepted = m_documentState->projection().findAcceptedOperation(
                remote.scope.stream, remote.identity.actor, remote.identity.counter);
            if (!accepted.ok()) return {ListRouteState::rejected, accepted.error()};
            if (accepted.value()
                && exactAcceptedEnvelope(*accepted.value(), operation)) {
                completed.push_back({operation, CollabText::ApplyOutcomeKind::duplicate});
                return {ListRouteState::duplicate, std::nullopt};
            }
        }
        const size_t bytes = charge(operation);
        if (!hasCapacity(bytes)) return {ListRouteState::backpressured, std::nullopt};
        const auto error = CollabText::Error(
            CollabText::ErrorCode::actor_conflict,
            "same list operation identity arrived with a different delivery envelope");
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::rejected, bytes, error});
        return {ListRouteState::rejected, error};
    }

    const size_t bytes = charge(operation);
    if (!hasCapacity(bytes)) return {ListRouteState::backpressured, std::nullopt};
    auto* stream = findStream(operation.operation.scope);
    if (!stream) {
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::unknown_stream, bytes, std::nullopt});
        return {ListRouteState::heldForUnknownStream, std::nullopt};
    }
    const auto result = applyKnown(operation, *stream, completed);
    if (result.state == ListRouteState::deferred
        || result.state == ListRouteState::rejected) {
        m_heldBytes += bytes;
        m_held.push_back({operation,
            result.state == ListRouteState::deferred ? HeldState::deferred
                                                     : HeldState::rejected,
            bytes, result.error});
    } else {
        retryDeferred(operation.operation.scope, *stream, completed);
    }
    return result;
}

void ScopedListRouter::retryDeferred(
    const CollabText::PositionScope& scope, ScopedIdListStream& stream,
    std::vector<CompletedListOperation>& completed)
{
    bool progressed;
    do {
        progressed = false;
        for (auto it = m_held.begin(); it != m_held.end();) {
            if (it->state != HeldState::deferred
                || it->operation.operation.scope != scope) {
                ++it;
                continue;
            }
            const auto result = applyKnown(it->operation, stream, completed);
            if (result.state == ListRouteState::applied
                || result.state == ListRouteState::duplicate) {
                m_heldBytes -= it->chargedBytes;
                it = m_held.erase(it);
                progressed = true;
            } else {
                if (result.state == ListRouteState::rejected) {
                    it->state = HeldState::rejected;
                    it->error = result.error;
                }
                ++it;
            }
        }
    } while (progressed);
    std::stable_sort(completed.begin(), completed.end(),
        [](const CompletedListOperation& left, const CompletedListOperation& right) {
            if (left.operation.deliverySequence != right.operation.deliverySequence)
                return left.operation.deliverySequence < right.operation.deliverySequence;
            return left.operation.bundleIndex < right.operation.bundleIndex;
        });
}

std::vector<QuarantinedListOperation> ScopedListRouter::quarantined() const
{
    std::vector<QuarantinedListOperation> result;
    for (const auto& held : m_held)
        if (held.state == HeldState::rejected && held.error)
            result.push_back({held.operation, *held.error});
    return result;
}

CollabText::Result<size_t> ScopedListRouter::restoreQuarantine(
    const std::vector<QByteArray>& records)
{
    using namespace CollabText;
    std::vector<Held> pending;
    size_t pendingBytes = 0;
    for (const auto& bytes : records) {
        auto decoded = QuarantineRecordCodec::decode(bytes);
        if (!decoded.ok()) return decoded.error();
        const auto& record = decoded.value();
        if (record.primitive != QuarantinePrimitive::list) continue;
        if (record.document != m_document)
            return Error(ErrorCode::wrong_document,
                         "list quarantine belongs to another document");
        if (record.epoch != m_epoch)
            return Error(ErrorCode::wrong_epoch,
                         "list quarantine belongs to another epoch");
        RemoteListOperation remote{
            PositionScope{record.document, record.stream, Primitive::list, record.epoch},
            {record.actor, record.counter}, record.payload};
        RoutedListOperation operation{remote, record.deliverySequence, record.actionId,
                                      record.bundleIndex, record.bundleOperationCount};
        auto checked = validate(operation);
        if (!checked.ok()) return checked.error();
        if (std::any_of(m_held.begin(), m_held.end(),
                [this, &operation](const Held& held) {
                    return sameEnvelope(held.operation, operation);
                })
            || std::any_of(pending.begin(), pending.end(),
                [this, &operation](const Held& held) {
                    return sameEnvelope(held.operation, operation);
                }))
            continue;
        const size_t bytesCharged = charge(operation);
        if (bytesCharged > std::numeric_limits<size_t>::max() - pendingBytes)
            return Error(ErrorCode::limit_exceeded,
                         "restored list quarantine size overflowed");
        pendingBytes += bytesCharged;
        pending.push_back({operation, HeldState::rejected, bytesCharged, record.reason});
    }
    if (pending.size() > m_budget.maxHeldOperations
            - std::min(m_held.size(), m_budget.maxHeldOperations)
        || pendingBytes > m_budget.maxHeldBytes
            - std::min(m_heldBytes, m_budget.maxHeldBytes))
        return Error(ErrorCode::deferred_queue_full,
                     "list quarantine recovery exceeds router capacity");
    for (auto& held : pending) m_held.push_back(std::move(held));
    m_heldBytes += pendingBytes;
    return pending.size();
}

CollabText::Result<void> ScopedListRouter::acknowledgeQuarantine(
    const RoutedListOperation& exactEnvelope)
{
    const auto found = std::find_if(m_held.begin(), m_held.end(),
        [this, &exactEnvelope](const Held& held) {
            return held.state == HeldState::rejected
                && sameEnvelope(held.operation, exactEnvelope);
        });
    if (found == m_held.end())
        return CollabText::Error(CollabText::ErrorCode::not_found,
                                 "list quarantine envelope is not held");
    if (!m_documentState)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "list quarantine requires a provider-backed document state");
    if (!found->error)
        return CollabText::Error(CollabText::ErrorCode::corrupt_record,
                                 "rejected list envelope has no quarantine reason");
    const auto& remote = found->operation.operation;
    QuarantineRecord record;
    record.document = remote.scope.document;
    record.epoch = remote.scope.epoch;
    record.stream = remote.scope.stream;
    record.primitive = QuarantinePrimitive::list;
    record.actor = remote.identity.actor;
    record.counter = remote.identity.counter;
    record.payload = remote.bytes;
    record.deliverySequence = found->operation.deliverySequence;
    record.actionId = found->operation.actionId;
    record.bundleIndex = found->operation.bundleIndex;
    record.bundleOperationCount = found->operation.bundleOperationCount;
    record.reason = *found->error;
    auto encoded = QuarantineRecordCodec::encode(record);
    if (!encoded.ok()) return encoded.error();
    auto stored = m_documentState->storage().storeQuarantinedOperation(encoded.value());
    if (!stored.ok()) return stored.error();
    m_heldBytes -= found->chargedBytes;
    m_held.erase(found);
    return {};
}

size_t ScopedListRouter::heldOperationCount() const noexcept { return m_held.size(); }
size_t ScopedListRouter::heldByteCount() const noexcept { return m_heldBytes; }

} // namespace Markoff::VNext
