// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ActionPublicationCoordinator.h>

#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/OwnerHistoryCodec.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <algorithm>
#include <set>
#include <tuple>

namespace Markoff::VNext {
namespace {
using namespace CollabText;

ActionPublicationResult result(ActionPublicationState state,
                               std::optional<Error> diagnostic = std::nullopt)
{
    return {state, std::nullopt, std::move(diagnostic)};
}

std::string documentName(const QString& key)
{
    const QByteArray utf8 = key.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}
}

CollabText::Result<void> validateAcceptedPrefixRevisions(
    const StorageRecoveryState& recovery)
{
    using namespace CollabText;
    if (recovery.acceptedRevision.token.isEmpty())
        return Error(ErrorCode::history_unavailable,
                     "accepted prefix has an empty current revision token");

    std::set<QByteArray> transactionIds;
    std::set<QByteArray> revisions;
    QByteArray previous;
    for (const auto& record : recovery.acceptedRecords) {
        if (record.transactionId.isEmpty() || record.format.isEmpty() || record.bytes.isEmpty()
            || record.priorRevision.token.isEmpty()
            || record.committedRevision.token.isEmpty())
            return Error(ErrorCode::history_unavailable,
                         "accepted record is missing identity, bytes or prefix revisions");
        if (!transactionIds.insert(record.transactionId).second)
            return Error(ErrorCode::corrupt_record,
                         "accepted prefix repeats a transaction ID");
        if (previous.isEmpty()) {
            revisions.insert(record.priorRevision.token);
        } else if (record.priorRevision.token != previous) {
            return Error(ErrorCode::pin_conflict,
                         "accepted record prior revision breaks prefix continuity");
        }
        if (record.priorRevision == record.committedRevision
            || !revisions.insert(record.committedRevision.token).second)
            return Error(ErrorCode::corrupt_record,
                         "distinct accepted prefixes share a revision token");
        previous = record.committedRevision.token;
    }
    if (!previous.isEmpty() && previous != recovery.acceptedRevision.token)
        return Error(ErrorCode::pin_conflict,
                     "accepted record chain does not end at the snapshot revision");
    return {};
}

ActionPublicationCoordinator::ActionPublicationCoordinator(DocumentStorageSession& session)
    : m_session(&session)
{}

CollabText::Result<void> ActionPublicationCoordinator::validate(
    const PreparedStorageTransaction& transaction) const
{
    using namespace CollabText;
    const std::string document = documentName(m_session->scope().documentKey);
    if (transaction.sharedRecordFormat != QByteArrayLiteral("MAB/3"))
        return Error(ErrorCode::unknown_format,
                     "action publication coordinator accepts only MAB records");
    auto bundle = ActionBundleCodec::decode(transaction.sharedRecord, document,
                                             m_session->scope().epoch);
    if (!bundle.ok()) return bundle.error();
    if (bundle.value().actionId != transaction.transactionId)
        return Error(ErrorCode::pin_conflict,
                     "MAB action ID does not match prepared transaction ID");
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) return recovery.error();
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) return revisions.error();
    if (bundle.value().kind == ActionKind::initialImport) {
        const auto source = SourceSnapshotCodec::decode(
            bundle.value().sourceSnapshot, document, m_session->scope().epoch);
        if (!source.ok()) return source.error();
        if (recovery.value().initialImport) {
            if (recovery.value().initialImport->transactionId != transaction.transactionId
                || recovery.value().initialImport->sourceMarkdown
                    != source.value().originalMarkdown)
                return Error(ErrorCode::pin_conflict,
                             "initial MAB differs from the durable import intent");
        } else {
            const bool alreadyAccepted = std::any_of(
                recovery.value().acceptedRecords.begin(),
                recovery.value().acceptedRecords.end(),
                [&](const AcceptedStorageRecord& record) {
                    return record.transactionId == transaction.transactionId
                        && record.format == transaction.sharedRecordFormat
                        && record.bytes == transaction.sharedRecord;
                });
            if (!alreadyAccepted)
                return Error(ErrorCode::history_unavailable,
                             "initial MAB has no matching durable import intent");
        }
    } else {
        if (recovery.value().initialImport)
            return Error(ErrorCode::history_unavailable,
                         "the pending initial import must be published before edits");
        bool hasAcceptedInitialImport = false;
        for (const auto& record : recovery.value().acceptedRecords) {
            if (record.format != QByteArrayLiteral("MAB/3"))
                continue;
            const auto acceptedBundle = ActionBundleCodec::decode(
                record.bytes, document, m_session->scope().epoch);
            if (!acceptedBundle.ok()) return acceptedBundle.error();
            if (acceptedBundle.value().kind == ActionKind::initialImport)
                hasAcceptedInitialImport = true;
        }
        if (!hasAcceptedInitialImport)
            return Error(ErrorCode::history_unavailable,
                         "the first accepted MAB action must be an authorized initial import");
    }
    if (bundle.value().operations.size() != transaction.operationIdentities.size())
        return Error(ErrorCode::invalid_identity,
                     "prepared operation claims do not match the MAB operation list");
    for (size_t i = 0; i < bundle.value().operations.size(); ++i) {
        const auto& operation = bundle.value().operations[i];
        const OperationIdentityClaim expected{
            QString::fromUtf8(operation.stream.data(),
                              static_cast<qsizetype>(operation.stream.size())),
            operation.producer, operation.counter};
        if (transaction.operationIdentities[i] != expected)
            return Error(ErrorCode::invalid_identity,
                         "prepared operation claim differs from its MAB envelope");
    }
    const auto ownerHistory = OwnerHistoryCodec::decode(
        transaction.localHistoryAfterCommit, document, m_session->scope().epoch,
        m_session->actor(), transaction.transactionId,
        transaction.expectedRevision.token);
    if (!ownerHistory.ok()) return ownerHistory.error();
    const auto& historyEvents = ownerHistory.value().actionIndex.events;
    if (historyEvents.empty() || historyEvents.back().actionId != transaction.transactionId
        || historyEvents.back().kind != bundle.value().kind)
        return Error(ErrorCode::invalid_identity,
                     "owner history does not end with the prepared action");

    using HistoryOperation = std::tuple<std::string, OperationPrimitive, ActorId, uint64_t>;
    std::set<HistoryOperation> expectedHistoryOperations;
    for (const auto& operation : bundle.value().operations)
        expectedHistoryOperations.emplace(operation.stream, operation.primitive,
                                           operation.producer, operation.counter);
    std::set<HistoryOperation> recordedHistoryOperations;
    for (const auto& group : historyEvents.back().groups) {
        if (group.primitive == OperationPrimitive::markoffMap
            && (!group.hasMapPriorState || group.operationIds.size() != 1
                || group.groupId != group.operationIds.front()))
            return Error(ErrorCode::invalid_identity,
                         "map history lacks its reversible singleton prior state");
        for (const auto& identity : group.operationIds)
            recordedHistoryOperations.emplace(group.stream, group.primitive,
                                                identity.actor, identity.counter);
    }
    if (recordedHistoryOperations != expectedHistoryOperations)
        return Error(ErrorCode::invalid_identity,
                     "owner history does not map every MAB operation exactly once");
    return {};
}

