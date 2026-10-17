// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/AcceptedPrefixProjection.h>
#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/CrossStreamHistoryCodec.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>
#include <markoff/vnext/ActionTransactionBuilder.h>
#include <markoff/vnext/ActionStager.h>
#include <markoff/vnext/OwnerHistoryCodec.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/ScopedIdListStream.h>
#include <markoff/vnext/ScopedOperationRouter.h>
#include <markoff/vnext/ScopedListRouter.h>
#include <markoff/vnext/ScopedMapRouter.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/QuarantineRecordCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <collabtext/Serialization.h>

#include "../../libs/markoff-collabtext-vnext/src/ReferenceProviderTestHooks.h"

#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcess>

#include <iostream>
#include <cstdlib>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

using namespace Markoff::VNext;
using namespace CollabText;

template<class T>
T take(Result<T> result);
void take(Result<void> result);

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void runCompositeBlockIdentityContracts()
{
    std::array<uint8_t, ActorId::kBytes> firstBytes{};
    std::array<uint8_t, ActorId::kBytes> secondBytes{};
    firstBytes.back() = 1;
    secondBytes.back() = 2;
    const BlockIdentity first{ActorId::from_bytes(firstBytes), 17};
    const BlockIdentity second{ActorId::from_bytes(secondBytes), 17};
    const BlockIdentity laterCounter{first.actor, 18};

    std::unordered_set<BlockIdentity, BlockIdentityHash> identities{
        first, second, laterCounter};
    require(first != second && identities.size() == 3
                && (first < second) && (first < laterCounter),
            "composite block identity comparison or hash collapsed actor/counter pairs");

    QHash<BlockIdentity, int> qtCache;
    qtCache.insert(first, 1);
    qtCache.insert(second, 2);
    require(qtCache.size() == 2 && qtCache.value(first) == 1
                && qtCache.value(second) == 2,
            "Qt block-identity hash did not preserve distinct structural insertions");
    const auto firstRoute = blockTextStreamName(first);
    const auto secondRoute = blockTextStreamName(second);
    require(firstRoute.ok() && secondRoute.ok() && firstRoute.value() != secondRoute.value(),
            "block stream tokens collapsed equal counters from distinct actors");
    const auto decodedRoute = parseBlockTextStreamName(firstRoute.value());
    require(decodedRoute.ok() && decodedRoute.value() == first,
            "block stream token did not round-trip the complete insertion identity");

}

void runRemoteAcceptanceStorageContract()
{
    QTemporaryDir directory;
    require(directory.isValid(), "could not create remote-acceptance storage root");
    auto provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(directory.path().toStdString())));
    const DocumentStorageScope scope{QStringLiteral("remote-acceptance-doc"), 1};
    auto session = take(provider->open(scope, StorageMode::durable));

    std::array<uint8_t, ActorId::kBytes> peerBytes{};
    peerBytes.back() = 0x71;
    const ActorId peer = ActorId::from_bytes(peerBytes);
    require(peer.valid() && peer != session->actor(),
            "remote acceptance test did not use an independent writer identity");

    PreparedStorageTransaction incoming;
    incoming.transactionId = QByteArrayLiteral("remote-action-1");
    incoming.expectedRevision = take(session->readRecoveryState()).acceptedRevision;
    incoming.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    incoming.sharedRecord = QByteArrayLiteral("exact-remote-MAB-bytes");
    incoming.operationIdentities.push_back(
        {QStringLiteral("markoff/block-text/v1/peer-block"), peer, 9});
    incoming.remoteAcceptance = true;

    const auto accepted = session->acceptRemoteAction(incoming);
    require(accepted.disposition == PublicationDisposition::committed
                && accepted.committedRevision,
            "provider failed to atomically accept remote action identities");
    const auto exactRetry = session->acceptRemoteAction(incoming);
    require(exactRetry.disposition == PublicationDisposition::committed
                && exactRetry.committedRevision == accepted.committedRevision,
            "remote acceptance retry did not return its exact committed prefix");
    const auto remoteOutbox = take(session->readRecoveryState()).pendingOutboundActions;
    require(remoteOutbox.empty()
                && !session->acknowledgeTransportReceipt(
                    incoming.transactionId, QByteArrayLiteral("peer-revision")).ok(),
            "remote accepted action appeared in the receiver's local transport outbox");

    auto changedBytes = incoming;
    changedBytes.sharedRecord.append("-changed");
    const auto changedRetry = session->acceptRemoteAction(changedBytes);
    require(changedRetry.disposition == PublicationDisposition::rejected
                && changedRetry.diagnostic
                && changedRetry.diagnostic->code() == ErrorCode::pin_conflict,
            "remote transaction ID was rebound to changed MAB bytes");

    auto reusedIdentity = incoming;
    reusedIdentity.transactionId = QByteArrayLiteral("remote-action-2");
    reusedIdentity.expectedRevision = *accepted.committedRevision;
    reusedIdentity.sharedRecord = QByteArrayLiteral("another-remote-MAB");
    const auto conflict = session->acceptRemoteAction(reusedIdentity);
    require(conflict.disposition == PublicationDisposition::rejected
                && conflict.diagnostic
                && conflict.diagnostic->code() == ErrorCode::pin_conflict,
            "provider accepted a remote identity already owned by an earlier MAB");

    auto uncertainAction = incoming;
    uncertainAction.transactionId = QByteArrayLiteral("remote-action-uncertain");
    uncertainAction.expectedRevision = *accepted.committedRevision;
    uncertainAction.sharedRecord = QByteArrayLiteral("uncertain-remote-MAB");
    uncertainAction.operationIdentities.front().counter = 10;
    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendAfterCommit);
    const auto uncertain = session->acceptRemoteAction(uncertainAction);
    require(uncertain.disposition == PublicationDisposition::uncertain,
            "injected remote post-commit fault did not report uncertainty");

    session.reset();
    auto reopened = take(provider->open(scope, StorageMode::durable));
    const auto recovery = take(reopened->readRecoveryState());
    require(recovery.acceptedRecords.size() == 2
                && recovery.acceptedRecords.front().transactionId == incoming.transactionId
                && recovery.acceptedRecords.front().bytes == incoming.sharedRecord
                && recovery.acceptedRecords.back().transactionId == uncertainAction.transactionId
                && recovery.acceptedRecords.back().bytes == uncertainAction.sharedRecord
                && recovery.acceptedRevision != *accepted.committedRevision
                && !recovery.localHistory,
            "remote action bytes, accepted revision, or local-history isolation failed on reopen");
    uncertainAction.expectedRevision = *accepted.committedRevision;
    const auto recoveredRetry = reopened->acceptRemoteAction(uncertainAction);
    require(recoveredRetry.disposition == PublicationDisposition::committed
                && recoveredRetry.committedRevision == recovery.acceptedRevision,
            "remote acceptance exact retry did not reconcile the committed uncertain action");
}

void runProviderBackedDocumentLifecycle()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("provider-document"), 3};
    auto document = take(ProviderBackedDocumentState::open(
        *provider, scope, StorageMode::ephemeral));
    require(document->state() == ProviderDocumentState::awaitingInitialImport
                && !document->canStartOrdinaryAction()
                && document->projection().idList() == nullptr,
            "empty provider-backed document became writable before import authorization");

    PreparedStorageTransaction prematureAction;
    const auto beforeAuthorization = document->publishAction(prematureAction);
    require(beforeAuthorization.state == ActionPublicationState::rejected
                && document->state() == ProviderDocumentState::awaitingInitialImport,
            "provider-backed document published an action before import authorization");
    const auto prematureGap = document->publishGap(prematureAction);
    require(prematureGap.state == ActionPublicationState::rejected
                && prematureGap.diagnostic
                && prematureGap.diagnostic->code() == ErrorCode::awaiting_state
                && document->state() == ProviderDocumentState::awaitingInitialImport,
            "provider-backed document published terminal coverage without a recovery barrier");

    const PendingImportIntent intent{QByteArrayLiteral("initial-import-1"),
                                     QByteArrayLiteral("# source\n")};
    take(document->authorizeInitialImport(intent));
    require(document->state() == ProviderDocumentState::initialImportAuthorized
                && !document->canStartOrdinaryAction()
                && document->recovery().initialImport == intent,
            "initial import was not durably authorized before operation allocation");
    take(document->authorizeInitialImport(intent));
    const auto malformedImport = document->publishAction(prematureAction);
    require(malformedImport.state == ActionPublicationState::rejected
                && document->state() == ProviderDocumentState::initialImportAuthorized,
            "a rejected initial action incorrectly removed its import authorization");

    SourceSnapshotRecord source;
    source.document = "provider-document";
    source.epoch = 3;
    source.originalMarkdown = intent.sourceMarkdown;
    ActionBundleCodec::Bundle initial;
    initial.document = source.document;
    initial.epoch = source.epoch;
    initial.deliverySequence = 1;
    initial.actionId = intent.transactionId;
    initial.kind = ActionKind::initialImport;
    initial.sourceSnapshot = take(SourceSnapshotCodec::encode(source));

    CrossStreamHistoryIndex actionIndex;
    actionIndex.document = source.document;
    actionIndex.epoch = source.epoch;
    actionIndex.owner = document->storage().actor();
    actionIndex.acceptedPrior = document->recovery().acceptedRevision.token;
    actionIndex.events.push_back({intent.transactionId, ActionKind::initialImport, {}, {}});
    OwnerHistorySnapshot ownerHistory;
    ownerHistory.document = source.document;
    ownerHistory.epoch = source.epoch;
    ownerHistory.owner = actionIndex.owner;
    ownerHistory.transactionId = intent.transactionId;
    ownerHistory.acceptedPrior = actionIndex.acceptedPrior;
    ownerHistory.actionIndex = actionIndex;

    PreparedStorageTransaction initialTransaction;
    initialTransaction.transactionId = intent.transactionId;
    initialTransaction.expectedRevision = document->recovery().acceptedRevision;
    initialTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    initialTransaction.sharedRecord = take(ActionBundleCodec::encode(initial));
    initialTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(ownerHistory));
    const auto imported = document->publishAction(initialTransaction);
    require(imported.state == ActionPublicationState::committed
                && imported.committedRevision
                && document->state() == ProviderDocumentState::ready
                && document->canStartOrdinaryAction()
                && document->recovery().acceptedRecords.size() == 1
                && !document->recovery().initialImport
                && document->recovery().localHistory,
            "provider-backed lifecycle did not publish and replay its authorized initial import");
    const auto gapWhileReady = document->publishGap(prematureAction);
    require(gapWhileReady.state == ActionPublicationState::rejected
                && gapWhileReady.diagnostic
                && gapWhileReady.diagnostic->code() == ErrorCode::awaiting_state
                && document->state() == ProviderDocumentState::ready,
            "provider-backed document published terminal coverage after recovery completed");

    ErrorCode offThreadResult = ErrorCode::awaiting_state;
    std::thread offThread([&] {
        const auto result = document->authorizeInitialImport(intent);
        if (!result.ok()) offThreadResult = result.error().code();
    });
    offThread.join();
    require(offThreadResult == ErrorCode::unsupported_operation,
            "provider-backed document accepted a lifecycle call off its owner thread");

    const PendingImportIntent changed{QByteArrayLiteral("initial-import-2"),
                                      QByteArrayLiteral("different source\n")};
    const auto mismatch = document->authorizeInitialImport(changed);
    require(!mismatch.ok(), "provider-backed document accepted a changed import retry");

    auto durableFallback = ProviderBackedDocumentState::open(
        *provider, DocumentStorageScope{QStringLiteral("durable-refusal"), 1},
        StorageMode::durable);
    require(!durableFallback.ok(),
            "ephemeral provider silently substituted for a durable document session");
}

void runAcceptedPrefixRevisionValidation()
{
    StorageRecoveryState recovery;
    recovery.acceptedRevision = {QByteArrayLiteral("revision:2")};
    recovery.acceptedRecords = {
        {QByteArrayLiteral("action-1"), QByteArrayLiteral("MAB/3"),
         QByteArrayLiteral("bytes-1"), {QByteArrayLiteral("revision:0")},
         {QByteArrayLiteral("revision:1")}},
        {QByteArrayLiteral("gap-2"), QByteArrayLiteral("MMG/1"),
         QByteArrayLiteral("bytes-2"), {QByteArrayLiteral("revision:1")},
         {QByteArrayLiteral("revision:2")}},
    };
    require(validateAcceptedPrefixRevisions(recovery).ok(),
            "revision validator rejected a continuous opaque-token chain");

    auto broken = recovery;
    broken.acceptedRecords[1].priorRevision.token = QByteArrayLiteral("revision:stale");
    const auto discontinuity = validateAcceptedPrefixRevisions(broken);
    require(!discontinuity.ok() && discontinuity.error().code() == ErrorCode::pin_conflict,
            "revision validator accepted a gap based on a nonadjacent prefix");

    broken = recovery;
    broken.acceptedRecords[1].committedRevision =
        broken.acceptedRecords[0].committedRevision;
    const auto reused = validateAcceptedPrefixRevisions(broken);
    require(!reused.ok() && reused.error().code() == ErrorCode::corrupt_record,
            "revision validator accepted reuse of an earlier prefix token");

    broken = recovery;
    broken.acceptedRevision.token = QByteArrayLiteral("revision:wrong-head");
    const auto wrongHead = validateAcceptedPrefixRevisions(broken);
    require(!wrongHead.ok() && wrongHead.error().code() == ErrorCode::pin_conflict,
            "revision validator accepted a chain that does not reach the snapshot head");
}

template<class T>
T take(Result<T> result)
{
    if (!result.ok())
        throw std::runtime_error(std::string(to_string(result.error().code()))
                                 + ": " + std::string(result.error().message()));
    return std::move(result).value();
}

void take(Result<void> result)
{
    if (!result.ok())
        throw std::runtime_error(std::string(to_string(result.error().code()))
                                 + ": " + std::string(result.error().message()));
}

void run()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("doc-uuid-1"), 1};
    auto session = take(provider->open(scope, StorageMode::ephemeral));
    const ActorId originalActor = session->actor();
    require(session->mode() == StorageMode::ephemeral && session->actor().valid(),
            "ephemeral session did not expose its mode and writer actor");

    const auto durable = provider->open(scope, StorageMode::durable);
    require(!durable.ok()
                && durable.error().code() == ErrorCode::unsupported_operation,
            "ephemeral provider silently accepted a durable open");

    const auto leaseConflict = provider->open(scope, StorageMode::ephemeral);
    require(!leaseConflict.ok()
                && leaseConflict.error().code() == ErrorCode::writer_conflict,
            "provider opened a second writer for the same document");
    auto otherEpoch = provider->open(
        DocumentStorageScope{scope.documentKey, 2}, StorageMode::ephemeral);
    require(!otherEpoch.ok()
                && otherEpoch.error().code() == ErrorCode::writer_conflict,
            "a second epoch bypassed the document-wide lease");

    const QString stream = QStringLiteral("markoff/block-text/v1/actor-with-slashes");
    const auto initial = take(session->readRecoveryState());
    require(initial.acceptedRevision.token == QByteArrayLiteral("ephemeral:0"),
            "fresh ephemeral session has no stable empty-prefix token");
    const auto reservation = take(session->reserve(stream, 3));
    require(reservation.stream == stream && reservation.actor == session->actor()
                && reservation.first == 1 && reservation.count == 3
                && take(session->nextCounter(stream)) == 4,
            "scoped reservation did not preserve its full stream and interval");

    PreparedStorageTransaction transaction;
    transaction.transactionId = QByteArrayLiteral("transaction-1");
    transaction.expectedRevision = initial.acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = QByteArrayLiteral("opaque-action-record");
    transaction.reservations.push_back(reservation);
    transaction.operationIdentities.push_back(
        {stream, session->actor(), reservation.first});
    transaction.localHistoryAfterCommit = QByteArrayLiteral("history-after-action");
    require(session->prepare(transaction).ok(),
            "provider refused a valid exact transaction preparation");
    require(session->prepare(transaction).ok(),
            "identical transaction retry was not idempotent");

    auto conflictingPreparation = transaction;
    conflictingPreparation.sharedRecord.append("-changed");
    const auto changedRetry = session->prepare(conflictingPreparation);
    require(!changedRetry.ok() && changedRetry.error().code() == ErrorCode::pin_conflict,
            "transaction ID was rebound to changed bytes");

    const auto preparedInspection = session->inspect(transaction);
    require(preparedInspection.disposition == PublicationDisposition::absent,
            "in-memory inspection did not prove an unappended transaction absent");
    const auto publication = session->append(transaction.transactionId);
    require(publication.disposition == PublicationDisposition::committed
                && publication.committedRevision.has_value(),
            "prepared transaction was not committed atomically");
    const auto retryPublication = session->append(transaction.transactionId);
    require(retryPublication.disposition == PublicationDisposition::committed
                && retryPublication.committedRevision == publication.committedRevision,
            "exact append retry did not return the original committed revision");
    const auto committedInspection = session->inspect(transaction);
    require(committedInspection.disposition == PublicationDisposition::committed
                && committedInspection.committedRevision == publication.committedRevision,
            "inspection did not recover the exact committed transaction");

    PrivateHistoryRecord history{transaction.transactionId,
                                 *publication.committedRevision,
                                 transaction.localHistoryAfterCommit};
    require(session->storeLocalHistory(history).ok(),
            "provider did not retain committed private history");
    require(session->acknowledgeMarkdownExport(*publication.committedRevision).ok(),
            "provider did not acknowledge an accepted export revision");
    const auto staleExport = session->acknowledgeMarkdownExport(initial.acceptedRevision);
    require(!staleExport.ok() && staleExport.error().code() == ErrorCode::counter_regression,
            "an older export acknowledgment replaced a newer watermark");

    const auto freshState = take(session->readRecoveryState());
    require(freshState.acceptedRecords.size() == 1
                && freshState.unresolvedReservations.empty()
                && freshState.localHistory == history
                && freshState.exportedRevision == publication.committedRevision,
            "recovery snapshot omitted accepted transaction-associated state");

    const auto nextReservation = take(session->reserve(stream, 1));
    require(nextReservation.first == 4,
            "provider reused a counter after a committed reservation");
    PreparedStorageTransaction reusedIdentity = transaction;
    reusedIdentity.transactionId = QByteArrayLiteral("transaction-2");
    reusedIdentity.expectedRevision = *publication.committedRevision;
    reusedIdentity.sharedRecord = QByteArrayLiteral("another-record");
    reusedIdentity.reservations = {nextReservation};
    reusedIdentity.operationIdentities = {{stream, session->actor(), reservation.first}};
    const auto identityConflict = session->prepare(reusedIdentity);
    require(!identityConflict.ok() && identityConflict.error().code() == ErrorCode::pin_conflict,
            "provider accepted a reused operation identity");

    PreparedStorageTransaction stale = transaction;
    stale.transactionId = QByteArrayLiteral("stale-transaction");
    const auto stalePrepare = session->prepare(stale);
    require(!stalePrepare.ok() && stalePrepare.error().code() == ErrorCode::counter_regression,
            "provider accepted a stale accepted-prefix revision");

    session.reset();
    auto reopened = take(provider->open(scope, StorageMode::ephemeral));
    const auto reopenedState = take(reopened->readRecoveryState());
    require(reopened->actor() == originalActor && reopened->actor().valid(),
            "ephemeral provider lost writer authority while alive");
    require(reopenedState.acceptedRecords.size() == 1
                && reopenedState.unresolvedReservations.size() == 1
                && reopenedState.localHistory == history
                && reopenedState.exportedRevision == publication.committedRevision,
            "reopened in-memory session lost its recovery state");

    reopened.reset();
    const DocumentStorageScope adapterScope{QStringLiteral("doc-uuid-2"), 1};
    auto adapterSession = take(provider->open(adapterScope, StorageMode::ephemeral));
    const auto blockStream = take(blockTextStreamName({adapterSession->actor(), 19}));
    const PositionScope textScope{"doc-uuid-2", blockStream, Primitive::text, 1};
    auto textStream = take(ScopedTextStream::create(*adapterSession, textScope));
    const auto textOperation = take(textStream->replaceUtf8(
        0, 0, QByteArrayLiteral("provider text")));
    require(textOperation.scope == textScope
                && textOperation.identity.actor == adapterSession->actor(),
            "text adapter did not allocate through the provider session");

    auto mapStream = take(ReplicatedMapStream::create(
        *adapterSession, MapScope{"doc-uuid-2", "frontmatter", 1}));
    require(mapStream->completeReplay().ok(),
            "provider-backed map adapter did not pass its replay barrier");
    const auto mapOperation = take(mapStream->set(
        QByteArrayLiteral("title"), QByteArrayLiteral("Provider-backed")));
    require(mapOperation.operation.producer == adapterSession->actor()
                && mapOperation.operation.counter == 1,
            "map adapter did not allocate through the provider session");

    auto listStream = take(ScopedIdListStream::create(
        *adapterSession,
        PositionScope{"doc-uuid-2", std::string(kOrderStreamName), Primitive::list, 1}));
    const auto firstBlock = take(listStream->insertAfter(std::nullopt, 42));
    require(firstBlock.insertedBlock.has_value()
                && firstBlock.identity.actor == adapterSession->actor(),
            "list adapter did not return the inserted structural block identity");
    const auto archivedListBytes = take(Crdt::encode_idlist_operation(firstBlock.operation));
    const auto decodedListOperation = take(Crdt::decode_idlist_operation(archivedListBytes));
    require(Crdt::idlist_op_identity(decodedListOperation) == firstBlock.identity,
            "literal IdList archive round-trip changed operation identity");
    const auto secondBlock = take(listStream->insertAfter(firstBlock.insertedBlock, 42));
    require(secondBlock.insertedBlock.has_value()
                && *secondBlock.insertedBlock != *firstBlock.insertedBlock,
            "repeated list values collapsed distinct structural identities");
    auto listEntries = take(listStream->visibleEntries());
    require(listEntries.size() == 2
                && listEntries[0].applicationValue == listEntries[1].applicationValue
                && listEntries[0].identity != listEntries[1].identity,
            "list projection did not preserve repeated values with exact identities");
    require(listStream->anchorOf(*secondBlock.insertedBlock).ok(),
            "list adapter could not derive an exact identity anchor");
    take(listStream->remove(*secondBlock.insertedBlock));
    require(take(listStream->visibleEntries()).size() == 1,
            "list adapter did not remove exactly the selected structural identity");
    const auto adapterRecovery = take(adapterSession->readRecoveryState());
    require(adapterRecovery.unresolvedReservations.size() == 5,
            "provider-backed text, map and list allocations were not journaled");

    const DocumentStorageScope gapStorageScope{QStringLiteral("doc-uuid-3"), 1};
    auto gapSession = take(provider->open(gapStorageScope, StorageMode::ephemeral));
    const PositionScope gapScope{"doc-uuid-3", std::string(kOrderStreamName),
                                 Primitive::list, 1};
    const auto gapReservation = take(gapSession->reserve(
        QString::fromStdString(std::string(kOrderStreamName)), 3));
    Crdt::CounterGap gap{gapReservation.actor, gapReservation.first,
                         gapReservation.count, Crdt::Global{}};
    const auto gapBytes = take(Crdt::encode_counter_gap(gap));
    const std::array<uint8_t, ActorId::kBytes> fixtureActorBytes{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const ActorId fixtureActor = ActorId::from_bytes(fixtureActorBytes);
    const auto fixtureGap = take(Crdt::encode_counter_gap(
        Crdt::CounterGap{fixtureActor, 1, 3, Crdt::Global{}}));
    const QByteArray literalGap = QByteArray::fromHex(QByteArrayLiteral(
        "43544750303030313a7b226163746f72223a223030303130323033303430353036303730383039306130623063306430653066222c226669727374223a2230303030303030303030303030303031222c22636f756e74223a2230303030303030303030303030303033222c2276223a7b7d7d"));
    require(QByteArray::fromStdString(fixtureGap) == literalGap
                && take(Crdt::decode_counter_gap(std::string_view(
                       literalGap.constData(), static_cast<size_t>(literalGap.size())))).actor
                    == fixtureActor,
            "literal CTGP archive changed its actor identity or canonical bytes");
    const auto decodedGap = take(Crdt::decode_counter_gap(gapBytes));
    require(decodedGap.actor == gap.actor && decodedGap.first == gap.first
                && decodedGap.count == gap.count && decodedGap.prior.empty(),
            "CTGP archive round-trip changed its scoped identity interval");
    const auto gapRecovery = take(gapSession->readRecoveryState());
    const QByteArray gapAcceptedPrior = gapRecovery.acceptedRevision.token;
    PreparedStorageTransaction gapTransaction;
    gapTransaction.transactionId = QByteArrayLiteral("terminal-gap-1");
    gapTransaction.expectedRevision = gapRecovery.acceptedRevision;
    const QByteArray scopedGapBytes = take(CounterGapCodec::encode(
        ScopedCounterGap{gapScope, gap, gapAcceptedPrior}));
    gapTransaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
    gapTransaction.sharedRecord = scopedGapBytes;
    gapTransaction.reservations = {gapReservation};
    require(gapSession->prepare(gapTransaction).ok(),
            "provider refused terminal-gap transaction preparation");
    const auto gapPublication = gapSession->append(gapTransaction.transactionId);
    require(gapPublication.disposition == PublicationDisposition::committed
                && gapPublication.committedRevision,
            "provider did not commit terminal-gap evidence before application");
    require(gapSession->storeLocalHistory(PrivateHistoryRecord{
                gapTransaction.transactionId, *gapPublication.committedRevision, {}}).ok(),
            "provider did not finish terminal-gap transaction history");
    auto gapList = take(ScopedIdListStream::create(*gapSession, gapScope));
    const auto gapOutcome = take(gapList->applyCommittedCounterGap(
        gapTransaction.sharedRecord, gapAcceptedPrior));
    require(gapOutcome.kind() == ApplyOutcomeKind::applied,
            "host-committed counter gap did not apply to its exact list scope");
    const auto afterGap = take(gapList->insertAfter(std::nullopt, 7));
    require(afterGap.identity.counter > gapReservation.first + gapReservation.count - 1,
            "list allocation reused an identity covered by the terminal gap");
}

