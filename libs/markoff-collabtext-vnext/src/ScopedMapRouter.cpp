// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ScopedMapRouter.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/QuarantineRecordCodec.h>
#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/StreamNames.h>

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
                           const RemoteMapOperation& operation,
                           const MarkoffMapOperation& decoded,
                           const std::string& route)
{
    const auto& accepted = original.operation;
    return accepted.primitive == OperationPrimitive::markoffMap
        && accepted.codec == OperationCodec::markoffOwned
        && accepted.targetBlock == operation.targetBlock
        && accepted.stream == route
        && accepted.producer == decoded.producer
        && accepted.counter == decoded.counter
        && accepted.payload == operation.bytes
        && original.deliverySequence == operation.deliverySequence
        && original.actionId == operation.actionId
        && original.operationIndex == operation.bundleIndex
        && original.operationCount == operation.bundleOperationCount;
}

} // namespace

ScopedMapRouter::ScopedMapRouter(std::string document, uint64_t epoch,
                                 RouterBudget budget)
    : m_document(std::move(document)), m_epoch(epoch), m_budget(budget) {}

CollabText::Result<void> ScopedMapRouter::validate(
    const RemoteMapOperation& operation) const
{
    using namespace CollabText;
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return Error(ErrorCode::invalid_identity, "map router has an invalid document scope");
    if (operation.scope.document != m_document)
        return Error(ErrorCode::wrong_document, "remote map has the wrong document key");
    if (operation.scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_epoch, "remote map has the wrong epoch");
    auto stream = mapStreamName(operation.scope.map);
    if (!stream.ok()) return stream.error();
    if (operation.deliverySequence == 0 || operation.actionId.isEmpty()
        || static_cast<size_t>(operation.actionId.size()) > kMaxActionIdBytes)
        return Error(ErrorCode::malformed, "remote map has invalid action metadata");
    if (operation.bundleOperationCount == 0
        || operation.bundleOperationCount > kMaxBundleOperationCount
        || operation.bundleIndex >= operation.bundleOperationCount)
        return Error(ErrorCode::malformed, "invalid map action bundle operation index");

    auto decoded = MarkoffMapCodec::decode(operation.bytes);
    if (!decoded.ok()) return decoded.error();
    if (decoded.value().map != operation.scope.map)
        return Error(ErrorCode::wrong_stream,
                     "remote MMP payload does not match its map stream");
    auto binding = MarkoffMapValueCodec::validateBinding(
        decoded.value(), operation.targetBlock);
    if (!binding.ok()) return binding.error();
    return {};
}

size_t ScopedMapRouter::charge(const RemoteMapOperation& operation) const noexcept
{
    const size_t fixed = sizeof(Held) + sizeof(uint64_t) + sizeof(uint32_t) * 2;
    const size_t document = operation.scope.document.size();
    const size_t map = operation.scope.map.size();
    const size_t action = static_cast<size_t>(operation.actionId.size());
    const size_t payload = static_cast<size_t>(operation.bytes.size());
    size_t total = fixed;
    for (const size_t part : {document, map, action, payload}) {
        if (part > std::numeric_limits<size_t>::max() - total)
            return std::numeric_limits<size_t>::max();
        total += part;
    }
    if (operation.targetBlock) {
        if (total > std::numeric_limits<size_t>::max() - 25)
            return std::numeric_limits<size_t>::max();
        total += 25;
    }
    return total;
}

bool ScopedMapRouter::hasCapacity(size_t bytes) const noexcept
{
    return m_held.size() < m_budget.maxHeldOperations
        && bytes <= m_budget.maxHeldBytes
        && m_heldBytes <= m_budget.maxHeldBytes - bytes;
}

bool ScopedMapRouter::sameEnvelope(const RemoteMapOperation& left,
                                   const RemoteMapOperation& right) const noexcept
{
    return left.scope == right.scope && left.targetBlock == right.targetBlock
        && left.bytes == right.bytes
        && left.deliverySequence == right.deliverySequence
        && left.actionId == right.actionId
        && left.bundleIndex == right.bundleIndex
        && left.bundleOperationCount == right.bundleOperationCount;
}

ReplicatedMapStream* ScopedMapRouter::findStream(const MapScope& scope) const noexcept
{
    const auto found = std::find_if(m_streams.cbegin(), m_streams.cend(),
        [&scope](const ReplicatedMapStream* stream) {
            return stream && stream->scope() == scope;
        });
    return found == m_streams.cend() ? nullptr : *found;
}