CollabText::Result<void> ActionPublicationCoordinator::validateGap(
    const PreparedStorageTransaction& transaction) const
{
    using namespace CollabText;
    if (!transaction.operationIdentities.empty()
        || transaction.reservations.size() != 1
        || !transaction.localHistoryAfterCommit.isEmpty())
        return Error(ErrorCode::malformed,
                     "gap transaction requires one reservation, no operation claims or owner history");
    const auto& reservation = transaction.reservations.front();
    if (reservation.actor != m_session->actor() || reservation.first == 0
        || reservation.count == 0)
        return Error(ErrorCode::invalid_identity,
                     "gap reservation does not belong to this writer");

    const std::string document = documentName(m_session->scope().documentKey);
    if (transaction.sharedRecordFormat == QByteArrayLiteral("MTG/1")) {
        const std::string stream = reservation.stream.toStdString();
        const PositionScope textScope{document, stream, Primitive::text,
                                      m_session->scope().epoch};
        auto decoded = CounterGapCodec::decode(
            transaction.sharedRecord, textScope, transaction.expectedRevision.token);
        if (decoded.ok()) {
            if (!parseBlockTextStreamName(stream).ok())
                return Error(ErrorCode::wrong_stream,
                             "MTG text reservation is not a canonical block stream");
        } else {
            const PositionScope listScope{document, stream, Primitive::list,
                                          m_session->scope().epoch};
            decoded = CounterGapCodec::decode(
                transaction.sharedRecord, listScope, transaction.expectedRevision.token);
            if (decoded.ok() && stream != kOrderStreamName)
                return Error(ErrorCode::wrong_stream,
                             "MTG list reservation is not the canonical order stream");
        }
        if (!decoded.ok()) return decoded.error();
        if (decoded.value().gap.actor != reservation.actor
            || decoded.value().gap.first != reservation.first
            || decoded.value().gap.count != reservation.count)
            return Error(ErrorCode::invalid_identity,
                         "MTG interval does not exactly cover its reservation");
        return {};
    }
    if (transaction.sharedRecordFormat == QByteArrayLiteral("MMG/1")) {
        const std::string stream = reservation.stream.toStdString();
        constexpr std::string_view prefix = "markoff/map/v1/";
        if (!stream.starts_with(prefix))
            return Error(ErrorCode::wrong_stream,
                         "MMG reservation is not on a canonical Markoff map route");
        const std::string map = stream.substr(prefix.size());
        auto decoded = MapCounterGapCodec::decode(transaction.sharedRecord, document,
            map, m_session->scope().epoch, transaction.expectedRevision.token);
        if (!decoded.ok()) return decoded.error();
        if (decoded.value().actor != reservation.actor
            || decoded.value().first != reservation.first
            || decoded.value().count != reservation.count)
            return Error(ErrorCode::invalid_identity,
                         "MMG interval does not exactly cover its reservation");
        return {};
    }
    return Error(ErrorCode::unknown_format,
                 "gap publication accepts only MTG/1 and MMG/1 records");
}