void runScopedCounterGapCodec()
{
    const std::array<uint8_t, ActorId::kBytes> actorBytes{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const PositionScope scope{"gap-doc", "block-text:001122", Primitive::text, 9};
    const QByteArray prior = QByteArray::fromHex(QByteArrayLiteral("a1b2c3"));
    const ScopedCounterGap record{
        scope, Crdt::CounterGap{ActorId::from_bytes(actorBytes), 41, 7, Crdt::Global{}}, prior};
    const QByteArray encoded = take(CounterGapCodec::encode(record));
    const QByteArray literalFixture = QByteArray::fromHex(QByteArrayLiteral(
        "4d544701000000076761702d646f6300000011626c6f636b2d746578743a303031313232"
        "01000000000000000900000003a1b2c30000007243544750303030313a7b226163746f7222"
        "3a223030303130323033303430353036303730383039306130623063306430653066222c22"
        "6669727374223a2230303030303030303030303030303239222c22636f756e74223a223030"
        "3030303030303030303030303037222c2276223a7b7d7d"));
    require(encoded == literalFixture,
            "scoped counter-gap codec no longer matches its literal wire fixture");
    const auto decoded = take(CounterGapCodec::decode(encoded, scope, prior));
    require(decoded.scope == scope && decoded.acceptedPrior == prior
                && decoded.gap.actor == record.gap.actor
                && decoded.gap.first == 41 && decoded.gap.count == 7,
            "scoped counter-gap round-trip changed scope, accepted prior or interval");

    auto wrongScope = scope;
    wrongScope.document = "other-doc";
    const auto wrongDocument = CounterGapCodec::decode(encoded, wrongScope, prior);
    require(!wrongDocument.ok() && wrongDocument.error().code() == ErrorCode::wrong_document,
            "scoped counter-gap accepted a different document");
    const auto wrongPrior = CounterGapCodec::decode(encoded, scope, QByteArrayLiteral("other"));
    require(!wrongPrior.ok() && wrongPrior.error().code() == ErrorCode::pin_conflict,
            "scoped counter-gap accepted a different accepted-prefix token");
    QByteArray unsupported = encoded;
    unsupported[3] = char(2);
    const auto badVersion = CounterGapCodec::decode(unsupported, scope, prior);
    require(!badVersion.ok() && badVersion.error().code() == ErrorCode::unsupported_version,
            "scoped counter-gap accepted an unknown version");
    const auto truncated = CounterGapCodec::decode(encoded.left(encoded.size() - 1), scope, prior);
    require(!truncated.ok(), "scoped counter-gap accepted truncation");

    const MapCounterGap mapGap{"gap-doc", "frontmatter", 9,
        ActorId::from_bytes(actorBytes), 101, 4, prior};
    const QByteArray encodedMapGap = take(MapCounterGapCodec::encode(mapGap));
    const QByteArray literalMapGap = QByteArray::fromHex(QByteArrayLiteral(
        "4d4d4701000000076761702d646f630000000b66726f6e746d6174746572"
        "0000000000000009000102030405060708090a0b0c0d0e0f000000000000"
        "0065000000000000000400000003a1b2c3"));
    require(encodedMapGap == literalMapGap,
            "Markoff map gap codec no longer matches its literal wire fixture");
    const auto decodedMapGap = take(MapCounterGapCodec::decode(
        encodedMapGap, mapGap.document, mapGap.map, mapGap.epoch, prior));
    require(decodedMapGap.actor == mapGap.actor && decodedMapGap.first == mapGap.first
                && decodedMapGap.count == mapGap.count
                && decodedMapGap.acceptedPrior == prior,
            "Markoff map gap round-trip changed its actor interval or accepted prior");
    const auto wrongMap = MapCounterGapCodec::decode(
        encodedMapGap, mapGap.document, "block-attrs", mapGap.epoch, prior);
    require(!wrongMap.ok() && wrongMap.error().code() == ErrorCode::wrong_stream,
            "Markoff map gap accepted another map stream");
    QByteArray unsupportedMapGap = encodedMapGap;
    unsupportedMapGap[3] = char(2);
    require(!MapCounterGapCodec::decode(unsupportedMapGap, mapGap.document,
                mapGap.map, mapGap.epoch, prior).ok(),
            "Markoff map gap accepted an unsupported version");
    require(!MapCounterGapCodec::decode(encodedMapGap.left(encodedMapGap.size() - 1),
                mapGap.document, mapGap.map, mapGap.epoch, prior).ok(),
            "Markoff map gap accepted truncation");
    auto overflowMapGap = mapGap;
    overflowMapGap.first = std::numeric_limits<uint64_t>::max();
    overflowMapGap.count = 2;
    require(!MapCounterGapCodec::encode(overflowMapGap).ok(),
            "Markoff map gap accepted an overflowing identity interval");
}

void runScopedListRouter()
{
    const DocumentStorageScope storageScope{QStringLiteral("list-router-doc"), 1};
    const PositionScope listScope{"list-router-doc", std::string(kOrderStreamName),
                                  Primitive::list, 1};
    auto writerProvider = take(makeEphemeralDocumentStorageProvider());
    auto firstReaderProvider = take(makeEphemeralDocumentStorageProvider());
    auto secondReaderProvider = take(makeEphemeralDocumentStorageProvider());
    auto writerSession = take(writerProvider->open(storageScope, StorageMode::ephemeral));
    auto firstReaderSession = take(firstReaderProvider->open(storageScope, StorageMode::ephemeral));
    auto secondReaderSession = take(secondReaderProvider->open(storageScope, StorageMode::ephemeral));
    auto writer = take(ScopedIdListStream::create(*writerSession, listScope));
    auto firstReader = take(ScopedIdListStream::create(*firstReaderSession, listScope));
    auto secondReader = take(ScopedIdListStream::create(*secondReaderSession, listScope));
    auto quarantineProvider = take(makeEphemeralDocumentStorageProvider());
    auto quarantineState = take(ProviderBackedDocumentState::open(
        *quarantineProvider, storageScope, StorageMode::ephemeral));

    const auto first = take(writer->insertAfter(std::nullopt, 55));
    const auto second = take(writer->insertAfter(first.insertedBlock, 55));
    const auto third = take(writer->insertAfter(second.insertedBlock, 55));
    const auto envelope = [&](const LocalListOperation& operation, uint64_t sequence,
                              QByteArray actionId) {
        return RoutedListOperation{
            RemoteListOperation{listScope, operation.identity, operation.bytes},
            sequence, std::move(actionId), 0, 1};
    };
    const auto firstEnvelope = envelope(first, 1, QByteArrayLiteral("list-action-1"));
    const auto secondEnvelope = envelope(second, 2, QByteArrayLiteral("list-action-2"));
    const auto thirdEnvelope = envelope(third, 3, QByteArrayLiteral("list-action-3"));

    ScopedListRouter bounded("list-router-doc", 1, {2, 1024 * 1024});
    std::vector<CompletedListOperation> completed;
    require(bounded.offer(firstEnvelope, completed).state
                == ListRouteState::heldForUnknownStream,
            "list router did not retain a structurally valid unknown stream operation");
    auto conflictingEnvelope = firstEnvelope;
    conflictingEnvelope.actionId = QByteArrayLiteral("list-conflict-action");
    const auto conflict = bounded.offer(conflictingEnvelope, completed);
    require(conflict.state == ListRouteState::rejected
                && bounded.quarantined().size() == 1
                && bounded.heldByteCount() > 0,
            "list router did not quarantine a reused held identity with a different envelope");
    const auto missingQuarantineStore = bounded.acknowledgeQuarantine(conflictingEnvelope);
    require(!missingQuarantineStore.ok()
                && missingQuarantineStore.error().code() == ErrorCode::awaiting_state
                && bounded.heldOperationCount() == 2,
            "list router acknowledged quarantine without durable document state");
    bounded.setDocumentState(quarantineState.get());
    require(bounded.offer(secondEnvelope, completed).state == ListRouteState::backpressured,
            "list router exceeded its retained-envelope budget");
    const auto registered = take(bounded.registerStream(*firstReader));
    take(bounded.acknowledgeQuarantine(conflictingEnvelope));
    const auto quarantineRecovery = take(quarantineState->storage().readRecoveryState());
    require(quarantineRecovery.quarantinedOperations.size() == 1,
            "list router released quarantine before durable persistence");
    const auto decodedQuarantine = take(QuarantineRecordCodec::decode(
        quarantineRecovery.quarantinedOperations.front()));
    require(decodedQuarantine.primitive == QuarantinePrimitive::list
                && decodedQuarantine.document == "list-router-doc"
                && decodedQuarantine.actor == first.identity.actor
                && decodedQuarantine.counter == first.identity.counter
                && decodedQuarantine.payload == first.bytes
                && decodedQuarantine.actionId == conflictingEnvelope.actionId
                && decodedQuarantine.reason.code() == ErrorCode::actor_conflict,
            "persisted list quarantine lost its operation envelope or rejection reason");
    require(registered.size() == 1
                && registered.front().operation.operation.bytes == first.bytes
                && bounded.heldOperationCount() == 0,
            "list registration did not drain the original envelope or preserve quarantine");
    require(bounded.offer(secondEnvelope, completed).state == ListRouteState::applied,
            "list router did not allow retry after backpressure was relieved");
    const auto boundedEntries = take(firstReader->visibleEntries());
    require(boundedEntries.size() == 2
                && boundedEntries[0].identity == *first.insertedBlock
                && boundedEntries[1].identity == *second.insertedBlock,
            "list router changed repeated application values or structural order");

    ScopedListRouter causal("list-router-doc", 1);
    (void)take(causal.registerStream(*secondReader));
    completed.clear();
    const auto thirdResult = causal.offer(thirdEnvelope, completed);
    const auto secondResult = causal.offer(secondEnvelope, completed);
    require(thirdResult.state == ListRouteState::deferred
                && secondResult.state == ListRouteState::deferred
                && causal.heldOperationCount() == 2,
            "list router did not retain operations whose structural dependencies are missing");
    const auto firstResult = causal.offer(firstEnvelope, completed);
    require(firstResult.state == ListRouteState::applied,
            "first operation in list dependency chain did not apply");
    require(completed.size() == 3,
            "list router did not transitively drain all deferred structural dependencies");
    require(completed[0].operation.deliverySequence == 1
                && completed[1].operation.deliverySequence == 2
                && completed[2].operation.deliverySequence == 3
                && completed[1].operation.operation.bytes == second.bytes
                && completed[2].operation.operation.bytes == third.bytes,
            "list router reported transitive dependency completions out of order");
    const auto causalEntries = take(secondReader->visibleEntries());
    require(causalEntries.size() == 3
                && causalEntries[0].identity == *first.insertedBlock
                && causalEntries[1].identity == *second.insertedBlock
                && causalEntries[2].identity == *third.insertedBlock,
            "deferred list delivery did not preserve full insertion identities");
}

void runScopedMapRouter()
{
    const DocumentStorageScope storageScope{QStringLiteral("map-router-doc"), 1};
    const MapScope mapScope{"map-router-doc", "frontmatter", 1};
    auto writerProvider = take(makeEphemeralDocumentStorageProvider());
    auto readerProvider = take(makeEphemeralDocumentStorageProvider());
    auto writerSession = take(writerProvider->open(storageScope, StorageMode::ephemeral));
    auto readerSession = take(readerProvider->open(storageScope, StorageMode::ephemeral));
    auto writer = take(ReplicatedMapStream::create(*writerSession, mapScope));
    auto reader = take(ReplicatedMapStream::create(*readerSession, mapScope));
    require(writer->completeReplay().ok() && reader->completeReplay().ok(),
            "map router streams did not complete replay");

    const auto older = take(writer->set(QByteArrayLiteral("title"), QByteArrayLiteral("old")));
    const auto newer = take(writer->set(QByteArrayLiteral("title"), QByteArrayLiteral("new")));
    const auto envelope = [&](const LocalMapOperation& operation, uint64_t sequence,
                              QByteArray actionId) {
        return RemoteMapOperation{mapScope, operation.targetBlock, operation.bytes,
                                  sequence, std::move(actionId), 0, 1};
    };
    const auto oldEnvelope = envelope(older, 1, QByteArrayLiteral("map-action-1"));
    const auto newEnvelope = envelope(newer, 2, QByteArrayLiteral("map-action-2"));

    ScopedMapRouter bounded("map-router-doc", 1, {1, 1024 * 1024});
    std::vector<CompletedMapOperation> completed;
    require(bounded.offer(newEnvelope, completed).state == MapRouteState::heldForUnknownStream
                && bounded.heldOperationCount() == 1,
            "map router did not retain a valid operation for an unknown stream");
    require(bounded.offer(oldEnvelope, completed).state == MapRouteState::backpressured,
            "map router exceeded its retained-operation budget");
    const auto drained = take(bounded.registerStream(*reader));
    require(drained.size() == 1 && drained.front().operation.bytes == newer.bytes
                && reader->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("new"))
                && bounded.heldOperationCount() == 0,
            "map registration did not apply and release its held envelope");
    require(bounded.offer(newEnvelope, completed).state == MapRouteState::duplicate,
            "map router did not report duplicate delivery explicitly");
    require(bounded.offer(oldEnvelope, completed).state == MapRouteState::stale,
            "map router did not report an older LWW operation as stale");
    require(completed.size() == 2
                && completed[0].outcome == MapApplyKind::duplicate
                && completed[1].outcome == MapApplyKind::stale,
            "map router completion outcomes diverged from map projection outcomes");

    auto zeroBudgetProvider = take(makeEphemeralDocumentStorageProvider());
    auto zeroBudgetSession = take(zeroBudgetProvider->open(
        storageScope, StorageMode::ephemeral));
    auto zeroBudgetReader = take(ReplicatedMapStream::create(
        *zeroBudgetSession, mapScope));
    require(zeroBudgetReader->completeReplay().ok(),
            "zero-budget map stream did not complete replay");
    ScopedMapRouter zeroBudget("map-router-doc", 1, {0, 0});
    completed.clear();
    require(zeroBudget.offer(newEnvelope, completed).state
                == MapRouteState::backpressured
                && zeroBudget.heldOperationCount() == 0,
            "zero-budget map router retained an unknown stream envelope");
    (void)take(zeroBudget.registerStream(*zeroBudgetReader));
    require(zeroBudget.offer(newEnvelope, completed).state == MapRouteState::applied
                && completed.size() == 1
                && zeroBudget.heldOperationCount() == 0
                && zeroBudgetReader->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("new")),
            "map router charged an immediately applied operation to held capacity");
}

void runConcurrentOfflineListIdentity()
{
    const DocumentStorageScope storageScope{QStringLiteral("offline-list-doc"), 1};
    const PositionScope listScope{"offline-list-doc", std::string(kOrderStreamName),
                                  Primitive::list, 1};
    auto providerA = take(makeEphemeralDocumentStorageProvider());
    auto providerB = take(makeEphemeralDocumentStorageProvider());
    auto providerReader = take(makeEphemeralDocumentStorageProvider());
    auto sessionA = take(providerA->open(storageScope, StorageMode::ephemeral));
    auto sessionB = take(providerB->open(storageScope, StorageMode::ephemeral));
    auto sessionReader = take(providerReader->open(storageScope, StorageMode::ephemeral));
    auto listA = take(ScopedIdListStream::create(*sessionA, listScope));
    auto listB = take(ScopedIdListStream::create(*sessionB, listScope));
    auto listReader = take(ScopedIdListStream::create(*sessionReader, listScope));

    const auto seed = take(listA->insertAfter(std::nullopt, 7));
    const RemoteListOperation seedRemote{listScope, seed.identity, seed.bytes};
    require(take(listB->applyRemote(seedRemote)).kind() == ApplyOutcomeKind::applied
                && take(listReader->applyRemote(seedRemote)).kind() == ApplyOutcomeKind::applied,
            "offline list peers could not establish the same structural predecessor");

    const auto offlineA = take(listA->insertAfter(seed.insertedBlock, 42));
    const auto offlineB = take(listB->insertAfter(seed.insertedBlock, 42));
    require(offlineA.insertedBlock && offlineB.insertedBlock
                && *offlineA.insertedBlock != *offlineB.insertedBlock
                && offlineA.insertedBlock->counter == offlineB.insertedBlock->counter
                && offlineA.insertedBlock->actor != offlineB.insertedBlock->actor,
            "concurrent insertions with repeated application values lost actor identity");

    const RemoteListOperation remoteA{listScope, offlineA.identity, offlineA.bytes};
    const RemoteListOperation remoteB{listScope, offlineB.identity, offlineB.bytes};
    require(take(listA->applyRemote(remoteB)).kind() == ApplyOutcomeKind::applied
                && take(listB->applyRemote(remoteA)).kind() == ApplyOutcomeKind::applied
                && take(listReader->applyRemote(remoteB)).kind() == ApplyOutcomeKind::applied
                && take(listReader->applyRemote(remoteA)).kind() == ApplyOutcomeKind::applied,
            "offline concurrent list operations did not merge on all replicas");

    const auto entriesA = take(listA->visibleEntries());
    const auto entriesB = take(listB->visibleEntries());
    const auto entriesReader = take(listReader->visibleEntries());
    require(entriesA.size() == 3 && entriesB.size() == 3 && entriesReader.size() == 3,
            "offline concurrent list insertion lost a repeated-valued block");
    for (size_t i = 0; i < entriesA.size(); ++i)
        require(entriesA[i].identity == entriesB[i].identity
                    && entriesA[i].identity == entriesReader[i].identity
                    && entriesA[i].applicationValue == entriesB[i].applicationValue
                    && entriesA[i].applicationValue == entriesReader[i].applicationValue,
                "offline peers disagreed on the full structural order projection");
    require(entriesA[0].identity == *seed.insertedBlock
                && entriesA[1].applicationValue == 42
                && entriesA[2].applicationValue == 42
                && entriesA[1].identity != entriesA[2].identity,
            "caller values were used as structural identity or sibling order authority");
}

void runMapCounterGapProjection()
{
    std::array<uint8_t, ActorId::kBytes> actorBytes{};
    actorBytes[0] = 0x37;
    const ActorId remoteActor = ActorId::from_bytes(actorBytes);
    auto provider = take(makeEphemeralDocumentStorageProvider());
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("map-gap-projection"), 4},
        StorageMode::ephemeral));
    auto map = take(ReplicatedMapStream::create(
        *session, MapScope{"map-gap-projection", "frontmatter", 4}));
    require(map->completeReplay().ok(), "map-gap test did not complete its replay barrier");

    const QByteArray prior = QByteArrayLiteral("accepted-prefix-before-gap");
    const MapCounterGap gap{"map-gap-projection", "frontmatter", 4,
                            remoteActor, 5, 3, prior};
    const QByteArray bytes = take(MapCounterGapCodec::encode(gap));
    require(map->applyCommittedCounterGap(bytes, prior).ok()
                && map->applyCommittedCounterGap(bytes, prior).ok(),
            "map projection did not apply an accepted gap idempotently");
    const auto wrongPrior = map->applyCommittedCounterGap(
        bytes, QByteArrayLiteral("different-prefix"));
    require(!wrongPrior.ok() && wrongPrior.error().code() == ErrorCode::pin_conflict,
            "map projection accepted an MMG record from a different prefix");

    const MapCounterGap overlap{"map-gap-projection", "frontmatter", 4,
                                remoteActor, 7, 2, QByteArrayLiteral("later-prefix")};
    const auto overlapping = map->applyCommittedCounterGap(
        take(MapCounterGapCodec::encode(overlap)), overlap.acceptedPrior);
    require(!overlapping.ok() && overlapping.error().code() == ErrorCode::pin_conflict,
            "map projection accepted overlapping terminal intervals");

    const auto covered = take(MarkoffMapCodec::encode(MarkoffMapOperation{
        "frontmatter", QByteArrayLiteral("covered"), QByteArrayLiteral("bad"),
        remoteActor, 6, false}));
    const auto coveredResult = map->applyRemote(covered);
    require(!coveredResult.ok() && coveredResult.error().code() == ErrorCode::invalid_identity,
            "map projection accepted an operation identity covered by MMG");

    const auto afterGap = take(MarkoffMapCodec::encode(MarkoffMapOperation{
        "frontmatter", QByteArrayLiteral("after"), QByteArrayLiteral("good"),
        remoteActor, 8, false}));
    require(take(map->applyRemote(afterGap)) == MapApplyKind::applied,
            "map projection refused an operation after terminal gap coverage");

    auto localMap = take(ReplicatedMapStream::create(
        *session, MapScope{"map-gap-projection", "custom", 4}));
    require(localMap->completeReplay().ok(),
            "local map-gap test did not complete its replay barrier");
    const MapCounterGap localGap{"map-gap-projection", "custom", 4,
        session->actor(), 1, 2, QByteArrayLiteral("local-prior")};
    const QByteArray localGapBytes = take(MapCounterGapCodec::encode(localGap));
    require(localMap->applyCommittedCounterGap(localGapBytes, localGap.acceptedPrior).ok(),
            "local map projection did not observe its committed gap interval");
    const auto localAfterGap = take(localMap->set(
        QByteArrayLiteral("kind"), QByteArrayLiteral("paragraph")));
    require(localAfterGap.operation.counter == 3,
            "local map allocation did not advance beyond its committed gap");
}

void runTextCounterGapProjection()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("text-gap-projection"), 6},
        StorageMode::ephemeral));
    const BlockIdentity block{session->actor(), 22};
    const PositionScope scope{"text-gap-projection",
        take(blockTextStreamName(block)), Primitive::text, 6};
    auto text = take(ScopedTextStream::create(*session, scope));
    const QByteArray prior = QByteArrayLiteral("accepted-prefix-before-text-gap");
    const ScopedCounterGap gap{scope,
        Crdt::CounterGap{session->actor(), 1, 2, Crdt::Global{}}, prior};
    const QByteArray bytes = take(CounterGapCodec::encode(gap));
    const auto applied = take(text->applyCommittedCounterGap(bytes, prior));
    require(applied.kind() == ApplyOutcomeKind::applied,
            "text projection did not apply its committed MTG/1 interval");
    const auto next = take(text->replaceUtf8(0, 0, QByteArrayLiteral("after gap")));
    require(next.identity.actor == session->actor() && next.identity.counter > 2,
            "text allocation did not advance beyond its committed same-actor gap");

    const auto wrongPrior = text->applyCommittedCounterGap(
        bytes, QByteArrayLiteral("different-prefix"));
    require(!wrongPrior.ok() && wrongPrior.error().code() == ErrorCode::pin_conflict,
            "text projection accepted a gap from a different accepted prefix");
}

