// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ScopedOperationRouter.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/QuarantineRecordCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <collabtext/Serialization.h>

#include <markoff/core/TextUnits.h>

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
                           const RemoteOperation& operation)
{
    const auto& accepted = original.operation;
    return accepted.primitive == OperationPrimitive::text
        && accepted.codec == OperationCodec::ctbo3
        && accepted.stream == operation.scope.stream
        && accepted.producer == operation.identity.actor
        && accepted.counter == operation.identity.counter
        && accepted.payload == operation.bytes
        && original.deliverySequence == operation.delivery_sequence
        && original.actionId == operation.action_id
        && original.operationIndex == operation.bundle_index
        && original.operationCount == operation.bundle_operation_count;
}

} // namespace

ScopedOperationRouter::ScopedOperationRouter(std::string document, uint64_t epoch,
                                             RouterBudget budget)
    : m_document(std::move(document)), m_epoch(epoch), m_budget(budget) {}

CollabText::Result<void> ScopedOperationRouter::validate(
    const RemoteOperation& operation) const
{
    using namespace CollabText;
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return Error(ErrorCode::invalid_identity, "router has an invalid document scope");
    if (operation.scope.document != m_document)
        return Error(ErrorCode::wrong_document, "remote operation has the wrong document key");
    if (operation.scope.stream.empty())
        return Error(ErrorCode::unknown_stream, "remote operation has an empty stream key");
    if (operation.scope.stream.size() > kMaxRouteStringBytes)
        return Error(ErrorCode::limit_exceeded, "remote stream key exceeds 256 bytes");
    if (!validUtf8(operation.scope.stream))
        return Error(ErrorCode::invalid_unicode, "remote stream key is not valid UTF-8");
    if (operation.scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_epoch, "remote operation has the wrong epoch");
    if (operation.scope.primitive != Primitive::text)
        return Error(ErrorCode::wrong_primitive, "text router received another primitive");
    if (!parseBlockTextStreamName(operation.scope.stream).ok())
        return Error(ErrorCode::wrong_stream,
                     "text router received a noncanonical block stream");
    if (operation.delivery_sequence == 0)
        return Error(ErrorCode::malformed, "remote operation has no delivery sequence");
    if (operation.action_id.isEmpty()
        || static_cast<size_t>(operation.action_id.size()) > kMaxActionIdBytes)
        return Error(ErrorCode::malformed, "remote operation has an invalid action identity");
    if (operation.bundle_operation_count == 0
        || operation.bundle_operation_count > kMaxBundleOperationCount
        || operation.bundle_index >= operation.bundle_operation_count)
        return Error(ErrorCode::malformed, "invalid action bundle operation index");
    if (!operation.identity.actor.valid() || operation.identity.counter == 0)
        return Error(ErrorCode::invalid_identity, "remote operation has an invalid identity");

    const std::string bytes(operation.bytes.constData(),
                            static_cast<size_t>(operation.bytes.size()));
    auto decoded = Crdt::decode_operation(bytes);
    if (!decoded.ok()) return decoded.error();
    if (Crdt::op_identity(decoded.value()) != operation.identity)
        return Error(ErrorCode::actor_conflict,
                     "remote envelope identity does not match operation bytes");
    return {};
}

size_t ScopedOperationRouter::charge(const RemoteOperation& operation) const noexcept
{
    const size_t fixed = sizeof(Held) + sizeof(uint64_t) + sizeof(uint32_t) * 2;
    const size_t document = operation.scope.document.size();
    const size_t stream = operation.scope.stream.size();
    const size_t action = static_cast<size_t>(operation.action_id.size());
    const size_t payload = static_cast<size_t>(operation.bytes.size());
    size_t total = fixed;
    for (const size_t part : {document, stream, action, payload}) {
        if (part > std::numeric_limits<size_t>::max() - total)
            return std::numeric_limits<size_t>::max();
        total += part;
    }
    return total;
}

bool ScopedOperationRouter::hasCapacity(size_t bytes) const noexcept
{
    return m_held.size() < m_budget.maxHeldOperations
        && bytes <= m_budget.maxHeldBytes
        && m_heldBytes <= m_budget.maxHeldBytes - bytes;
}

