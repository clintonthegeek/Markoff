// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ReplicatedMapStream.h>

#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return Markoff::TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool actorLess(const ActorId& left, const ActorId& right)
{
    const auto leftBytes = left.bytes();
    const auto rightBytes = right.bytes();
    return std::lexicographical_compare(leftBytes.begin(), leftBytes.end(),
                                        rightBytes.begin(), rightBytes.end());
}

bool stampLess(uint64_t leftCounter, const ActorId& leftActor,
               uint64_t rightCounter, const ActorId& rightActor)
{
    if (leftCounter != rightCounter) return leftCounter < rightCounter;
    return actorLess(leftActor, rightActor);
}

QByteArray projectionKey(const QByteArray& key,
                        const std::optional<BlockIdentity>& targetBlock)
{
    QByteArray result;
    result.reserve((targetBlock ? 25 : 1) + key.size());
    result.append(targetBlock ? char(1) : char(0));
    if (targetBlock) {
        const auto actor = targetBlock->actor.bytes();
        result.append(reinterpret_cast<const char*>(actor.data()),
                      static_cast<qsizetype>(actor.size()));
        for (int i = 0; i != 8; ++i)
            result.append(static_cast<char>(targetBlock->counter >> (56 - i * 8)));
    }
    result.append(key);
    return result;
}

} // namespace

CollabText::Result<ActionOperation> toActionOperation(
    const LocalMapOperation& operation)
{
    auto stream = mapStreamName(operation.scope.map);
    if (!stream.ok()) return stream.error();
    if (operation.operation.map != operation.scope.map
        || operation.operation.counter == 0
        || !operation.operation.producer.valid())
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "local map operation does not match its stream scope");
    return ActionOperation{stream.value(), OperationPrimitive::markoffMap,
                           operation.targetBlock, operation.operation.producer,
                           operation.operation.counter, OperationCodec::markoffOwned,
                           operation.bytes};
}

CollabText::Result<HistoryGroupAssociation> toHistoryAssociation(
    const LocalMapOperation& operation)
{
    auto action = toActionOperation(operation);
    if (!action.ok()) return action.error();
    auto decoded = MarkoffMapCodec::decode(operation.bytes);
    if (!decoded.ok()) return decoded.error();
    const auto& payload = decoded.value();
    if (payload.map != operation.operation.map || payload.key != operation.operation.key
        || payload.value != operation.operation.value
        || payload.producer != operation.operation.producer
        || payload.counter != operation.operation.counter
        || payload.tombstone != operation.operation.tombstone)
        return CollabText::Error(CollabText::ErrorCode::pin_conflict,
            "map history bytes differ from their local operation metadata");
    if (!operation.priorValueExisted && !operation.priorValue.isEmpty())
        return CollabText::Error(CollabText::ErrorCode::malformed,
            "absent map prior state cannot carry a value");

    const CollabText::Crdt::Identity identity{
        operation.operation.producer, operation.operation.counter};
    HistoryGroupAssociation association;
    association.stream = action.value().stream;
    association.primitive = OperationPrimitive::markoffMap;
    association.groupId = identity;
    association.operationIds.push_back(identity);
    association.hasMapPriorState = true;
    association.mapPriorValueExisted = operation.priorValueExisted;
    association.mapPriorValue = operation.priorValue;
    return association;
}

ReplicatedMapStream::ReplicatedMapStream(
    DocumentStorageSession& session, MapScope scope)
    : m_session(&session), m_scope(std::move(scope)) {}

ReplicatedMapStream::~ReplicatedMapStream() = default;

CollabText::Result<std::unique_ptr<ReplicatedMapStream>> ReplicatedMapStream::create(
    DocumentStorageSession& session, MapScope scope)
{
    using namespace CollabText;
    if (scope.document.empty() || scope.document.size() > 256
        || !validUtf8(scope.document) || scope.epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "map stream requires a valid document and epoch");
    const QString documentKey = QString::fromUtf8(
        scope.document.data(), static_cast<qsizetype>(scope.document.size()));
    if (session.scope().documentKey != documentKey
        || session.scope().epoch != scope.epoch)
        return Error(ErrorCode::wrong_document,
                     "provider session does not match map stream scope");
    auto streamName = mapStreamName(scope.map);
    if (!streamName.ok()) return streamName.error();
    return std::unique_ptr<ReplicatedMapStream>(new ReplicatedMapStream(
        session, std::move(scope)));
}

const MapScope& ReplicatedMapStream::scope() const noexcept { return m_scope; }

const CollabText::ActorId& ReplicatedMapStream::actor() const noexcept
{
    return m_session->actor();
}

CollabText::Result<void> ReplicatedMapStream::completeReplay()
{
    if (m_replayComplete)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "map replay barrier was already completed");
    m_replayComplete = true;
    return {};
}