void runSourceSnapshotCodec()
{
    const QByteArray source = QByteArrayLiteral("# T\r\n\r\nbody\n");
    SourceSnapshotRecord record;
    record.document = "source-doc";
    record.epoch = 3;
    record.originalMarkdown = source;
    record.separators.push_back({3, QByteArrayLiteral("\r\n\r\n")});
    record.parserVersion = "markoff-parser/1";
    record.parserCache = QByteArray::fromHex(QByteArrayLiteral("00ff1020"));
    const QByteArray encoded = take(SourceSnapshotCodec::encode(record));
    const QByteArray literalSnapshot = QByteArray::fromHex(QByteArrayLiteral(
        "4d4453010000000a736f757263652d646f6300000000000000030000000c2320540d0a0d0a"
        "626f64790a000000010000000000000003000000040d0a0d0a000000106d61726b6f66662d"
        "7061727365722f3100000020ae1daef04ddab3808a9a67f417c720cafdcff2bceb27502066"
        "ec77a5fd4439d9000000204033e6f229164922f1600f00a2dacd22e9b9bbdad58f82dd9509"
        "5b0bb648eb830000000400ff1020"));
    require(encoded == literalSnapshot,
            "source snapshot codec no longer matches its literal byte fixture");
    const auto decoded = take(SourceSnapshotCodec::decode(encoded, "source-doc", 3));
    require(decoded.originalMarkdown == source && decoded.separators == record.separators
                && decoded.parserVersion == record.parserVersion
                && decoded.parserCache == record.parserCache,
            "source snapshot codec did not preserve original bytes, separators and cache");

    auto wrongScope = SourceSnapshotCodec::decode(encoded, "other-doc", 3);
    require(!wrongScope.ok() && wrongScope.error().code() == ErrorCode::wrong_document,
            "source snapshot accepted another document");
    QByteArray unsupported = encoded;
    unsupported[3] = char(2);
    require(!SourceSnapshotCodec::decode(unsupported, "source-doc", 3).ok(),
            "source snapshot accepted an unsupported version");
    require(!SourceSnapshotCodec::decode(encoded.left(encoded.size() - 1),
                "source-doc", 3).ok(),
            "source snapshot accepted truncation");
    QByteArray staleCache = encoded;
    staleCache[32] = 'X'; // Change source text but leave its separator and cache digest intact.
    require(!SourceSnapshotCodec::decode(staleCache, "source-doc", 3).ok(),
            "source snapshot accepted a parser cache for different source bytes");
    QByteArray corruptedCache = encoded;
    corruptedCache[corruptedCache.size() - 1] ^= char(1);
    require(!SourceSnapshotCodec::decode(corruptedCache, "source-doc", 3).ok(),
            "source snapshot accepted a corrupted parser cache");

    auto invalidSeparator = record;
    invalidSeparator.separators[0].bytes = QByteArrayLiteral("\n\n\n\n");
    require(!SourceSnapshotCodec::encode(invalidSeparator).ok(),
            "source snapshot accepted separators that do not match exact bytes");

    ActionBundleCodec::Bundle initialImport;
    initialImport.document = record.document;
    initialImport.epoch = record.epoch;
    initialImport.deliverySequence = 1;
    initialImport.actionId = QByteArrayLiteral("source-import-1");
    initialImport.kind = ActionKind::initialImport;
    initialImport.sourceSnapshot = encoded;
    const QByteArray importBundle = take(ActionBundleCodec::encode(initialImport));
    const QByteArray literalImportBundle = QByteArray::fromHex(QByteArrayLiteral(
        "4d414203000a736f757263652d646f6300000000000000030000000000000001000f736f"
        "757263652d696d706f72742d310400000000000000a24d4453010000000a736f757263652d"
        "646f6300000000000000030000000c2320540d0a0d0a626f64790a00000001000000000000"
        "0003000000040d0a0d0a000000106d61726b6f66662d7061727365722f3100000020ae1d"
        "aef04ddab3808a9a67f417c720cafdcff2bceb27502066ec77a5fd4439d9000000204033"
        "e6f229164922f1600f00a2dacd22e9b9bbdad58f82dd95095b0bb648eb830000000400ff"
        "1020"));
    require(importBundle == literalImportBundle,
            "initial source MAB/3 differs from its literal wire fixture");
    const auto decodedImport = take(ActionBundleCodec::decode(
        importBundle, record.document, record.epoch));
    require(decodedImport.sourceSnapshot == encoded,
            "MAB/3 lost its exact initial source snapshot");
    auto replacement = initialImport;
    replacement.actionId = QByteArrayLiteral("source-replacement-1");
    replacement.deliverySequence = 2;
    replacement.kind = ActionKind::replacement;
    const std::array<uint8_t, ActorId::kBytes> replacementActorBytes{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const auto replacementActor = ActorId::from_bytes(replacementActorBytes);
    replacement.operations.push_back({
        "markoff/map/v1/frontmatter", OperationPrimitive::markoffMap,
        std::nullopt, replacementActor,
        1, OperationCodec::markoffOwned, QByteArrayLiteral("opaque")});
    // Use a well-formed Markoff-owned operation payload so the envelope's
    // redundant identity fields are verified by the same production decoder.
    const QByteArray replacementPayload = take(MarkoffMapCodec::encode({
        "frontmatter", QByteArrayLiteral("raw"), QByteArrayLiteral("title: New"),
        replacementActor, 1, false}));
    replacement.operations.front().payload = replacementPayload;
    const QByteArray replacementBundle = take(ActionBundleCodec::encode(replacement));
    const auto decodedReplacement = take(ActionBundleCodec::decode(
        replacementBundle, record.document, record.epoch));
    require(decodedReplacement.kind == ActionKind::replacement
                && decodedReplacement.sourceSnapshot == encoded
                && decodedReplacement.operations.size() == 1,
            "replacement MAB/3 did not preserve its exact source snapshot and operation");
    auto replacementUndo = replacement;
    replacementUndo.actionId = QByteArrayLiteral("source-replacement-undo-1");
    replacementUndo.deliverySequence = 3;
    replacementUndo.kind = ActionKind::undo;
    const auto decodedReplacementUndo = take(ActionBundleCodec::decode(
        take(ActionBundleCodec::encode(replacementUndo)), record.document, record.epoch));
    require(decodedReplacementUndo.kind == ActionKind::undo
                && decodedReplacementUndo.sourceSnapshot == encoded,
            "replacement undo MAB/3 rejected its exact restored-source snapshot");
    auto wrongKind = initialImport;
    wrongKind.kind = ActionKind::edit;
    require(!ActionBundleCodec::encode(wrongKind).ok(),
            "MAB accepted initial-source metadata on an ordinary edit");
    auto missingSource = initialImport;
    missingSource.sourceSnapshot.clear();
    require(!ActionBundleCodec::encode(missingSource).ok(),
            "new initial import was encoded without original source bytes");
}

void runCrossStreamHistoryCodec()
{
    const std::array<uint8_t, ActorId::kBytes> actorBytes{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const ActorId owner = ActorId::from_bytes(actorBytes);
    const auto blockStream = take(blockTextStreamName(BlockIdentity{owner, 1}));
    const Crdt::Identity orderGroup{owner, 5};
    const Crdt::Identity textGroup{owner, 10};
    CrossStreamHistoryIndex index;
    index.document = "history-doc";
    index.epoch = 4;
    index.owner = owner;
    index.acceptedPrior = QByteArrayLiteral("revision-4");
    SourceSnapshotRecord priorReplacementSource;
    priorReplacementSource.document = index.document;
    priorReplacementSource.epoch = index.epoch;
    priorReplacementSource.originalMarkdown = QByteArrayLiteral("prior source\n");
    index.events.push_back({QByteArrayLiteral("A"), ActionKind::replacement, {}, {
        {std::string(kOrderStreamName), OperationPrimitive::idList, orderGroup,
         {{owner, 6}}},
        {blockStream, OperationPrimitive::text, textGroup,
         {{owner, 10}, {owner, 11}}}},
        take(SourceSnapshotCodec::encode(priorReplacementSource))});
    HistoryGroupAssociation mapHistory;
    mapHistory.stream = take(mapStreamName("frontmatter"));
    mapHistory.primitive = OperationPrimitive::markoffMap;
    mapHistory.groupId = {owner, 16};
    mapHistory.operationIds = {{owner, 16}};
    mapHistory.hasMapPriorState = true;
    mapHistory.mapPriorValueExisted = true;
    mapHistory.mapPriorValue = QByteArrayLiteral("prior-map-value");
    index.events.back().groups.push_back(mapHistory);
    index.events.push_back({QByteArrayLiteral("U"), ActionKind::undo,
        QByteArrayLiteral("A"), {
            {std::string(kOrderStreamName), OperationPrimitive::idList, orderGroup,
             {{owner, 12}}},
            {blockStream, OperationPrimitive::text, textGroup,
             {{owner, 13}}}}});
    index.events.push_back({QByteArrayLiteral("R"), ActionKind::redo,
        QByteArrayLiteral("U"), {
            {std::string(kOrderStreamName), OperationPrimitive::idList, orderGroup,
             {{owner, 14}}},
            {blockStream, OperationPrimitive::text, textGroup,
             {{owner, 15}}}}});
    index.retirements.push_back({std::string(kOrderStreamName),
        OperationPrimitive::idList, orderGroup, QByteArrayLiteral("R")});

    const QByteArray encoded = take(CrossStreamHistoryCodec::encode(index));
    auto emptyIndex = index;
    emptyIndex.events.clear();
    emptyIndex.retirements.clear();
    const QByteArray literalIndex = QByteArray::fromHex(QByteArrayLiteral(
        "4d414903000b686973746f72792d646f630000000000000004"
        "000102030405060708090a0b0c0d0e0f000a7265766973696f6e2d34"
        "0000000000000000"));
    require(take(CrossStreamHistoryCodec::encode(emptyIndex)) == literalIndex,
            "cross-stream history codec no longer matches its literal wire fixture");
    const QByteArray unsupportedVersionIndex = QByteArray::fromHex(QByteArrayLiteral(
        "4d414904000b686973746f72792d646f630000000000000004"
        "000102030405060708090a0b0c0d0e0f000a7265766973696f6e2d34"
        "0000000000000000"));
    const auto unsupportedVersionHistory = CrossStreamHistoryCodec::decode(unsupportedVersionIndex,
        index.document, index.epoch, owner, index.acceptedPrior);
    require(!unsupportedVersionHistory.ok()
                && unsupportedVersionHistory.error().code() == ErrorCode::unsupported_version,
            "obsolete MAI/2 history version was accepted");
    const auto decoded = take(CrossStreamHistoryCodec::decode(
        encoded, index.document, index.epoch, owner, index.acceptedPrior));
    require(decoded.events == index.events && decoded.retirements == index.retirements,
            "cross-stream history round-trip lost action, group or retirement associations");

    OwnerHistorySnapshot ownerSnapshot;
    ownerSnapshot.document = index.document;
    ownerSnapshot.epoch = index.epoch;
    ownerSnapshot.owner = owner;
    ownerSnapshot.transactionId = QByteArrayLiteral("R");
    ownerSnapshot.acceptedPrior = index.acceptedPrior;
    ownerSnapshot.streams.push_back({
        PositionScope{index.document, std::string(kOrderStreamName), Primitive::list, index.epoch},
        owner, {}});
    ownerSnapshot.streams.push_back({
        PositionScope{index.document, blockStream, Primitive::text, index.epoch}, owner, {}});
    ownerSnapshot.actionIndex = index;
    const QByteArray ownerBytes = take(OwnerHistoryCodec::encode(ownerSnapshot));
    auto invalidHistoryRoute = ownerSnapshot;
    invalidHistoryRoute.streams.front().scope.stream = "markoff/text/forged";
    require(!OwnerHistoryCodec::encode(invalidHistoryRoute).ok(),
            "owner history accepted a noncanonical Markoff order-stream route");
    auto missingStreamHistory = ownerSnapshot;
    missingStreamHistory.streams.pop_back();
    const auto missingStreamHistoryResult = OwnerHistoryCodec::encode(missingStreamHistory);
    require(!missingStreamHistoryResult.ok()
                && missingStreamHistoryResult.error().code() == ErrorCode::history_unavailable,
            "owner history omitted a text stream referenced by its action index");
    QByteArray forgedOwnerRoute = ownerBytes;
    forgedOwnerRoute.replace(QByteArrayLiteral("markoff/order/v1"),
                             QByteArrayLiteral("markoff/other/v1"));
    require(!OwnerHistoryCodec::decode(forgedOwnerRoute, index.document, index.epoch,
                owner, ownerSnapshot.transactionId, ownerSnapshot.acceptedPrior).ok(),
            "owner history decoder accepted a noncanonical Markoff order-stream route");
    const auto restoredOwner = take(OwnerHistoryCodec::decode(
        ownerBytes, index.document, index.epoch, owner,
        ownerSnapshot.transactionId, index.acceptedPrior));
    require(restoredOwner.streams.size() == 2
                && restoredOwner.streams[0].scope.stream == kOrderStreamName
                && restoredOwner.streams[1].scope.stream == blockStream
                && restoredOwner.actionIndex.events == index.events,
            "composite owner history did not bind MAH streams to the MAI index");
    require(!OwnerHistoryCodec::decode(ownerBytes, index.document, index.epoch, owner,
                QByteArrayLiteral("another-transaction"), index.acceptedPrior).ok(),
            "composite owner history accepted another transaction ID");
    require(!OwnerHistoryCodec::decode(ownerBytes, index.document, index.epoch,
                owner, ownerSnapshot.transactionId, QByteArrayLiteral("different-revision")).ok(),
            "composite owner history accepted an index from another revision");
    QByteArray badOwnerHistoryVersion = ownerBytes;
    badOwnerHistoryVersion[3] = char(2);
    const auto badOwnerVersion = OwnerHistoryCodec::decode(
        badOwnerHistoryVersion, index.document, index.epoch, owner,
        ownerSnapshot.transactionId, index.acceptedPrior);
    require(!badOwnerVersion.ok()
                && badOwnerVersion.error().code() == ErrorCode::unsupported_version,
            "composite owner history accepted an unsupported version");
    require(!OwnerHistoryCodec::decode(ownerBytes.left(ownerBytes.size() - 1),
                index.document, index.epoch, owner, ownerSnapshot.transactionId,
                index.acceptedPrior).ok(),
            "composite owner history accepted truncation");
    const auto wrongRevision = CrossStreamHistoryCodec::decode(
        encoded, index.document, index.epoch, owner, QByteArrayLiteral("revision-5"));
    require(!wrongRevision.ok() && wrongRevision.error().code() == ErrorCode::pin_conflict,
            "cross-stream history accepted a different accepted-prefix revision");
    auto wrongOwner = owner;
    auto foreignBytes = actorBytes;
    foreignBytes[0] ^= 1;
    wrongOwner = ActorId::from_bytes(foreignBytes);
    const auto foreignHistory = CrossStreamHistoryCodec::decode(
        encoded, index.document, index.epoch, wrongOwner, index.acceptedPrior);
    require(!foreignHistory.ok() && foreignHistory.error().code() == ErrorCode::actor_conflict,
            "cross-stream history accepted another owner actor");
    auto duplicate = index;
    duplicate.events[0].groups.push_back(duplicate.events[0].groups[0]);
    require(!CrossStreamHistoryCodec::encode(duplicate).ok(),
            "cross-stream history accepted duplicate group claims");
    auto duplicateOperation = index;
    duplicateOperation.events[1].groups[0].operationIds[0] =
        duplicateOperation.events[0].groups[0].operationIds[0];
    require(!CrossStreamHistoryCodec::encode(duplicateOperation).ok(),
            "cross-stream history associated one operation with multiple actions");
    auto mapWithoutPrior = index;
    mapWithoutPrior.events[0].groups.back().hasMapPriorState = false;
    mapWithoutPrior.events[0].groups.back().mapPriorValueExisted = false;
    mapWithoutPrior.events[0].groups.back().mapPriorValue.clear();
    require(!CrossStreamHistoryCodec::encode(mapWithoutPrior).ok(),
            "MAI/3 accepted a map action without its reversible prior state");
    auto invalidMapPrior = index;
    invalidMapPrior.events[0].groups.back().mapPriorValueExisted = false;
    require(!CrossStreamHistoryCodec::encode(invalidMapPrior).ok(),
            "MAI/3 accepted bytes for an absent map prior value");
    auto orphanParent = index;
    orphanParent.events[1].parentActionId = QByteArrayLiteral("missing");
    require(!CrossStreamHistoryCodec::encode(orphanParent).ok(),
            "cross-stream history accepted an unknown undo parent");
    auto redoOfEdit = index;
    redoOfEdit.events[2].parentActionId = QByteArrayLiteral("A");
    require(!CrossStreamHistoryCodec::encode(redoOfEdit).ok(),
            "cross-stream history accepted a redo whose parent was not an undo");
    auto undoOfUndo = index;
    undoOfUndo.events[2].kind = ActionKind::undo;
    undoOfUndo.events[2].parentActionId = QByteArrayLiteral("U");
    require(!CrossStreamHistoryCodec::encode(undoOfUndo).ok(),
            "cross-stream history accepted an undo whose parent was an undo");
    auto unrelatedUndoGroup = index;
    unrelatedUndoGroup.events.insert(unrelatedUndoGroup.events.begin() + 1,
        {QByteArrayLiteral("B"), ActionKind::edit, {}, {
            {std::string(kOrderStreamName), OperationPrimitive::idList,
             {owner, 20}, {{owner, 21}}}}});
    unrelatedUndoGroup.events[2].groups.front() =
        unrelatedUndoGroup.events[1].groups.front();
    unrelatedUndoGroup.events[2].groups.front().operationIds = {{owner, 22}};
    require(!CrossStreamHistoryCodec::encode(unrelatedUndoGroup).ok(),
            "cross-stream history allowed undo to compensate a sibling action group");
    auto retirementByEdit = index;
    retirementByEdit.retirements.front().retirementActionId = QByteArrayLiteral("A");
    require(!CrossStreamHistoryCodec::encode(retirementByEdit).ok(),
            "cross-stream history allowed an edit action to retire a redoable group");
    QByteArray unsupported = encoded;
    unsupported[3] = char(4);
    require(!CrossStreamHistoryCodec::decode(
                unsupported, index.document, index.epoch, owner, index.acceptedPrior).ok(),
            "cross-stream history accepted an unsupported version");
    require(!CrossStreamHistoryCodec::decode(encoded.left(encoded.size() - 1),
                index.document, index.epoch, owner, index.acceptedPrior).ok(),
            "cross-stream history accepted truncation");
}

void runActionPublicationCoordinatorRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create action-coordinator recovery directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("coordinator-doc"), 2};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    const auto before = take(session->readRecoveryState());
    const QByteArray transactionId = QByteArrayLiteral("coordinator-import-1");
    SourceSnapshotRecord source;
    source.document = "coordinator-doc";
    source.epoch = 2;
    source.originalMarkdown = QByteArrayLiteral("# Durable source\r\n");
    require(session->beginInitialImport(
                PendingImportIntent{transactionId, source.originalMarkdown}).ok(),
            "provider refused the exact durable first-import intent");
    require(session->beginInitialImport(
                PendingImportIntent{transactionId, source.originalMarkdown}).ok(),
            "provider refused an exact retry of the durable import intent");
    const auto conflictingIntent = session->beginInitialImport(PendingImportIntent{
        transactionId, QByteArrayLiteral("different source")});
    require(!conflictingIntent.ok()
                && conflictingIntent.error().code() == ErrorCode::pin_conflict,
            "provider rebound a pending import ID to different source bytes");
    session.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(storageScope, StorageMode::durable));
    const auto reopenedIntent = take(session->readRecoveryState()).initialImport;
    require(reopenedIntent
                && reopenedIntent->transactionId == transactionId
                && reopenedIntent->sourceMarkdown == source.originalMarkdown,
            "provider did not persist the exact first-import intent across reopen");
    const MapScope mapScope{"coordinator-doc", "frontmatter", 2};
    const std::string mapRoute = take(mapStreamName(mapScope.map));
    const auto reservation = take(session->reserve(
        QString::fromStdString(mapRoute), 1));
    const MarkoffMapOperation mapPayload{
        mapScope.map, QByteArrayLiteral("title"), QByteArrayLiteral("Imported"),
        session->actor(), reservation.first, false};
    const QByteArray mapBytes = take(MarkoffMapCodec::encode(mapPayload));
    const auto mapAssociation = take(toHistoryAssociation(
        LocalMapOperation{mapScope, std::nullopt, mapPayload, mapBytes}));

    const QByteArray sourceBytes = take(SourceSnapshotCodec::encode(source));
    ActionBundleCodec::Bundle bundle;
    bundle.document = source.document;
    bundle.epoch = source.epoch;
    bundle.deliverySequence = 1;
    bundle.actionId = transactionId;
    bundle.kind = ActionKind::initialImport;
    bundle.sourceSnapshot = sourceBytes;
    bundle.operations.push_back({mapRoute, OperationPrimitive::markoffMap, std::nullopt,
        session->actor(), reservation.first, OperationCodec::markoffOwned, mapBytes});

    CrossStreamHistoryIndex actionIndex;
    actionIndex.document = source.document;
    actionIndex.epoch = source.epoch;
    actionIndex.owner = session->actor();
    actionIndex.acceptedPrior = before.acceptedRevision.token;
    actionIndex.events.push_back({transactionId, ActionKind::initialImport, {},
                                  {mapAssociation}});
    OwnerHistorySnapshot ownerHistory;
    ownerHistory.document = source.document;
    ownerHistory.epoch = source.epoch;
    ownerHistory.owner = session->actor();
    ownerHistory.transactionId = transactionId;
    ownerHistory.acceptedPrior = before.acceptedRevision.token;
    ownerHistory.actionIndex = actionIndex;

    PreparedStorageTransaction transaction;
    transaction.transactionId = transactionId;
    transaction.expectedRevision = before.acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = take(ActionBundleCodec::encode(bundle));
    auto ordinaryAction = bundle;
    ordinaryAction.actionId = QByteArrayLiteral("ordinary-action-1");
    ordinaryAction.deliverySequence = 2;
    ordinaryAction.kind = ActionKind::edit;
    ordinaryAction.sourceSnapshot.clear();
    const QByteArray ordinaryBytes = take(ActionBundleCodec::encode(ordinaryAction));
    require(ordinaryBytes.left(4) == QByteArrayLiteral("MAB\x03")
                && ordinaryBytes.endsWith(QByteArray(4, char(0))),
            "ordinary action did not use canonical MAB/3 with an empty source field");
    require(take(ActionBundleCodec::decode(ordinaryBytes, source.document, source.epoch))
                .sourceSnapshot.isEmpty(),
            "canonical MAB/3 ordinary action acquired an import source");
    QByteArray obsoleteBundle = ordinaryBytes;
    obsoleteBundle[3] = char(1);
    const auto obsoleteRead = ActionBundleCodec::decode(
        obsoleteBundle, source.document, source.epoch);
    require(!obsoleteRead.ok()
                && obsoleteRead.error().code() == ErrorCode::unsupported_version,
            "noncanonical MAB/1 action was accepted");
    obsoleteBundle = ordinaryBytes;
    obsoleteBundle[3] = char(2);
    const auto mab2Read = ActionBundleCodec::decode(
        obsoleteBundle, source.document, source.epoch);
    require(!mab2Read.ok()
                && mab2Read.error().code() == ErrorCode::unsupported_version,
            "abandoned MAB/2 action was accepted");
    auto unsupportedCodecAction = ordinaryAction;
    const BlockIdentity invalidCodecBlock{session->actor(), reservation.first};
    unsupportedCodecAction.operations.push_back({
        take(blockTextStreamName(invalidCodecBlock)), OperationPrimitive::text,
        invalidCodecBlock, session->actor(), reservation.first,
        static_cast<OperationCodec>(1), QByteArrayLiteral("unsupported")});
    require(!ActionBundleCodec::encode(unsupportedCodecAction).ok(),
            "abandoned CTBO/0002 operation codec was accepted");
    transaction.reservations = {reservation};
    transaction.operationIdentities = {{QString::fromStdString(mapRoute),
                                         session->actor(), reservation.first}};
    transaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(ownerHistory));

    auto prematureEdit = bundle;
    prematureEdit.kind = ActionKind::edit;
    prematureEdit.sourceSnapshot.clear();
    auto prematureEditTransaction = transaction;
    prematureEditTransaction.sharedRecord = take(ActionBundleCodec::encode(prematureEdit));
    ActionPublicationCoordinator pendingImportCoordinator(*session);
    const auto rejectedEdit = pendingImportCoordinator.publish(prematureEditTransaction);
    require(rejectedEdit.state == ActionPublicationState::rejected,
            "coordinator allowed an edit before the pending initial import");

    auto changedSource = source;
    changedSource.originalMarkdown = QByteArrayLiteral("# Changed source\n");
    auto changedImport = bundle;
    changedImport.sourceSnapshot = take(SourceSnapshotCodec::encode(changedSource));
    auto changedTransaction = transaction;
    changedTransaction.sharedRecord = take(ActionBundleCodec::encode(changedImport));
    ActionPublicationCoordinator wrongIntentCoordinator(*session);
    const auto wrongIntent = wrongIntentCoordinator.publish(changedTransaction);
    const auto afterWrongIntent = take(session->readRecoveryState());
    require(wrongIntent.state == ActionPublicationState::rejected
                && afterWrongIntent.unresolvedTransactions.empty()
                && afterWrongIntent.unresolvedReservations.size() == 1,
            "coordinator accepted an initial MAB whose source differs from its durable intent");

    auto incompleteHistory = ownerHistory;
    incompleteHistory.actionIndex.events.back().groups.clear();
    auto incompleteTransaction = transaction;
    incompleteTransaction.localHistoryAfterCommit =
        take(OwnerHistoryCodec::encode(incompleteHistory));
    ActionPublicationCoordinator invalidHistoryCoordinator(*session);
    const auto invalidHistoryResult = invalidHistoryCoordinator.publish(incompleteTransaction);
    const auto afterRejectedHistory = take(session->readRecoveryState());
    require(invalidHistoryResult.state == ActionPublicationState::rejected
                && afterRejectedHistory.unresolvedTransactions.empty()
                && afterRejectedHistory.unresolvedReservations.size() == 1,
            "coordinator accepted owner history that omitted its MAB map operation");

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::historyBeforeAtomicRename);
    ActionPublicationCoordinator coordinator(*session);
    const auto pendingHistory = coordinator.publish(transaction);
    require(pendingHistory.state == ActionPublicationState::committedHistoryPending
                && pendingHistory.committedRevision && coordinator.blocked(),
            "coordinator did not retain its barrier after private-history failure");
    const auto beforeHistoryRecovery = take(session->readRecoveryState());
    require(beforeHistoryRecovery.acceptedRecords.size() == 1
                && beforeHistoryRecovery.unresolvedTransactions.size() == 1
                && !beforeHistoryRecovery.localHistory,
            "history failure lost the committed action or cleared its preparation");
    const auto blockedRetry = coordinator.publish(transaction);
    require(blockedRetry.state == ActionPublicationState::uncertain
                && coordinator.blocked(),
            "coordinator allowed another write while committed private history was pending");

    session.reset();
    provider.reset();
    auto reopenedProvider = take(makeReferenceDocumentStorageProvider(root));
    auto reopenedDocument = take(ProviderBackedDocumentState::open(
        *reopenedProvider, storageScope, StorageMode::durable));
    require(reopenedDocument->state() == ProviderDocumentState::ready,
            "document lifecycle did not finish committed owner history on reopen");
    const auto& after = reopenedDocument->recovery();
    require(after.acceptedRecords.size() == 1
                && after.acceptedRecords.front().transactionId == transactionId
                && after.unresolvedTransactions.empty()
                && after.localHistory
                && after.localHistory->committedTransactionId == transactionId
                && after.localHistory->acceptedRevision == after.acceptedRevision
                && after.localHistory->bytes == transaction.localHistoryAfterCommit
                && !after.initialImport,
            "lifecycle recovery lost the committed action or exact owner history");
}

void runEmptyInitialImportAbsenceRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create empty-import recovery directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("empty-import-doc"), 1};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(scope, StorageMode::durable));
    const auto before = take(session->readRecoveryState());
    const QByteArray transactionId = QByteArrayLiteral("empty-import-1");
    require(session->beginInitialImport(PendingImportIntent{transactionId, {}}).ok(),
            "provider refused an empty Markdown import intent");

    SourceSnapshotRecord source;
    source.document = "empty-import-doc";
    source.epoch = 1;
    source.originalMarkdown = {};
    ActionBundleCodec::Bundle bundle;
    bundle.document = source.document;
    bundle.epoch = source.epoch;
    bundle.deliverySequence = 1;
    bundle.actionId = transactionId;
    bundle.kind = ActionKind::initialImport;
    bundle.sourceSnapshot = take(SourceSnapshotCodec::encode(source));

    CrossStreamHistoryIndex index;
    index.document = source.document;
    index.epoch = source.epoch;
    index.owner = session->actor();
    index.acceptedPrior = before.acceptedRevision.token;
    index.events.push_back({transactionId, ActionKind::initialImport, {}, {}});
    OwnerHistorySnapshot history;
    history.document = source.document;
    history.epoch = source.epoch;
    history.owner = session->actor();
    history.transactionId = transactionId;
    history.acceptedPrior = before.acceptedRevision.token;
    history.actionIndex = index;

    PreparedStorageTransaction transaction;
    transaction.transactionId = transactionId;
    transaction.expectedRevision = before.acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = take(ActionBundleCodec::encode(bundle));
    transaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(history));

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
    ActionPublicationCoordinator coordinator(*session);
    const auto absent = coordinator.publish(transaction);
    const auto afterAbsence = take(session->readRecoveryState());
    require(absent.state == ActionPublicationState::absent
                && !coordinator.blocked()
                && afterAbsence.unresolvedTransactions.empty()
                && afterAbsence.unresolvedReservations.empty(),
            "zero-operation absent import left a preparation or write barrier behind");

    bool rebuilt = false;
    bool recoveryBarrierHeld = false;
    const auto recovered = coordinator.recoverAndRebuild(transaction,
        [&](const StorageRecoveryState& accepted) -> Result<void> {
            recoveryBarrierHeld = coordinator.blocked();
            auto projection = AcceptedPrefixProjection::replay(*session, accepted);
            if (!projection.ok()) return projection.error();
            rebuilt = true;
            return {};
        });
    require(recovered.state == ActionPublicationState::absent && rebuilt
                && recoveryBarrierHeld
                && !coordinator.blocked(),
            "zero-operation absence recovery stayed blocked despite having no gaps to publish");

    const DocumentStorageScope lifecycleScope{
        QStringLiteral("empty-import-lifecycle"), 1};
    auto lifecycle = take(ProviderBackedDocumentState::open(
        *provider, lifecycleScope, StorageMode::durable));
    const QByteArray firstId = QByteArrayLiteral("empty-lifecycle-attempt-1");
    const QByteArray retryId = QByteArrayLiteral("empty-lifecycle-attempt-2");
    const PendingImportIntent firstIntent{firstId, {}};
    take(lifecycle->authorizeInitialImport(firstIntent));
    const auto makeEmptyImport = [&](const QByteArray& id) {
        SourceSnapshotRecord retrySource;
        retrySource.document = "empty-import-lifecycle";
        retrySource.epoch = 1;
        ActionBundleCodec::Bundle retryBundle;
        retryBundle.document = retrySource.document;
        retryBundle.epoch = retrySource.epoch;
        retryBundle.deliverySequence = 1;
        retryBundle.actionId = id;
        retryBundle.kind = ActionKind::initialImport;
        retryBundle.sourceSnapshot = take(SourceSnapshotCodec::encode(retrySource));

        CrossStreamHistoryIndex retryIndex;
        retryIndex.document = retrySource.document;
        retryIndex.epoch = retrySource.epoch;
        retryIndex.owner = lifecycle->storage().actor();
        retryIndex.acceptedPrior = lifecycle->recovery().acceptedRevision.token;
        retryIndex.events.push_back({id, ActionKind::initialImport, {}, {}});
        OwnerHistorySnapshot retryHistory;
        retryHistory.document = retrySource.document;
        retryHistory.epoch = retrySource.epoch;
        retryHistory.owner = retryIndex.owner;
        retryHistory.transactionId = id;
        retryHistory.acceptedPrior = retryIndex.acceptedPrior;
        retryHistory.actionIndex = retryIndex;

        PreparedStorageTransaction retryTransaction;
        retryTransaction.transactionId = id;
        retryTransaction.expectedRevision = lifecycle->recovery().acceptedRevision;
        retryTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        retryTransaction.sharedRecord = take(ActionBundleCodec::encode(retryBundle));
        retryTransaction.localHistoryAfterCommit = take(
            OwnerHistoryCodec::encode(retryHistory));
        return retryTransaction;
    };
    const auto firstTransaction = makeEmptyImport(firstId);
    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
    const auto failedImport = lifecycle->publishAction(firstTransaction);
    require(failedImport.state == ActionPublicationState::absent
                && lifecycle->state() == ProviderDocumentState::initialImportAuthorized,
            "empty initial import absence lost its retryable lifecycle state");
    const PendingImportIntent changedRetry{
        QByteArrayLiteral("empty-lifecycle-changed-source"),
        QByteArrayLiteral("different source")};
    const auto changedRetryResult = lifecycle->authorizeInitialImport(changedRetry);
    require(!changedRetryResult.ok()
                && changedRetryResult.error().code() == ErrorCode::pin_conflict,
            "lifecycle owner rebound a proven-absent import to different source bytes");
    lifecycle.reset();
    lifecycle = take(ProviderBackedDocumentState::open(
        *provider, lifecycleScope, StorageMode::durable));
    require(lifecycle->state() == ProviderDocumentState::initialImportAuthorized
                && lifecycle->recovery().initialImport == firstIntent
                && lifecycle->recovery().unresolvedTransactions.empty()
                && lifecycle->recovery().unresolvedReservations.empty(),
            "reopen lost the proven-absent initial-import intent or terminal state");
    const PendingImportIntent retryIntent{retryId, {}};
    take(lifecycle->authorizeInitialImport(retryIntent));
    require(lifecycle->recovery().initialImport == retryIntent,
            "lifecycle owner did not rebind the exact source after proven absence");
    const auto retriedImport = lifecycle->publishAction(makeEmptyImport(retryId));
    require(retriedImport.state == ActionPublicationState::committed
                && lifecycle->state() == ProviderDocumentState::ready
                && lifecycle->recovery().acceptedRecords.size() == 1
                && lifecycle->recovery().acceptedRecords.front().transactionId == retryId
                && !lifecycle->recovery().initialImport,
            "lifecycle owner did not commit a fresh zero-operation import identity");

    const MapScope mapScope{"empty-import-lifecycle", "frontmatter", 1};
    auto map = take(ReplicatedMapStream::create(lifecycle->storage(), mapScope));
    take(map->completeReplay());
    const auto mapOperation = take(map->set(QByteArrayLiteral("title"),
                                             QByteArrayLiteral("tentative")));
    const QByteArray editId = QByteArrayLiteral("empty-lifecycle-edit");
    ActionBundleCodec::Bundle editBundle;
    editBundle.document = mapScope.document;
    editBundle.epoch = mapScope.epoch;
    editBundle.deliverySequence = 2;
    editBundle.actionId = editId;
    editBundle.kind = ActionKind::edit;
    editBundle.operations.push_back(take(toActionOperation(mapOperation)));

    auto priorHistory = take(OwnerHistoryCodec::decode(
        lifecycle->recovery().localHistory->bytes, mapScope.document, mapScope.epoch,
        lifecycle->storage().actor(), retryId,
        lifecycle->recovery().acceptedRecords.front().priorRevision.token));
    priorHistory.transactionId = editId;
    priorHistory.acceptedPrior = lifecycle->recovery().acceptedRevision.token;
    priorHistory.actionIndex.acceptedPrior = priorHistory.acceptedPrior;
    priorHistory.actionIndex.events.push_back({editId, ActionKind::edit, {}, {
        take(toHistoryAssociation(mapOperation))}});

    const auto editRecovery = take(lifecycle->storage().readRecoveryState());
    require(editRecovery.unresolvedReservations.size() == 1,
            "lifecycle map edit did not retain its reserved identity");
    PreparedStorageTransaction editTransaction;
    editTransaction.transactionId = editId;
    editTransaction.expectedRevision = editRecovery.acceptedRevision;
    editTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    editTransaction.sharedRecord = take(ActionBundleCodec::encode(editBundle));
    editTransaction.reservations = editRecovery.unresolvedReservations;
    editTransaction.operationIdentities.push_back({
        QString::fromStdString(take(mapStreamName(mapScope.map))),
        mapOperation.operation.producer, mapOperation.operation.counter});
    editTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(priorHistory));

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
    const auto absentEdit = lifecycle->publishAction(editTransaction);
    require(absentEdit.state == ActionPublicationState::absent
                && lifecycle->state() == ProviderDocumentState::recoveryRequired
                && !lifecycle->canStartOrdinaryAction(),
            "live lifecycle owner did not retain the absent action barrier");
    map.reset();
    const auto rebuiltEdit = lifecycle->recoverActionAndRebuild(editTransaction);
    require(rebuiltEdit.state == ActionPublicationState::absent
                && lifecycle->state() == ProviderDocumentState::recoveryRequired
                && lifecycle->projection().mapStream(mapScope.map) == nullptr,
            "lifecycle owner did not rebuild accepted state before terminal gap publication");

    const auto reservation = editTransaction.reservations.front();
    const auto current = take(lifecycle->storage().readRecoveryState());
    const MapCounterGap gap{mapScope.document, mapScope.map, mapScope.epoch,
        reservation.actor, reservation.first, reservation.count,
        current.acceptedRevision.token};
    PreparedStorageTransaction gapTransaction;
    gapTransaction.transactionId = QByteArrayLiteral("empty-lifecycle-map-gap");
    gapTransaction.expectedRevision = current.acceptedRevision;
    gapTransaction.sharedRecordFormat = QByteArrayLiteral("MMG/1");
    gapTransaction.sharedRecord = take(MapCounterGapCodec::encode(gap));
    gapTransaction.reservations = {reservation};
    const auto committedGap = lifecycle->publishGap(gapTransaction);
    const auto* recoveredMap = lifecycle->projection().mapStream(mapScope.map);
    require(committedGap.state == ActionPublicationState::committed
                && lifecycle->state() == ProviderDocumentState::ready
                && lifecycle->canStartOrdinaryAction()
                && recoveredMap && recoveredMap->values().empty(),
            "lifecycle owner did not resume writes after rebuilding and covering the absent map action");

    const QByteArray uncertainId = QByteArrayLiteral("empty-lifecycle-uncertain-edit");
    auto postGapMap = take(ReplicatedMapStream::create(lifecycle->storage(), mapScope));
    take(postGapMap->applyCommittedCounterGap(
        gapTransaction.sharedRecord, gapTransaction.expectedRevision.token));
    take(postGapMap->completeReplay());
    const auto acceptedMapOperation = take(postGapMap->set(
        QByteArrayLiteral("title"), QByteArrayLiteral("accepted")));
    const auto beforeUncertain = take(lifecycle->storage().readRecoveryState());
    ActionBundleCodec::Bundle uncertainBundle;
    uncertainBundle.document = mapScope.document;
    uncertainBundle.epoch = mapScope.epoch;
    uncertainBundle.deliverySequence = 2;
    uncertainBundle.actionId = uncertainId;
    uncertainBundle.kind = ActionKind::edit;
    uncertainBundle.operations.push_back(take(toActionOperation(acceptedMapOperation)));
    CrossStreamHistoryIndex uncertainIndex;
    uncertainIndex.document = mapScope.document;
    uncertainIndex.epoch = mapScope.epoch;
    uncertainIndex.owner = lifecycle->storage().actor();
    uncertainIndex.acceptedPrior = beforeUncertain.acceptedRevision.token;
    uncertainIndex.events.push_back({retryId, ActionKind::initialImport, {}, {}});
    uncertainIndex.events.push_back({uncertainId, ActionKind::edit, {}, {
        take(toHistoryAssociation(acceptedMapOperation))}});
    OwnerHistorySnapshot uncertainHistory;
    uncertainHistory.document = mapScope.document;
    uncertainHistory.epoch = mapScope.epoch;
    uncertainHistory.owner = uncertainIndex.owner;
    uncertainHistory.transactionId = uncertainId;
    uncertainHistory.acceptedPrior = uncertainIndex.acceptedPrior;
    uncertainHistory.actionIndex = uncertainIndex;
    PreparedStorageTransaction uncertainTransaction;
    uncertainTransaction.transactionId = uncertainId;
    uncertainTransaction.expectedRevision = beforeUncertain.acceptedRevision;
    uncertainTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    uncertainTransaction.sharedRecord = take(ActionBundleCodec::encode(uncertainBundle));
    uncertainTransaction.reservations = beforeUncertain.unresolvedReservations;
    uncertainTransaction.operationIdentities.push_back({
        QString::fromStdString(take(mapStreamName(mapScope.map))),
        acceptedMapOperation.operation.producer,
        acceptedMapOperation.operation.counter});
    uncertainTransaction.localHistoryAfterCommit = take(
        OwnerHistoryCodec::encode(uncertainHistory));
    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::historyBeforeAtomicRename);
    const auto historyPending = lifecycle->publishAction(uncertainTransaction);
    require(historyPending.state == ActionPublicationState::committedHistoryPending
                && lifecycle->state() == ProviderDocumentState::recoveryRequired
                && !lifecycle->canStartOrdinaryAction(),
            "lifecycle owner did not close editing after committed history failure");
    const auto recoveredCommit = lifecycle->recoverActionAndRebuild(uncertainTransaction);
    const auto* acceptedMap = lifecycle->projection().mapStream(mapScope.map);
    require(recoveredCommit.state == ActionPublicationState::committed
                && lifecycle->state() == ProviderDocumentState::ready
                && lifecycle->canStartOrdinaryAction()
                && lifecycle->recovery().acceptedRecords.size() == 3
                && lifecycle->recovery().acceptedRecords.back().transactionId == uncertainId
                && acceptedMap
                && acceptedMap->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("accepted")),
            "lifecycle recovery did not refresh the projection after inspection proved commit");
}

void runAbsentMapGapCoordinatorRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create absent-gap recovery directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("absent-gap-doc"), 5};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    const QByteArray actionId = QByteArrayLiteral("absent-action-1");
    const MapScope mapScope{"absent-gap-doc", "frontmatter", 5};
    const std::string mapRoute = take(mapStreamName(mapScope.map));
    SourceSnapshotRecord acceptedSource;
    acceptedSource.document = mapScope.document;
    acceptedSource.epoch = mapScope.epoch;
    acceptedSource.originalMarkdown = QByteArrayLiteral("# Accepted\n");
    require(session->beginInitialImport(PendingImportIntent{
                QByteArrayLiteral("accepted-map-import"), acceptedSource.originalMarkdown}).ok(),
            "provider refused the accepted map fixture's import intent");

    // Establish accepted state that a real absence recovery must reconstruct
    // after discarding the tentative projection.
    const auto acceptedReservation = take(session->reserve(
        QString::fromStdString(mapRoute), 1));
    const MarkoffMapOperation acceptedPayload{
        mapScope.map, QByteArrayLiteral("title"), QByteArrayLiteral("Accepted"),
        session->actor(), acceptedReservation.first, false};
    ActionBundleCodec::Bundle acceptedBundle;
    acceptedBundle.document = mapScope.document;
    acceptedBundle.epoch = mapScope.epoch;
    acceptedBundle.deliverySequence = 1;
    acceptedBundle.actionId = QByteArrayLiteral("accepted-map-import");
    acceptedBundle.kind = ActionKind::initialImport;
    acceptedBundle.sourceSnapshot = take(SourceSnapshotCodec::encode(acceptedSource));
    const QByteArray acceptedMapBytes = take(MarkoffMapCodec::encode(acceptedPayload));
    acceptedBundle.operations.push_back({mapRoute, OperationPrimitive::markoffMap,
        std::nullopt, session->actor(), acceptedReservation.first,
        OperationCodec::markoffOwned, acceptedMapBytes});
    const auto acceptedMapAssociation = take(toHistoryAssociation(
        LocalMapOperation{mapScope, std::nullopt, acceptedPayload, acceptedMapBytes}));
    const auto acceptedPrior = take(session->readRecoveryState()).acceptedRevision;
    CrossStreamHistoryIndex acceptedIndex;
    acceptedIndex.document = mapScope.document;
    acceptedIndex.epoch = mapScope.epoch;
    acceptedIndex.owner = session->actor();
    acceptedIndex.acceptedPrior = acceptedPrior.token;
    acceptedIndex.events.push_back({acceptedBundle.actionId, ActionKind::initialImport,
                                    {}, {acceptedMapAssociation}});
    OwnerHistorySnapshot acceptedHistory;
    acceptedHistory.document = mapScope.document;
    acceptedHistory.epoch = mapScope.epoch;
    acceptedHistory.owner = session->actor();
    acceptedHistory.transactionId = acceptedBundle.actionId;
    acceptedHistory.acceptedPrior = acceptedPrior.token;
    acceptedHistory.actionIndex = acceptedIndex;
    PreparedStorageTransaction acceptedTransaction;
    acceptedTransaction.transactionId = acceptedBundle.actionId;
    acceptedTransaction.expectedRevision = acceptedPrior;
    acceptedTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    acceptedTransaction.sharedRecord = take(ActionBundleCodec::encode(acceptedBundle));
    acceptedTransaction.reservations = {acceptedReservation};
    acceptedTransaction.operationIdentities = {{QString::fromStdString(mapRoute),
        session->actor(), acceptedReservation.first}};
    acceptedTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(acceptedHistory));
    ActionPublicationCoordinator initialCoordinator(*session);
    const auto acceptedResult = initialCoordinator.publish(acceptedTransaction);
    require(acceptedResult.state == ActionPublicationState::committed,
            "could not establish accepted map state for absent-action rebuild test");

    const auto before = take(session->readRecoveryState());
    const auto reservation = take(session->reserve(QString::fromStdString(mapRoute), 1));
    const MarkoffMapOperation mapPayload{
        mapScope.map, QByteArrayLiteral("title"), QByteArrayLiteral("Tentative"),
        session->actor(), reservation.first, false};
    const QByteArray mapBytes = take(MarkoffMapCodec::encode(mapPayload));
    const auto mapAssociation = take(toHistoryAssociation(
        LocalMapOperation{mapScope, std::nullopt, mapPayload, mapBytes}));
    SourceSnapshotRecord source;
    source.document = mapScope.document;
    source.epoch = mapScope.epoch;
    source.originalMarkdown = QByteArrayLiteral("# Original\n");
    ActionBundleCodec::Bundle bundle;
    bundle.document = source.document;
    bundle.epoch = source.epoch;
    bundle.deliverySequence = 1;
    bundle.actionId = actionId;
    bundle.kind = ActionKind::edit;
    bundle.operations.push_back({mapRoute, OperationPrimitive::markoffMap,
        std::nullopt, session->actor(), reservation.first,
        OperationCodec::markoffOwned, mapBytes});

    CrossStreamHistoryIndex actionIndex;
    actionIndex.document = source.document;
    actionIndex.epoch = source.epoch;
    actionIndex.owner = session->actor();
    actionIndex.acceptedPrior = before.acceptedRevision.token;
    actionIndex.events.push_back({actionId, ActionKind::edit, {},
                                  {mapAssociation}});
    OwnerHistorySnapshot history;
    history.document = source.document;
    history.epoch = source.epoch;
    history.owner = session->actor();
    history.transactionId = actionId;
    history.acceptedPrior = before.acceptedRevision.token;
    history.actionIndex = actionIndex;

    PreparedStorageTransaction abandoned;
    abandoned.transactionId = actionId;
    abandoned.expectedRevision = before.acceptedRevision;
    abandoned.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    abandoned.sharedRecord = take(ActionBundleCodec::encode(bundle));
    abandoned.reservations = {reservation};
    abandoned.operationIdentities = {{QString::fromStdString(mapRoute),
                                       session->actor(), reservation.first}};
    abandoned.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(history));
    require(session->prepare(abandoned).ok(),
            "could not persist preparation before simulated process interruption");
    session.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    auto recoveredDocument = take(ProviderBackedDocumentState::open(
        *provider, storageScope, StorageMode::durable));
    require(recoveredDocument->state() == ProviderDocumentState::recoveryRequired
                && !recoveredDocument->canStartOrdinaryAction(),
            "lifecycle open did not preserve the absent-action write barrier");
    auto& recoveredSession = recoveredDocument->storage();
    require(recoveredSession.append(actionId).disposition == PublicationDisposition::rejected,
            "a transaction resolved absent could still be appended later");

    const auto rebuiltMap = recoveredDocument->projection().mapStream(mapScope.map);
    require(rebuiltMap
                && rebuiltMap->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("Accepted")),
            "lifecycle open did not rebuild accepted state before gap recovery");

    const MapCounterGap terminalGap{mapScope.document, mapScope.map, mapScope.epoch,
        recoveredSession.actor(), reservation.first, reservation.count,
        before.acceptedRevision.token};
    PreparedStorageTransaction gapTransaction;
    gapTransaction.transactionId = QByteArrayLiteral("absent-action-gap-1");
    gapTransaction.expectedRevision = before.acceptedRevision;
    gapTransaction.sharedRecordFormat = QByteArrayLiteral("MMG/1");
    gapTransaction.sharedRecord = take(MapCounterGapCodec::encode(terminalGap));
    gapTransaction.reservations = {reservation};
    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
    const auto uncertainGap = recoveredDocument->publishGap(gapTransaction);
    require(uncertainGap.state == ActionPublicationState::uncertain
                && recoveredDocument->state() == ProviderDocumentState::recoveryRequired
                && !recoveredDocument->canStartOrdinaryAction(),
            "lifecycle did not retain the barrier after uncertain terminal-gap append");

    recoveredDocument.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    recoveredDocument = take(ProviderBackedDocumentState::open(
        *provider, storageScope, StorageMode::durable));
    require(recoveredDocument->state() == ProviderDocumentState::ready,
            "lifecycle did not recover committed terminal MMG coverage after reopen");
    require(recoveredDocument->canStartOrdinaryAction(),
            "lifecycle did not reopen ordinary actions after terminal-gap recovery");
    const auto& afterGap = recoveredDocument->recovery();
    require(afterGap.acceptedRecords.size() == 2
                && afterGap.acceptedRecords.front().transactionId
                    == acceptedTransaction.transactionId
                && afterGap.acceptedRecords.back().transactionId
                    == gapTransaction.transactionId
                && afterGap.acceptedRecords.front().priorRevision.token.size() > 0
                && afterGap.acceptedRecords.front().committedRevision
                    == afterGap.acceptedRecords.back().priorRevision
                && afterGap.acceptedRecords.back().committedRevision
                    == afterGap.acceptedRevision
                && afterGap.unresolvedTransactions.empty()
                && afterGap.unresolvedReservations.empty()
                && take(recoveredDocument->storage().nextCounter(
                    QString::fromStdString(mapRoute)))
                    == reservation.first + reservation.count,
            "committed MMG did not cover the absent action's exact map interval");
    const auto* replayedMap = recoveredDocument->projection().mapStream(mapScope.map);
    require(replayedMap
                && replayedMap->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("Accepted")),
            "fresh replay did not apply accepted map state and terminal MMG coverage");
}

void runNoncanonicalTextGapRouteRejected()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create noncanonical-gap test directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("bad-gap-route-doc"), 1};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    const auto prior = take(session->readRecoveryState()).acceptedRevision;
    const auto reservation = take(session->reserve(QStringLiteral("forged/text/v1"), 1));
    const std::string document = storageScope.documentKey.toStdString();
    const PositionScope forgedScope{document, "forged/text/v1", Primitive::text,
                                    storageScope.epoch};
    const ScopedCounterGap gap{forgedScope,
        Crdt::CounterGap{reservation.actor, reservation.first, reservation.count,
                         Crdt::Global{}}, prior.token};
    PreparedStorageTransaction transaction;
    transaction.transactionId = QByteArrayLiteral("forged-text-gap-route");
    transaction.expectedRevision = prior;
    transaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
    transaction.sharedRecord = take(CounterGapCodec::encode(gap));
    transaction.reservations = {reservation};
    ActionPublicationCoordinator coordinator(*session);
    const auto result = coordinator.publishGap(transaction);
    require(result.state == ActionPublicationState::rejected
                && result.diagnostic
                && result.diagnostic->code() == ErrorCode::wrong_stream,
            "coordinator accepted an MTG reservation outside canonical Markoff streams");
}

void runListGapCoordinatorRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create list-gap recovery directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("list-gap-doc"), 6};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    const auto prior = take(session->readRecoveryState()).acceptedRevision;
    const PositionScope listScope{"list-gap-doc", std::string(kOrderStreamName),
                                  Primitive::list, 6};
    const auto reservation = take(session->reserve(
        QString::fromStdString(std::string(kOrderStreamName)), 2));
    const ScopedCounterGap scopedGap{listScope,
        Crdt::CounterGap{reservation.actor, reservation.first, reservation.count,
                         Crdt::Global{}}, prior.token};
    PreparedStorageTransaction transaction;
    transaction.transactionId = QByteArrayLiteral("list-terminal-gap-1");
    transaction.expectedRevision = prior;
    transaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
    transaction.sharedRecord = take(CounterGapCodec::encode(scopedGap));
    transaction.reservations = {reservation};
    ActionPublicationCoordinator coordinator(*session);
    const auto published = coordinator.publishGap(transaction);
    require(published.state == ActionPublicationState::committed
                && published.committedRevision && !coordinator.blocked(),
            "coordinator failed to commit a terminal MTG/1 list gap");

    session.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(storageScope, StorageMode::durable));
    coordinator = ActionPublicationCoordinator(*session);
    const auto recovered = coordinator.recover(transaction);
    require(recovered.state == ActionPublicationState::committed
                && recovered.committedRevision && !coordinator.blocked(),
            "coordinator failed to recover the committed MTG/1 list gap");
    const auto state = take(session->readRecoveryState());
    require(state.acceptedRecords.size() == 1
                && state.acceptedRecords.front().format == QByteArrayLiteral("MTG/1")
                && state.unresolvedReservations.empty()
                && take(session->nextCounter(QString::fromStdString(
                       std::string(kOrderStreamName))))
                    == reservation.first + reservation.count,
            "recovered MTG/1 list gap left a reservation unresolved or reusable");
}

void runReplicatedMapCompensation()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope documentScope{QStringLiteral("map-undo-doc"), 12};
    auto session = take(provider->open(documentScope, StorageMode::ephemeral));
    const MapScope mapScope{"map-undo-doc", "frontmatter", 12};
    auto map = take(ReplicatedMapStream::create(*session, mapScope));
    require(map->completeReplay().ok(), "map undo projection did not complete replay");
    std::vector<LocalMapOperation> operations;

    const auto first = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("One")));
    require(!first.priorValueExisted && first.priorValue.isEmpty(),
            "new map write did not capture its absent prior value");
    operations.push_back(first);
    const auto firstUndo = take(map->compensate(first));
    require(firstUndo.operation.tombstone && !map->get(QByteArrayLiteral("title")),
            "map compensation did not emit a replicated tombstone");
    operations.push_back(firstUndo);
    const auto firstRedo = take(map->compensate(firstUndo));
    require(!firstRedo.operation.tombstone
                && map->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("One")),
            "compensating the map tombstone did not restore its prior value");
    operations.push_back(firstRedo);

    const auto update = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("Two")));
    require(update.priorValueExisted && update.priorValue == QByteArrayLiteral("One"),
            "map update did not capture the prior visible value");
    operations.push_back(update);
    const auto updateAssociation = take(toHistoryAssociation(update));
    CrossStreamHistoryIndex mapHistory;
    mapHistory.document = mapScope.document;
    mapHistory.epoch = mapScope.epoch;
    mapHistory.owner = session->actor();
    mapHistory.acceptedPrior = QByteArrayLiteral("map-prefix-before-update");
    mapHistory.events.push_back({QByteArrayLiteral("map-update-action"),
        ActionKind::edit, {}, {updateAssociation}});
    const auto encodedMapHistory = take(CrossStreamHistoryCodec::encode(mapHistory));
    const auto restoredMapHistory = take(CrossStreamHistoryCodec::decode(
        encodedMapHistory, mapScope.document, mapScope.epoch, session->actor(),
        mapHistory.acceptedPrior));
    const auto& restoredAssociation = restoredMapHistory.events.back().groups.front();
    require(restoredAssociation.hasMapPriorState
                && restoredAssociation.mapPriorValueExisted
                && restoredAssociation.mapPriorValue == QByteArrayLiteral("One"),
            "MAI/3 did not retain the map value needed for undo after reopen");
    LocalMapOperation restoredUpdate{mapScope, update.targetBlock, update.operation,
        update.bytes, restoredAssociation.mapPriorValueExisted,
        restoredAssociation.mapPriorValue};
    auto rebuiltBeforeUndo = take(ReplicatedMapStream::create(*session, mapScope));
    for (size_t i = 0; i <= 3; ++i)
        require(take(rebuiltBeforeUndo->applyRemote(operations[i].bytes,
                    operations[i].targetBlock)) == MapApplyKind::applied,
                "fresh map did not replay through the mapped update action");
    require(rebuiltBeforeUndo->completeReplay().ok(),
            "fresh map did not complete replay before restored undo");
    const auto restoredUndo = take(rebuiltBeforeUndo->compensate(restoredUpdate));
    require(restoredUndo.operation.tombstone == false
                && rebuiltBeforeUndo->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("One")),
            "reopened MAI/3 map metadata could not produce replicated compensation");

    const auto updateUndo = take(map->compensate(update));
    require(map->get(QByteArrayLiteral("title"))
                == std::optional<QByteArray>(QByteArrayLiteral("One")),
            "map update compensation did not restore its prior value");
    operations.push_back(updateUndo);

    const auto older = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("Three")));
    operations.push_back(older);
    const auto newer = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("Four")));
    operations.push_back(newer);
    const auto staleCompensation = map->compensate(older);
    require(!staleCompensation.ok()
                && staleCompensation.error().code() == ErrorCode::pin_conflict
                && map->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("Four")),
            "selective map compensation overwrote a later same-key action");

    auto replayed = take(ReplicatedMapStream::create(*session, mapScope));
    require(replayed->completeReplay().ok(), "fresh map projection did not complete replay");
    for (const auto& operation : operations) {
        const auto result = take(replayed->applyRemote(operation.bytes,
                                                       operation.targetBlock));
        require(result == MapApplyKind::applied,
                "fresh map projection did not replay a compensation operation");
    }
    require(replayed->get(QByteArrayLiteral("title"))
                == std::optional<QByteArray>(QByteArrayLiteral("Four")),
            "replayed map compensation history changed the visible final value");
}

