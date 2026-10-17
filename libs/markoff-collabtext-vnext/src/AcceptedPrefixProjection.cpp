// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/AcceptedPrefixProjection.h>
#include <markoff/vnext/HistoryRetirement.h>

#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/MarkoffMapCodec.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <algorithm>
#include <set>
#include <tuple>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

std::string sessionDocument(const DocumentStorageSession& session)
{
    const QByteArray utf8 = session.scope().documentKey.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

Error replayError(const char* message)
{
    return Error(ErrorCode::history_unavailable, message);
}

Result<void> requireApplied(const ApplyOutcome& outcome, const char* message)
{
    if (outcome.kind() == ApplyOutcomeKind::applied) return {};
    if (outcome.kind() == ApplyOutcomeKind::rejected && outcome.error())
        return *outcome.error();
    return replayError(message);
}

} // namespace

Result<std::unique_ptr<AcceptedPrefixProjection>> AcceptedPrefixProjection::replay(
    DocumentStorageSession& session, const StorageRecoveryState& recovery)
{
    const std::string document = sessionDocument(session);
    if (document.empty() || session.scope().epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "accepted-prefix replay requires a valid document scope");
    auto revisions = validateAcceptedPrefixRevisions(recovery);
    if (!revisions.ok()) return revisions.error();

    auto projection = std::unique_ptr<AcceptedPrefixProjection>(
        new AcceptedPrefixProjection(recovery.acceptedRevision, document,
                                     session.scope().epoch));
    bool sawInitialImport = false;
    std::set<std::tuple<std::string, std::string, uint64_t>> acceptedOperationIdentities;
    std::vector<QByteArray> acceptedSourceSnapshots;
    if (recovery.localHistory) {
        const auto& privateRecord = *recovery.localHistory;
        const auto committed = std::find_if(recovery.acceptedRecords.begin(),
            recovery.acceptedRecords.end(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == privateRecord.committedTransactionId;
            });
        if (committed != recovery.acceptedRecords.end()) {
            auto ownerHistory = OwnerHistoryCodec::decode(privateRecord.bytes, document,
                session.scope().epoch, session.actor(), privateRecord.committedTransactionId,
                committed->priorRevision.token);
            if (ownerHistory.ok()) {
                for (const auto& event : ownerHistory.value().actionIndex.events) {
                    if (event.kind == ActionKind::replacement
                        && !event.sourceSnapshotBefore.isEmpty())
                        acceptedSourceSnapshots.push_back(event.sourceSnapshotBefore);
                }
            }
        }
    }
    for (const auto& record : recovery.acceptedRecords) {
        projection->m_acceptedRecords.push_back(record);
        if (record.format == QByteArrayLiteral("MAB/3")) {
            auto bundle = ActionBundleCodec::decode(record.bytes, document,
                                                     session.scope().epoch);
            if (!bundle.ok()) return bundle.error();
            if (bundle.value().actionId != record.transactionId)
                return Error(ErrorCode::pin_conflict,
                             "accepted MAB action ID differs from its provider transaction");
            auto checkedIdentities = ActionBundleCodec::validateOperationIdentities(
                bundle.value());
            if (!checkedIdentities.ok()) return checkedIdentities.error();
            for (size_t i = 0; i < bundle.value().operations.size(); ++i) {
                const auto& operation = bundle.value().operations[i];
                const auto actor = operation.producer.bytes();
                const std::string actorKey(
                    reinterpret_cast<const char*>(actor.data()), actor.size());
                if (!acceptedOperationIdentities.emplace(operation.stream, actorKey,
                                                          operation.counter).second)
                    return Error(ErrorCode::pin_conflict,
                                 "accepted prefix reuses a scoped operation identity across MAB records");
            }
            if (bundle.value().kind == ActionKind::initialImport) {
                if (sawInitialImport)
                    return Error(ErrorCode::history_unavailable,
                                 "accepted prefix contains more than one initial import");
                auto source = SourceSnapshotCodec::decode(
                    bundle.value().sourceSnapshot, document, session.scope().epoch);
                if (!source.ok()) return source.error();
                if (recovery.initialImport
                    && (recovery.initialImport->transactionId != record.transactionId
                        || recovery.initialImport->sourceMarkdown
                            != source.value().originalMarkdown))
                    return Error(ErrorCode::pin_conflict,
                                 "accepted initial MAB differs from the durable import intent");
                sawInitialImport = true;
                acceptedSourceSnapshots.push_back(bundle.value().sourceSnapshot);
            } else if (bundle.value().kind == ActionKind::replacement) {
                if (!sawInitialImport)
                    return Error(ErrorCode::history_unavailable,
                                 "replacement cannot precede the initial import");
                auto source = SourceSnapshotCodec::decode(
                    bundle.value().sourceSnapshot, document, session.scope().epoch);
                if (!source.ok()) return source.error();
                acceptedSourceSnapshots.push_back(bundle.value().sourceSnapshot);
            } else if (!sawInitialImport) {
                return Error(ErrorCode::history_unavailable,
                             "accepted edits cannot precede the initial import");
            } else if (!bundle.value().sourceSnapshot.isEmpty()) {
                auto source = SourceSnapshotCodec::decode(
                    bundle.value().sourceSnapshot, document, session.scope().epoch);
                if (!source.ok()) return source.error();
                if (std::find(acceptedSourceSnapshots.cbegin(),
                              acceptedSourceSnapshots.cend(),
                              bundle.value().sourceSnapshot)
                    == acceptedSourceSnapshots.cend())
                    return Error(ErrorCode::pin_conflict,
                                 "compensation source snapshot is not from accepted history");
                acceptedSourceSnapshots.push_back(bundle.value().sourceSnapshot);
            }
        }
        auto replayed = projection->replayRecord(session, record);
        if (!replayed.ok()) return replayed.error();
    }
    for (auto& [name, map] : projection->m_mapStreams) {
        Q_UNUSED(name)
        auto complete = map->completeReplay();
        if (!complete.ok()) return complete.error();
    }
    if (recovery.localHistory) {
        const auto& privateRecord = *recovery.localHistory;
        const auto committed = std::find_if(recovery.acceptedRecords.begin(),
            recovery.acceptedRecords.end(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == privateRecord.committedTransactionId;
            });
        if (committed == recovery.acceptedRecords.end()
            || committed->committedRevision != privateRecord.acceptedRevision)
            return Error(ErrorCode::pin_conflict,
                         "private owner history does not identify its accepted MAB revision");
        auto ownerHistory = OwnerHistoryCodec::decode(privateRecord.bytes, document,
            session.scope().epoch, session.actor(), privateRecord.committedTransactionId,
            committed->priorRevision.token);
        if (!ownerHistory.ok()) return ownerHistory.error();
        for (const auto& event : ownerHistory.value().actionIndex.events) {
            if (event.kind == ActionKind::replacement
                && !event.sourceSnapshotBefore.isEmpty())
                acceptedSourceSnapshots.push_back(event.sourceSnapshotBefore);
        }
        auto committedBundle = ActionBundleCodec::decode(
            committed->bytes, document, session.scope().epoch);
        if (!committedBundle.ok()) return committedBundle.error();
        using HistoryOperation = std::tuple<std::string, uint8_t, std::string, uint64_t>;
        const auto actorBytes = [](const ActorId& actor) {
            const auto bytes = actor.bytes();
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        std::set<HistoryOperation> expectedOperations;
        for (const auto& operation : committedBundle.value().operations)
            expectedOperations.emplace(operation.stream,
                static_cast<uint8_t>(operation.primitive), actorBytes(operation.producer),
                operation.counter);
        std::set<HistoryOperation> restoredOperations;
        size_t restoredOperationCount = 0;
        const auto& historyIndex = ownerHistory.value().actionIndex;
        if (historyIndex.events.empty()
            || historyIndex.events.back().actionId != committedBundle.value().actionId
            || historyIndex.events.back().kind != committedBundle.value().kind)
            return Error(ErrorCode::invalid_identity,
                         "owner history event does not describe its committed MAB action");
        size_t historyEventIndex = 0;
        for (auto record = recovery.acceptedRecords.begin(); ; ++record) {
            if (record->format == QByteArrayLiteral("MAB/3")) {
                auto acceptedBundle = ActionBundleCodec::decode(
                    record->bytes, document, session.scope().epoch);
                if (!acceptedBundle.ok()) return acceptedBundle.error();
                const bool locallyProduced = std::any_of(
                    acceptedBundle.value().operations.begin(),
                    acceptedBundle.value().operations.end(), [&](const ActionOperation& op) {
                        return op.producer == session.actor();
                    });
                const bool matchesNextOwnerEvent =
                    historyEventIndex < historyIndex.events.size()
                    && historyIndex.events[historyEventIndex].actionId
                        == acceptedBundle.value().actionId;
                const bool ownerAction = locallyProduced || matchesNextOwnerEvent;
                if (ownerAction) {
                    if (historyEventIndex >= historyIndex.events.size())
                        return Error(ErrorCode::history_unavailable,
                            "owner history omits a locally authored accepted action");
                    const auto& event = historyIndex.events[historyEventIndex++];
                    if (event.actionId != acceptedBundle.value().actionId
                        || event.kind != acceptedBundle.value().kind)
                        return Error(ErrorCode::pin_conflict,
                            "owner history action ancestry differs from accepted MAB order");
                    std::set<HistoryOperation> expected;
                    for (const auto& operation : acceptedBundle.value().operations)
                        expected.emplace(operation.stream,
                            static_cast<uint8_t>(operation.primitive),
                            actorBytes(operation.producer), operation.counter);
                    std::set<HistoryOperation> indexed;
                    size_t indexedCount = 0;
                    for (const auto& group : event.groups) {
                        if (group.primitive == OperationPrimitive::markoffMap
                            && (!group.hasMapPriorState || group.operationIds.size() != 1
                                || group.groupId != group.operationIds.front()))
                            return Error(ErrorCode::invalid_identity,
                                "owner map history lacks its reversible singleton prior state");
                        for (const auto& identity : group.operationIds) {
                            ++indexedCount;
                            indexed.emplace(group.stream,
                                static_cast<uint8_t>(group.primitive),
                                actorBytes(identity.actor), identity.counter);
                        }
                    }
                    if (indexed != expected || indexedCount != expected.size())
                        return Error(ErrorCode::invalid_identity,
                            "owner history does not map an accepted MAB's operations exactly once");
                }
            }
            if (record == committed) break;
        }
        if (historyEventIndex != historyIndex.events.size())
            return Error(ErrorCode::history_unavailable,
                         "owner history contains actions beyond its committed accepted prefix");
        for (const auto& group : historyIndex.events.back().groups) {
            if (group.primitive == OperationPrimitive::markoffMap
                && (!group.hasMapPriorState || group.operationIds.size() != 1
                    || group.groupId != group.operationIds.front()))
                return Error(ErrorCode::invalid_identity,
                             "owner map history lacks its reversible singleton prior state");
            for (const auto& identity : group.operationIds) {
                ++restoredOperationCount;
                restoredOperations.emplace(group.stream,
                    static_cast<uint8_t>(group.primitive), actorBytes(identity.actor),
                    identity.counter);
            }
        }
        if (restoredOperations != expectedOperations
            || restoredOperationCount != expectedOperations.size())
            return Error(ErrorCode::invalid_identity,
                         "owner history does not map its committed MAB operations exactly once");

        std::set<std::pair<uint8_t, std::string>> restoredRoutes;
        for (const auto& stream : ownerHistory.value().streams) {
            if (stream.actor != session.actor()
                || stream.scope.document != document
                || stream.scope.epoch != session.scope().epoch)
                return Error(ErrorCode::wrong_stream,
                             "owner-local history has an invalid actor or document scope");
            const auto route = std::make_pair(
                static_cast<uint8_t>(stream.scope.primitive), stream.scope.stream);
            if (!restoredRoutes.insert(route).second)
                return Error(ErrorCode::invalid_identity,
                             "owner-local history repeats a stream snapshot");
            if (stream.scope.primitive == Primitive::list) {
                if (stream.scope.stream != kOrderStreamName || !projection->m_idList)
                    return Error(ErrorCode::wrong_stream,
                                 "owner-local list history has no canonical replayed stream");
                auto restored = projection->m_idList->restoreLocalHistory(stream.snapshot);
                if (!restored.ok()) return restored.error();
            } else if (stream.scope.primitive == Primitive::text) {
                if (!parseBlockTextStreamName(stream.scope.stream).ok())
                    return Error(ErrorCode::wrong_stream,
                                 "owner-local text history has a noncanonical stream");
                auto found = projection->m_textStreams.find(stream.scope.stream);
                if (found == projection->m_textStreams.end())
                    return Error(ErrorCode::unknown_stream,
                                 "owner-local text history names an unreplayed stream");
                auto restored = found->second->restoreLocalHistory(stream.snapshot);
                if (!restored.ok()) return restored.error();
            } else {
                return Error(ErrorCode::wrong_primitive,
                             "owner-local history contains an unsupported primitive");
            }
        }
        projection->m_ownerHistory = std::move(ownerHistory).value();
        auto acked = acknowledgeDurableRetirements(
            *projection, *projection->m_ownerHistory);
        if (!acked.ok())
            return acked.error();
    }
    return projection;
}

Result<void> AcceptedPrefixProjection::replayRecord(
    DocumentStorageSession& session, const AcceptedStorageRecord& record)
{
    using namespace CollabText;
    const std::string document = sessionDocument(session);
    const uint64_t epoch = session.scope().epoch;
    if (record.format == QByteArrayLiteral("MAB/3")) {
        auto decoded = ActionBundleCodec::decode(record.bytes, document, epoch);
        if (!decoded.ok()) return decoded.error();
        auto& bundle = decoded.value();
        if (bundle.actionId != record.transactionId)
            return Error(ErrorCode::pin_conflict,
                         "accepted MAB action ID differs from its provider transaction");
        auto identities = ActionBundleCodec::validateOperationIdentities(bundle);
        if (!identities.ok()) return identities.error();

        for (const auto& operation : bundle.operations) {
            if (operation.primitive == OperationPrimitive::idList) {
                if (operation.codec != OperationCodec::ctlo2
                    || operation.stream != kOrderStreamName
                    || operation.targetBlock)
                    return Error(ErrorCode::wrong_stream,
                                 "accepted IdList operation has an invalid route or codec");
                if (!m_idList) {
                    auto created = ScopedIdListStream::create(session, PositionScope{
                        document, operation.stream, Primitive::list, epoch});
                    if (!created.ok()) return created.error();
                    m_idList = std::move(created).value();
                }
                const auto applied = m_idList->applyRemote(RemoteListOperation{
                    PositionScope{document, operation.stream, Primitive::list, epoch},
                    {operation.producer, operation.counter}, operation.payload});
                if (!applied.ok()) return applied.error();
                auto accepted = requireApplied(applied.value(),
                    "accepted IdList operation was duplicate or deferred during replay");
                if (!accepted.ok()) return accepted.error();
                continue;
            }

            if (operation.primitive == OperationPrimitive::text) {
                if (operation.codec != OperationCodec::ctbo3
                    || !operation.targetBlock)
                    return Error(ErrorCode::wrong_stream,
                                 "accepted text operation has an invalid target or codec");
                auto canonicalStream = blockTextStreamName(*operation.targetBlock);
                if (!canonicalStream.ok()) return canonicalStream.error();
                if (canonicalStream.value() != operation.stream)
                    return Error(ErrorCode::wrong_stream,
                                 "accepted text target does not match its canonical stream");
                auto found = m_textStreams.find(operation.stream);
                if (found == m_textStreams.end()) {
                    auto created = ScopedTextStream::create(session, PositionScope{
                        document, operation.stream, Primitive::text, epoch});
                    if (!created.ok()) return created.error();
                    found = m_textStreams.emplace(
                        operation.stream, std::move(created).value()).first;
                }
                const auto applied = found->second->applyRemote(RemoteOperation{
                    PositionScope{document, operation.stream, Primitive::text, epoch},
                    {operation.producer, operation.counter}, operation.payload});
                if (!applied.ok()) return applied.error();
                auto accepted = requireApplied(applied.value(),
                    "accepted text operation was duplicate or deferred during replay");
                if (!accepted.ok()) return accepted.error();
                continue;
            }

            if (operation.primitive == OperationPrimitive::markoffMap) {
                if (operation.codec != OperationCodec::markoffOwned)
                    return Error(ErrorCode::unknown_format,
                                 "accepted map operation has an unsupported codec");
                auto payload = MarkoffMapCodec::decode(operation.payload);
                if (!payload.ok()) return payload.error();
                auto canonicalStream = mapStreamName(payload.value().map);
                if (!canonicalStream.ok()) return canonicalStream.error();
                if (canonicalStream.value() != operation.stream
                    || payload.value().producer != operation.producer
                    || payload.value().counter != operation.counter)
                    return Error(ErrorCode::wrong_stream,
                                 "accepted map payload differs from its routed identity");
                auto found = m_mapStreams.find(payload.value().map);
                if (found == m_mapStreams.end()) {
                    auto created = ReplicatedMapStream::create(session, MapScope{
                        document, payload.value().map, epoch});
                    if (!created.ok()) return created.error();
                    found = m_mapStreams.emplace(
                        payload.value().map, std::move(created).value()).first;
                }
                auto applied = found->second->applyRemote(
                    operation.payload, operation.targetBlock);
                if (!applied.ok()) return applied.error();
                if (applied.value() == MapApplyKind::duplicate)
                    return replayError("accepted map operation was duplicated during replay");
                continue; // An older LWW value may validly replay as stale.
            }

            return Error(ErrorCode::wrong_primitive,
                         "accepted action contains an unknown operation primitive");
        }
        return {};
    }

    if (record.format == QByteArrayLiteral("MTG/1")) {
        auto gap = CounterGapCodec::decodeRecord(record.bytes);
        if (!gap.ok()) return gap.error();
        if (gap.value().scope.document != document
            || gap.value().scope.epoch != epoch
            || gap.value().acceptedPrior != record.priorRevision.token)
            return Error(ErrorCode::pin_conflict,
                         "accepted MTG does not match its document, epoch or prefix revision");
        if (gap.value().scope.primitive == Primitive::list) {
            if (gap.value().scope.stream != kOrderStreamName)
                return Error(ErrorCode::wrong_stream,
                             "accepted list gap is not on the canonical order stream");
            if (!m_idList) {
                auto created = ScopedIdListStream::create(session, gap.value().scope);
                if (!created.ok()) return created.error();
                m_idList = std::move(created).value();
            }
            auto applied = m_idList->applyCommittedCounterGap(
                record.bytes, record.priorRevision.token);
            if (!applied.ok()) return applied.error();
            return requireApplied(applied.value(),
                "accepted IdList gap was not applied during replay");
        }
        if (!parseBlockTextStreamName(gap.value().scope.stream).ok())
            return Error(ErrorCode::unknown_stream,
                         "accepted text gap is not on a canonical block stream");
        auto found = m_textStreams.find(gap.value().scope.stream);
        if (found == m_textStreams.end()) {
            auto created = ScopedTextStream::create(session, gap.value().scope);
            if (!created.ok()) return created.error();
            found = m_textStreams.emplace(
                gap.value().scope.stream, std::move(created).value()).first;
        }
        auto applied = found->second->applyCommittedCounterGap(
            record.bytes, record.priorRevision.token);
        if (!applied.ok()) return applied.error();
        return requireApplied(applied.value(),
            "accepted text gap was not applied during replay");
    }

    if (record.format == QByteArrayLiteral("MMG/1")) {
        auto gap = MapCounterGapCodec::decodeRecord(record.bytes);
        if (!gap.ok()) return gap.error();
        if (gap.value().document != document || gap.value().epoch != epoch
            || gap.value().acceptedPrior != record.priorRevision.token)
            return Error(ErrorCode::pin_conflict,
                         "accepted MMG does not match its document, epoch or prefix revision");
        auto found = m_mapStreams.find(gap.value().map);
        if (found == m_mapStreams.end()) {
            auto created = ReplicatedMapStream::create(session, MapScope{
                document, gap.value().map, epoch});
            if (!created.ok()) return created.error();
            found = m_mapStreams.emplace(
                gap.value().map, std::move(created).value()).first;
        }
        return found->second->applyCommittedCounterGap(
            record.bytes, record.priorRevision.token);
    }

    return Error(ErrorCode::unknown_format,
                 "accepted prefix contains an unsupported record format");
}

const ScopedTextStream* AcceptedPrefixProjection::textStream(
    std::string_view stream) const noexcept
{
    const auto found = m_textStreams.find(std::string(stream));
    return found == m_textStreams.end() ? nullptr : found->second.get();
}

ScopedTextStream* AcceptedPrefixProjection::mutableTextStream(
    std::string_view stream) noexcept
{
    const auto found = m_textStreams.find(std::string(stream));
    return found == m_textStreams.end() ? nullptr : found->second.get();
}

const ReplicatedMapStream* AcceptedPrefixProjection::mapStream(
    std::string_view map) const noexcept
{
    const auto found = m_mapStreams.find(std::string(map));
    return found == m_mapStreams.end() ? nullptr : found->second.get();
}

ReplicatedMapStream* AcceptedPrefixProjection::mutableMapStream(
    std::string_view map) noexcept
{
    const auto found = m_mapStreams.find(std::string(map));
    return found == m_mapStreams.end() ? nullptr : found->second.get();
}

Result<ScopedIdListStream*> AcceptedPrefixProjection::ensureIdList(
    DocumentStorageSession& session)
{
    if (m_idList)
        return m_idList.get();
    PositionScope scope{m_document, std::string(kOrderStreamName),
                        Primitive::list, m_epoch};
    auto created = ScopedIdListStream::create(session, std::move(scope));
    if (!created.ok()) return created.error();
    m_idList = std::move(created).value();
    return m_idList.get();
}

Result<ScopedTextStream*> AcceptedPrefixProjection::ensureTextStream(
    DocumentStorageSession& session, const BlockIdentity& block)
{
    auto route = blockTextStreamName(block);
    if (!route.ok()) return route.error();
    if (auto* existing = mutableTextStream(route.value()))
        return existing;
    PositionScope scope{m_document, route.value(), Primitive::text, m_epoch};
    auto created = ScopedTextStream::create(session, std::move(scope));
    if (!created.ok()) return created.error();
    auto [found, inserted] = m_textStreams.emplace(
        route.value(), std::move(created).value());
    if (!inserted)
        return Error(ErrorCode::invalid_identity,
                     "text stream was concurrently inserted into one projection");
    return found->second.get();
}

Result<ReplicatedMapStream*> AcceptedPrefixProjection::ensureMapStream(
    DocumentStorageSession& session, const MapScope& scope)
{
    if (scope.document != m_document || scope.epoch != m_epoch)
        return Error(ErrorCode::wrong_document,
                     "map scope differs from the accepted projection");
    if (auto* existing = mutableMapStream(scope.map))
        return existing;
    auto created = ReplicatedMapStream::create(session, scope);
    if (!created.ok()) return created.error();
    auto replayReady = created.value()->completeReplay();
    if (!replayReady.ok()) return replayReady.error();
    auto [found, inserted] = m_mapStreams.emplace(
        scope.map, std::move(created).value());
    if (!inserted)
        return Error(ErrorCode::invalid_identity,
                     "map stream was concurrently inserted into one projection");
    return found->second.get();
}

Result<void> AcceptedPrefixProjection::refreshFrom(
    DocumentStorageSession& session, AcceptedPrefixProjection& replayed)
{
    if (m_document != replayed.m_document || m_epoch != replayed.m_epoch)
        return Error(ErrorCode::wrong_document,
                     "cannot refresh a projection from another document scope");
    if (replayed.m_idList && !m_idList) {
        m_idList = std::move(replayed.m_idList);
    } else if (replayed.m_idList && m_idList) {
        auto refreshed = m_idList->replaceStateFrom(*replayed.m_idList);
        if (!refreshed.ok()) return refreshed.error();
    } else if (!replayed.m_idList && m_idList) {
        PositionScope scope{m_document, std::string(kOrderStreamName),
                            Primitive::list, m_epoch};
        auto empty = ScopedIdListStream::create(session, std::move(scope));
        if (!empty.ok()) return empty.error();
        auto refreshed = m_idList->replaceStateFrom(*empty.value());
        if (!refreshed.ok()) return refreshed.error();
    }

    std::set<std::string> refreshedTextRoutes;
    for (auto replayedIt = replayed.m_textStreams.begin();
         replayedIt != replayed.m_textStreams.end();) {
        refreshedTextRoutes.insert(replayedIt->first);
        const auto current = m_textStreams.find(replayedIt->first);
        if (current == m_textStreams.end()) {
            m_textStreams.emplace(replayedIt->first,
                                  std::move(replayedIt->second));
        } else {
            auto refreshed = current->second->replaceStateFrom(*replayedIt->second);
            if (!refreshed.ok()) return refreshed.error();
        }
        replayedIt = replayed.m_textStreams.erase(replayedIt);
    }
    // Routes created only by a definitely absent/rejected tentative action
    // remain as empty stream objects. This keeps router bindings stable while
    // ensuring the rejected engine state and initial-import barrier are gone.
    for (auto& [route, current] : m_textStreams) {
        if (refreshedTextRoutes.contains(route))
            continue;
        auto empty = ScopedTextStream::create(session, current->scope());
        if (!empty.ok()) return empty.error();
        auto refreshed = current->replaceStateFrom(*empty.value());
        if (!refreshed.ok()) return refreshed.error();
    }

    std::set<std::string> refreshedMapRoutes;
    for (auto replayedIt = replayed.m_mapStreams.begin();
         replayedIt != replayed.m_mapStreams.end();) {
        refreshedMapRoutes.insert(replayedIt->first);
        const auto current = m_mapStreams.find(replayedIt->first);
        if (current == m_mapStreams.end()) {
            m_mapStreams.emplace(replayedIt->first,
                                 std::move(replayedIt->second));
        } else {
            auto refreshed = current->second->replaceStateFrom(*replayedIt->second);
            if (!refreshed.ok()) return refreshed.error();
        }
        replayedIt = replayed.m_mapStreams.erase(replayedIt);
    }
    for (auto& [mapName, current] : m_mapStreams) {
        if (refreshedMapRoutes.contains(mapName))
            continue;
        auto empty = ReplicatedMapStream::create(session, current->scope());
        if (!empty.ok()) return empty.error();
        auto complete = empty.value()->completeReplay();
        if (!complete.ok()) return complete.error();
        auto refreshed = current->replaceStateFrom(*empty.value());
        if (!refreshed.ok()) return refreshed.error();
    }

    m_acceptedRevision = std::move(replayed.m_acceptedRevision);
    m_ownerHistory = std::move(replayed.m_ownerHistory);
    m_acceptedRecords = std::move(replayed.m_acceptedRecords);
    return {};
}

Result<void> AcceptedPrefixProjection::acceptStagedLocalCommit(
    const AcceptedStorageRecord& record, const AcceptedRevision& acceptedRevision,
    OwnerHistorySnapshot ownerHistory)
{
    if (record.priorRevision != m_acceptedRevision
        || record.committedRevision != acceptedRevision
        || acceptedRevision.token.isEmpty()
        || record.format != QByteArrayLiteral("MAB/3"))
        return Error(CollabText::ErrorCode::pin_conflict,
                     "staged local commit does not extend this projection revision");

    auto bundle = ActionBundleCodec::decode(record.bytes, m_document, m_epoch);
    if (!bundle.ok()) return bundle.error();
    auto identities = ActionBundleCodec::validateOperationIdentities(bundle.value());
    if (!identities.ok()) return identities.error();
    if (bundle.value().actionId != record.transactionId
        || ownerHistory.document != m_document || ownerHistory.epoch != m_epoch
        || ownerHistory.transactionId != record.transactionId
        || ownerHistory.acceptedPrior != record.priorRevision.token)
        return Error(CollabText::ErrorCode::invalid_identity,
                     "staged local history does not match the accepted MAB");

    if (bundle.value().kind == ActionKind::initialImport) {
        for (auto& [route, stream] : m_textStreams) {
            (void)route;
            if (!stream->m_initialImportPending) continue;
            auto committed = stream->commitInitialImport();
            if (!committed.ok()) return committed.error();
        }
    }

    // The lifecycle caller has verified this is the provider's exact next
    // record and that its durable history bytes match ownerHistory. The staged
    // stream adapters already contain these operations and local undo groups.
    m_acceptedRecords.push_back(record);
    m_acceptedRevision = acceptedRevision;
    m_ownerHistory = std::move(ownerHistory);
    return {};
}

Result<std::optional<AcceptedActionOperation>>
AcceptedPrefixProjection::findAcceptedOperation(
    std::string_view stream, const CollabText::ActorId& actor,
    uint64_t counter) const
{
    const std::string expectedStream(stream);
    std::optional<AcceptedActionOperation> match;
    for (const auto& record : m_acceptedRecords) {
        if (record.format != QByteArrayLiteral("MAB/3")) continue;
        auto decoded = ActionBundleCodec::decode(record.bytes,
            m_document, m_epoch);
        if (!decoded.ok()) return decoded.error();
        const auto& bundle = decoded.value();
        if (bundle.actionId != record.transactionId)
            return Error(ErrorCode::pin_conflict,
                         "accepted MAB action ID differs from its provider transaction");
        for (size_t i = 0; i < bundle.operations.size(); ++i) {
            const auto& operation = bundle.operations[i];
            if (operation.stream != expectedStream
                || operation.producer != actor || operation.counter != counter)
                continue;
            if (match)
                return Error(ErrorCode::pin_conflict,
                             "accepted history repeats a scoped operation identity");
            match = AcceptedActionOperation{operation, bundle.deliverySequence,
                bundle.actionId, static_cast<uint32_t>(i),
                static_cast<uint32_t>(bundle.operations.size()),
                record.priorRevision, record.committedRevision};
        }
    }
    return match;
}

} // namespace Markoff::VNext