MapRouteResult ScopedMapRouter::applyKnown(
    const RemoteMapOperation& operation, ReplicatedMapStream& stream,
    std::vector<CompletedMapOperation>& completed)
{
    auto result = stream.applyRemote(operation.bytes, operation.targetBlock);
    if (!result.ok()) return {MapRouteState::rejected, result.error()};
    switch (result.value()) {
    case MapApplyKind::applied:
        completed.push_back({operation, result.value()});
        return {MapRouteState::applied, std::nullopt};
    case MapApplyKind::stale:
        completed.push_back({operation, result.value()});
        return {MapRouteState::stale, std::nullopt};
    case MapApplyKind::duplicate: {
        if (m_documentState) {
            auto decoded = MarkoffMapCodec::decode(operation.bytes);
            if (!decoded.ok()) return {MapRouteState::rejected, decoded.error()};
            const auto stream = mapStreamName(operation.scope.map);
            if (!stream.ok()) return {MapRouteState::rejected, stream.error()};
            auto accepted = m_documentState->projection().findAcceptedOperation(
                stream.value(), decoded.value().producer, decoded.value().counter);
            if (!accepted.ok()) return {MapRouteState::rejected, accepted.error()};
            if (!accepted.value())
                return {MapRouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::history_unavailable,
                        "Core map duplicate has no matching accepted MAB identity")};
            if (!exactAcceptedEnvelope(*accepted.value(), operation,
                                       decoded.value(), stream.value()))
                return {MapRouteState::rejected,
                    CollabText::Error(CollabText::ErrorCode::actor_conflict,
                        "accepted map identity arrived with different MAB envelope bytes")};
        }
        completed.push_back({operation, result.value()});
        return {MapRouteState::duplicate, std::nullopt};
    }
    }
    return {MapRouteState::rejected,
            CollabText::Error(CollabText::ErrorCode::malformed,
                              "unknown Markoff map apply outcome")};
}

CollabText::Result<std::vector<CompletedMapOperation>>
ScopedMapRouter::registerStream(ReplicatedMapStream& stream)
{
    using namespace CollabText;
    const auto& scope = stream.scope();
    if (m_document.empty() || m_document.size() > kMaxRouteStringBytes
        || !validUtf8(m_document) || m_epoch == 0)
        return Error(ErrorCode::invalid_identity, "map router has an invalid document scope");
    if (scope.document != m_document)
        return Error(ErrorCode::wrong_document,
                     "registered map belongs to another document");
    if (scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_epoch, "registered map belongs to another epoch");
    if (!mapStreamName(scope.map).ok())
        return Error(ErrorCode::unknown_stream, "registered map name is not canonical");
    if (findStream(scope))
        return Error(ErrorCode::writer_conflict,
                     "a map stream with this scope is already registered");
    m_streams.push_back(&stream);

    std::vector<CompletedMapOperation> completed;
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (it->state != HeldState::unknown_stream || it->operation.scope != scope) {
            ++it;
            continue;
        }
        const MapRouteResult result = applyKnown(it->operation, stream, completed);
        if (result.state == MapRouteState::applied
            || result.state == MapRouteState::stale
            || result.state == MapRouteState::duplicate) {
            m_heldBytes -= it->chargedBytes;
            it = m_held.erase(it);
        } else {
            it->state = HeldState::rejected;
            it->error = result.error;
            ++it;
        }
    }
    return completed;
}

void ScopedMapRouter::unregisterStream(ReplicatedMapStream& stream) noexcept
{
    std::erase(m_streams, &stream);
}

MapRouteResult ScopedMapRouter::offer(
    const RemoteMapOperation& operation,
    std::vector<CompletedMapOperation>& completed)
{
    auto valid = validate(operation);
    if (!valid.ok()) return {MapRouteState::rejected, valid.error()};

    auto decoded = MarkoffMapCodec::decode(operation.bytes);
    if (!decoded.ok()) return {MapRouteState::rejected, decoded.error()};
    const auto identity = CollabText::Crdt::Identity(decoded.value().producer,
                                                     decoded.value().counter);
    for (const auto& held : m_held) {
        if (held.operation.scope != operation.scope) continue;
        auto heldDecoded = MarkoffMapCodec::decode(held.operation.bytes);
        if (!heldDecoded.ok()
            || heldDecoded.value().producer != identity.actor
            || heldDecoded.value().counter != identity.counter)
            continue;
        if (sameEnvelope(held.operation, operation))
            return {held.state == HeldState::unknown_stream
                        ? MapRouteState::heldForUnknownStream
                        : MapRouteState::rejected,
                    held.error};
        if (m_documentState) {
            const auto route = mapStreamName(operation.scope.map);
            if (!route.ok()) return {MapRouteState::rejected, route.error()};
            auto accepted = m_documentState->projection().findAcceptedOperation(
                route.value(), identity.actor, identity.counter);
            if (!accepted.ok()) return {MapRouteState::rejected, accepted.error()};
            if (accepted.value()
                && exactAcceptedEnvelope(*accepted.value(), operation,
                                         decoded.value(), route.value())) {
                completed.push_back({operation, MapApplyKind::duplicate});
                return {MapRouteState::duplicate, std::nullopt};
            }
        }
        const size_t bytes = charge(operation);
        if (!hasCapacity(bytes)) return {MapRouteState::backpressured, std::nullopt};
        const auto error = CollabText::Error(
            CollabText::ErrorCode::actor_conflict,
            "same map operation identity arrived with different envelope bytes");
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::rejected, bytes, error});
        return {MapRouteState::rejected, error};
    }

    ReplicatedMapStream* stream = findStream(operation.scope);
    if (!stream) {
        const size_t bytes = charge(operation);
        if (!hasCapacity(bytes))
            return {MapRouteState::backpressured, std::nullopt};
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::unknown_stream, bytes,
                          std::nullopt});
        return {MapRouteState::heldForUnknownStream, std::nullopt};
    }

    const MapRouteResult result = applyKnown(operation, *stream, completed);
    if (result.state == MapRouteState::rejected) {
        const size_t bytes = charge(operation);
        if (!hasCapacity(bytes)) {
            // A rejected apply has no projection effect. Keep the envelope
            // caller-owned when quarantine cannot be retained within budget.
            return {MapRouteState::backpressured, std::nullopt};
        }
        m_heldBytes += bytes;
        m_held.push_back({operation, HeldState::rejected, bytes, result.error});
    }
    return result;
}