void runReferenceProvider()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create reference-provider test directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("durable-doc"), 7};
    ActorId actor;
    QByteArray revision;
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        actor = session->actor();
        const auto state = take(session->readRecoveryState());
        require(state.acceptedRevision.token == QByteArrayLiteral("reference:0"),
                "reference provider did not create a stable empty prefix");
        auto reservation = take(session->reserve(
            QStringLiteral("markoff/block-text/v1/durable"), 2));
        require(reservation.first == 1 && reservation.count == 2,
                "reference provider did not persist its first reservation");
        const auto unattached = take(session->reserve(
            QStringLiteral("markoff/annotation/v1/durable"), 4));
        require(unattached.first == 1 && unattached.count == 4,
                "reference provider did not persist an unattached reservation");
        const auto conflict = provider->open(scope, StorageMode::durable);
        require(!conflict.ok() && conflict.error().code() == ErrorCode::writer_conflict,
                "reference provider allowed two sessions for one document");
        PreparedStorageTransaction transaction;
        transaction.transactionId = QByteArrayLiteral("durable-tx-1");
        transaction.expectedRevision = state.acceptedRevision;
        transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        transaction.sharedRecord = QByteArrayLiteral("durable-action");
        transaction.reservations = {reservation};
        transaction.operationIdentities = {{reservation.stream, actor, reservation.first}};
        transaction.localHistoryAfterCommit = QByteArrayLiteral("private-history");
        require(session->prepare(transaction).ok(),
                "reference provider did not persist exact preparation");
    }
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        require(session->actor() == actor,
                "reference provider did not preserve actor identity across reopen");
        auto state = take(session->readRecoveryState());
        require(state.acceptedRevision.token == QByteArrayLiteral("reference:0")
                    && state.acceptedRecords.empty()
                    && state.unresolvedTransactions.size() == 1
                    && state.unresolvedReservations.size() == 1
                    && state.unresolvedReservations[0].stream
                        == QStringLiteral("markoff/annotation/v1/durable"),
                "reference provider lost its reservation or accepted an unappended record");
        PreparedStorageTransaction transaction;
        transaction.transactionId = QByteArrayLiteral("durable-tx-1");
        transaction.expectedRevision = state.acceptedRevision;
        transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        transaction.sharedRecord = QByteArrayLiteral("durable-action");
        transaction.reservations = state.unresolvedTransactions[0].reservations;
        transaction.operationIdentities = {{transaction.reservations[0].stream,
                                            actor,
                                            transaction.reservations[0].first}};
        transaction.localHistoryAfterCommit = QByteArrayLiteral("private-history");
        require(session->inspect(transaction).disposition == PublicationDisposition::absent,
                "reference provider did not resolve prepared-but-unappended work as absent");
        auto publication = session->append(transaction.transactionId);
        require(publication.disposition == PublicationDisposition::committed
                    && publication.committedRevision
                    && publication.committedRevision->token == QByteArrayLiteral("reference:1"),
                "reference provider did not commit the prepared transaction");
        revision = publication.committedRevision->token;
    }
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        const auto state = take(session->readRecoveryState());
        require(state.acceptedRecords.size() == 1
                    && state.acceptedRevision.token == revision
                    && state.unresolvedTransactions.size() == 1
                    && state.unresolvedReservations.size() == 1,
                "reference provider did not recover the accepted prefix after restart");
    }
}

void runDurableProviderAdapterReplay()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create durable-adapter replay directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("durable-adapter-doc"), 1};
    const PositionScope listScope{"durable-adapter-doc", std::string(kOrderStreamName),
                                  Primitive::list, 1};
    BlockIdentity insertedIdentity;
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto document = take(ProviderBackedDocumentState::open(
            *provider, storageScope, StorageMode::durable));
        auto* session = &document->storage();
        const QByteArray importMarkdown = QByteArrayLiteral("# Durable\n");
        const PendingImportIntent importIntent{
            QByteArrayLiteral("adapter-action-1"), importMarkdown};
        require(document->authorizeInitialImport(importIntent).ok(),
                "durable adapter action could not register its initial import");
        auto* list = take(document->stagingIdList());
        auto local = take(list->insertAfter(std::nullopt, 9001));
        insertedIdentity = *local.insertedBlock;

        const auto textStreamName = take(blockTextStreamName(insertedIdentity));
        const PositionScope textScope{"durable-adapter-doc", textStreamName,
                                      Primitive::text, 1};
        auto* text = take(document->stagingTextStream(insertedIdentity));
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::reservationBeforeAtomicRename);
        const auto allocationFailure = text->replaceUtf8(
            0, 0, QByteArrayLiteral("must not appear"));
        const ScopedReservation listReservation{
            QString::fromStdString(listScope.stream), session->actor(),
            local.identity.counter, 1};
        const auto afterAllocationFailure = take(session->readRecoveryState());
        require(!allocationFailure.ok() && text->text().isEmpty()
                    && take(session->nextCounter(
                           QString::fromStdString(textScope.stream))) == 1
                    && afterAllocationFailure.unresolvedReservations
                        == std::vector<ScopedReservation>{listReservation},
                "failed durable allocation mutated text or consumed its counter");
        const auto textLocal = take(text->replaceUtf8(0, 0, QByteArrayLiteral("Durable text")));
        require(textLocal.encodable(), "durable text adapter returned unencodable bytes");

        auto* map = take(document->stagingMapStream(
            MapScope{"durable-adapter-doc", "frontmatter", 1}));
        const QByteArray oversizedValue(1024 * 1024 + 1, 'x');
        const auto oversizedMapWrite = map->set(
            QByteArrayLiteral("too-large"), oversizedValue);
        require(!oversizedMapWrite.ok()
                    && take(session->nextCounter(
                           QStringLiteral("markoff/map/v1/frontmatter"))) == 1,
                "rejected map encoding consumed a durable identity reservation");
        const auto mapLocal = take(map->set(QByteArrayLiteral("title"),
                                            QByteArrayLiteral("Durable")));
        require(local.identity.actor == textLocal.identity.actor
                    && textLocal.identity.actor == mapLocal.operation.producer
                    && local.identity.counter == mapLocal.operation.counter,
                "list/map streams did not demonstrate stream-scoped actor/counter identity");
        ActionStager stager("durable-adapter-doc", 1, 1,
                            QByteArrayLiteral("adapter-action-1"),
                            ActionKind::initialImport);
        SourceSnapshotRecord source;
        source.document = "durable-adapter-doc";
        source.epoch = 1;
        source.originalMarkdown = importMarkdown;
        stager.setSourceSnapshot(take(SourceSnapshotCodec::encode(source)));
        require(stager.add(local).ok() && stager.add(textLocal).ok()
                    && stager.add(mapLocal).ok(),
                "action stager rejected typed local stream operations");
        const auto& bundle = stager.bundle();
        const auto actionBytes = take(ActionBundleCodec::encode(bundle));

        const auto recovery = take(session->readRecoveryState());
        require(recovery.unresolvedReservations.size() == 3,
                "text, map and list allocations did not create exact reservations");
        const auto listHistory = list->localHistorySnapshot();
        const auto textHistory = text->localHistorySnapshot();
        OwnerHistorySnapshot ownerHistory;
        ownerHistory.document = bundle.document;
        ownerHistory.epoch = bundle.epoch;
        ownerHistory.owner = session->actor();
        ownerHistory.transactionId = bundle.actionId;
        ownerHistory.acceptedPrior = recovery.acceptedRevision.token;
        ownerHistory.streams = {
            {PositionScope{bundle.document, std::string(kOrderStreamName),
                           Primitive::list, bundle.epoch}, session->actor(), listHistory},
            {PositionScope{bundle.document, textScope.stream, Primitive::text,
                           bundle.epoch}, session->actor(), textHistory}};
        ownerHistory.actionIndex.document = bundle.document;
        ownerHistory.actionIndex.epoch = bundle.epoch;
        ownerHistory.actionIndex.owner = session->actor();
        ownerHistory.actionIndex.acceptedPrior = recovery.acceptedRevision.token;
        ActionHistoryEvent historyEvent;
        historyEvent.actionId = bundle.actionId;
        historyEvent.kind = ActionKind::initialImport;
        for (const auto& entry : listHistory.entries)
            historyEvent.groups.push_back({std::string(kOrderStreamName),
                OperationPrimitive::idList, entry.group_id, entry.operation_ids});
        for (const auto& entry : textHistory.entries)
            historyEvent.groups.push_back({textScope.stream, OperationPrimitive::text,
                entry.group_id, entry.operation_ids});
        historyEvent.groups.push_back(take(toHistoryAssociation(mapLocal)));
        ownerHistory.actionIndex.events.push_back(std::move(historyEvent));
        const auto& operationReservations = stager.operationReservations();
        auto transaction = take(ActionTransactionBuilder::build(
            *session, bundle, ownerHistory, operationReservations));
        require(transaction.sharedRecord == actionBytes,
                "transaction builder changed the canonical MAB bytes");
        auto staleOwnerHistory = ownerHistory;
        staleOwnerHistory.acceptedPrior = QByteArrayLiteral("stale-prefix");
        require(!ActionTransactionBuilder::build(
                    *session, bundle, staleOwnerHistory, operationReservations).ok(),
                "transaction builder accepted owner history from a stale prefix");
        auto omittedMapOperation = bundle;
        omittedMapOperation.operations.pop_back();
        require(!ActionTransactionBuilder::build(
                    *session, omittedMapOperation, ownerHistory,
                    operationReservations).ok(),
                "transaction builder accepted a staged reservation omitted from its MAB");
        const auto publication = document->publishAction(transaction);
        require(publication.state == ActionPublicationState::committed
                    && publication.committedRevision
                    && document->state() == ProviderDocumentState::ready
                    && document->recovery().acceptedRecords.size() == 1
                    && !document->recovery().initialImport,
                "provider-backed lifecycle did not commit and replay the multi-stream import");

        // Publication replayed the accepted prefix into the owner-held stream
        // wrappers; borrowed handles now expose refreshed accepted state.
        list = take(document->stagingIdList());
        text = take(document->stagingTextStream(insertedIdentity));
        map = take(document->stagingMapStream(
            MapScope{"durable-adapter-doc", "frontmatter", 1}));

        const auto checkAcceptedOperation = [&](const std::string& stream,
                const ActorId& actor, uint64_t counter, const QByteArray& payload,
                uint32_t index) {
            auto accepted = take(document->projection().findAcceptedOperation(
                stream, actor, counter));
            require(accepted && accepted->operation.stream == stream
                        && accepted->operation.producer == actor
                        && accepted->operation.counter == counter
                        && accepted->operation.payload == payload
                        && accepted->deliverySequence == bundle.deliverySequence
                        && accepted->actionId == bundle.actionId
                        && accepted->operationIndex == index
                        && accepted->operationCount == bundle.operations.size()
                        && accepted->priorRevision
                            == document->recovery().acceptedRecords.front().priorRevision
                        && accepted->committedRevision
                            == document->recovery().acceptedRecords.front().committedRevision,
                    "accepted-operation index lost exact MAB identity or envelope metadata");
            return *accepted;
        };
        checkAcceptedOperation(
            local.scope.stream, local.identity.actor, local.identity.counter,
            local.bytes, 0);
        const auto acceptedText = checkAcceptedOperation(
            textScope.stream, textLocal.identity.actor, textLocal.identity.counter,
            textLocal.bytes, 1);
        const auto acceptedMap = checkAcceptedOperation(
            take(mapStreamName(mapLocal.scope.map)),
            mapLocal.operation.producer, mapLocal.operation.counter,
            mapLocal.bytes, 2);
        QByteArray changedPayload = acceptedText.operation.payload;
        changedPayload.append('!');
        require(changedPayload != acceptedText.operation.payload
                    && acceptedMap.operation.payload == mapLocal.bytes,
                "accepted operation lookup did not retain exact payload bytes");

        ScopedOperationRouter acceptedRouter("durable-adapter-doc", 1);
        acceptedRouter.setDocumentState(document.get());
        take(acceptedRouter.registerStream(*text));
        const RemoteOperation acceptedEnvelope{
            textScope, {textLocal.identity.actor, textLocal.identity.counter},
            textLocal.bytes, bundle.deliverySequence, bundle.actionId, 1, 3};
        std::vector<CompletedRemoteOperation> acceptedCompletions;
        auto noncanonicalTextRoute = acceptedEnvelope;
        noncanonicalTextRoute.scope.stream = "forged/text/v1";
        const auto rejectedTextRoute = acceptedRouter.offer(
            noncanonicalTextRoute, acceptedCompletions);
        require(rejectedTextRoute.state == RouteState::rejected
                    && rejectedTextRoute.error
                    && rejectedTextRoute.error->code() == ErrorCode::wrong_stream
                    && acceptedRouter.heldOperationCount() == 0,
                "text router held an operation on a noncanonical block route");
        const auto exactDuplicate = acceptedRouter.offer(
            acceptedEnvelope, acceptedCompletions);
        require(exactDuplicate.state == RouteState::duplicate
                    && acceptedCompletions.size() == 1
                    && acceptedCompletions.front().outcome
                        == ApplyOutcomeKind::duplicate,
                "text router did not acknowledge an exact accepted MAB duplicate from history");
        auto conflictingEnvelope = acceptedEnvelope;
        conflictingEnvelope.action_id = QByteArrayLiteral("changed-action-id");
        const auto conflictingDuplicate = acceptedRouter.offer(
            conflictingEnvelope, acceptedCompletions);
        require(conflictingDuplicate.state == RouteState::rejected
                    && conflictingDuplicate.error
                    && conflictingDuplicate.error->code() == ErrorCode::actor_conflict
                    && acceptedRouter.quarantined().size() == 1,
                "text router trusted Core duplicate handling for a changed accepted envelope");
        take(acceptedRouter.acknowledgeQuarantine(conflictingEnvelope));
        auto durableQuarantine = take(document->storage().readRecoveryState());
        require(durableQuarantine.quarantinedOperations.size() == 1,
                "text router acknowledged its conflict before provider persistence");
        const auto textQuarantine = take(QuarantineRecordCodec::decode(
            durableQuarantine.quarantinedOperations.back()));
        require(textQuarantine.primitive == QuarantinePrimitive::text
                    && textQuarantine.actor == textLocal.identity.actor
                    && textQuarantine.counter == textLocal.identity.counter
                    && textQuarantine.payload == textLocal.bytes
                    && textQuarantine.actionId == conflictingEnvelope.action_id,
                "text quarantine persistence changed the rejected MAB envelope");
        ScopedOperationRouter restoredTextRouter("durable-adapter-doc", 1);
        require(take(restoredTextRouter.restoreQuarantine(
                    durableQuarantine.quarantinedOperations)) == 1
                    && restoredTextRouter.quarantined().size() == 1
                    && restoredTextRouter.quarantined().front().reason.code()
                        == ErrorCode::actor_conflict,
                "text router did not rehydrate its durable quarantine after reopen");
        QuarantineRecord forgedRouteRecord;
        forgedRouteRecord.document = "durable-adapter-doc";
        forgedRouteRecord.epoch = 1;
        forgedRouteRecord.stream = "forged/text/v1";
        forgedRouteRecord.primitive = QuarantinePrimitive::text;
        forgedRouteRecord.actor = textLocal.identity.actor;
        forgedRouteRecord.counter = textLocal.identity.counter + 1000;
        forgedRouteRecord.payload = textLocal.bytes;
        forgedRouteRecord.deliverySequence = bundle.deliverySequence;
        forgedRouteRecord.actionId = QByteArrayLiteral("forged-quarantine-route");
        forgedRouteRecord.bundleOperationCount = 1;
        forgedRouteRecord.reason = Error(ErrorCode::actor_conflict,
                                         "forged quarantine route");
        const QByteArray forgedRouteBytes = take(
            QuarantineRecordCodec::encode(forgedRouteRecord));
        const size_t heldBeforeForgedRestore = restoredTextRouter.heldOperationCount();
        const auto rejectedForgedRestore = restoredTextRouter.restoreQuarantine(
            {forgedRouteBytes});
        require(!rejectedForgedRestore.ok()
                    && rejectedForgedRestore.error().code() == ErrorCode::wrong_stream
                    && restoredTextRouter.heldOperationCount() == heldBeforeForgedRestore,
                "text quarantine restore admitted a noncanonical route or partially mutated");

        ScopedListRouter acceptedListRouter("durable-adapter-doc", 1);
        acceptedListRouter.setDocumentState(document.get());
        take(acceptedListRouter.registerStream(*list));
        const RoutedListOperation acceptedListEnvelope{
            RemoteListOperation{listScope, local.identity, local.bytes},
            bundle.deliverySequence, bundle.actionId, 0, 3};
        std::vector<CompletedListOperation> acceptedListCompletions;
        const auto exactListDuplicate = acceptedListRouter.offer(
            acceptedListEnvelope, acceptedListCompletions);
        auto conflictingListEnvelope = acceptedListEnvelope;
        conflictingListEnvelope.bundleIndex = 1;
        const auto conflictingListDuplicate = acceptedListRouter.offer(
            conflictingListEnvelope, acceptedListCompletions);
        const auto exactListAfterConflict = acceptedListRouter.offer(
            acceptedListEnvelope, acceptedListCompletions);
        require(exactListDuplicate.state == ListRouteState::duplicate,
                "list router did not acknowledge its accepted MAB duplicate");
        require(conflictingListDuplicate.state == ListRouteState::rejected
                    && conflictingListDuplicate.error
                    && conflictingListDuplicate.error->code() == ErrorCode::actor_conflict
                    && acceptedListRouter.quarantined().size() == 1,
                "list router did not quarantine a changed accepted MAB envelope");
        require(exactListAfterConflict.state == ListRouteState::duplicate
                    && acceptedListCompletions.size() == 2,
                "list router did not prioritize its accepted MAB envelope over a quarantined conflict");
        take(acceptedListRouter.acknowledgeQuarantine(conflictingListEnvelope));
        durableQuarantine = take(document->storage().readRecoveryState());
        require(durableQuarantine.quarantinedOperations.size() == 2,
                "list router acknowledged its conflict before provider persistence");
        const auto durableListQuarantine = take(QuarantineRecordCodec::decode(
            durableQuarantine.quarantinedOperations.back()));
        require(durableListQuarantine.primitive == QuarantinePrimitive::list
                    && durableListQuarantine.actor == local.identity.actor
                    && durableListQuarantine.counter == local.identity.counter
                    && durableListQuarantine.payload == local.bytes
                    && durableListQuarantine.bundleIndex == 1,
                "list quarantine persistence changed the rejected MAB envelope");
        ScopedListRouter restoredListRouter("durable-adapter-doc", 1);
        require(take(restoredListRouter.restoreQuarantine(
                    durableQuarantine.quarantinedOperations)) == 1
                    && restoredListRouter.quarantined().size() == 1,
                "list router did not rehydrate its durable quarantine after reopen");

        ScopedMapRouter acceptedMapRouter("durable-adapter-doc", 1);
        acceptedMapRouter.setDocumentState(document.get());
        take(acceptedMapRouter.registerStream(*map));
        const RemoteMapOperation acceptedMapEnvelope{
            mapLocal.scope, mapLocal.targetBlock, mapLocal.bytes,
            bundle.deliverySequence, bundle.actionId, 2, 3};
        std::vector<CompletedMapOperation> acceptedMapCompletions;
        const auto exactMapDuplicate = acceptedMapRouter.offer(
            acceptedMapEnvelope, acceptedMapCompletions);
        auto conflictingMapEnvelope = acceptedMapEnvelope;
        conflictingMapEnvelope.actionId = QByteArrayLiteral("changed-map-action");
        const auto conflictingMapDuplicate = acceptedMapRouter.offer(
            conflictingMapEnvelope, acceptedMapCompletions);
        const auto exactMapAfterConflict = acceptedMapRouter.offer(
            acceptedMapEnvelope, acceptedMapCompletions);
        require(exactMapDuplicate.state == MapRouteState::duplicate,
                "map router did not acknowledge its accepted MAB duplicate");
        require(conflictingMapDuplicate.state == MapRouteState::rejected
                    && conflictingMapDuplicate.error
                    && conflictingMapDuplicate.error->code() == ErrorCode::actor_conflict
                    && acceptedMapRouter.quarantined().size() == 1,
                "map router did not quarantine a changed accepted MAB envelope");
        require(exactMapAfterConflict.state == MapRouteState::duplicate
                    && acceptedMapCompletions.size() == 2,
                "map router did not prioritize its accepted MAB envelope over a quarantined conflict");
        take(acceptedMapRouter.acknowledgeQuarantine(conflictingMapEnvelope));
        durableQuarantine = take(document->storage().readRecoveryState());
        require(durableQuarantine.quarantinedOperations.size() == 3,
                "map router acknowledged its conflict before provider persistence");
        const auto durableMapQuarantine = take(QuarantineRecordCodec::decode(
            durableQuarantine.quarantinedOperations.back()));
        require(durableMapQuarantine.primitive == QuarantinePrimitive::map
                    && durableMapQuarantine.actor == mapLocal.operation.producer
                    && durableMapQuarantine.counter == mapLocal.operation.counter
                    && durableMapQuarantine.targetBlock == mapLocal.targetBlock
                    && durableMapQuarantine.payload == mapLocal.bytes,
                "map quarantine persistence changed the rejected MAB envelope");
        ScopedMapRouter restoredMapRouter("durable-adapter-doc", 1);
        require(take(restoredMapRouter.restoreQuarantine(
                    durableQuarantine.quarantinedOperations)) == 1
                    && restoredMapRouter.quarantined().size() == 1,
                "map router did not rehydrate its durable quarantine after reopen");
        ScopedMapRouter undersizedMapRouter("durable-adapter-doc", 1, {0, 0});
        const auto capacityRefusal = undersizedMapRouter.restoreQuarantine(
            durableQuarantine.quarantinedOperations);
        require(!capacityRefusal.ok()
                    && capacityRefusal.error().code() == ErrorCode::deferred_queue_full
                    && undersizedMapRouter.heldOperationCount() == 0,
                "map quarantine recovery partially exceeded its bounded capacity");

        const auto textEdit = take(text->replaceUtf8(
            text->text().size(), text->text().size(), QByteArrayLiteral("!")));
        const auto editHistory = text->localHistorySnapshot();
        require(!editHistory.entries.empty(),
                "ordinary text edit did not create owner-local undo history");
        const QByteArray editId = QByteArrayLiteral("adapter-action-2");
        ActionBundleCodec::Bundle editBundle;
        editBundle.document = "durable-adapter-doc";
        editBundle.epoch = 1;
        editBundle.deliverySequence = 2;
        editBundle.actionId = editId;
        editBundle.kind = ActionKind::edit;
        editBundle.operations.push_back({textScope.stream, OperationPrimitive::text,
            insertedIdentity, textEdit.identity.actor, textEdit.identity.counter,
            OperationCodec::ctbo3, textEdit.bytes});

        const auto editRecovery = take(session->readRecoveryState());
        ownerHistory.transactionId = editId;
        ownerHistory.acceptedPrior = publication.committedRevision->token;
        ownerHistory.actionIndex.acceptedPrior = publication.committedRevision->token;
        ownerHistory.streams = {
            {PositionScope{bundle.document, std::string(kOrderStreamName),
                           Primitive::list, bundle.epoch}, session->actor(),
             list->localHistorySnapshot()},
            {textScope, session->actor(), editHistory}};
        const auto& textGroup = editHistory.entries.back();
        ownerHistory.actionIndex.events.push_back({editId, ActionKind::edit, {}, {
            {textScope.stream, OperationPrimitive::text, textGroup.group_id,
             textGroup.operation_ids}}});

        PreparedStorageTransaction editTransaction;
        editTransaction.transactionId = editId;
        editTransaction.expectedRevision = editRecovery.acceptedRevision;
        editTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        editTransaction.sharedRecord = take(ActionBundleCodec::encode(editBundle));
        editTransaction.reservations = editRecovery.unresolvedReservations;
        editTransaction.operationIdentities = {{QString::fromStdString(textScope.stream),
            textEdit.identity.actor, textEdit.identity.counter}};
        editTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(ownerHistory));
        const auto edited = document->publishAction(editTransaction);
        require(edited.state == ActionPublicationState::committed
                    && edited.committedRevision
                    && document->state() == ProviderDocumentState::ready
                    && document->recovery().acceptedRecords.size() == 2
                    && document->projection().textStream(textScope.stream)
                    && document->projection().textStream(textScope.stream)->text()
                        == QByteArrayLiteral("Durable text!")
                    && document->recovery().localHistory
                    && document->recovery().localHistory->committedTransactionId == editId,
                "ready provider-backed document did not publish and replay an ordinary edit");
        acceptedCompletions.clear();
        const auto duplicateAfterProjectionRefresh = acceptedRouter.offer(
            acceptedEnvelope, acceptedCompletions);
        require(duplicateAfterProjectionRefresh.state == RouteState::duplicate
                    && acceptedCompletions.size() == 1,
                "text router kept a stale accepted projection after lifecycle publication");
    }
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(storageScope, StorageMode::durable));
        const auto recovery = take(session->readRecoveryState());
        require(recovery.acceptedRecords.size() == 2
                    && recovery.unresolvedTransactions.empty()
                    && recovery.localHistory
                    && recovery.localHistory->committedTransactionId
                        == QByteArrayLiteral("adapter-action-2"),
                "durable adapter action did not survive provider restart");
        auto projection = take(AcceptedPrefixProjection::replay(*session, recovery));
        require(projection->idList()
                    && take(projection->idList()->visibleEntries()).size() == 1,
                "accepted-prefix replay did not reconstruct the structural projection");
        const auto replayedEntries = take(projection->idList()->visibleEntries());
        const auto replayedTextStream = take(blockTextStreamName(replayedEntries.front().identity));
        require(projection->textStream(replayedTextStream)
                    && projection->textStream(replayedTextStream)->text()
                        == QByteArrayLiteral("Durable text!")
                    && projection->mapStream("frontmatter")
                    && projection->mapStream("frontmatter")->get(
                        QByteArrayLiteral("title")) == QByteArrayLiteral("Durable"),
                "accepted-prefix replay did not rebuild the initial import and ordinary edit");
        const auto restoredTextHistory = projection->textStream(
            replayedTextStream)->localHistorySnapshot();
        require(restoredTextHistory.cursor == 2
                    && restoredTextHistory.entries.size() == 2,
                "owner-local undo history did not restore both accepted text actions");
        auto duplicateBundle = take(ActionBundleCodec::decode(
            recovery.acceptedRecords.front().bytes, "durable-adapter-doc", 1));
        duplicateBundle.actionId = QByteArrayLiteral("adapter-duplicate-identity");
        duplicateBundle.deliverySequence = 2;
        duplicateBundle.kind = ActionKind::edit;
        duplicateBundle.sourceSnapshot.clear();
        auto duplicateBytes = take(ActionBundleCodec::encode(duplicateBundle));
        auto duplicatePrefix = recovery;
        duplicatePrefix.acceptedRecords.push_back(AcceptedStorageRecord{
            duplicateBundle.actionId, QByteArrayLiteral("MAB/3"), duplicateBytes,
            recovery.acceptedRecords.back().committedRevision,
            AcceptedRevision{QByteArrayLiteral("test-revision:duplicate-prefix")}});
        duplicatePrefix.acceptedRevision =
            AcceptedRevision{QByteArrayLiteral("test-revision:duplicate-prefix")};
        require(!AcceptedPrefixProjection::replay(*session, duplicatePrefix).ok(),
                "accepted-prefix replay allowed an operation identity to reappear in a later MAB");
        auto list = take(ScopedIdListStream::create(*session, listScope));
        const auto bundle = take(ActionBundleCodec::decode(
            recovery.acceptedRecords[0].bytes, "durable-adapter-doc", 1));
        require(bundle.operations.size() == 3,
                "recovered adapter action has the wrong operation count");
        const auto& listOperation = bundle.operations[0];
        const auto listOutcome = take(list->applyRemote(RemoteListOperation{
            listScope, {listOperation.producer, listOperation.counter},
            listOperation.payload}));
        require(listOutcome.kind() == ApplyOutcomeKind::applied,
                "recovered list operation did not replay into a fresh projection");
        const auto listEntries = take(list->visibleEntries());
        const auto replayedBlock = listEntries.at(0).identity;
        const auto textScopeStream = take(blockTextStreamName(replayedBlock));
        const PositionScope textScope{"durable-adapter-doc", textScopeStream,
                                      Primitive::text, 1};
        auto text = take(ScopedTextStream::create(*session, textScope));
        const auto& textOperation = bundle.operations[1];
        const auto textOutcome = take(text->applyRemote(RemoteOperation{
            textScope, {textOperation.producer, textOperation.counter},
            textOperation.payload, 1, bundle.actionId, 1, 3}));
        require(textOutcome.is_visible_change(),
                "recovered text operation did not replay into a fresh projection");
        auto map = take(ReplicatedMapStream::create(
            *session, MapScope{"durable-adapter-doc", "frontmatter", 1}));
        const auto& mapOperation = bundle.operations[2];
        const auto mapOutcome = take(map->applyRemote(mapOperation.payload,
                                                       mapOperation.targetBlock));
        require(mapOutcome == MapApplyKind::applied && map->completeReplay().ok(),
                "recovered map operation did not replay into a fresh projection");
        require(text->text() == QByteArrayLiteral("Durable text")
                    && map->get(QByteArrayLiteral("title"))
                        == std::optional<QByteArray>(QByteArrayLiteral("Durable")),
                "durable adapter replay changed text or map state");
        require(listOutcome.kind() == ApplyOutcomeKind::applied,
                "recovered list operation did not replay into a fresh projection");
        const auto visible = take(list->visibleEntries());
        require(visible.size() == 1 && visible[0].identity == insertedIdentity
                    && visible[0].applicationValue == 9001,
                "durable list replay changed the structural identity or value");
        require(take(session->nextCounter(QString::fromStdString(
                    std::string(kOrderStreamName)))) > listOperation.counter,
                "list replay did not advance durable local counter observation");
    }
}