ScopedTextStream* ScopedOperationRouter::findStream(
    const CollabText::PositionScope& scope) const noexcept
{
    const auto it = std::find_if(m_streams.cbegin(), m_streams.cend(),
        [&scope](const ScopedTextStream* stream) {
            return stream && stream->scope() == scope;
        });
    return it == m_streams.cend() ? nullptr : *it;
}

RouteResult ScopedOperationRouter::applyKnown(
    const RemoteOperation& operation, ScopedTextStream& stream,
    std::vector<CompletedRemoteOperation>& completed)
{
    auto applied = stream.applyRemote(operation);
    if (!applied.ok()) return {RouteState::rejected, applied.error()};
    switch (applied.value().kind()) {
    case CollabText::ApplyOutcomeKind::applied:
        completed.push_back({operation, CollabText::ApplyOutcomeKind::applied});
        return {RouteState::applied, std::nullopt};
    case CollabText::ApplyOutcomeKind::duplicate: {
        if (m_documentState) {
            auto accepted = m_documentState->projection().findAcceptedOperation(
                operation.scope.stream, operation.identity.actor,
                operation.identity.counter);
            if (!accepted.ok()) return {RouteState::rejected, accepted.error()};
            if (!accepted.value())
                return {RouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::history_unavailable,
                        "Core duplicate has no matching accepted MAB identity")};
            if (!exactAcceptedEnvelope(*accepted.value(), operation))
                return {RouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::actor_conflict,
                        "accepted operation identity arrived with different MAB envelope bytes")};
        }
        completed.push_back({operation, CollabText::ApplyOutcomeKind::duplicate});
        return {RouteState::duplicate, std::nullopt};
    }
    case CollabText::ApplyOutcomeKind::deferred:
        return {RouteState::deferred, std::nullopt};
    case CollabText::ApplyOutcomeKind::rejected:
        return {RouteState::rejected, applied.value().error()};
    }
    return {RouteState::rejected,
            CollabText::Error(CollabText::ErrorCode::malformed,
                              "unknown CollabText apply outcome")};
}

CollabText::Result<std::vector<CompletedRemoteOperation>>
ScopedOperationRouter::registerStream(ScopedTextStream& stream)
{
    const auto& scope = stream.scope();
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "router has an invalid document scope");
    if (scope.document != m_document)
        return CollabText::Error(CollabText::ErrorCode::wrong_document,
                                 "registered text stream belongs to another document");
    if (scope.epoch != m_epoch)
        return CollabText::Error(CollabText::ErrorCode::wrong_epoch,
                                 "registered text stream belongs to another epoch");
    if (scope.primitive != CollabText::Primitive::text)
        return CollabText::Error(CollabText::ErrorCode::wrong_primitive,
                                 "registered stream is not a text stream");
    if (!parseBlockTextStreamName(scope.stream).ok())
        return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                                 "registered text stream is not a canonical block stream");
    if (findStream(scope))
        return CollabText::Error(CollabText::ErrorCode::writer_conflict,
                                 "a stream with this scope is already registered");
    m_streams.push_back(&stream);

    std::vector<CompletedRemoteOperation> completed;
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (it->state != HeldState::unknown_stream
            || it->operation.scope != scope) {
            ++it;
            continue;
        }
        const RouteResult result = applyKnown(it->operation, stream, completed);
        if (result.state == RouteState::applied
            || result.state == RouteState::duplicate) {
            m_heldBytes -= it->chargedBytes;
            it = m_held.erase(it);
        } else {
            it->state = result.state == RouteState::deferred
                ? HeldState::deferred : HeldState::rejected;
            it->error = result.error;
            ++it;
        }
    }
    retryDeferred(scope, stream, completed);
    return completed;
}

void ScopedOperationRouter::unregisterStream(ScopedTextStream& stream) noexcept
{
    std::erase(m_streams, &stream);
}