ActionPublicationResult ActionPublicationCoordinator::finishCommitted(
    const PreparedStorageTransaction& transaction, const AcceptedRevision& revision)
{
    auto stored = m_session->storeLocalHistory(PrivateHistoryRecord{
        transaction.transactionId, revision, transaction.localHistoryAfterCommit});
    if (!stored.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committedHistoryPending, revision, stored.error()};
    }
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committed, revision, recovery.error()};
    }
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committed, revision, revisions.error()};
    }
    m_blocked = !recovery.value().unresolvedTransactions.empty()
        || !recovery.value().unresolvedReservations.empty();
    if (m_blocked) {
        return {ActionPublicationState::committed, revision,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "action committed, but other transactions or reservations still require recovery")};
    }
    return {ActionPublicationState::committed, revision, std::nullopt};
}

ActionPublicationResult ActionPublicationCoordinator::finishGap(
    const PreparedStorageTransaction& transaction, const AcceptedRevision& revision)
{
    auto finalized = m_session->storeLocalHistory(PrivateHistoryRecord{
        transaction.transactionId, revision, {}});
    if (!finalized.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committedHistoryPending, revision,
                finalized.error()};
    }
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committed, revision, recovery.error()};
    }
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) {
        m_blocked = true;
        return {ActionPublicationState::committed, revision, revisions.error()};
    }
    m_blocked = !recovery.value().unresolvedTransactions.empty()
        || !recovery.value().unresolvedReservations.empty();
    if (!m_blocked && m_absenceNeedsRebuild && m_absenceRebuilt) {
        m_absenceNeedsRebuild = false;
        m_absenceRebuilt = false;
    }
    if (m_blocked)
        return {ActionPublicationState::committed, revision,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "gap committed, but other transactions or reservations remain unresolved")};
    return {ActionPublicationState::committed, revision, std::nullopt};
}