void runStagedProjectionRejectRollback()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create staged-projection rollback directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("staged-reject-doc"), 1};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto document = take(ProviderBackedDocumentState::open(
        *provider, scope, StorageMode::durable));
    const QByteArray transactionId = QByteArrayLiteral("staged-reject-import");
    const QByteArray markdown = QByteArrayLiteral("tentative");
    take(document->authorizeInitialImport({transactionId, markdown}));

    ActionStager incompleteStager("staged-reject-doc", 1, 1, transactionId,
                                  ActionKind::initialImport);
    const auto historyBuildRejected = document->publishStagedAction(
        incompleteStager, OwnerHistorySnapshot{});
    require(historyBuildRejected.state == ActionPublicationState::rejected
                && document->state() == ProviderDocumentState::initialImportAuthorized
                && !document->canStartOrdinaryAction(),
            "staged-action construction failure lost import authorization or opened edits");

    std::array<uint8_t, ActorId::kBytes> actorBytes{};
    actorBytes.back() = 9;
    const BlockIdentity block{ActorId::from_bytes(actorBytes), 7};
    const auto route = take(blockTextStreamName(block));
    auto* stagedText = take(document->stagingTextStream(block));
    const auto seed = take(stagedText->seedInitialImport(markdown));
    require(seed && stagedText->text() == markdown,
            "lifecycle staging accessor did not expose the tentative import projection");

    const auto recovery = take(document->storage().readRecoveryState());
    PreparedStorageTransaction malformed;
    malformed.transactionId = transactionId;
    malformed.expectedRevision = recovery.acceptedRevision;
    malformed.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    malformed.sharedRecord = QByteArrayLiteral("not a canonical MAB");
    malformed.reservations = seed->reservations;
    const auto rejected = document->publishAction(malformed);
    require(rejected.state == ActionPublicationState::rejected
                && rejected.diagnostic
                && document->state() == ProviderDocumentState::recoveryRequired
                && !document->canStartOrdinaryAction()
                && document->projection().textStream(route)
                && document->projection().textStream(route)->text().isEmpty()
                && document->recovery().initialImport
                    == PendingImportIntent{transactionId, markdown},
            "definite action rejection left tentative content visible or opened the write barrier");
}

void runSelectiveStructuralUndo()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("selective-undo-doc"), 1},
        StorageMode::ephemeral));
    const PositionScope scope{"selective-undo-doc", std::string(kOrderStreamName),
                              Primitive::list, 1};
    auto list = take(ScopedIdListStream::create(*session, scope));
    std::vector<RemoteListOperation> accepted;
    const auto record = [&](const LocalListOperation& operation) {
        accepted.push_back({operation.scope, operation.identity, operation.bytes});
    };

    const auto actionA = take(list->insertAfter(std::nullopt, 101));
    record(actionA);
    const auto historyA = list->localHistorySnapshot();
    require(historyA.cursor == 1 && historyA.entries.size() == 1,
            "action A did not create one structural undo group");
    const auto groupA = historyA.entries[0].group_id;

    const auto actionB = take(list->insertAfter(actionA.insertedBlock, 202));
    record(actionB);
    const auto historyAB = list->localHistorySnapshot();
    require(historyAB.cursor == 2 && historyAB.entries.size() == 2,
            "action B did not create a second structural undo group");
    const auto groupB = historyAB.entries[1].group_id;
    require(groupA != groupB,
            "distinct block actions unexpectedly share one undo group");

    const auto undoA = take(list->undoGroup(groupA));
    require(undoA.has_value(), "selective undo did not produce a list operation");
    record(*undoA);
    auto visible = take(list->visibleEntries());
    require(visible.size() == 1 && visible[0].identity == *actionB.insertedBlock
                && visible[0].applicationValue == 202,
            "selective undo of block A changed newer block B");
    require(list->localHistorySnapshot().cursor == 1
                && list->localHistorySnapshot().entries[0].group_id == groupB,
            "selective undo did not leave block B as the next ordinary undo");

    const auto undoB = take(list->undo());
    require(undoB.has_value(), "ordinary undo did not undo remaining block B");
    record(*undoB);
    require(take(list->visibleEntries()).empty(),
            "undoing block B after selective undo left structural entries visible");
    const auto afterUndos = list->localHistorySnapshot();
    require(afterUndos.cursor == 0 && afterUndos.entries.size() == 2
                && afterUndos.entries[0].group_id == groupB
                && afterUndos.entries[1].group_id == groupA,
            "selective undo history was not represented in its persisted order");

    // Rebuild a fresh projection from accepted operations and restore the
    // owner-local history exactly as a reopened document does.
    list.reset();
    auto reopened = take(ScopedIdListStream::create(*session, scope));
    for (const auto& operation : accepted) {
        const auto applied = take(reopened->applyRemote(operation));
        require(applied.kind() == ApplyOutcomeKind::applied,
                "reopened structural projection rejected accepted history");
    }
    require(take(reopened->visibleEntries()).empty(),
            "replayed selective undo prefix did not restore the empty list");
    require(reopened->restoreLocalHistory(afterUndos).ok(),
            "reopened structural projection rejected its saved history");

    const auto redoA = take(reopened->redoGroup(groupA));
    require(redoA.has_value(), "selective redo did not restore block A");
    auto afterRedoA = take(reopened->visibleEntries());
    require(afterRedoA.size() == 1 && afterRedoA[0].identity == *actionA.insertedBlock,
            "selective redo of block A did not restore its exact identity");
    const auto redoB = take(reopened->redoGroup(groupB));
    require(redoB.has_value(), "selective redo did not restore block B");
    const auto afterRedoB = take(reopened->visibleEntries());
    require(afterRedoB.size() == 2
                && afterRedoB[0].identity == *actionA.insertedBlock
                && afterRedoB[1].identity == *actionB.insertedBlock
                && afterRedoB[1].applicationValue == 202,
            "redo after restart did not restore both exact structural identities");
}

void runCrossStreamSelectiveActionUndo()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("cross-stream-undo-doc"), 1},
        StorageMode::ephemeral));
    const PositionScope listScope{"cross-stream-undo-doc",
                                  std::string(kOrderStreamName), Primitive::list, 1};
    auto list = take(ScopedIdListStream::create(*session, listScope));
    std::vector<LocalListOperation> listOps;
    std::vector<LocalOperation> textAOps;
    std::vector<LocalOperation> textBOps;

    const auto blockAOp = take(list->insertAfter(std::nullopt, 11));
    listOps.push_back(blockAOp);
    const auto blockA = *blockAOp.insertedBlock;
    const PositionScope textAScope{"cross-stream-undo-doc",
                                   take(blockTextStreamName(blockA)),
                                   Primitive::text, 1};
    auto textA = take(ScopedTextStream::create(*session, textAScope));
    const auto textAOp = take(textA->replaceUtf8(0, 0, QByteArrayLiteral("A")));
    textAOps.push_back(textAOp);
    const auto listGroupA = list->localHistorySnapshot().entries[0].group_id;
    const auto textGroupA = textA->localHistorySnapshot().entries[0].group_id;

    const auto blockBOp = take(list->insertAfter(blockA, 22));
    listOps.push_back(blockBOp);
    const auto blockB = *blockBOp.insertedBlock;
    const PositionScope textBScope{"cross-stream-undo-doc",
                                   take(blockTextStreamName(blockB)),
                                   Primitive::text, 1};
    auto textB = take(ScopedTextStream::create(*session, textBScope));
    const auto textBOp = take(textB->replaceUtf8(0, 0, QByteArrayLiteral("B")));
    textBOps.push_back(textBOp);
    const auto listGroupB = list->localHistorySnapshot().entries[1].group_id;
    const auto textGroupB = textB->localHistorySnapshot().entries[0].group_id;

    // Action A spans the shared order stream and block A's text stream. Undo
    // both exact groups in reverse action order; block B remains untouched.
    const auto undoTextA = take(textA->undoGroup(textGroupA));
    require(undoTextA.has_value(), "action A text compensation was not produced");
    textAOps.push_back(*undoTextA);
    const auto undoListA = take(list->undoGroup(listGroupA));
    require(undoListA.has_value(), "action A order compensation was not produced");
    listOps.push_back(*undoListA);
    require(textA->text().isEmpty() && textB->text() == QByteArrayLiteral("B"),
            "undoing action A changed block B text or left block A text visible");
    auto visible = take(list->visibleEntries());
    require(visible.size() == 1 && visible[0].identity == blockB,
            "undoing action A removed newer block B from the shared order stream");

    // The next document action is undone normally across its two streams.
    const auto undoTextB = take(textB->undo());
    require(undoTextB.has_value(), "action B text undo was not produced");
    textBOps.push_back(*undoTextB);
    const auto undoListB = take(list->undo());
    require(undoListB.has_value(), "action B order undo was not produced");
    listOps.push_back(*undoListB);
    require(textA->text().isEmpty() && textB->text().isEmpty()
                && take(list->visibleEntries()).empty(),
            "undoing action B after A did not restore the empty document projection");

    const auto listHistory = list->localHistorySnapshot();
    const auto textAHistory = textA->localHistorySnapshot();
    const auto textBHistory = textB->localHistorySnapshot();
    list.reset();
    textA.reset();
    textB.reset();

    // A fresh set of per-stream projections replays each stream's accepted
    // operation prefix and then restores owner-local group mappings.
    auto reopenedList = take(ScopedIdListStream::create(*session, listScope));
    for (const auto& operation : listOps) {
        const auto result = take(reopenedList->applyRemote(
            {operation.scope, operation.identity, operation.bytes}));
        require(result.kind() == ApplyOutcomeKind::applied,
                "reopen rejected a shared order-stream action operation");
    }
    auto reopenedTextA = take(ScopedTextStream::create(*session, textAScope));
    for (const auto& operation : textAOps) {
        const auto result = take(reopenedTextA->applyRemote(
            {operation.scope, operation.identity, operation.bytes}));
        require(result.is_visible_change(),
                "reopen rejected a block A text action operation");
    }
    auto reopenedTextB = take(ScopedTextStream::create(*session, textBScope));
    for (const auto& operation : textBOps) {
        const auto result = take(reopenedTextB->applyRemote(
            {operation.scope, operation.identity, operation.bytes}));
        require(result.is_visible_change(),
                "reopen rejected a block B text action operation");
    }
    require(reopenedList->restoreLocalHistory(listHistory).ok()
                && reopenedTextA->restoreLocalHistory(textAHistory).ok()
                && reopenedTextB->restoreLocalHistory(textBHistory).ok(),
            "reopen did not restore all owner-local stream histories");

    const auto redoTextA = take(reopenedTextA->redoGroup(textGroupA));
    const auto redoListA = take(reopenedList->redoGroup(listGroupA));
    require(redoTextA.has_value() && redoListA.has_value()
                && reopenedTextA->text() == QByteArrayLiteral("A")
                && reopenedTextB->text().isEmpty(),
            "selective redo of action A did not restore exactly its text stream");
    visible = take(reopenedList->visibleEntries());
    require(visible.size() == 1 && visible[0].identity == blockA,
            "selective redo of action A did not restore its original block identity");

    const auto redoTextB = take(reopenedTextB->redoGroup(textGroupB));
    const auto redoListB = take(reopenedList->redoGroup(listGroupB));
    require(redoTextB.has_value() && redoListB.has_value()
                && reopenedTextA->text() == QByteArrayLiteral("A")
                && reopenedTextB->text() == QByteArrayLiteral("B"),
            "redo of action B did not restore exactly its text stream");
    visible = take(reopenedList->visibleEntries());
    require(visible.size() == 2 && visible[0].identity == blockA
                && visible[1].identity == blockB,
            "redo across streams did not restore both original block identities");
}

void runDurableCrossStreamUndoAction()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create durable cross-stream undo directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("durable-undo-doc"), 21};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    ActionPublicationCoordinator coordinator(*session);

    const std::string document = "durable-undo-doc";
    const QByteArray actionA = QByteArrayLiteral("durable-action-A");
    SourceSnapshotRecord initialSource;
    initialSource.document = document;
    initialSource.epoch = storageScope.epoch;
    initialSource.originalMarkdown = QByteArrayLiteral("---\ntitle: A\n---\nA text\n");
    require(session->beginInitialImport(PendingImportIntent{
                actionA, initialSource.originalMarkdown}).ok(),
            "durable undo trace could not register its initial source");
    const PositionScope listScope{document, std::string(kOrderStreamName),
                                  Primitive::list, storageScope.epoch};
    const MapScope mapScope{document, "frontmatter", storageScope.epoch};
    auto list = take(ScopedIdListStream::create(*session, listScope));
    auto map = take(ReplicatedMapStream::create(*session, mapScope));
    require(map->completeReplay().ok(), "durable undo map did not complete replay");

    auto indexedHistory = CrossStreamHistoryIndex{};
    indexedHistory.document = document;
    indexedHistory.epoch = storageScope.epoch;
    indexedHistory.owner = session->actor();
    PreparedStorageTransaction lastTransaction;

    auto listActionOperation = [](const LocalListOperation& operation) {
        return ActionOperation{operation.scope.stream, OperationPrimitive::idList,
            std::nullopt, operation.identity.actor, operation.identity.counter,
            OperationCodec::ctlo2, operation.bytes};
    };
    auto textActionOperation = [](const LocalOperation& operation,
                                  const BlockIdentity& block) {
        return ActionOperation{operation.scope.stream, OperationPrimitive::text,
            block, operation.identity.actor, operation.identity.counter,
            OperationCodec::ctbo3, operation.bytes};
    };
    auto listGroup = [](const PositionScope& scope,
                        const Crdt::LocalUndoEntrySnapshot& entry) {
        return HistoryGroupAssociation{scope.stream, OperationPrimitive::idList,
            entry.group_id, entry.operation_ids};
    };
    auto textGroup = [](const PositionScope& scope,
                        const Crdt::LocalUndoEntrySnapshot& entry) {
        return HistoryGroupAssociation{scope.stream, OperationPrimitive::text,
            entry.group_id, entry.operation_ids};
    };

    auto publishAction = [&](ActionBundleCodec::Bundle bundle,
                             ActionHistoryEvent event,
                             std::vector<LocalHistoryRecord> streams,
                             std::vector<RetiredHistoryAssociation> retirements = {},
                             bool injectUncertainCommit = false) {
        auto recovery = take(session->readRecoveryState());
        indexedHistory.acceptedPrior = recovery.acceptedRevision.token;
        event.actionId = bundle.actionId;
        event.kind = bundle.kind;
        indexedHistory.events.push_back(std::move(event));
        indexedHistory.retirements.insert(indexedHistory.retirements.end(),
            retirements.begin(), retirements.end());

        OwnerHistorySnapshot ownerHistory;
        ownerHistory.document = document;
        ownerHistory.epoch = storageScope.epoch;
        ownerHistory.owner = session->actor();
        ownerHistory.transactionId = bundle.actionId;
        ownerHistory.acceptedPrior = recovery.acceptedRevision.token;
        ownerHistory.streams = std::move(streams);
        ownerHistory.actionIndex = indexedHistory;

        PreparedStorageTransaction transaction;
        transaction.transactionId = bundle.actionId;
        transaction.expectedRevision = recovery.acceptedRevision;
        transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        transaction.sharedRecord = take(ActionBundleCodec::encode(bundle));
        transaction.reservations = recovery.unresolvedReservations;
        for (const auto& operation : bundle.operations)
            transaction.operationIdentities.push_back({
                QString::fromUtf8(operation.stream.data(),
                    static_cast<qsizetype>(operation.stream.size())),
                operation.producer, operation.counter});
        transaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(ownerHistory));
        lastTransaction = transaction;
        if (injectUncertainCommit)
            Testing::failNextReferenceProviderCommitAt(
                Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
        const auto result = coordinator.publish(transaction);
        if (injectUncertainCommit) {
            require(result.state == ActionPublicationState::uncertain
                        && coordinator.blocked(),
                    "undo action did not preserve the barrier after uncertain append");
            const auto blockedRetry = coordinator.publish(transaction);
            require(blockedRetry.state == ActionPublicationState::uncertain
                        && coordinator.blocked(),
                    "coordinator allowed another action before uncertain undo recovery");
            return;
        }
        require(result.state == ActionPublicationState::committed
                    && result.committedRevision && !coordinator.blocked(),
                "coordinator failed to publish one complete cross-stream action");
    };

    const auto listA = take(list->insertAfter(std::nullopt, 101));
    const BlockIdentity blockA = *listA.insertedBlock;
    const PositionScope textAScope{document, take(blockTextStreamName(blockA)),
                                   Primitive::text, storageScope.epoch};
    auto textA = take(ScopedTextStream::create(*session, textAScope));
    const auto textAOperation = take(textA->replaceUtf8(0, 0, QByteArrayLiteral("A text")));
    const auto mapA = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("A")));
    const auto mapAGroup = take(toHistoryAssociation(mapA));
    const auto listHistoryA = list->localHistorySnapshot();
    const auto textHistoryA = textA->localHistorySnapshot();
    require(listHistoryA.entries.size() == 1 && textHistoryA.entries.size() == 1,
            "action A did not produce one history group per affected engine");

    ActionBundleCodec::Bundle bundleA;
    bundleA.document = document;
    bundleA.epoch = storageScope.epoch;
    bundleA.deliverySequence = 1;
    bundleA.actionId = actionA;
    bundleA.kind = ActionKind::initialImport;
    bundleA.sourceSnapshot = take(SourceSnapshotCodec::encode(initialSource));
    bundleA.operations = {listActionOperation(listA),
                          textActionOperation(textAOperation, blockA),
                          take(toActionOperation(mapA))};
    ActionHistoryEvent eventA;
    eventA.groups = {listGroup(listScope, listHistoryA.entries[0]),
                     textGroup(textAScope, textHistoryA.entries[0]), mapAGroup};
    publishAction(std::move(bundleA), std::move(eventA), {
        {listScope, session->actor(), listHistoryA},
        {textAScope, session->actor(), textHistoryA}});

    const QByteArray actionB = QByteArrayLiteral("durable-action-B");
    const auto listB = take(list->insertAfter(blockA, 202));
    const BlockIdentity blockB = *listB.insertedBlock;
    const PositionScope textBScope{document, take(blockTextStreamName(blockB)),
                                   Primitive::text, storageScope.epoch};
    auto textB = take(ScopedTextStream::create(*session, textBScope));
    const auto textBOperation = take(textB->replaceUtf8(0, 0, QByteArrayLiteral("B text")));
    const auto mapB = take(map->set(QByteArrayLiteral("author"), QByteArrayLiteral("B")));
    const auto mapBGroup = take(toHistoryAssociation(mapB));
    const auto listHistoryB = list->localHistorySnapshot();
    const auto textHistoryB = textB->localHistorySnapshot();
    require(listHistoryB.entries.size() == 2 && textHistoryB.entries.size() == 1,
            "action B did not retain both shared-order groups");

    ActionBundleCodec::Bundle bundleB;
    bundleB.document = document;
    bundleB.epoch = storageScope.epoch;
    bundleB.deliverySequence = 2;
    bundleB.actionId = actionB;
    bundleB.kind = ActionKind::edit;
    bundleB.operations = {listActionOperation(listB),
                          textActionOperation(textBOperation, blockB),
                          take(toActionOperation(mapB))};
    ActionHistoryEvent eventB;
    eventB.groups = {listGroup(listScope, listHistoryB.entries[1]),
                     textGroup(textBScope, textHistoryB.entries[0]), mapBGroup};
    publishAction(std::move(bundleB), std::move(eventB), {
        {listScope, session->actor(), listHistoryB},
        {textAScope, session->actor(), textHistoryA},
        {textBScope, session->actor(), textHistoryB}});

    const auto groupAList = listHistoryA.entries[0].group_id;
    const auto groupAText = textHistoryA.entries[0].group_id;
    const auto undoTextA = take(textA->undoGroup(groupAText));
    const auto undoListA = take(list->undoGroup(groupAList));
    require(undoTextA && undoListA,
            "selective undo did not produce text and order compensation");
    const auto undoMapA = take(map->compensate(mapA));
    require(textA->text().isEmpty() && textB->text() == QByteArrayLiteral("B text")
                && take(list->visibleEntries()).size() == 1
                && take(list->visibleEntries()).front().identity == blockB
                && !map->get(QByteArrayLiteral("title"))
                && map->get(QByteArrayLiteral("author"))
                    == std::optional<QByteArray>(QByteArrayLiteral("B")),
            "selective undo changed action B while compensating action A");

    const auto listHistoryUndo = list->localHistorySnapshot();
    const auto textHistoryUndo = textA->localHistorySnapshot();
    const auto textHistoryBUndo = textB->localHistorySnapshot();
    const QByteArray undoActionId = QByteArrayLiteral("durable-undo-A");
    ActionBundleCodec::Bundle undoBundle;
    undoBundle.document = document;
    undoBundle.epoch = storageScope.epoch;
    undoBundle.deliverySequence = 3;
    undoBundle.actionId = undoActionId;
    undoBundle.kind = ActionKind::undo;
    undoBundle.operations = {take(toActionOperation(undoMapA)),
        textActionOperation(*undoTextA, blockA), listActionOperation(*undoListA)};

    const auto undoMapGroup = take(toHistoryAssociation(undoMapA));
    HistoryGroupAssociation undoTextGroup{textAScope.stream, OperationPrimitive::text,
        groupAText, {{undoTextA->identity.actor, undoTextA->identity.counter}}};
    HistoryGroupAssociation undoListGroup{std::string(kOrderStreamName),
        OperationPrimitive::idList, groupAList,
        {{undoListA->identity.actor, undoListA->identity.counter}}};
    ActionHistoryEvent undoEvent;
    undoEvent.parentActionId = actionA;
    undoEvent.groups = {undoMapGroup, undoTextGroup, undoListGroup};
    publishAction(std::move(undoBundle), std::move(undoEvent), {
        {listScope, session->actor(), listHistoryUndo},
        {textAScope, session->actor(), textHistoryUndo},
        {textBScope, session->actor(), textHistoryBUndo}}, {}, true);

    list.reset();
    textA.reset();
    textB.reset();
    map.reset();
    session.reset();
    provider.reset();

    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(storageScope, StorageMode::durable));
    ActionPublicationCoordinator recoveryCoordinator(*session);
    const auto resolvedUndo = recoveryCoordinator.recover(lastTransaction);
    require(resolvedUndo.state == ActionPublicationState::committed
                && resolvedUndo.committedRevision && !recoveryCoordinator.blocked(),
            "reopen did not reconcile the exact uncertain cross-stream undo action");
    const auto accepted = take(session->readRecoveryState());
    require(accepted.acceptedRecords.size() == 3,
            "cross-stream undo did not append exactly three document actions");
    const auto committedUndo = take(ActionBundleCodec::decode(
        accepted.acceptedRecords.back().bytes, document, storageScope.epoch));
    require(committedUndo.kind == ActionKind::undo
                && committedUndo.operations.size() == 3
                && accepted.unresolvedTransactions.empty()
                && accepted.unresolvedReservations.empty()
                && accepted.localHistory
                && accepted.localHistory->committedTransactionId == undoActionId,
            "cross-stream undo was not one committed action with durable owner history");
    const auto recovered = take(session->readRecoveryState());
    auto forgedEarlierHistory = recovered;
    require(forgedEarlierHistory.localHistory.has_value(),
            "cross-stream undo recovery did not return owner history to validate");
    auto forgedOwnerHistory = take(OwnerHistoryCodec::decode(
        forgedEarlierHistory.localHistory->bytes, document, storageScope.epoch,
        session->actor(), forgedEarlierHistory.localHistory->committedTransactionId,
        accepted.acceptedRecords.back().priorRevision.token));
    require(!forgedOwnerHistory.actionIndex.events.front().groups.empty()
                && !forgedOwnerHistory.actionIndex.events.front().groups.front()
                    .operationIds.empty(),
            "cross-stream import history fixture has no operation to forge");
    forgedOwnerHistory.actionIndex.events.front().groups.front()
        .operationIds.front().counter += 1000000000;
    forgedEarlierHistory.localHistory->bytes = take(
        OwnerHistoryCodec::encode(forgedOwnerHistory));
    const auto rejectedForgedHistory = AcceptedPrefixProjection::replay(
        *session, forgedEarlierHistory);
    require(!rejectedForgedHistory.ok()
                && rejectedForgedHistory.error().code() == ErrorCode::invalid_identity,
            "accepted-prefix replay trusted stale owner history for an earlier MAB");
    auto replay = take(AcceptedPrefixProjection::replay(*session, recovered));
    const auto entries = take(replay->idList()->visibleEntries());
    const auto* replayTextA = replay->textStream(textAScope.stream);
    const auto* replayTextB = replay->textStream(textBScope.stream);
    const auto* replayMap = replay->mapStream(mapScope.map);
    require(replay->ownerHistory()
                && replay->ownerHistory()->transactionId == undoActionId
                && entries.size() == 1 && entries.front().identity == blockB
                && replayTextA && replayTextB
                && replayTextA->text().isEmpty()
                && replayTextB->text() == QByteArrayLiteral("B text")
                && replay->idList()->localHistorySnapshot().entries.size() == 2
                && replayTextA->localHistorySnapshot().entries.size() == 1
                && replayTextB->localHistorySnapshot().entries.size() == 1
                && replayMap && !replayMap->get(QByteArrayLiteral("title"))
                && replayMap->get(QByteArrayLiteral("author"))
                    == std::optional<QByteArray>(QByteArrayLiteral("B")),
            "reopen did not reconstruct the selective cross-stream undo projection");

    // Recreate mutable adapters from the durable prefix, restore owner history,
    // and publish a selective redo as a new MAB action with fresh identities.
    require(replay->ownerHistory().has_value(),
            "reopened undo projection did not expose durable owner history");
    auto redoList = take(ScopedIdListStream::create(*session, listScope));
    auto redoTextA = take(ScopedTextStream::create(*session, textAScope));
    auto redoTextB = take(ScopedTextStream::create(*session, textBScope));
    auto redoMap = take(ReplicatedMapStream::create(*session, mapScope));
    for (const auto& record : accepted.acceptedRecords) {
        const auto acceptedBundle = take(ActionBundleCodec::decode(
            record.bytes, document, storageScope.epoch));
        for (const auto& operation : acceptedBundle.operations) {
            if (operation.primitive == OperationPrimitive::idList) {
                const auto applied = take(redoList->applyRemote(RemoteListOperation{
                    listScope, {operation.producer, operation.counter}, operation.payload}));
                require(applied.kind() == ApplyOutcomeKind::applied,
                        "redo rebuild rejected an accepted structural operation");
            } else if (operation.primitive == OperationPrimitive::text) {
                auto* stream = operation.stream == textAScope.stream
                    ? redoTextA.get() : redoTextB.get();
                const auto applied = take(stream->applyRemote(RemoteOperation{
                    stream->scope(), {operation.producer, operation.counter},
                    operation.payload}));
                require(applied.is_visible_change(),
                        "redo rebuild rejected an accepted text operation");
            } else {
                const auto applied = take(redoMap->applyRemote(operation.payload,
                                                               operation.targetBlock));
                require(applied == MapApplyKind::applied,
                        "redo rebuild rejected an accepted map operation");
            }
        }
    }
    require(redoMap->completeReplay().ok(),
            "redo map projection did not complete accepted-prefix replay");
    const auto restoredHistory = *replay->ownerHistory();
    for (const auto& stream : restoredHistory.streams) {
        if (stream.scope == listScope)
            require(redoList->restoreLocalHistory(stream.snapshot).ok(),
                    "redo rebuild rejected structural owner history");
        else if (stream.scope == textAScope)
            require(redoTextA->restoreLocalHistory(stream.snapshot).ok(),
                    "redo rebuild rejected block A owner history");
        else if (stream.scope == textBScope)
            require(redoTextB->restoreLocalHistory(stream.snapshot).ok(),
                    "redo rebuild rejected block B owner history");
    }
    const auto redoListOperation = take(redoList->redoGroup(groupAList));
    const auto redoTextOperation = take(redoTextA->redoGroup(groupAText));
    const auto redoMapOperation = take(redoMap->set(
        QByteArrayLiteral("title"), QByteArrayLiteral("A")));
    require(redoListOperation && redoTextOperation
                && redoTextA->text() == QByteArrayLiteral("A text")
                && redoTextB->text() == QByteArrayLiteral("B text")
                && redoMap->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("A"))
                && redoMap->get(QByteArrayLiteral("author"))
                    == std::optional<QByteArray>(QByteArrayLiteral("B")),
            "post-restart selective redo did not restore A while preserving B");
    const auto redoneEntries = take(redoList->visibleEntries());
    require(redoneEntries.size() == 2 && redoneEntries[0].identity == blockA
                && redoneEntries[1].identity == blockB,
            "post-restart redo changed the original block identities or order");

    const QByteArray redoActionId = QByteArrayLiteral("durable-redo-A");
    ActionBundleCodec::Bundle redoBundle;
    redoBundle.document = document;
    redoBundle.epoch = storageScope.epoch;
    redoBundle.deliverySequence = 4;
    redoBundle.actionId = redoActionId;
    redoBundle.kind = ActionKind::redo;
    redoBundle.operations = {listActionOperation(*redoListOperation),
        textActionOperation(*redoTextOperation, blockA),
        take(toActionOperation(redoMapOperation))};
    auto redoHistoryIndex = restoredHistory.actionIndex;
    auto redoRecovery = take(session->readRecoveryState());
    redoHistoryIndex.acceptedPrior = redoRecovery.acceptedRevision.token;
    HistoryGroupAssociation redoListGroup{listScope.stream,
        OperationPrimitive::idList, groupAList,
        {{redoListOperation->identity.actor, redoListOperation->identity.counter}}};
    HistoryGroupAssociation redoTextGroup{textAScope.stream,
        OperationPrimitive::text, groupAText,
        {{redoTextOperation->identity.actor, redoTextOperation->identity.counter}}};
    auto redoMapGroup = take(toHistoryAssociation(redoMapOperation));
    redoHistoryIndex.events.push_back({redoActionId, ActionKind::redo,
        undoActionId, {redoListGroup, redoTextGroup, redoMapGroup}});
    redoHistoryIndex.retirements.push_back({std::string(kOrderStreamName),
        OperationPrimitive::idList, groupAList, redoActionId});
    redoHistoryIndex.retirements.push_back({textAScope.stream,
        OperationPrimitive::text, groupAText, redoActionId});
    redoHistoryIndex.retirements.push_back({mapAGroup.stream,
        OperationPrimitive::markoffMap, mapAGroup.groupId, redoActionId});

    OwnerHistorySnapshot redoOwnerHistory;
    redoOwnerHistory.document = document;
    redoOwnerHistory.epoch = storageScope.epoch;
    redoOwnerHistory.owner = session->actor();
    redoOwnerHistory.transactionId = redoActionId;
    redoOwnerHistory.acceptedPrior = redoRecovery.acceptedRevision.token;
    redoOwnerHistory.streams = {
        {listScope, session->actor(), redoList->localHistorySnapshot()},
        {textAScope, session->actor(), redoTextA->localHistorySnapshot()},
        {textBScope, session->actor(), redoTextB->localHistorySnapshot()}};
    redoOwnerHistory.actionIndex = redoHistoryIndex;

    PreparedStorageTransaction redoTransaction;
    redoTransaction.transactionId = redoActionId;
    redoTransaction.expectedRevision = redoRecovery.acceptedRevision;
    redoTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    redoTransaction.sharedRecord = take(ActionBundleCodec::encode(redoBundle));
    redoTransaction.reservations = redoRecovery.unresolvedReservations;
    for (const auto& operation : redoBundle.operations)
        redoTransaction.operationIdentities.push_back({
            QString::fromUtf8(operation.stream.data(),
                static_cast<qsizetype>(operation.stream.size())),
            operation.producer, operation.counter});
    redoTransaction.localHistoryAfterCommit = take(
        OwnerHistoryCodec::encode(redoOwnerHistory));
    ActionPublicationCoordinator redoCoordinator(*session);
    const auto redoPublication = redoCoordinator.publish(redoTransaction);
    require(redoPublication.state == ActionPublicationState::committed
                && redoPublication.committedRevision && !redoCoordinator.blocked(),
            "coordinator did not durably publish post-restart selective redo");

    redoList.reset();
    redoTextA.reset();
    redoTextB.reset();
    redoMap.reset();
    replay.reset();
    session.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(storageScope, StorageMode::durable));
    const auto redoneState = take(session->readRecoveryState());
    const auto durableRedo = take(AcceptedPrefixProjection::replay(*session, redoneState));
    const auto durableRedoEntries = take(durableRedo->idList()->visibleEntries());
    const auto* durableRedoTextA = durableRedo->textStream(textAScope.stream);
    const auto* durableRedoTextB = durableRedo->textStream(textBScope.stream);
    const auto* durableRedoMap = durableRedo->mapStream(mapScope.map);
    require(redoneState.acceptedRecords.size() == 4
                && durableRedoEntries.size() == 2
                && durableRedoEntries[0].identity == blockA
                && durableRedoEntries[1].identity == blockB
                && durableRedoTextA && durableRedoTextA->text() == QByteArrayLiteral("A text")
                && durableRedoTextB && durableRedoTextB->text() == QByteArrayLiteral("B text")
                && durableRedoMap
                && durableRedoMap->get(QByteArrayLiteral("title"))
                    == std::optional<QByteArray>(QByteArrayLiteral("A"))
                && durableRedoMap->get(QByteArrayLiteral("author"))
                    == std::optional<QByteArray>(QByteArrayLiteral("B"))
                && durableRedo->ownerHistory()
                && durableRedo->ownerHistory()->transactionId == redoActionId,
            "second reopen did not replay the exact durable redo state and history");
}