std::vector<QuarantinedMapOperation> ScopedMapRouter::quarantined() const
{
    std::vector<QuarantinedMapOperation> result;
    for (const auto& held : m_held)
        if (held.state == HeldState::rejected && held.error)
            result.push_back({held.operation, *held.error});
    return result;
}

CollabText::Result<size_t> ScopedMapRouter::restoreQuarantine(
    const std::vector<QByteArray>& records)
{
    using namespace CollabText;
    std::vector<Held> pending;
    size_t pendingBytes = 0;
    for (const auto& bytes : records) {
        auto decodedRecord = QuarantineRecordCodec::decode(bytes);
        if (!decodedRecord.ok()) return decodedRecord.error();
        const auto& record = decodedRecord.value();
        if (record.primitive != QuarantinePrimitive::map) continue;
        if (record.document != m_document)
            return Error(ErrorCode::wrong_document,
                         "map quarantine belongs to another document");
        if (record.epoch != m_epoch)
            return Error(ErrorCode::wrong_epoch,
                         "map quarantine belongs to another epoch");
        auto decodedOperation = MarkoffMapCodec::decode(record.payload);
        if (!decodedOperation.ok()) return decodedOperation.error();
        if (decodedOperation.value().producer != record.actor
            || decodedOperation.value().counter != record.counter)
            return Error(ErrorCode::actor_conflict,
                         "map quarantine identity differs from its operation bytes");
        RemoteMapOperation operation{
            MapScope{record.document, record.stream, record.epoch},
            record.targetBlock, record.payload, record.deliverySequence,
            record.actionId, record.bundleIndex, record.bundleOperationCount};
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
                         "restored map quarantine size overflowed");
        pendingBytes += bytesCharged;
        pending.push_back({operation, HeldState::rejected, bytesCharged, record.reason});
    }
    if (pending.size() > m_budget.maxHeldOperations
            - std::min(m_held.size(), m_budget.maxHeldOperations)
        || pendingBytes > m_budget.maxHeldBytes
            - std::min(m_heldBytes, m_budget.maxHeldBytes))
        return Error(ErrorCode::deferred_queue_full,
                     "map quarantine recovery exceeds router capacity");
    for (auto& held : pending) m_held.push_back(std::move(held));
    m_heldBytes += pendingBytes;
    return pending.size();
}

CollabText::Result<void> ScopedMapRouter::acknowledgeQuarantine(
    const RemoteMapOperation& exactEnvelope)
{
    const auto found = std::find_if(m_held.begin(), m_held.end(),
        [this, &exactEnvelope](const Held& held) {
            return held.state == HeldState::rejected
                && sameEnvelope(held.operation, exactEnvelope);
        });
    if (found == m_held.end())
        return CollabText::Error(CollabText::ErrorCode::not_found,
                                 "map quarantine envelope is not held");
    if (!m_documentState)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "map quarantine requires a provider-backed document state");
    if (!found->error)
        return CollabText::Error(CollabText::ErrorCode::corrupt_record,
                                 "rejected map envelope has no quarantine reason");
    auto decoded = MarkoffMapCodec::decode(found->operation.bytes);
    if (!decoded.ok()) return decoded.error();
    QuarantineRecord record;
    record.document = found->operation.scope.document;
    record.epoch = found->operation.scope.epoch;
    record.stream = found->operation.scope.map;
    record.primitive = QuarantinePrimitive::map;
    record.actor = decoded.value().producer;
    record.counter = decoded.value().counter;
    record.targetBlock = found->operation.targetBlock;
    record.payload = found->operation.bytes;
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

size_t ScopedMapRouter::heldOperationCount() const noexcept { return m_held.size(); }
size_t ScopedMapRouter::heldByteCount() const noexcept { return m_heldBytes; }

} // namespace Markoff::VNext