ActionPublicationResult ActionPublicationCoordinator::publish(
    const PreparedStorageTransaction& transaction)
{
    using namespace CollabText;
    if (m_blocked)
        return result(ActionPublicationState::uncertain,
            Error(ErrorCode::commit_uncertain,
                  "action publication is blocked pending transaction recovery"));
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) {
        m_blocked = true;
        return result(ActionPublicationState::uncertain, recovery.error());
    }
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) {
        m_blocked = true;
        return result(ActionPublicationState::uncertain, revisions.error());
    }
    const bool unrelatedReservation = std::any_of(
        recovery.value().unresolvedReservations.begin(),
        recovery.value().unresolvedReservations.end(),
        [&](const ScopedReservation& pending) {
            return std::find(transaction.reservations.begin(),
                             transaction.reservations.end(), pending)
                == transaction.reservations.end();
        });
    if (!recovery.value().unresolvedTransactions.empty() || unrelatedReservation) {
        m_blocked = true;
        return result(ActionPublicationState::uncertain,
            Error(ErrorCode::history_unavailable,
                  "prior transaction or reservation requires recovery before writing"));
    }
    if (transaction.expectedRevision != recovery.value().acceptedRevision) {
        if (!transaction.reservations.empty()) m_blocked = true;
        return result(ActionPublicationState::rejected,
            Error(ErrorCode::counter_regression,
                  "action was staged against a stale accepted prefix"));
    }
    auto valid = validate(transaction);
    if (!valid.ok()) {
        if (!transaction.reservations.empty()) m_blocked = true;
        return result(ActionPublicationState::rejected, valid.error());
    }

    auto prepared = m_session->prepare(transaction);
    if (!prepared.ok()) {
        if (!transaction.reservations.empty()) m_blocked = true;
        return result(ActionPublicationState::rejected, prepared.error());
    }
    auto publication = m_session->append(transaction.transactionId);
    if (publication.disposition == PublicationDisposition::uncertain) {
        publication = m_session->inspect(transaction);
    }
    switch (publication.disposition) {
    case PublicationDisposition::committed:
        if (!publication.committedRevision) {
            m_blocked = true;
            return result(ActionPublicationState::uncertain,
                Error(ErrorCode::commit_uncertain,
                      "committed action has no accepted revision"));
        }
        return finishCommitted(transaction, *publication.committedRevision);
    case PublicationDisposition::absent:
        {
            auto resolved = m_session->resolveAbsentPrepared(transaction);
            if (!resolved.ok() && resolved.error().code() != ErrorCode::not_found) {
                m_blocked = true;
                return result(ActionPublicationState::uncertain, resolved.error());
            }
        }
        m_absenceNeedsRebuild = !transaction.reservations.empty();
        m_absenceRebuilt = false;
        m_blocked = !transaction.reservations.empty();
        return result(ActionPublicationState::absent, publication.diagnostic);
    case PublicationDisposition::uncertain:
        m_blocked = true;
        return result(ActionPublicationState::uncertain, publication.diagnostic);
    case PublicationDisposition::rejected:
        m_blocked = !transaction.reservations.empty();
        return result(ActionPublicationState::rejected, publication.diagnostic);
    }
    m_blocked = true;
    return result(ActionPublicationState::uncertain);
}

ActionPublicationResult ActionPublicationCoordinator::publishGap(
    const PreparedStorageTransaction& transaction)
{
    using namespace CollabText;
    m_blocked = true;
    if (m_absenceNeedsRebuild && !m_absenceRebuilt)
        return result(ActionPublicationState::uncertain,
            CollabText::Error(CollabText::ErrorCode::history_unavailable,
                "tentative projections must be rebuilt from the accepted prefix before gap publication"));
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) return result(ActionPublicationState::uncertain, recovery.error());
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) return result(ActionPublicationState::uncertain, revisions.error());
    if (transaction.expectedRevision != recovery.value().acceptedRevision)
        return result(ActionPublicationState::rejected,
            Error(ErrorCode::counter_regression,
                  "gap was staged against a stale accepted prefix"));
    if (!recovery.value().unresolvedTransactions.empty())
        return result(ActionPublicationState::uncertain,
            Error(ErrorCode::history_unavailable,
                  "prepared transactions must be reconciled before gap publication"));
    auto valid = validateGap(transaction);
    if (!valid.ok()) return result(ActionPublicationState::rejected, valid.error());
    if (std::find(recovery.value().unresolvedReservations.begin(),
                  recovery.value().unresolvedReservations.end(),
                  transaction.reservations.front())
        == recovery.value().unresolvedReservations.end())
        return result(ActionPublicationState::rejected,
            Error(ErrorCode::invalid_identity,
                  "gap does not cover an unresolved durable reservation"));

    auto prepared = m_session->prepare(transaction);
    if (!prepared.ok()) return result(ActionPublicationState::rejected, prepared.error());
    auto publication = m_session->append(transaction.transactionId);
    if (publication.disposition == PublicationDisposition::uncertain)
        publication = m_session->inspect(transaction);
    switch (publication.disposition) {
    case PublicationDisposition::committed:
        if (!publication.committedRevision)
            return result(ActionPublicationState::uncertain,
                Error(ErrorCode::commit_uncertain,
                      "committed gap has no accepted revision"));
        return finishGap(transaction, *publication.committedRevision);
    case PublicationDisposition::absent:
        {
            auto resolved = m_session->resolveAbsentPrepared(transaction);
            if (!resolved.ok() && resolved.error().code() != ErrorCode::not_found) {
                m_blocked = true;
                return result(ActionPublicationState::uncertain, resolved.error());
            }
        }
        m_blocked = true;
        return result(ActionPublicationState::absent, publication.diagnostic);
    case PublicationDisposition::uncertain:
        m_blocked = true;
        return result(ActionPublicationState::uncertain, publication.diagnostic);
    case PublicationDisposition::rejected:
        m_blocked = true;
        return result(ActionPublicationState::rejected, publication.diagnostic);
    }
    m_blocked = true;
    return result(ActionPublicationState::uncertain);
}