std::optional<QByteArray> ReplicatedMapStream::get(
    const QByteArray& key, std::optional<BlockIdentity> targetBlock) const
{
    const auto found = m_entries.constFind(projectionKey(key, targetBlock));
    if (found == m_entries.cend() || found->tombstone) return std::nullopt;
    return found->value;
}

std::vector<MapProjectionEntry> ReplicatedMapStream::values() const
{
    std::vector<MapProjectionEntry> result;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (!it->tombstone) {
            result.push_back({it->targetBlock, it->key, it->value});
        }
    return result;
}

CollabText::Result<LocalMapOperation> ReplicatedMapStream::set(
    const QByteArray& key, const QByteArray& value,
    std::optional<BlockIdentity> targetBlock)
{
    return write(key, value, false, std::move(targetBlock));
}

CollabText::Result<LocalMapOperation> ReplicatedMapStream::remove(
    const QByteArray& key, std::optional<BlockIdentity> targetBlock)
{
    return write(key, {}, true, std::move(targetBlock));
}

CollabText::Result<LocalMapOperation> ReplicatedMapStream::compensate(
    const LocalMapOperation& source)
{
    using namespace CollabText;
    if (source.scope != m_scope)
        return Error(ErrorCode::wrong_stream,
                     "map compensation belongs to another map scope");
    if (source.operation.producer != actor())
        return Error(ErrorCode::invalid_identity,
                     "map compensation is limited to this writer's operations");
    auto decoded = MarkoffMapCodec::decode(source.bytes);
    if (!decoded.ok()) return decoded.error();
    const auto& operation = decoded.value();
    if (operation.map != source.operation.map || operation.key != source.operation.key
        || operation.value != source.operation.value
        || operation.producer != source.operation.producer
        || operation.counter != source.operation.counter
        || operation.tombstone != source.operation.tombstone)
        return Error(ErrorCode::pin_conflict,
                     "map compensation bytes differ from their operation metadata");
    if (operation.map != m_scope.map)
        return Error(ErrorCode::wrong_stream,
                     "map compensation operation belongs to another stream");
    auto binding = MarkoffMapValueCodec::validateBinding(operation, source.targetBlock);
    if (!binding.ok()) return binding.error();

    const auto storageKey = projectionKey(operation.key, source.targetBlock);
    const auto current = m_entries.constFind(storageKey);
    if (current == m_entries.cend() || current->actor != operation.producer
        || current->counter != operation.counter
        || current->tombstone != operation.tombstone || current->value != operation.value)
        return Error(ErrorCode::pin_conflict,
                     "map compensation would overwrite a later LWW operation");
    return write(operation.key, source.priorValue,
                 !source.priorValueExisted, source.targetBlock);
}

CollabText::Result<LocalMapOperation> ReplicatedMapStream::write(
    const QByteArray& key, const QByteArray& value, bool tombstone,
    std::optional<BlockIdentity> targetBlock)
{
    using namespace CollabText;
    if (!m_replayComplete)
        return Error(ErrorCode::awaiting_state,
                     "map writes require replay of the accepted log prefix");
    MarkoffMapOperation operation{m_scope.map, key, value, actor(), 1, tombstone};
    auto preflight = MarkoffMapCodec::encode(operation);
    if (!preflight.ok()) return preflight.error();
    auto binding = MarkoffMapValueCodec::validateBinding(operation, targetBlock);
    if (!binding.ok()) return binding.error();

    const auto streamName = mapStreamName(m_scope.map);
    if (!streamName.ok()) return streamName.error();
    const QByteArray storageKey = projectionKey(key, targetBlock);
    auto found = m_entries.find(storageKey);
    auto nextCounter = m_session->nextCounter(QString::fromStdString(streamName.value()));
    if (!nextCounter.ok()) return nextCounter.error();
    const uint64_t expectedCounter = nextCounter.value();
    for (const auto& gap : m_committedGaps) {
        if (gap.actor != actor()) continue;
        const uint64_t last = gap.first + gap.count - 1;
        if (expectedCounter <= last)
            return Error(ErrorCode::counter_regression,
                         "local map allocation has not advanced beyond committed gap coverage");
    }
    if (found != m_entries.end()
        && !stampLess(found->counter, found->actor,
                      expectedCounter, actor()))
        return Error(ErrorCode::counter_regression,
                     "next map identity would not win against the observed entry");
    operation.counter = expectedCounter;
    auto encoded = MarkoffMapCodec::encode(operation);
    if (!encoded.ok()) return encoded.error();

    auto reservation = m_session->reserve(
        QString::fromStdString(streamName.value()), 1);
    if (!reservation.ok()) return reservation.error();
    if (reservation.value().stream != QString::fromStdString(streamName.value())
        || reservation.value().actor != actor()
        || reservation.value().count != 1 || reservation.value().first == 0)
        return Error(ErrorCode::invalid_identity,
                     "provider returned a mismatched map reservation");
    const uint64_t allocatedCounter = reservation.value().first;
    if (allocatedCounter != expectedCounter)
        return Error(ErrorCode::counter_regression,
                     "reserved map identity changed after its preflight check");

    const bool priorValueExisted = found != m_entries.end() && !found->tombstone;
    const QByteArray priorValue = priorValueExisted ? found->value : QByteArray{};
    const ScopedReservation exactReservation = reservation.value();
    Entry entry{targetBlock, key, value, operation.producer,
                operation.counter, tombstone};
    m_entries.insert(storageKey, std::move(entry));
    return LocalMapOperation{m_scope, std::move(targetBlock),
                             std::move(operation), std::move(encoded).value(),
                             priorValueExisted, priorValue, exactReservation};
}