RouteResult ScopedOperationRouter::offer(
    const RemoteOperation& operation,
    std::vector<CompletedRemoteOperation>& completed)
{
    bool identityConflict = false;
    for (const auto& held : m_held) {
        if (held.operation.scope != operation.scope
            || held.operation.identity != operation.identity)
            continue;
        if (sameEnvelope(held.operation, operation))
            return {held.state == HeldState::unknown_stream
                        ? RouteState::held_for_unknown_stream
                        : held.state == HeldState::deferred
                            ? RouteState::deferred : RouteState::rejected,
                    held.error};
        identityConflict = true;
    }

    if (identityConflict) {
        if (m_documentState) {
            auto accepted = m_documentState->projection().findAcceptedOperation(
                operation.scope.stream, operation.identity.actor,
                operation.identity.counter);
            if (!accepted.ok()) return {RouteState::rejected, accepted.error()};
            if (accepted.value()
                && exactAcceptedEnvelope(*accepted.value(), operation)) {
                completed.push_back({operation, CollabText::ApplyOutcomeKind::duplicate});
                return {RouteState::duplicate, std::nullopt};
            }
        }
        const size_t bytes = charge(operation);
        if (!hasCapacity(bytes)) return {RouteState::backpressured, std::nullopt};
        const auto error = CollabText::Error(
            CollabText::ErrorCode::actor_conflict,
            "same operation identity arrived with different envelope bytes");
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::rejected, bytes, error});
        return {RouteState::rejected, error};
    }

    auto valid = validate(operation);
    if (!valid.ok()) return {RouteState::rejected, valid.error()};

    const size_t bytes = charge(operation);
    if (!hasCapacity(bytes)) return {RouteState::backpressured, std::nullopt};

    ScopedTextStream* stream = findStream(operation.scope);
    if (!stream) {
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::unknown_stream, bytes,
                          std::nullopt});
        return {RouteState::held_for_unknown_stream, std::nullopt};
    }

    const auto scope = operation.scope;
    const RouteResult result = applyKnown(operation, *stream, completed);
    if (result.state == RouteState::deferred || result.state == RouteState::rejected) {
        if (result.state == RouteState::rejected && !hasCapacity(bytes))
            return {RouteState::backpressured, std::nullopt};
        m_heldBytes += bytes;
        m_held.push_back({operation,
            result.state == RouteState::deferred ? HeldState::deferred : HeldState::rejected,
            bytes, result.error});
    } else {
        retryDeferred(scope, *stream, completed);
    }
    return result;
}

std::vector<QuarantinedRemoteOperation> ScopedOperationRouter::quarantined() const
{
    std::vector<QuarantinedRemoteOperation> result;
    for (const auto& held : m_held) {
        if (held.state == HeldState::rejected && held.error)
            result.push_back({held.operation, *held.error});
    }
    return result;
}

CollabText::Result<size_t> ScopedOperationRouter::restoreQuarantine(
    const std::vector<QByteArray>& records)
{
    using namespace CollabText;
    std::vector<Held> pending;
    size_t pendingBytes = 0;
    for (const auto& bytes : records) {
        auto decoded = QuarantineRecordCodec::decode(bytes);
        if (!decoded.ok()) return decoded.error();
        const auto& record = decoded.value();
        if (record.primitive != QuarantinePrimitive::text) continue;
        if (record.document != m_document)
            return Error(ErrorCode::wrong_document,
                         "text quarantine belongs to another document");
        if (record.epoch != m_epoch)
            return Error(ErrorCode::wrong_epoch,
                         "text quarantine belongs to another epoch");
        RemoteOperation operation{
            PositionScope{record.document, record.stream, Primitive::text, record.epoch},
            {record.actor, record.counter}, record.payload, record.deliverySequence,
            record.actionId, record.bundleIndex, record.bundleOperationCount};
        auto checked = validate(operation);
        if (!checked.ok()) return checked.error();
        const auto duplicate = std::find_if(m_held.begin(), m_held.end(),
            [this, &operation](const Held& held) {
                return sameEnvelope(held.operation, operation);
            });
        if (duplicate != m_held.end()) continue;
        const auto batchDuplicate = std::find_if(pending.begin(), pending.end(),
            [this, &operation](const Held& held) {
                return sameEnvelope(held.operation, operation);
            });
        if (batchDuplicate != pending.end()) continue;
        const size_t bytesCharged = charge(operation);
        if (bytesCharged > std::numeric_limits<size_t>::max() - pendingBytes)
            return Error(ErrorCode::limit_exceeded,
                         "restored text quarantine size overflowed");
        pendingBytes += bytesCharged;
        pending.push_back({operation, HeldState::rejected, bytesCharged, record.reason});
    }
    if (pending.size() > m_budget.maxHeldOperations
            - std::min(m_held.size(), m_budget.maxHeldOperations)
        || pendingBytes > m_budget.maxHeldBytes
            - std::min(m_heldBytes, m_budget.maxHeldBytes))
        return Error(ErrorCode::deferred_queue_full,
                     "text quarantine recovery exceeds router capacity");
    for (auto& held : pending) m_held.push_back(std::move(held));
    m_heldBytes += pendingBytes;
    return pending.size();
}