void runAbruptProcessRestart()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create process-restart test directory");
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--reserve-and-exit"), temp.path()});
    require(child.waitForStarted(), "could not start reference-provider child process");
    require(child.waitForFinished(15000), "reference-provider child process timed out");
    require(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
            "reference-provider child process failed before exiting");
    const QByteArray actorBytes = child.readAllStandardOutput().trimmed();
    require(!actorBytes.isEmpty(), "child process did not report its writer actor");

    auto provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temp.path().toStdString())));
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("process-restart-doc"), 1},
        StorageMode::durable));
    const auto state = take(session->readRecoveryState());
    require(state.unresolvedReservations.size() == 1
                && state.unresolvedReservations[0].stream
                    == QStringLiteral("markoff/block-text/v1/process-restart")
                && state.unresolvedReservations[0].first == 1
                && state.unresolvedReservations[0].count == 5
                && QByteArray::fromStdString(session->actor().to_string()) == actorBytes,
            "reference provider did not recover identity and reservation after process exit");
}

void runQuarantinePersistence()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create quarantine persistence test directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("quarantine-doc"), 1};
    const QByteArray original("MQR/1\0exact-envelope", 20);
    const QByteArray largeRecord(10 * 1024 * 1024, 'q');
    QByteArray encodedEnvelope;
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        const auto streamName = take(blockTextStreamName(
            BlockIdentity{session->actor(), 1}));
        const PositionScope textScope{"quarantine-doc", streamName,
                                      Primitive::text, 1};
        auto text = take(ScopedTextStream::create(*session, textScope));
        auto operation = take(text->replaceUtf8(0, 0, QByteArrayLiteral("x")));
        QuarantineRecord record;
        record.document = "quarantine-doc";
        record.epoch = 1;
        record.stream = textScope.stream;
        record.primitive = QuarantinePrimitive::text;
        record.actor = operation.identity.actor;
        record.counter = operation.identity.counter;
        record.payload = operation.bytes;
        record.deliverySequence = 1;
        record.actionId = QByteArrayLiteral("quarantine-action");
        record.bundleOperationCount = 1;
        record.reason = Error(ErrorCode::actor_conflict, "reused identity");
        encodedEnvelope = take(QuarantineRecordCodec::encode(record));
        take(session->storeQuarantinedOperation(original));
        take(session->storeQuarantinedOperation(original));
        take(session->storeQuarantinedOperation(encodedEnvelope));
        take(session->storeQuarantinedOperation(largeRecord));
        const auto state = take(session->readRecoveryState());
        require(state.quarantinedOperations
                    == std::vector<QByteArray>{original, encodedEnvelope, largeRecord},
                "quarantine retry was not idempotent or changed its bytes");
    }
    auto recoveredProvider = take(makeReferenceDocumentStorageProvider(root));
    auto reopened = take(recoveredProvider->open(scope, StorageMode::durable));
    const auto recovered = take(reopened->readRecoveryState());
    require(recovered.quarantinedOperations
                == std::vector<QByteArray>{original, encodedEnvelope, largeRecord},
            "durable reopen lost the quarantined envelope");
    ScopedOperationRouter restored("quarantine-doc", 1);
    require(take(restored.restoreQuarantine({encodedEnvelope})) == 1
                && restored.quarantined().size() == 1
                && restored.quarantined().front().operation.bytes
                    == QuarantineRecordCodec::decode(encodedEnvelope).value().payload,
            "reopened provider quarantine was not restored into its text router");
}

void runQuarantineAcknowledgementUncertainty()
{
#if defined(Q_OS_UNIX)
    using Testing::ReferenceProviderFaultPoint;
    using Testing::failNextReferenceProviderCommitAt;
    QTemporaryDir temp;
    require(temp.isValid(), "could not create quarantine uncertainty test directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("quarantine-uncertain-doc"), 1};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    std::vector<QByteArray> persisted;
    {
        auto document = take(ProviderBackedDocumentState::open(
            *provider, scope, StorageMode::durable));
        const PositionScope listScope{"quarantine-uncertain-doc",
            std::string(kOrderStreamName), Primitive::list, 1};
        auto stream = take(ScopedIdListStream::create(document->storage(), listScope));
        const auto local = take(stream->insertAfter(std::nullopt, 23));
        RoutedListOperation original{
            RemoteListOperation{listScope, local.identity, local.bytes},
            1, QByteArrayLiteral("uncertain-action"), 0, 1};
        auto conflict = original;
        conflict.actionId = QByteArrayLiteral("conflicting-action");
        ScopedListRouter router("quarantine-uncertain-doc", 1);
        router.setDocumentState(document.get());
        std::vector<CompletedListOperation> completed;
        require(router.offer(original, completed).state
                    == ListRouteState::heldForUnknownStream
                    && router.offer(conflict, completed).state == ListRouteState::rejected,
                "router did not retain the conflicting list envelope for the fault trace");
        failNextReferenceProviderCommitAt(ReferenceProviderFaultPoint::afterAtomicRename);
        const auto acknowledgment = router.acknowledgeQuarantine(conflict);
        require(!acknowledgment.ok()
                    && acknowledgment.error().code() == ErrorCode::commit_uncertain
                    && router.heldOperationCount() == 2,
                "uncertain quarantine persistence released the router's held envelope");
        stream.reset();
    }
    provider.reset();
    auto recoveredProvider = take(makeReferenceDocumentStorageProvider(root));
    auto reopened = take(recoveredProvider->open(scope, StorageMode::durable));
    persisted = take(reopened->readRecoveryState()).quarantinedOperations;
    require(persisted.size() == 1,
            "post-rename quarantine bytes were not recoverable after reopen");
    ScopedListRouter restored("quarantine-uncertain-doc", 1);
    require(take(restored.restoreQuarantine(persisted)) == 1
                && restored.quarantined().front().operation.actionId
                    == QByteArrayLiteral("conflicting-action"),
            "reopen did not restore the exact uncertainly acknowledged envelope");
#endif
}

void runInitialImportIntentUncertainty()
{
#if defined(Q_OS_UNIX)
    using Testing::ReferenceProviderFaultPoint;
    using Testing::failNextReferenceProviderCommitAt;
    QTemporaryDir temp;
    require(temp.isValid(), "could not create initial-import uncertainty test directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("import-intent-uncertain"), 1};
    const PendingImportIntent intent{QByteArrayLiteral("initial-import-action"),
                                     QByteArrayLiteral("source\r\nexact")};
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto document = take(ProviderBackedDocumentState::open(
            *provider, scope, StorageMode::durable));
        failNextReferenceProviderCommitAt(ReferenceProviderFaultPoint::afterAtomicRename);
        const auto authorization = document->authorizeInitialImport(intent);
        require(!authorization.ok()
                    && authorization.error().code() == ErrorCode::commit_uncertain
                    && document->state() == ProviderDocumentState::awaitingInitialImport
                    && !document->canStartOrdinaryAction(),
                "post-rename initial-import uncertainty was reported as definite failure");
        const auto poisoned = document->storage().readRecoveryState();
        require(!poisoned.ok()
                    && poisoned.error().code() == ErrorCode::commit_uncertain,
                "initial-import uncertainty left the live storage session usable");
    }
    auto recoveredProvider = take(makeReferenceDocumentStorageProvider(root));
    auto reopened = take(ProviderBackedDocumentState::open(
        *recoveredProvider, scope, StorageMode::durable));
    require(reopened->state() == ProviderDocumentState::initialImportAuthorized
                && reopened->recovery().initialImport == intent,
            "reopen did not recover the exact first-import intent after uncertainty");
#endif
}

void runCorruptionRefusal()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create corruption test directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("corrupt-doc"), 1};
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        take(session->reserve(QStringLiteral("markoff/block-text/v1/corrupt"), 1));
    }
    std::filesystem::path stateFile;
    for (const auto& entry : std::filesystem::directory_iterator(root / "documents")) {
        if (entry.path().extension() == ".mref") {
            stateFile = entry.path();
            break;
        }
    }
    require(!stateFile.empty(), "reference provider did not create a state file");
    {
        std::ofstream damaged(stateFile, std::ios::binary | std::ios::trunc);
        damaged << "not an MREF record";
    }
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    const auto opened = provider->open(scope, StorageMode::durable);
    require(!opened.ok() && opened.error().code() == ErrorCode::corrupt_record,
            "reference provider silently replaced corrupt durable authority");
}

void runIdentityAuthorityLossRefusal()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create authority-loss test directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("authority-loss-doc"), 1};
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        take(session->reserve(QStringLiteral("markoff/block-text/v1/authority-loss"), 1));
    }
    std::error_code ec;
    std::filesystem::remove_all(root / "identity", ec);
    require(!ec, "could not remove private writer identity state for fault test");
    auto replacementProvider = take(makeReferenceDocumentStorageProvider(root));
    auto reopened = replacementProvider->open(scope, StorageMode::durable);
    require(!reopened.ok() && reopened.error().code() == ErrorCode::corrupt_record,
            "reference provider replaced lost writer authority for registered state");
}

void runPrecommitWriteFailureRecovery()
{
#if defined(Q_OS_UNIX)
    QTemporaryDir temp;
    require(temp.isValid(), "could not create precommit-failure test directory");
    const std::filesystem::path root(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("precommit-failure-doc"), 1};
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        const auto heldDirectory = root / "documents-held";
        std::filesystem::rename(root / "documents", heldDirectory);
        { std::ofstream blocker(root / "documents"); blocker << "block"; }
        const auto failed = session->reserve(
            QStringLiteral("markoff/block-text/v1/precommit-failure"), 2);
        require(!failed.ok() && failed.error().code() == ErrorCode::io_error,
                "pre-commit reservation write failure was not reported as definite absence");
        std::filesystem::remove(root / "documents");
        std::filesystem::rename(heldDirectory, root / "documents");
    }
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        auto session = take(provider->open(scope, StorageMode::durable));
        const auto state = take(session->readRecoveryState());
        require(state.unresolvedReservations.empty()
                    && take(session->nextCounter(
                        QStringLiteral("markoff/block-text/v1/precommit-failure"))) == 1,
                "failed pre-commit reservation consumed durable counter authority");
    }
#endif
}

void runPostRenameSyncFaultRecovery()
{
#if defined(Q_OS_UNIX)
    using Markoff::VNext::Testing::ReferenceProviderFaultPoint;
    using Markoff::VNext::Testing::failNextReferenceProviderCommitAt;
    QTemporaryDir temp;
    require(temp.isValid(), "could not create post-rename fault test directory");
    const std::filesystem::path root(temp.path().toStdString());

    // A reservation whose rename is visible but whose durability barrier fails
    // must be recoverable as an unresolved exact interval, never reused.
    {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        const DocumentStorageScope scope{QStringLiteral("fault-reservation-doc"), 1};
        auto session = take(provider->open(scope, StorageMode::durable));
        failNextReferenceProviderCommitAt(
            ReferenceProviderFaultPoint::afterAtomicRename);
        const auto reservation = session->reserve(
            QStringLiteral("markoff/block-text/v1/fault-reservation"), 2);
        require(!reservation.ok()
                    && reservation.error().code() == ErrorCode::commit_uncertain,
                "post-rename reservation failure was not reported uncertain");
        session.reset();
        auto reopened = take(provider->open(scope, StorageMode::durable));
        const auto state = take(reopened->readRecoveryState());
        require(state.unresolvedReservations.size() == 1
                    && state.unresolvedReservations[0].first == 1
                    && state.unresolvedReservations[0].count == 2
                    && take(reopened->nextCounter(
                           QStringLiteral("markoff/block-text/v1/fault-reservation"))) == 3,
                "recovery lost or reused the reservation committed before sync failure");
    }

    const auto verifyAppendUncertainty = [&](const QString& documentKey,
                                             ReferenceProviderFaultPoint fault,
                                             const QString& stream) {
        auto provider = take(makeReferenceDocumentStorageProvider(root));
        const DocumentStorageScope scope{documentKey, 1};
        auto session = take(provider->open(scope, StorageMode::durable));
        const auto initial = take(session->readRecoveryState());
        const auto reservation = take(session->reserve(stream, 1));
        PreparedStorageTransaction transaction;
        transaction.transactionId = documentKey.toUtf8() + QByteArrayLiteral("-txn");
        transaction.expectedRevision = initial.acceptedRevision;
        transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        transaction.sharedRecord = QByteArrayLiteral("exact accepted action bytes");
        transaction.reservations = {reservation};
        transaction.operationIdentities = {
            {stream, session->actor(), reservation.first}};
        transaction.localHistoryAfterCommit = QByteArrayLiteral("history-after-action");
        require(session->prepare(transaction).ok(),
                "reference provider refused post-rename fault transaction");

        failNextReferenceProviderCommitAt(fault);
        const auto uncertain = session->append(transaction.transactionId);
        require(uncertain.disposition == PublicationDisposition::uncertain
                    && uncertain.diagnostic.has_value()
                    && uncertain.diagnostic->code() == ErrorCode::commit_uncertain,
                "post-rename append sync failure was not reported uncertain");
        session.reset();

        auto reopened = take(provider->open(scope, StorageMode::durable));
        const auto recovery = take(reopened->readRecoveryState());
        require(recovery.acceptedRecords.size() == 1
                    && recovery.acceptedRecords[0].transactionId
                        == transaction.transactionId
                    && recovery.acceptedRecords[0].bytes == transaction.sharedRecord,
                "reopen did not recover the exact action after sync uncertainty");
        const auto inspected = reopened->inspect(transaction);
        require(inspected.disposition == PublicationDisposition::committed
                    && inspected.committedRevision.has_value(),
                "inspection did not resolve the original transaction as committed");
        const auto retry = reopened->append(transaction.transactionId);
        require(retry.disposition == PublicationDisposition::committed
                    && retry.committedRevision == inspected.committedRevision
                    && take(reopened->readRecoveryState()).acceptedRecords.size() == 1,
                "retry duplicated or changed the uncertain committed transaction");
    };

    verifyAppendUncertainty(QStringLiteral("fault-file-sync-doc"),
        ReferenceProviderFaultPoint::fileSync,
        QStringLiteral("markoff/block-text/v1/fault-file-sync"));
    verifyAppendUncertainty(QStringLiteral("fault-directory-sync-doc"),
        ReferenceProviderFaultPoint::directorySync,
        QStringLiteral("markoff/block-text/v1/fault-directory-sync"));
#endif
}

void runIndependentProcessLease()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create process-lease test directory");
    auto provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temp.path().toStdString())));
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("lease-doc"), 1}, StorageMode::durable));
    QProcess competitor;
    competitor.start(QCoreApplication::applicationFilePath(),
                     {QStringLiteral("--try-open"), temp.path()});
    require(competitor.waitForStarted(), "could not start competing provider process");
    require(competitor.waitForFinished(15000), "competing provider process timed out");
    require(competitor.exitStatus() == QProcess::NormalExit && competitor.exitCode() == 0,
            "independent process obtained durable writer authority for a leased root");
}

void runPreparationPrecommitFailure()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create prepare-failure test directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope scope{QStringLiteral("prepare-failure-doc"), 1};
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(scope, StorageMode::durable));
    const auto reservation = take(session->reserve(
        QStringLiteral("markoff/block-text/v1/prepare-failure"), 1));
    const auto prior = take(session->readRecoveryState());
    PreparedStorageTransaction transaction;
    transaction.transactionId = QByteArrayLiteral("prepare-failure-action");
    transaction.expectedRevision = prior.acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = QByteArrayLiteral("exact-action-bytes");
    transaction.reservations = {reservation};

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::prepareBeforeAtomicRename);
    const auto failed = session->prepare(transaction);
    require(!failed.ok() && failed.error().code() == ErrorCode::io_error,
            "provider did not report a pre-rename prepare failure as definite");
    auto afterFailure = take(session->readRecoveryState());
    require(afterFailure.unresolvedTransactions.empty()
                && afterFailure.unresolvedReservations == std::vector<ScopedReservation>{reservation},
            "failed preparation lost its reservation or left a phantom transaction");

    session.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(scope, StorageMode::durable));
    auto afterReopen = take(session->readRecoveryState());
    require(afterReopen.unresolvedTransactions.empty()
                && afterReopen.unresolvedReservations == std::vector<ScopedReservation>{reservation}
                && afterReopen.acceptedRevision == prior.acceptedRevision,
            "reopen did not preserve the prior prefix and exact reservation after failed prepare");
    require(session->prepare(transaction).ok(),
            "provider refused an exact preparation retry after proven pre-rename failure");
}

void runMultiStreamPrepareFailureRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create multi-stream prepare-failure directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("prepare-multi-doc"), 8};
    const std::string document = "prepare-multi-doc";
    const std::string orderStream(kOrderStreamName);
    const MapScope mapScope{document, "frontmatter", storageScope.epoch};
    const std::string mapRoute = take(mapStreamName(mapScope.map));
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    auto beforeImport = take(session->readRecoveryState());
    const QByteArray importId = QByteArrayLiteral("prepare-multi-import");
    const QByteArray sourceBytes = QByteArrayLiteral("seed\n");
    require(session->beginInitialImport(PendingImportIntent{importId, sourceBytes}).ok(),
            "multi-stream fixture could not authorize its initial import");
    SourceSnapshotRecord source{document, storageScope.epoch, sourceBytes};
    ActionBundleCodec::Bundle importBundle;
    importBundle.document = document;
    importBundle.epoch = storageScope.epoch;
    importBundle.deliverySequence = 1;
    importBundle.actionId = importId;
    importBundle.kind = ActionKind::initialImport;
    importBundle.sourceSnapshot = take(SourceSnapshotCodec::encode(source));
    CrossStreamHistoryIndex historyIndex;
    historyIndex.document = document;
    historyIndex.epoch = storageScope.epoch;
    historyIndex.owner = session->actor();
    historyIndex.acceptedPrior = beforeImport.acceptedRevision.token;
    historyIndex.events.push_back({importId, ActionKind::initialImport, {}, {}});
    OwnerHistorySnapshot importHistory;
    importHistory.document = document;
    importHistory.epoch = storageScope.epoch;
    importHistory.owner = session->actor();
    importHistory.transactionId = importId;
    importHistory.acceptedPrior = beforeImport.acceptedRevision.token;
    importHistory.actionIndex = historyIndex;
    PreparedStorageTransaction importTransaction;
    importTransaction.transactionId = importId;
    importTransaction.expectedRevision = beforeImport.acceptedRevision;
    importTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    importTransaction.sharedRecord = take(ActionBundleCodec::encode(importBundle));
    importTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(importHistory));
    ActionPublicationCoordinator importCoordinator(*session);
    const auto importPublication = importCoordinator.publish(importTransaction);
    require(importPublication.state == ActionPublicationState::committed
                && importPublication.committedRevision,
            "multi-stream fixture failed to publish its empty initial import");

    const PositionScope listScope{document, orderStream, Primitive::list,
                                  storageScope.epoch};
    auto list = take(ScopedIdListStream::create(*session, listScope));
    const auto listOperation = take(list->insertAfter(std::nullopt, 17));
    const BlockIdentity block = *listOperation.insertedBlock;
    const PositionScope textScope{document, take(blockTextStreamName(block)),
                                  Primitive::text, storageScope.epoch};
    auto text = take(ScopedTextStream::create(*session, textScope));
    const auto textOperation = take(text->replaceUtf8(0, 0, QByteArrayLiteral("tentative")));
    auto map = take(ReplicatedMapStream::create(*session, mapScope));
    require(map->completeReplay().ok(), "multi-stream map replay did not complete");
    const auto mapOperation = take(map->set(QByteArrayLiteral("title"),
                                            QByteArrayLiteral("tentative")));
    const auto localHistory = take(session->readRecoveryState());
    require(localHistory.unresolvedReservations.size() == 3,
            "tentative list, text and map writes did not reserve all three streams");

    auto listAction = ActionOperation{orderStream, OperationPrimitive::idList,
        std::nullopt, listOperation.identity.actor, listOperation.identity.counter,
        OperationCodec::ctlo2, listOperation.bytes};
    auto textAction = ActionOperation{textScope.stream, OperationPrimitive::text,
        block, textOperation.identity.actor, textOperation.identity.counter,
        OperationCodec::ctbo3, textOperation.bytes};
    auto mapAction = take(toActionOperation(mapOperation));
    ActionBundleCodec::Bundle editBundle;
    editBundle.document = document;
    editBundle.epoch = storageScope.epoch;
    editBundle.deliverySequence = 2;
    editBundle.actionId = QByteArrayLiteral("tentative-multistream-edit");
    editBundle.kind = ActionKind::edit;
    editBundle.operations = {listAction, textAction, mapAction};
    const auto listSnapshot = list->localHistorySnapshot();
    const auto textSnapshot = text->localHistorySnapshot();
    historyIndex = importHistory.actionIndex;
    historyIndex.acceptedPrior = localHistory.acceptedRevision.token;
    historyIndex.events.push_back({editBundle.actionId, ActionKind::edit, {}, {
        {orderStream, OperationPrimitive::idList,
         listSnapshot.entries.front().group_id, listSnapshot.entries.front().operation_ids},
        {textScope.stream, OperationPrimitive::text,
         textSnapshot.entries.front().group_id, textSnapshot.entries.front().operation_ids},
        take(toHistoryAssociation(mapOperation))}});
    OwnerHistorySnapshot editHistory;
    editHistory.document = document;
    editHistory.epoch = storageScope.epoch;
    editHistory.owner = session->actor();
    editHistory.transactionId = editBundle.actionId;
    editHistory.acceptedPrior = localHistory.acceptedRevision.token;
    editHistory.streams = {{listScope, session->actor(), listSnapshot},
                           {textScope, session->actor(), textSnapshot}};
    editHistory.actionIndex = historyIndex;
    PreparedStorageTransaction editTransaction;
    editTransaction.transactionId = editBundle.actionId;
    editTransaction.expectedRevision = localHistory.acceptedRevision;
    editTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    editTransaction.sharedRecord = take(ActionBundleCodec::encode(editBundle));
    editTransaction.reservations = localHistory.unresolvedReservations;
    for (const auto& operation : editBundle.operations)
        editTransaction.operationIdentities.push_back({
            QString::fromUtf8(operation.stream.data(),
                static_cast<qsizetype>(operation.stream.size())),
            operation.producer, operation.counter});
    editTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(editHistory));

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
    ActionPublicationCoordinator editCoordinator(*session);
    const auto absent = editCoordinator.publish(editTransaction);
    require(absent.state == ActionPublicationState::absent && editCoordinator.blocked(),
            "proven-absent multi-stream append did not retain the action write barrier");
    const auto blockedRetry = editCoordinator.publish(editTransaction);
    require(blockedRetry.state == ActionPublicationState::uncertain
                && editCoordinator.blocked(),
            "coordinator allowed the absent multi-stream action to bypass recovery");

    const auto listReservation = std::find_if(editTransaction.reservations.begin(),
        editTransaction.reservations.end(), [&](const ScopedReservation& reservation) {
            return reservation.stream == QString::fromStdString(orderStream);
        });
    require(listReservation != editTransaction.reservations.end(),
            "absent multi-stream action lost its structural-list reservation");
    const auto firstReservation = *listReservation;
    const PositionScope firstScope{document, firstReservation.stream.toStdString(),
        Primitive::list, storageScope.epoch};
    const ScopedCounterGap firstGap{firstScope,
        Crdt::CounterGap{firstReservation.actor, firstReservation.first,
            firstReservation.count, Crdt::Global{}},
        editTransaction.expectedRevision.token};
    PreparedStorageTransaction prematureGap;
    prematureGap.transactionId = QByteArrayLiteral("premature-multistream-gap");
    prematureGap.expectedRevision = editTransaction.expectedRevision;
    prematureGap.sharedRecordFormat = QByteArrayLiteral("MTG/1");
    prematureGap.sharedRecord = take(CounterGapCodec::encode(firstGap));
    prematureGap.reservations = {firstReservation};
    const auto blockedGap = editCoordinator.publishGap(prematureGap);
    require(blockedGap.state == ActionPublicationState::uncertain
                && blockedGap.diagnostic
                && blockedGap.diagnostic->code() == ErrorCode::history_unavailable,
            "coordinator published counter gaps before rebuilding the accepted projection");
    bool acceptedProjectionRebuilt = false;
    const auto rebuiltAbsence = editCoordinator.recoverAndRebuild(editTransaction,
        [&](const StorageRecoveryState& accepted) -> Result<void> {
            auto projection = AcceptedPrefixProjection::replay(*session, accepted);
            if (!projection.ok()) return projection.error();
            acceptedProjectionRebuilt = true;
            return {};
        });
    require(rebuiltAbsence.state == ActionPublicationState::absent
                && acceptedProjectionRebuilt
                && editCoordinator.blocked(),
            "coordinator did not require a successful accepted-prefix rebuild before gaps");

    list.reset();
    text.reset();
    map.reset();
    session.reset();
    auto recovered = take(ProviderBackedDocumentState::open(
        *provider, storageScope, StorageMode::durable));
    require(recovered->state() == ProviderDocumentState::recoveryRequired
                && !recovered->canStartOrdinaryAction()
                && recovered->recovery().unresolvedTransactions.empty()
                && recovered->recovery().unresolvedReservations.size() == 3
                && !recovered->projection().idList()
                && !recovered->projection().textStream(textScope.stream)
                && !recovered->projection().mapStream(mapScope.map),
            "reopen exposed tentative multi-stream state or lost its reservations");
    auto& recoveredSession = recovered->storage();
    require(recoveredSession.resolveAbsentPrepared(editTransaction).ok(),
            "failed multi-stream preparation could not be resolved as absent");

    QByteArray lastGapId;
    for (size_t index = 0; index < localHistory.unresolvedReservations.size(); ++index) {
        const auto reservation = localHistory.unresolvedReservations[index];
        const auto current = take(recovered->storage().readRecoveryState());
        PreparedStorageTransaction gapTransaction;
        gapTransaction.transactionId = QByteArrayLiteral("multi-gap-")
            + QByteArray::number(index + 1);
        lastGapId = gapTransaction.transactionId;
        gapTransaction.expectedRevision = current.acceptedRevision;
        gapTransaction.reservations = {reservation};
        if (reservation.stream == QString::fromStdString(mapRoute)) {
            const MapCounterGap gap{document, mapScope.map, storageScope.epoch,
                reservation.actor, reservation.first, reservation.count,
                current.acceptedRevision.token};
            gapTransaction.sharedRecordFormat = QByteArrayLiteral("MMG/1");
            gapTransaction.sharedRecord = take(MapCounterGapCodec::encode(gap));
        } else {
            const bool isList = reservation.stream == QString::fromStdString(orderStream);
            const PositionScope scope{document, reservation.stream.toStdString(),
                isList ? Primitive::list : Primitive::text, storageScope.epoch};
            const ScopedCounterGap gap{scope,
                Crdt::CounterGap{reservation.actor, reservation.first,
                    reservation.count, Crdt::Global{}}, current.acceptedRevision.token};
            gapTransaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
            gapTransaction.sharedRecord = take(CounterGapCodec::encode(gap));
        }
        if (index == 0) {
            Testing::failNextReferenceProviderCommitAt(
                Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
            const auto uncertainGap = recovered->publishGap(gapTransaction);
            require(uncertainGap.state == ActionPublicationState::uncertain
                        && recovered->state() == ProviderDocumentState::recoveryRequired,
                    "mid-recovery gap uncertainty did not retain the lifecycle barrier");
            recovered.reset();
            recovered = take(ProviderBackedDocumentState::open(
                *provider, storageScope, StorageMode::durable));
            require(recovered->state() == ProviderDocumentState::recoveryRequired
                        && !recovered->canStartOrdinaryAction()
                        && recovered->recovery().acceptedRecords.size() == 2
                        && recovered->recovery().unresolvedReservations.size() == 2,
                    "reopen did not recover the exact first gap while retaining remaining reservations");
        } else {
            const auto committedGap = recovered->publishGap(gapTransaction);
            require(committedGap.state == ActionPublicationState::committed
                        && committedGap.committedRevision,
                    "recovery failed to terminally cover one multi-stream reservation");
        }
        if (index + 1 < localHistory.unresolvedReservations.size())
            require(recovered->state() == ProviderDocumentState::recoveryRequired
                        && !recovered->canStartOrdinaryAction(),
                    "document became writable before all action reservations were covered");
    }
    require(recovered->state() == ProviderDocumentState::ready
                && recovered->canStartOrdinaryAction(),
            "document did not become ready after all three gaps committed");
    const auto finalState = take(recovered->storage().readRecoveryState());
    require(finalState.acceptedRecords.size() == 4
                && finalState.acceptedRecords.front().transactionId == importId
                && finalState.acceptedRecords.back().transactionId == lastGapId
                && finalState.unresolvedTransactions.empty()
                && finalState.unresolvedReservations.empty(),
            "multi-stream failure recovery lost accepted prefix or terminal gaps");
    const auto* recoveredList = recovered->projection().idList();
    const auto* recoveredText = recovered->projection().textStream(textScope.stream);
    const auto* recoveredMap = recovered->projection().mapStream(mapScope.map);
    require(recoveredList && take(recoveredList->visibleEntries()).empty()
                && recoveredText && recoveredText->text().isEmpty()
                && recoveredMap && recoveredMap->values().empty(),
            "fresh replay exposed one of the failed action's tentative edits");
}

void runSecondStreamAllocationFailureRecovery()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create second-stream allocation-failure directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("allocation-order-doc"), 9};
    const std::string document = "allocation-order-doc";
    const PositionScope listScope{document, std::string(kOrderStreamName),
                                  Primitive::list, storageScope.epoch};
    const QByteArray importId = QByteArrayLiteral("allocation-order-import");
    const QByteArray source = QByteArrayLiteral("initial\n");
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    require(session->beginInitialImport(PendingImportIntent{importId, source}).ok(),
            "allocation-failure fixture could not authorize source import");
    auto list = take(ScopedIdListStream::create(*session, listScope));
    const auto tentativeList = take(list->insertAfter(std::nullopt, 3));
    const auto tentativeBlock = *tentativeList.insertedBlock;
    const PositionScope textScope{document, take(blockTextStreamName(tentativeBlock)),
                                  Primitive::text, storageScope.epoch};
    auto text = take(ScopedTextStream::create(*session, textScope));
    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::reservationBeforeAtomicRename);
    const auto failedText = text->replaceUtf8(0, 0, QByteArrayLiteral("unpublished"));
    const ScopedReservation listReservation{QString::fromStdString(listScope.stream),
        session->actor(), tentativeList.identity.counter, 1};
    const auto failedState = take(session->readRecoveryState());
    require(!failedText.ok() && text->text().isEmpty()
                && failedState.unresolvedReservations
                    == std::vector<ScopedReservation>{listReservation}
                && take(session->nextCounter(QString::fromStdString(textScope.stream))) == 1,
            "second-stream reservation failure changed text or lost the earlier list reservation");

    text.reset();
    list.reset();
    session.reset();
    auto recovered = take(ProviderBackedDocumentState::open(
        *provider, storageScope, StorageMode::durable));
    require(recovered->state() == ProviderDocumentState::recoveryRequired
                && !recovered->canStartOrdinaryAction()
                && recovered->recovery().acceptedRecords.empty()
                && recovered->recovery().unresolvedReservations
                    == std::vector<ScopedReservation>{listReservation}
                && !recovered->projection().idList()
                && !recovered->projection().textStream(textScope.stream),
            "reopen exposed the first stream's tentative edit after later allocation failed");

    const auto current = take(recovered->storage().readRecoveryState());
    const ScopedCounterGap scopedGap{listScope,
        Crdt::CounterGap{listReservation.actor, listReservation.first,
            listReservation.count, Crdt::Global{}}, current.acceptedRevision.token};
    PreparedStorageTransaction gapTransaction;
    gapTransaction.transactionId = QByteArrayLiteral("allocation-order-gap");
    gapTransaction.expectedRevision = current.acceptedRevision;
    gapTransaction.sharedRecordFormat = QByteArrayLiteral("MTG/1");
    gapTransaction.sharedRecord = take(CounterGapCodec::encode(scopedGap));
    gapTransaction.reservations = {listReservation};
    const auto gapResult = recovered->publishGap(gapTransaction);
    require(gapResult.state == ActionPublicationState::committed
                && gapResult.committedRevision
                && recovered->state() == ProviderDocumentState::initialImportAuthorized
                && !recovered->canStartOrdinaryAction(),
            "failed multi-stream staging did not terminally cover list allocation before import");
    const auto finalState = take(recovered->storage().readRecoveryState());
    const auto* listProjection = recovered->projection().idList();
    require(finalState.acceptedRecords.size() == 1
                && finalState.acceptedRecords.front().format == QByteArrayLiteral("MTG/1")
                && finalState.unresolvedReservations.empty()
                && finalState.initialImport
                && finalState.initialImport->transactionId == importId
                && take(recovered->storage().nextCounter(
                       QString::fromStdString(listScope.stream)))
                    == listReservation.first + listReservation.count
                && take(recovered->storage().nextCounter(
                       QString::fromStdString(textScope.stream))) == 1
                && listProjection && take(listProjection->visibleEntries()).empty(),
            "allocation recovery reused a list ID or fabricated text state");
}

void runInitialImportRetryAfterAbsentGap()
{
    QTemporaryDir temp;
    require(temp.isValid(), "could not create initial-import retry directory");
    const auto root = std::filesystem::path(temp.path().toStdString());
    const DocumentStorageScope storageScope{QStringLiteral("import-retry-doc"), 1};
    const MapScope mapScope{"import-retry-doc", "frontmatter", 1};
    const std::string mapRoute = take(mapStreamName(mapScope.map));
    auto provider = take(makeReferenceDocumentStorageProvider(root));
    auto session = take(provider->open(storageScope, StorageMode::durable));
    const auto prior = take(session->readRecoveryState()).acceptedRevision;
    const QByteArray sourceBytes = QByteArrayLiteral("---\ntitle: Initial\n---\nBody\n");
    const QByteArray firstId = QByteArrayLiteral("initial-import-attempt-1");
    require(session->beginInitialImport(PendingImportIntent{firstId, sourceBytes}).ok(),
            "provider refused the first initial-import attempt");
    const auto reservation = take(session->reserve(QString::fromStdString(mapRoute), 1));
    const MarkoffMapOperation payload{mapScope.map, QByteArrayLiteral("title"),
        QByteArrayLiteral("Initial"), session->actor(), reservation.first, false};
    const QByteArray mapBytes = take(MarkoffMapCodec::encode(payload));
    const auto mapHistory = take(toHistoryAssociation(
        LocalMapOperation{mapScope, std::nullopt, payload, mapBytes}));
    SourceSnapshotRecord source;
    source.document = mapScope.document;
    source.epoch = mapScope.epoch;
    source.originalMarkdown = sourceBytes;
    ActionBundleCodec::Bundle bundle;
    bundle.document = mapScope.document;
    bundle.epoch = mapScope.epoch;
    bundle.deliverySequence = 1;
    bundle.actionId = firstId;
    bundle.kind = ActionKind::initialImport;
    bundle.sourceSnapshot = take(SourceSnapshotCodec::encode(source));
    bundle.operations.push_back({mapRoute, OperationPrimitive::markoffMap,
        std::nullopt, session->actor(), reservation.first,
        OperationCodec::markoffOwned, mapBytes});
    CrossStreamHistoryIndex index;
    index.document = mapScope.document;
    index.epoch = mapScope.epoch;
    index.owner = session->actor();
    index.acceptedPrior = prior.token;
    index.events.push_back({firstId, ActionKind::initialImport, {}, {mapHistory}});
    OwnerHistorySnapshot history;
    history.document = mapScope.document;
    history.epoch = mapScope.epoch;
    history.owner = session->actor();
    history.transactionId = firstId;
    history.acceptedPrior = prior.token;
    history.actionIndex = index;
    PreparedStorageTransaction transaction;
    transaction.transactionId = firstId;
    transaction.expectedRevision = prior;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = take(ActionBundleCodec::encode(bundle));
    transaction.reservations = {reservation};
    transaction.operationIdentities = {{QString::fromStdString(mapRoute),
        session->actor(), reservation.first}};
    transaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(history));

    Testing::failNextReferenceProviderCommitAt(
        Testing::ReferenceProviderFaultPoint::prepareBeforeAtomicRename);
    ActionPublicationCoordinator coordinator(*session);
    const auto prepareFailure = coordinator.publish(transaction);
    require(prepareFailure.state == ActionPublicationState::rejected
                && coordinator.blocked(),
            "coordinator did not retain its write barrier after definite prepare failure");
    const auto blockedRetry = coordinator.publish(transaction);
    require(blockedRetry.state == ActionPublicationState::uncertain
                && coordinator.blocked(),
            "coordinator allowed retry before recovering the failed preparation");

    session.reset();
    auto reopenedDocument = take(ProviderBackedDocumentState::open(
        *provider, storageScope, StorageMode::durable));
    const auto& absenceRecovery = reopenedDocument->recovery();
    require(reopenedDocument->state() == ProviderDocumentState::recoveryRequired
                && !reopenedDocument->canStartOrdinaryAction()
                && absenceRecovery.acceptedRecords.empty()
                && absenceRecovery.unresolvedTransactions.empty()
                && absenceRecovery.unresolvedReservations
                    == std::vector<ScopedReservation>{reservation},
            "document open did not rebuild the accepted empty prefix and retain its write barrier");
    const MapCounterGap gap{mapScope.document, mapScope.map, mapScope.epoch,
        reopenedDocument->storage().actor(), reservation.first, reservation.count,
        prior.token};
    auto& recoveredSession = reopenedDocument->storage();
    require(recoveredSession.resolveAbsentPrepared(transaction).ok(),
            "lifecycle recovery did not retire the exact absent preparation");
    PreparedStorageTransaction gapTransaction;
    gapTransaction.transactionId = QByteArrayLiteral("initial-import-gap-1");
    gapTransaction.expectedRevision = prior;
    gapTransaction.sharedRecordFormat = QByteArrayLiteral("MMG/1");
    gapTransaction.sharedRecord = take(MapCounterGapCodec::encode(gap));
    gapTransaction.reservations = {reservation};
    require(reopenedDocument->publishGap(gapTransaction).state
                == ActionPublicationState::committed
                && reopenedDocument->state() == ProviderDocumentState::initialImportAuthorized,
            "failed first-import reservation was not terminally covered");

    require(recoveredSession.resolveAbsentPrepared(transaction).ok()
                && take(recoveredSession.readRecoveryState()).unresolvedReservations.empty(),
            "repeated absence resolution reintroduced a gap-covered reservation");

    const auto retiredRetry = recoveredSession.prepare(transaction);
    require(!retiredRetry.ok()
                && retiredRetry.error().code() == CollabText::ErrorCode::pin_conflict,
            "provider allowed the retired pre-rename transaction ID to be prepared again");

    const QByteArray retryId = QByteArrayLiteral("initial-import-attempt-2");
    require(recoveredSession.beginInitialImport(PendingImportIntent{retryId, sourceBytes}).ok(),
            "provider refused a new initial-import ID after absence and gap coverage");
    auto recovery = take(recoveredSession.readRecoveryState());
    require(recovery.initialImport
                && recovery.initialImport->transactionId == retryId
                && recovery.initialImport->sourceMarkdown == sourceBytes
                && take(recoveredSession.nextCounter(QString::fromStdString(mapRoute)))
                    == reservation.first + reservation.count,
            "new import intent changed its source or reused the abandoned counter");
    reopenedDocument.reset();
    provider.reset();
    provider = take(makeReferenceDocumentStorageProvider(root));
    session = take(provider->open(storageScope, StorageMode::durable));
    recovery = take(session->readRecoveryState());
    require(recovery.initialImport
                && recovery.initialImport->transactionId == retryId
                && recovery.initialImport->sourceMarkdown == sourceBytes,
            "reopen lost the retried initial-import intent");

    const auto retryReservation = take(session->reserve(QString::fromStdString(mapRoute), 1));
    const MarkoffMapOperation retryPayload{mapScope.map, QByteArrayLiteral("title"),
        QByteArrayLiteral("Initial"), session->actor(), retryReservation.first, false};
    const QByteArray retryMapBytes = take(MarkoffMapCodec::encode(retryPayload));
    const auto retryMapHistory = take(toHistoryAssociation(LocalMapOperation{
        mapScope, std::nullopt, retryPayload, retryMapBytes}));
    ActionBundleCodec::Bundle retryBundle;
    retryBundle.document = mapScope.document;
    retryBundle.epoch = mapScope.epoch;
    retryBundle.deliverySequence = 1;
    retryBundle.actionId = retryId;
    retryBundle.kind = ActionKind::initialImport;
    retryBundle.sourceSnapshot = take(SourceSnapshotCodec::encode(source));
    retryBundle.operations.push_back({mapRoute, OperationPrimitive::markoffMap,
        std::nullopt, session->actor(), retryReservation.first,
        OperationCodec::markoffOwned, retryMapBytes});
    CrossStreamHistoryIndex retryIndex;
    retryIndex.document = mapScope.document;
    retryIndex.epoch = mapScope.epoch;
    retryIndex.owner = session->actor();
    retryIndex.acceptedPrior = recovery.acceptedRevision.token;
    retryIndex.events.push_back({retryId, ActionKind::initialImport, {},
                                 {retryMapHistory}});
    OwnerHistorySnapshot retryHistory;
    retryHistory.document = mapScope.document;
    retryHistory.epoch = mapScope.epoch;
    retryHistory.owner = session->actor();
    retryHistory.transactionId = retryId;
    retryHistory.acceptedPrior = recovery.acceptedRevision.token;
    retryHistory.actionIndex = retryIndex;
    PreparedStorageTransaction retryTransaction;
    retryTransaction.transactionId = retryId;
    retryTransaction.expectedRevision = recovery.acceptedRevision;
    retryTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    retryTransaction.sharedRecord = take(ActionBundleCodec::encode(retryBundle));
    retryTransaction.reservations = {retryReservation};
    retryTransaction.operationIdentities = {{QString::fromStdString(mapRoute),
        session->actor(), retryReservation.first}};
    retryTransaction.localHistoryAfterCommit = take(OwnerHistoryCodec::encode(retryHistory));
    ActionPublicationCoordinator retryCoordinator(*session);
    const auto retriedImport = retryCoordinator.publish(retryTransaction);
    const auto afterRetry = take(session->readRecoveryState());
    require(retriedImport.state == ActionPublicationState::committed
                && retriedImport.committedRevision && !retryCoordinator.blocked()
                && afterRetry.acceptedRecords.size() == 2
                && afterRetry.acceptedRecords.back().transactionId == retryId
                && afterRetry.unresolvedTransactions.empty()
                && afterRetry.unresolvedReservations.empty()
                && !afterRetry.initialImport
                && afterRetry.localHistory
                && afterRetry.localHistory->bytes == retryTransaction.localHistoryAfterCommit,
            "fresh initial-import attempt did not commit the exact source and owner history");
}

int reserveAndExit(const QString& root)
{
    auto provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(root.toStdString())));
    auto session = take(provider->open(
        DocumentStorageScope{QStringLiteral("process-restart-doc"), 1},
        StorageMode::durable));
    take(session->reserve(QStringLiteral("markoff/block-text/v1/process-restart"), 5));
    std::cout << session->actor().to_string() << std::endl;
    // Deliberately skip C++ destructors: the next process must recover only
    // state that was committed by the provider before reserve() returned.
    std::_Exit(0);
}

int tryOpenWhileLeased(const QString& root)
{
    auto provider = makeReferenceDocumentStorageProvider(
        std::filesystem::path(root.toStdString()));
    if (!provider.ok())
        return provider.error().code() == ErrorCode::writer_conflict ? 0 : 2;
    auto session = provider.value()->open(
        DocumentStorageScope{QStringLiteral("lease-doc"), 1}, StorageMode::durable);
    if (!session.ok())
        return session.error().code() == ErrorCode::writer_conflict ? 0 : 3;
    return 4;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3
        && app.arguments()[1] == QStringLiteral("--reserve-and-exit")) {
        try {
            return reserveAndExit(app.arguments()[2]);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    if (app.arguments().size() == 3
        && app.arguments()[1] == QStringLiteral("--try-open"))
        return tryOpenWhileLeased(app.arguments()[2]);
    try {
        runCompositeBlockIdentityContracts();
        runRemoteAcceptanceStorageContract();
        runProviderBackedDocumentLifecycle();
        runAcceptedPrefixRevisionValidation();
        run();
        runScopedListRouter();
        runScopedMapRouter();
        runConcurrentOfflineListIdentity();
        runScopedCounterGapCodec();
        runMapCounterGapProjection();
        runTextCounterGapProjection();
        runReplicatedMapCompensation();
        runSourceSnapshotCodec();
        runCrossStreamHistoryCodec();
        runActionPublicationCoordinatorRecovery();
        runEmptyInitialImportAbsenceRecovery();
        runAbsentMapGapCoordinatorRecovery();
        runNoncanonicalTextGapRouteRejected();
        runListGapCoordinatorRecovery();
        runReferenceProvider();
        runDurableProviderAdapterReplay();
        runStagedProjectionRejectRollback();
        runSelectiveStructuralUndo();
        runCrossStreamSelectiveActionUndo();
        runDurableCrossStreamUndoAction();
        runAbruptProcessRestart();
        runQuarantinePersistence();
        runQuarantineAcknowledgementUncertainty();
        runInitialImportIntentUncertainty();
        runCorruptionRefusal();
        runIdentityAuthorityLossRefusal();
        runPrecommitWriteFailureRecovery();
        runPostRenameSyncFaultRecovery();
        runPreparationPrecommitFailure();
        runMultiStreamPrepareFailureRecovery();
        runSecondStreamAllocationFailureRecovery();
        runInitialImportRetryAfterAbsentGap();
        runIndependentProcessLease();
        std::cout << "Document storage provider checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Document storage provider checks failed: "
                  << error.what() << '\n';
        return 1;
    }
}