CollabText::Result<MapApplyKind> ReplicatedMapStream::applyRemote(
    const QByteArray& operationBytes,
    std::optional<BlockIdentity> targetBlock)
{
    using namespace CollabText;
    auto decoded = MarkoffMapCodec::decode(operationBytes);
    if (!decoded.ok()) return decoded.error();
    auto& operation = decoded.value();
    if (operation.map != m_scope.map)
        return Error(ErrorCode::wrong_stream,
                     "MMP operation belongs to another Markoff map stream");
    auto binding = MarkoffMapValueCodec::validateBinding(operation, targetBlock);
    if (!binding.ok()) return binding.error();
    for (const auto& gap : m_committedGaps) {
        if (gap.actor == operation.producer
            && operation.counter >= gap.first
            && operation.counter - gap.first < gap.count)
            return Error(ErrorCode::invalid_identity,
                         "map operation identity is covered by committed MMG/1 metadata");
    }
    auto streamName = mapStreamName(m_scope.map);
    if (!streamName.ok()) return streamName.error();
    auto observed = m_session->observeCounter(
        QString::fromStdString(streamName.value()), operation.counter);
    if (!observed.ok()) return observed.error();

    const QByteArray storageKey = projectionKey(operation.key, targetBlock);
    const auto found = m_entries.constFind(storageKey);
    if (found != m_entries.cend()) {
        const Entry& current = found.value();
        if (current.counter == operation.counter && current.actor == operation.producer) {
            if (current.tombstone == operation.tombstone
                && current.value == operation.value)
                return MapApplyKind::duplicate;
            return Error(ErrorCode::actor_conflict,
                         "map identity was reused with different key state");
        }
        if (!stampLess(current.counter, current.actor,
                       operation.counter, operation.producer))
            return MapApplyKind::stale;
    }

    Entry entry{targetBlock, operation.key, operation.value, operation.producer,
                operation.counter, operation.tombstone};
    m_entries.insert(storageKey, std::move(entry));
    return MapApplyKind::applied;
}

CollabText::Result<void> ReplicatedMapStream::applyCommittedCounterGap(
    const QByteArray& scopedRecord, const QByteArray& expectedAcceptedPrior)
{
    using namespace CollabText;
    auto decoded = MapCounterGapCodec::decode(
        scopedRecord, m_scope.document, m_scope.map, m_scope.epoch,
        expectedAcceptedPrior);
    if (!decoded.ok()) return decoded.error();

    const uint64_t last = decoded.value().first + decoded.value().count - 1;
    for (const auto& existing : m_committedGaps) {
        if (existing.actor != decoded.value().actor) continue;
        const uint64_t existingLast = existing.first + existing.count - 1;
        const bool overlaps = decoded.value().first <= existingLast
            && existing.first <= last;
        if (!overlaps) continue;
        if (existing.first == decoded.value().first
            && existing.count == decoded.value().count
            && existing.acceptedPrior == decoded.value().acceptedPrior)
            return {};
        return Error(ErrorCode::pin_conflict,
                     "MMG/1 interval overlaps different committed map-gap metadata");
    }
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const Entry& entry = it.value();
        if (entry.actor == decoded.value().actor
            && entry.counter >= decoded.value().first
            && entry.counter <= last)
            return Error(ErrorCode::invalid_identity,
                         "MMG/1 interval covers an operation already applied to this map");
    }
    const auto stream = mapStreamName(m_scope.map);
    if (!stream.ok()) return stream.error();
    if (decoded.value().actor == actor()
        && last != std::numeric_limits<uint64_t>::max()) {
        auto observed = m_session->observeCounter(
            QString::fromStdString(stream.value()), last);
        if (!observed.ok()) return observed.error();
    }
    m_committedGaps.push_back(std::move(decoded).value());
    return {};
}

CollabText::Result<void> ReplicatedMapStream::replaceStateFrom(
    ReplicatedMapStream& replayed)
{
    if (m_session != replayed.m_session || m_scope != replayed.m_scope)
        return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                                 "cannot refresh a map stream from another scope");
    m_entries.swap(replayed.m_entries);
    m_committedGaps.swap(replayed.m_committedGaps);
    std::swap(m_replayComplete, replayed.m_replayComplete);
    return {};
}

} // namespace Markoff::VNext