CollabText::Result<void> ScopedOperationRouter::acknowledgeQuarantine(
    const RemoteOperation& exactEnvelope)
{
    const auto it = std::find_if(m_held.begin(), m_held.end(),
        [this, &exactEnvelope](const Held& held) {
            return held.state == HeldState::rejected
                && sameEnvelope(held.operation, exactEnvelope);
        });
    if (it == m_held.end())
        return CollabText::Error(CollabText::ErrorCode::not_found,
                                 "text quarantine envelope is not held");
    if (!m_documentState)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "text quarantine requires a provider-backed document state");
    if (!it->error)
        return CollabText::Error(CollabText::ErrorCode::corrupt_record,
                                 "rejected text envelope has no quarantine reason");
    QuarantineRecord record;
    record.document = it->operation.scope.document;
    record.epoch = it->operation.scope.epoch;
    record.stream = it->operation.scope.stream;
    record.primitive = QuarantinePrimitive::text;
    record.actor = it->operation.identity.actor;
    record.counter = it->operation.identity.counter;
    record.payload = it->operation.bytes;
    record.deliverySequence = it->operation.delivery_sequence;
    record.actionId = it->operation.action_id;
    record.bundleIndex = it->operation.bundle_index;
    record.bundleOperationCount = it->operation.bundle_operation_count;
    record.reason = *it->error;
    auto encoded = QuarantineRecordCodec::encode(record);
    if (!encoded.ok()) return encoded.error();
    auto stored = m_documentState->storage().storeQuarantinedOperation(encoded.value());
    if (!stored.ok()) return stored.error();
    m_heldBytes -= it->chargedBytes;
    m_held.erase(it);
    return {};
}

bool ScopedOperationRouter::sameEnvelope(const RemoteOperation& left,
                                         const RemoteOperation& right) const noexcept
{
    return left.scope == right.scope && left.identity == right.identity
        && left.bytes == right.bytes
        && left.delivery_sequence == right.delivery_sequence
        && left.action_id == right.action_id
        && left.bundle_index == right.bundle_index
        && left.bundle_operation_count == right.bundle_operation_count;
}

void ScopedOperationRouter::retryDeferred(
    const CollabText::PositionScope& scope, ScopedTextStream& stream,
    std::vector<CompletedRemoteOperation>& completed)
{
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (it->state != HeldState::deferred || it->operation.scope != scope) {
            ++it;
            continue;
        }
        const RouteResult result = applyKnown(it->operation, stream, completed);
        if (result.state == RouteState::applied
            || result.state == RouteState::duplicate) {
            m_heldBytes -= it->chargedBytes;
            it = m_held.erase(it);
        } else {
            if (result.state == RouteState::rejected) {
                it->state = HeldState::rejected;
                it->error = result.error;
            }
            ++it;
        }
    }
}

size_t ScopedOperationRouter::heldOperationCount() const noexcept
{
    return m_held.size();
}

size_t ScopedOperationRouter::heldByteCount() const noexcept
{
    return m_heldBytes;
}

} // namespace Markoff::VNext