ActionPublicationResult ActionPublicationCoordinator::recover(
    const PreparedStorageTransaction& transaction)
{
    auto recovery = m_session->readRecoveryState();
    if (!recovery.ok()) {
        m_blocked = true;
        return result(ActionPublicationState::uncertain, recovery.error());
    }
    auto revisions = validateAcceptedPrefixRevisions(recovery.value());
    if (!revisions.ok()) {
        m_blocked = true;
        return result(ActionPublicationState::uncertain, revisions.error());
    }
    const bool isGap = transaction.sharedRecordFormat == QByteArrayLiteral("MTG/1")
        || transaction.sharedRecordFormat == QByteArrayLiteral("MMG/1");
    auto valid = isGap ? validateGap(transaction) : validate(transaction);
    if (!valid.ok()) {
        m_blocked = true;
        return result(ActionPublicationState::rejected, valid.error());
    }
    auto publication = m_session->inspect(transaction);
    switch (publication.disposition) {
    case PublicationDisposition::committed:
        if (!publication.committedRevision) {
            m_blocked = true;
            return result(ActionPublicationState::uncertain,
                CollabText::Error(CollabText::ErrorCode::commit_uncertain,
                                  "committed action has no accepted revision"));
        }
        return isGap ? finishGap(transaction, *publication.committedRevision)
                     : finishCommitted(transaction, *publication.committedRevision);
    case PublicationDisposition::absent:
        {
            auto resolved = m_session->resolveAbsentPrepared(transaction);
            if (!resolved.ok() && resolved.error().code() != CollabText::ErrorCode::not_found) {
                m_blocked = true;
                return result(ActionPublicationState::uncertain, resolved.error());
            }
        }
        if (!isGap) {
            m_absenceNeedsRebuild = true;
            m_absenceRebuilt = false;
        }
        m_blocked = m_absenceNeedsRebuild
            || !recovery.value().unresolvedReservations.empty();
        return result(ActionPublicationState::absent, publication.diagnostic);
    case PublicationDisposition::uncertain:
        m_blocked = true;
        return result(ActionPublicationState::uncertain, publication.diagnostic);
    case PublicationDisposition::rejected:
        m_blocked = true;
        return result(ActionPublicationState::rejected, publication.diagnostic);
    }
    m_blocked = true;
    return result(ActionPublicationState::uncertain);
}

ActionPublicationResult ActionPublicationCoordinator::recoverAndRebuild(
    const PreparedStorageTransaction& transaction,
    const std::function<CollabText::Result<void>(const StorageRecoveryState&)>& rebuild)
{
    if (!rebuild) {
        m_blocked = true;
        return result(ActionPublicationState::rejected,
            CollabText::Error(CollabText::ErrorCode::malformed,
                              "absence recovery requires a projection rebuild callback"));
    }
    auto recovered = recover(transaction);
    if (recovered.state != ActionPublicationState::absent)
        return recovered;
    auto state = m_session->readRecoveryState();
    if (!state.ok()) {
        m_blocked = true;
        recovered.diagnostic = state.error();
        return recovered;
    }
    auto rebuilt = rebuild(state.value());
    if (!rebuilt.ok()) {
        m_blocked = true;
        recovered.diagnostic = rebuilt.error();
        return recovered;
    }
    const auto rebuiltState = m_session->readRecoveryState();
    if (!rebuiltState.ok()) {
        m_blocked = true;
        recovered.diagnostic = rebuiltState.error();
        return recovered;
    }
    m_absenceNeedsRebuild = !rebuiltState.value().unresolvedReservations.empty();
    m_absenceRebuilt = m_absenceNeedsRebuild;
    m_blocked = !rebuiltState.value().unresolvedTransactions.empty()
        || !rebuiltState.value().unresolvedReservations.empty();
    return recovered;
}

} // namespace Markoff::VNext
