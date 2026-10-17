// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/BlockEdit.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/ActionStager.h>
#include <markoff/vnext/ActionTransactionBuilder.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/OwnerHistoryCodec.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/QuarantineRecordCodec.h>
#include <markoff/vnext/StreamNames.h>
#include <markoff/vnext/UndoDepth.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <iostream>
#include <utility>
#include <vector>
#include <QFile>
#include <QTemporaryDir>

#include "../../libs/markoff-collabtext-vnext/src/ReferenceProviderTestHooks.h"

namespace {

using namespace Markoff::VNext;

template<class T>
T take(CollabText::Result<T> result)
{
    if (!result.ok())
        throw std::runtime_error(std::string(result.error().message()));
    return std::move(result).value();
}

void take(CollabText::Result<void> result)
{
    if (!result.ok())
        throw std::runtime_error(std::string(result.error().message()));
}

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void seedOneBlock(ProviderBackedDocumentState& lifecycle,
                  const QByteArray& source)
{
    const auto& storageScope = lifecycle.storage().scope();
    const std::string document = storageScope.documentKey.toUtf8().toStdString();
    const QByteArray actionId = QByteArrayLiteral("provider-backed-initial-import");
    take(lifecycle.authorizeInitialImport({actionId, source}));

    auto* list = take(lifecycle.stagingIdList());
    auto listOperation = take(list->insertAfter(std::nullopt, 4242));
    require(listOperation.insertedBlock.has_value(),
            "initial list operation did not return its full block identity");
    const BlockIdentity block = *listOperation.insertedBlock;
    auto* text = take(lifecycle.stagingTextStream(block));
    auto textOperation = take(text->seedInitialImport(source));
    require(textOperation.has_value(),
            "nonempty initial text did not produce its stream operation");

    ActionStager stager(document, storageScope.epoch, 1, actionId,
                        ActionKind::initialImport);
    SourceSnapshotRecord sourceRecord{document, storageScope.epoch, source};
    stager.setSourceSnapshot(take(SourceSnapshotCodec::encode(sourceRecord)));
    take(stager.add(listOperation));
    take(stager.add(*textOperation));

    const auto recovery = take(lifecycle.storage().readRecoveryState());
    const auto listHistory = list->localHistorySnapshot();
    const auto textHistory = text->localHistorySnapshot();
    OwnerHistorySnapshot ownerHistory;
    ownerHistory.document = document;
    ownerHistory.epoch = storageScope.epoch;
    ownerHistory.owner = lifecycle.storage().actor();
    ownerHistory.transactionId = actionId;
    ownerHistory.acceptedPrior = recovery.acceptedRevision.token;
    ownerHistory.streams = {
        {list->scope(), lifecycle.storage().actor(), listHistory},
        {text->scope(), lifecycle.storage().actor(), textHistory},
    };
    ownerHistory.actionIndex.document = document;
    ownerHistory.actionIndex.epoch = storageScope.epoch;
    ownerHistory.actionIndex.owner = lifecycle.storage().actor();
    ownerHistory.actionIndex.acceptedPrior = recovery.acceptedRevision.token;
    ActionHistoryEvent event;
    event.actionId = actionId;
    event.kind = ActionKind::initialImport;
    for (const auto& operation : stager.bundle().operations) {
        const CollabText::Crdt::Identity id{operation.producer, operation.counter};
        event.groups.push_back({operation.stream, operation.primitive, id, {id}});
    }
    ownerHistory.actionIndex.events.push_back(std::move(event));

    auto transaction = take(ActionTransactionBuilder::build(
        lifecycle.storage(), stager.bundle(), ownerHistory,
        stager.operationReservations()));
    const auto published = lifecycle.publishAction(transaction);
    if (published.state != ActionPublicationState::committed
        || lifecycle.state() != ProviderDocumentState::ready) {
        throw std::runtime_error("initial publication state="
            + std::to_string(static_cast<int>(published.state))
            + " lifecycle state="
            + std::to_string(static_cast<int>(lifecycle.state()))
            + " diagnostic=" + (published.diagnostic
                ? std::string(published.diagnostic->message()) : "none"));
    }
}

void runLifecycleRemoteAcceptance()
{
    auto receiverProvider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("remote-lifecycle-doc"), 1};
    auto receiver = take(ProviderBackedDocumentState::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    seedOneBlock(*receiver, QByteArrayLiteral("base"));
    const auto entries = take(receiver->projection().idList()->visibleEntries());
    require(entries.size() == 1, "remote lifecycle fixture did not seed one block");

    const auto localHistoryBefore = receiver->recovery().localHistory;
    auto peerProvider = take(makeEphemeralDocumentStorageProvider());
    auto peerSession = take(peerProvider->open(scope, StorageMode::ephemeral));
    const MapScope mapScope{"remote-lifecycle-doc", "block-attrs", 1};
    auto peerMap = take(ReplicatedMapStream::create(*peerSession, mapScope));
    take(peerMap->completeReplay());
    const QByteArray encodedValue = take(MarkoffMapValueCodec::encodeAttribute(
        Markoff::AttrValue{QStringLiteral("remote-value")}));
    auto remoteOperation = take(peerMap->set(QByteArrayLiteral("remote-key"),
        encodedValue, entries.front().identity));

    ActionStager remoteAction("remote-lifecycle-doc", 1, 2,
                              QByteArrayLiteral("peer-map-action"), ActionKind::edit);
    take(remoteAction.add(remoteOperation));
    PreparedStorageTransaction transaction;
    transaction.transactionId = QByteArrayLiteral("peer-map-action");
    transaction.expectedRevision = receiver->recovery().acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = take(ActionBundleCodec::encode(remoteAction.bundle()));
    transaction.remoteAcceptance = true;
    for (const auto& operation : remoteAction.bundle().operations)
        transaction.operationIdentities.push_back({
            QString::fromStdString(operation.stream), operation.producer,
            operation.counter});

    const auto accepted = receiver->acceptRemoteAction(transaction);
    require(accepted.state == ActionPublicationState::committed
                && accepted.committedRevision
                && receiver->state() == ProviderDocumentState::ready,
            "lifecycle did not accept and replay a valid remote MAB before returning");
    const auto* acceptedMap = receiver->projection().mapStream("block-attrs");
    require(acceptedMap
                && acceptedMap->get(QByteArrayLiteral("remote-key"),
                                    entries.front().identity)
                    == encodedValue
                && receiver->recovery().localHistory == localHistoryBefore,
            "remote MAB replay failed or changed receiver-local undo history");

    auto unknownBlockBytes = std::array<uint8_t, CollabText::ActorId::kBytes>{};
    unknownBlockBytes.back() = 77;
    auto invalidBundle = remoteAction.bundle();
    invalidBundle.actionId = QByteArrayLiteral("peer-unknown-block-action");
    invalidBundle.operations.front().targetBlock = BlockIdentity{
        CollabText::ActorId::from_bytes(unknownBlockBytes), 9901};
    PreparedStorageTransaction invalidTransaction;
    invalidTransaction.transactionId = invalidBundle.actionId;
    invalidTransaction.expectedRevision = receiver->recovery().acceptedRevision;
    invalidTransaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    invalidTransaction.sharedRecord = take(ActionBundleCodec::encode(invalidBundle));
    invalidTransaction.remoteAcceptance = true;
    for (const auto& operation : invalidBundle.operations)
        invalidTransaction.operationIdentities.push_back({
            QString::fromStdString(operation.stream), operation.producer,
            operation.counter});
    const auto acceptedCountBeforeInvalid = receiver->recovery().acceptedRecords.size();
    const auto invalidResult = receiver->acceptRemoteAction(invalidTransaction);
    require(invalidResult.state == ActionPublicationState::rejected
                && receiver->recovery().acceptedRecords.size() == acceptedCountBeforeInvalid,
            "remote MAB targeting an unknown block was committed");

    const auto duplicate = receiver->acceptRemoteAction(transaction);
    require(duplicate.state == ActionPublicationState::committed
                && duplicate.committedRevision == accepted.committedRevision
                && receiver->recovery().localHistory == localHistoryBefore,
            "exact remote MAB retry did not preserve its original committed revision");
}

void runTwoWriterConvergence()
{
    auto firstProvider = take(makeEphemeralDocumentStorageProvider());
    auto secondProvider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("two-writer-convergence"), 1};
    auto first = take(ProviderBackedDocumentState::open(
        *firstProvider, scope, StorageMode::ephemeral));
    seedOneBlock(*first, QByteArrayLiteral("base"));
    const auto initialRecord = first->recovery().acceptedRecords.front();
    const auto initialBundle = take(ActionBundleCodec::decode(
        initialRecord.bytes, "two-writer-convergence", 1));

    auto second = take(ProviderBackedDocumentState::open(
        *secondProvider, scope, StorageMode::ephemeral));
    PreparedStorageTransaction initial;
    initial.transactionId = initialBundle.actionId;
    initial.expectedRevision = second->recovery().acceptedRevision;
    initial.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    initial.sharedRecord = initialRecord.bytes;
    initial.remoteAcceptance = true;
    for (const auto& operation : initialBundle.operations)
        initial.operationIdentities.push_back({QString::fromStdString(operation.stream),
            operation.producer, operation.counter});
    require(second->acceptRemoteAction(initial).state
                == ActionPublicationState::committed
                && second->state() == ProviderDocumentState::ready,
            "second writer could not accept the common initial MAB through lifecycle");

    const auto firstEntries = take(first->projection().idList()->visibleEntries());
    const auto secondEntries = take(second->projection().idList()->visibleEntries());
    require(firstEntries.size() == 1 && secondEntries.size() == 1
                && firstEntries.front().identity == secondEntries.front().identity,
            "two writers did not recover the same structural identity");
    const auto block = firstEntries.front().identity;
    const auto baselineText = std::find_if(initialBundle.operations.begin(),
        initialBundle.operations.end(), [&](const ActionOperation& operation) {
            return operation.primitive == OperationPrimitive::text
                && operation.targetBlock == std::optional<BlockIdentity>(block);
        });
    require(baselineText != initialBundle.operations.end(),
            "common initial MAB did not contain block text");
    auto makePeerAction = [&](const QByteArray& actionId, const QByteArray& suffix) {
        auto peerProvider = take(makeEphemeralDocumentStorageProvider());
        auto peerSession = take(peerProvider->open(scope, StorageMode::ephemeral));
        const auto route = take(blockTextStreamName(block));
        auto peerText = take(ScopedTextStream::create(*peerSession,
            CollabText::PositionScope{"two-writer-convergence", route,
                CollabText::Primitive::text, 1}));
        const auto baseline = peerText->applyRemote(RemoteOperation{
            CollabText::PositionScope{"two-writer-convergence", route,
                CollabText::Primitive::text, 1},
            {baselineText->producer, baselineText->counter}, baselineText->payload});
        require(baseline.ok()
                    && baseline.value().kind() == CollabText::ApplyOutcomeKind::applied,
                "peer edit fixture could not replay the common text baseline");
        auto operation = take(peerText->replaceUtf8(4, 4, suffix));
        ActionStager action("two-writer-convergence", 1, 2, actionId,
                            ActionKind::edit);
        take(action.add(operation));
        return action;
    };
    auto firstAction = makePeerAction(QByteArrayLiteral("writer-A-edit"),
                                      QByteArrayLiteral("-A"));
    auto secondAction = makePeerAction(QByteArrayLiteral("writer-B-edit"),
                                       QByteArrayLiteral("-B"));

    auto transactionFor = [&](const ActionStager& stager,
                              const AcceptedRevision& revision) {
        PreparedStorageTransaction transaction;
        transaction.transactionId = stager.bundle().actionId;
        transaction.expectedRevision = revision;
        transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
        transaction.sharedRecord = take(ActionBundleCodec::encode(stager.bundle()));
        transaction.remoteAcceptance = true;
        for (const auto& operation : stager.bundle().operations)
            transaction.operationIdentities.push_back({
                QString::fromStdString(operation.stream), operation.producer,
                operation.counter});
        return transaction;
    };
    require(first->acceptRemoteAction(transactionFor(firstAction,
                first->recovery().acceptedRevision)).state
                == ActionPublicationState::committed
                && second->acceptRemoteAction(transactionFor(secondAction,
                    second->recovery().acceptedRevision)).state
                    == ActionPublicationState::committed,
            "two writers could not accept the first concurrent MAB");
    require(first->acceptRemoteAction(transactionFor(secondAction,
                first->recovery().acceptedRevision)).state
                == ActionPublicationState::committed
                && second->acceptRemoteAction(transactionFor(firstAction,
                    second->recovery().acceptedRevision)).state
                    == ActionPublicationState::committed,
            "two writers did not accept the reordered concurrent MAB");
    const auto* firstProjection = first->projection().textStream(
        take(blockTextStreamName(block)));
    const auto* secondProjection = second->projection().textStream(
        take(blockTextStreamName(block)));
    if (!firstProjection || !secondProjection
        || firstProjection->text() != secondProjection->text()
        || !firstProjection->text().contains("-A")
        || !firstProjection->text().contains("-B")) {
        throw std::runtime_error("concurrent peer edits did not converge: first="
            + (firstProjection ? firstProjection->text().toStdString() : "<missing>")
            + "; second="
            + (secondProjection ? secondProjection->text().toStdString() : "<missing>"));
    }
}

void runRemoteInitialImportConsumer()
{
    auto sourceProvider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("remote-initial-import"), 1};
    auto source = take(ProviderBackedDocumentState::open(
        *sourceProvider, scope, StorageMode::ephemeral));
    seedOneBlock(*source, QByteArrayLiteral("base"));
    const QByteArray initialMab = source->recovery().acceptedRecords.front().bytes;
    source.reset();

    auto receiverProvider = take(makeEphemeralDocumentStorageProvider());
    auto receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    const auto accepted = receiver->acceptRemoteAction(initialMab);
    const auto blocks = receiver->iterateBlocks();
    require(accepted.state == ActionPublicationState::committed
                && receiver->canStartOrdinaryAction()
                && blocks.size() == 1
                && receiver->blockText(blocks.front()) == QByteArrayLiteral("base")
                && take(receiver->snapshotForExport()).markdown == QByteArrayLiteral("base"),
            "MarkoffDocument did not register a peer initial import and its source snapshot");
    require(receiver->applyBlockEdit({blocks.front(), 4, 0,
                QByteArrayLiteral("-local")}).state == ActionPublicationState::committed,
            "receiver could not publish a local edit after peer initial import");
    receiver.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->blockText(blocks.front()) == QByteArrayLiteral("base-local")
                && receiver->canUndoForBlock(blocks.front()),
            "remote initial import broke local history replay after reopen");
}

void runRemoteReplacementConsumer()
{
    const DocumentStorageScope scope{QStringLiteral("remote-replacement"), 1};
    const QByteArray original = QByteArrayLiteral("Original\n");
    const QByteArray replacement = QByteArrayLiteral("Replacement\n");
    auto sourceProvider = take(makeEphemeralDocumentStorageProvider());
    auto source = take(Markoff::MarkoffDocument::open(
        *sourceProvider, scope, StorageMode::ephemeral));
    require(source->importInitialMarkdown(original).state
                == ActionPublicationState::committed,
            "replacement fixture could not publish the common initial import");
    source.reset();
    auto sourceSession = take(sourceProvider->open(scope, StorageMode::ephemeral));
    const auto sourceRecovery = take(sourceSession->readRecoveryState());
    const QByteArray initialMab = sourceRecovery.acceptedRecords.front().bytes;
    sourceSession.reset();

    auto peerProvider = take(makeEphemeralDocumentStorageProvider());
    auto peer = take(Markoff::MarkoffDocument::open(
        *peerProvider, scope, StorageMode::ephemeral));
    const auto peerImport = peer->acceptRemoteAction(initialMab);
    require(peerImport.state == ActionPublicationState::committed,
            "peer could not register the imported source");
    const auto peerReplacement = peer->replaceMarkdown(replacement);
    require(peerReplacement.state == ActionPublicationState::committed,
            "peer could not replace the imported source");
    peer.reset();
    auto peerSession = take(peerProvider->open(scope, StorageMode::ephemeral));
    const auto peerRecovery = take(peerSession->readRecoveryState());
    require(peerRecovery.acceptedRecords.size() == 2,
            "peer replacement did not append exactly one MAB");
    const QByteArray replacementMab = peerRecovery.acceptedRecords.back().bytes;
    peerSession.reset();

    auto receiverProvider = take(makeEphemeralDocumentStorageProvider());
    auto receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->acceptRemoteAction(initialMab).state
                == ActionPublicationState::committed,
            "receiver could not register the common initial import");
    const auto beforeReplacement = receiver->iterateBlocks();
    const auto replacementAccepted = receiver->acceptRemoteAction(replacementMab);
    if (replacementAccepted.state != ActionPublicationState::committed)
        throw std::runtime_error("receiver rejected a peer replacement MAB: "
            + (replacementAccepted.diagnostic
                ? std::string(replacementAccepted.diagnostic->message()) : "no diagnostic"));
    const auto replacedBlocks = receiver->iterateBlocks();
    require(replacedBlocks.size() == 1
                && replacedBlocks.front() != beforeReplacement.front()
                && receiver->blockText(replacedBlocks.front())
                    == QByteArrayLiteral("Replacement")
                && take(receiver->snapshotForExport()).markdown == replacement,
            "peer replacement did not refresh source bytes and block identities");
    require(receiver->applyBlockEdit({replacedBlocks.front(), 11, 0,
                QByteArrayLiteral("-local")}).state == ActionPublicationState::committed,
            "receiver could not edit after peer replacement");
    receiver.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->blockText(replacedBlocks.front())
                == QByteArrayLiteral("Replacement-local")
                && receiver->canUndoForBlock(replacedBlocks.front()),
            "local undo history did not replay after a peer replacement");
}

void runDeferredRemoteActionRouting()
{
    const DocumentStorageScope scope{QStringLiteral("deferred-remote-action"), 1};
    auto sourceProvider = take(makeEphemeralDocumentStorageProvider());
    auto source = take(Markoff::MarkoffDocument::open(
        *sourceProvider, scope, StorageMode::ephemeral));
    require(source->importInitialMarkdown(QByteArrayLiteral("base")).state
                == ActionPublicationState::committed,
            "deferred routing fixture could not publish its baseline");
    source.reset();
    auto sourceSession = take(sourceProvider->open(scope, StorageMode::ephemeral));
    const auto initialRecord = take(sourceSession->readRecoveryState()).acceptedRecords.front();
    const auto initialBundle = take(ActionBundleCodec::decode(
        initialRecord.bytes, "deferred-remote-action", 1));
    sourceSession.reset();

    auto receiverProvider = take(makeEphemeralDocumentStorageProvider());
    auto receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->acceptRemoteAction(initialRecord.bytes).state
                == ActionPublicationState::committed,
            "deferred routing receiver could not accept the baseline");
    const auto block = receiver->iterateBlocks().front();
    const BlockIdentity identity{
        CollabText::ActorId::from_bytes(block.actorBytes()), block.counter()};
    const auto route = take(blockTextStreamName(identity));
    auto peerProvider = take(makeEphemeralDocumentStorageProvider());
    auto peerSession = take(peerProvider->open(scope, StorageMode::ephemeral));
    auto peerText = take(ScopedTextStream::create(*peerSession,
        CollabText::PositionScope{"deferred-remote-action", route,
            CollabText::Primitive::text, 1}));
    const auto baseText = std::find_if(initialBundle.operations.begin(),
        initialBundle.operations.end(), [&](const ActionOperation& operation) {
            return operation.primitive == OperationPrimitive::text
                && operation.targetBlock == std::optional<BlockIdentity>(identity);
        });
    require(baseText != initialBundle.operations.end(),
            "deferred routing baseline lacks text");
    const auto replayedBase = peerText->applyRemote(RemoteOperation{
        CollabText::PositionScope{"deferred-remote-action", route,
            CollabText::Primitive::text, 1},
        {baseText->producer, baseText->counter}, baseText->payload});
    require(replayedBase.ok()
                && replayedBase.value().kind() == CollabText::ApplyOutcomeKind::applied,
            "peer could not replay deferred routing baseline text");
    auto firstOperation = take(peerText->replaceUtf8(4, 4, QByteArrayLiteral("-A")));
    auto dependentOperation = take(peerText->replaceUtf8(6, 6, QByteArrayLiteral("B")));
    ActionStager firstAction("deferred-remote-action", 1, 2,
        QByteArrayLiteral("dependency-first"), ActionKind::edit);
    ActionStager dependentAction("deferred-remote-action", 1, 3,
        QByteArrayLiteral("dependency-second"), ActionKind::edit);
    take(firstAction.add(firstOperation));
    take(dependentAction.add(dependentOperation));
    const QByteArray firstMab = take(ActionBundleCodec::encode(firstAction.bundle()));
    const QByteArray dependentMab = take(ActionBundleCodec::encode(dependentAction.bundle()));

    ActionStager reusedActionId("deferred-remote-action", 1, 2,
        initialBundle.actionId, ActionKind::edit);
    take(reusedActionId.add(firstOperation));
    const QByteArray reusedIdMab = take(ActionBundleCodec::encode(
        reusedActionId.bundle()));

    take(receiver->setRemoteActionQueueBudget({0, 0}));
    const auto backpressured = receiver->routeRemoteAction(dependentMab);
    require(backpressured.state == Markoff::RemoteActionRouteState::backpressured
                && backpressured.committed.empty()
                && receiver->pendingRemoteActionCount() == 0
                && receiver->blockText(block) == QByteArrayLiteral("base"),
            "zero-capacity route did not preserve caller ownership of the MAB");
    take(receiver->setRemoteActionQueueBudget({}));
    const auto quarantined = receiver->routeRemoteAction(reusedIdMab);
    require(quarantined.state == Markoff::RemoteActionRouteState::quarantined
                && quarantined.quarantined
                    == std::vector<QByteArray>{initialBundle.actionId}
                && receiver->pendingRemoteActionCount() == 0
                && receiver->blockText(block) == QByteArrayLiteral("base"),
            "changed accepted action ID was acknowledged without durable quarantine");
    const auto quarantineRetry = receiver->routeRemoteAction(reusedIdMab);
    require(quarantineRetry.state == Markoff::RemoteActionRouteState::quarantined,
            "exact rejected MAB retry was not acknowledged as quarantined");
    receiver.reset();
    auto quarantineSession = take(receiverProvider->open(scope, StorageMode::ephemeral));
    const auto quarantineRecovery = take(quarantineSession->readRecoveryState());
    require(quarantineRecovery.quarantinedOperations.size() == 1,
            "document boundary did not persist its rejected MAB quarantine");
    const auto quarantineRecord = take(QuarantineRecordCodec::decode(
        quarantineRecovery.quarantinedOperations.front()));
    require(quarantineRecord.actionId == initialBundle.actionId
                && quarantineRecord.originalActionEnvelope == reusedIdMab
                && quarantineRecord.reason.code() == CollabText::ErrorCode::pin_conflict,
            "document quarantine lost exact MAB bytes, action identity or reason");
    quarantineSession.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    const auto recoveredQuarantines = take(receiver->quarantinedRemoteActions());
    require(recoveredQuarantines.size() == 1
                && recoveredQuarantines.front().actionId == initialBundle.actionId
                && recoveredQuarantines.front().originalMab == reusedIdMab
                && recoveredQuarantines.front().reason.code()
                    == CollabText::ErrorCode::pin_conflict,
            "reopened document did not recover its exact quarantined remote MAB");
    const auto deferred = receiver->routeRemoteAction(dependentMab);
    require(deferred.state == Markoff::RemoteActionRouteState::deferred
                && deferred.committed.empty()
                && receiver->pendingRemoteActionCount() == 1
                && receiver->blockText(block) == QByteArrayLiteral("base"),
            "causally deferred MAB was acknowledged or applied prematurely");
    receiver.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->pendingRemoteActionCount() == 1,
            "causally deferred MAB was lost across provider reopen");
    require(receiver->acceptRemoteAction(firstMab).state
                == ActionPublicationState::committed
                && receiver->pendingRemoteActionCount() == 1
                && receiver->blockText(block) == QByteArrayLiteral("base-A"),
            "accepted dependency unexpectedly consumed deferred retry state");
    receiver.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->pendingRemoteActionCount() == 1
                && receiver->blockText(block) == QByteArrayLiteral("base-A"),
            "accepted dependency or deferred MAB did not survive provider reopen");
    // The sender has no receipt for this MAB and retransmits after reconnect.
    // That exact delivery must trigger retry against the now-accepted
    // dependency instead of remaining stuck in the recovered wait queue.
    const auto retriedWaits = receiver->routeRemoteAction(dependentMab);
    if (!(retriedWaits.state == Markoff::RemoteActionRouteState::committed
                && retriedWaits.committed.size() == 1
                && retriedWaits.committed[0].actionId == dependentAction.bundle().actionId
                && receiver->pendingRemoteActionCount() == 0
                && receiver->blockText(block) == QByteArrayLiteral("base-AB"))) {
        throw std::runtime_error("retransmitted deferred MAB did not retry: state="
            + std::to_string(static_cast<int>(retriedWaits.state))
            + "; receipts=" + std::to_string(retriedWaits.committed.size())
            + "; pending=" + std::to_string(receiver->pendingRemoteActionCount())
            + "; text=" + receiver->blockText(block).toStdString()
            + "; error=" + (retriedWaits.diagnostic
                ? std::string(retriedWaits.diagnostic->message()) : "none"));
    }

    auto missingBundle = firstAction.bundle();
    missingBundle.deliverySequence = 4;
    missingBundle.actionId = QByteArrayLiteral("unknown-target-wait");
    const BlockIdentity missingIdentity{identity.actor, identity.counter + 1000000};
    require(!missingBundle.operations.empty(),
            "deferred routing fixture has no operation for unknown target");
    missingBundle.operations.front().targetBlock = missingIdentity;
    missingBundle.operations.front().stream = take(
        blockTextStreamName(missingIdentity));
    const QByteArray missingTargetMab = take(ActionBundleCodec::encode(missingBundle));
    const auto missingTarget = receiver->routeRemoteAction(missingTargetMab);
    require(missingTarget.state == Markoff::RemoteActionRouteState::deferred
                && missingTarget.committed.empty()
                && receiver->pendingRemoteActionCount() == 1
                && receiver->blockText(block) == QByteArrayLiteral("base-AB"),
            "unknown block stream was quarantined or acknowledged instead of retained");
    receiver.reset();
    receiver = take(Markoff::MarkoffDocument::open(
        *receiverProvider, scope, StorageMode::ephemeral));
    require(receiver->pendingRemoteActionCount() == 1,
            "unknown-target MAB was lost across provider reopen");
}

const ActionHistoryEvent* firstTextEdit(const CrossStreamHistoryIndex& index)
{
    for (const auto& event : index.events) {
        if (event.kind != ActionKind::edit)
            continue;
        if (std::any_of(event.groups.begin(), event.groups.end(),
                [](const HistoryGroupAssociation& group) {
                    return group.primitive == OperationPrimitive::text;
                }))
            return &event;
    }
    return nullptr;
}

const ActionHistoryEvent* lastTextEdit(const CrossStreamHistoryIndex& index)
{
    const ActionHistoryEvent* last = nullptr;
    for (const auto& event : index.events) {
        if (event.kind != ActionKind::edit)
            continue;
        if (std::any_of(event.groups.begin(), event.groups.end(),
                [](const HistoryGroupAssociation& group) {
                    return group.primitive == OperationPrimitive::text;
                }))
            last = &event;
    }
    return last;
}

const HistoryGroupAssociation* textGroup(const ActionHistoryEvent& event)
{
    for (const auto& group : event.groups)
        if (group.primitive == OperationPrimitive::text)
            return &group;
    return nullptr;
}

void runActorUndoDepth()
{
    auto provider = take(makeEphemeralDocumentStorageProvider());
    const DocumentStorageScope scope{QStringLiteral("actor-undo-depth"), 1};
    auto document = take(Markoff::MarkoffDocument::open(
        *provider, scope, StorageMode::ephemeral));
    const auto imported = document->importInitialMarkdown(QByteArrayLiteral("base\n"));
    require(imported.state == ActionPublicationState::committed,
            "undo-depth fixture could not import its base paragraph");
    const auto blocks = document->iterateBlocks();
    require(blocks.size() == 1, "undo-depth fixture did not import one block");
    const Markoff::BlockId block = blocks.front();
    const QByteArray base = document->blockText(block);
    require(!base.isEmpty(), "undo-depth fixture imported an empty paragraph");

    for (size_t i = 0; i < kActorUndoDepth + 1; ++i) {
        const auto text = document->blockText(block);
        const auto published = document->applyBlockEdit(
            {block, uint32_t(text.size()), 0, QByteArrayLiteral("x")});
        require(published.state == ActionPublicationState::committed,
                "undo-depth fixture could not publish the next text edit");
    }
    const QByteArray full = document->blockText(block);
    require(full == base + QByteArray(int(kActorUndoDepth + 1), 'x'),
            "undo-depth fixture did not retain every committed character");
    const auto exported = document->snapshotForExport();
    require(exported.ok() && document->blockText(block) == full,
            "export changed the document while reading a revision-bound snapshot");

    document.reset();
    {
        auto session = take(provider->open(scope, StorageMode::ephemeral));
        const auto recovery = take(session->readRecoveryState());
        require(recovery.localHistory.has_value(),
                "undo-depth fixture committed no owner history");
        const auto committed = std::find_if(recovery.acceptedRecords.cbegin(),
            recovery.acceptedRecords.cend(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == recovery.localHistory->committedTransactionId
                    && record.committedRevision == recovery.localHistory->acceptedRevision;
            });
        require(committed != recovery.acceptedRecords.cend(),
                "undo-depth owner history does not name its committed action");
        const std::string documentName = scope.documentKey.toUtf8().toStdString();
        const auto history = take(OwnerHistoryCodec::decode(
            recovery.localHistory->bytes, documentName, scope.epoch, session->actor(),
            recovery.localHistory->committedTransactionId, committed->priorRevision.token));
        const auto* oldest = firstTextEdit(history.actionIndex);
        const auto* retiring = lastTextEdit(history.actionIndex);
        require(oldest && retiring && oldest != retiring
                    && history.actionIndex.events.back().actionId == retiring->actionId,
                "undo-depth history did not end on the action that crossed the bound");
        const auto* oldestGroup = textGroup(*oldest);
        require(oldestGroup != nullptr, "oldest text edit has no text group");
        const bool recorded = std::any_of(history.actionIndex.retirements.begin(),
            history.actionIndex.retirements.end(), [&](const RetiredHistoryAssociation& item) {
                return item.stream == oldestGroup->stream
                    && item.primitive == oldestGroup->primitive
                    && item.groupId.actor == oldestGroup->groupId.actor
                    && item.groupId.counter == oldestGroup->groupId.counter
                    && item.retirementActionId == retiring->actionId;
            });
        require(recorded,
                "the action that crossed the undo depth did not record retirement of the oldest group");
        const auto* retiringGroup = textGroup(*retiring);
        const bool retiredItself = retiringGroup && std::any_of(
            history.actionIndex.retirements.begin(), history.actionIndex.retirements.end(),
            [&](const RetiredHistoryAssociation& item) {
                return item.groupId.actor == retiringGroup->groupId.actor
                    && item.groupId.counter == retiringGroup->groupId.counter
                    && item.stream == retiringGroup->stream;
            });
        require(!retiredItself, "the newest edit retired its own group");
    }

    document = take(Markoff::MarkoffDocument::open(
        *provider, scope, StorageMode::ephemeral));
    const auto reopenedBlocks = document->iterateBlocks();
    require(reopenedBlocks.size() == 1 && document->blockText(reopenedBlocks.front()) == full,
            "reopen after history retirement changed the accepted text");
    const Markoff::BlockId reopened = reopenedBlocks.front();
    require(document->canUndoForBlock(reopened),
            "a younger edit was not undoable after reopen");
    document->undoD2();
    require(document->blockText(reopened) == full.left(full.size() - 1),
            "undo after retirement did not remove the youngest edit");
    int undos = 1;
    while (document->canUndoForBlock(reopened) && undos <= int(kActorUndoDepth) + 2) {
        const auto before = document->blockText(reopened);
        document->undoD2();
        ++undos;
        require(document->blockText(reopened) == before.left(before.size() - 1),
                "an in-window undo removed more than its own edit");
    }
    require(undos == int(kActorUndoDepth)
                && document->blockText(reopened) == base + QByteArrayLiteral("x")
                && !document->canUndoForBlock(reopened),
            "the oldest edit was still undoable after the depth retirement");
    document->undoD2();
    require(document->blockText(reopened) == base + QByteArrayLiteral("x"),
            "undo of a retired group changed the accepted text");

    const DocumentStorageScope refusalScope{QStringLiteral("actor-undo-depth-refusal"), 1};
    auto refusal = take(Markoff::MarkoffDocument::open(
        *provider, refusalScope, StorageMode::ephemeral));
    require(refusal->importInitialMarkdown(QByteArrayLiteral("base\n")).state
                == ActionPublicationState::committed,
            "retirement-refusal fixture could not import its base paragraph");
    const auto refusalBlocks = refusal->iterateBlocks();
    require(refusalBlocks.size() == 1, "retirement-refusal fixture did not import one block");
    const Markoff::BlockId refusalBlock = refusalBlocks.front();
    for (size_t i = 0; i < kActorUndoDepth + 1; ++i) {
        const auto text = refusal->blockText(refusalBlock);
        require(refusal->applyBlockEdit(
                    {refusalBlock, uint32_t(text.size()), 0, QByteArrayLiteral("x")}).state
                    == ActionPublicationState::committed,
                "retirement-refusal fixture could not fill the undo window");
    }
    const QByteArray beforeRefusal = refusal->blockText(refusalBlock);
    require(refusal->canUndoForBlock(refusalBlock),
            "retirement-refusal fixture has no younger undo before the failed action");
    Testing::failNextHistoryRetirement();
    const auto refused = refusal->applyBlockEdit(
        {refusalBlock, uint32_t(beforeRefusal.size()), 0, QByteArrayLiteral("z")});
    require(refused.state == ActionPublicationState::rejected
                && refused.diagnostic
                && std::string(refused.diagnostic->message())
                    == "retirement record could not be attached"
                && refusal->blockText(refusalBlock) == beforeRefusal
                && !refusal->canStartOrdinaryAction(),
            "a failed retirement committed the new action or changed accepted text");
    require(refusal->recoverPendingAllocations().ok()
                && refusal->canStartOrdinaryAction()
                && refusal->blockText(refusalBlock) == beforeRefusal,
            "gapping the refused edit's reservation did not restore the previous writable state");
    refusal->undoD2();
    require(refusal->blockText(refusalBlock) == beforeRefusal.left(beforeRefusal.size() - 1),
            "a failed retirement left the previous undo unable to remove the younger edit");
}

} // namespace

int main()
{
    try {
        runActorUndoDepth();
        runLifecycleRemoteAcceptance();
        runTwoWriterConvergence();
        runRemoteInitialImportConsumer();
        runRemoteReplacementConsumer();
        runDeferredRemoteActionRouting();
        auto provider = take(makeEphemeralDocumentStorageProvider());
        const DocumentStorageScope scope{QStringLiteral("markoff-document-edit"), 1};
        const QByteArray original = QByteArrayLiteral("alpha");

        auto lifecycle = take(ProviderBackedDocumentState::open(
            *provider, scope, StorageMode::ephemeral));
        seedOneBlock(*lifecycle, original);
        const QByteArray initialImportMab =
            lifecycle->recovery().acceptedRecords.front().bytes;
        lifecycle.reset();

        auto document = take(Markoff::MarkoffDocument::open(
            *provider, scope, StorageMode::ephemeral));
        require(document->canStartOrdinaryAction(),
                "reopened MarkoffDocument did not restore writable readiness");
        Markoff::SessionParams sessionParams;
        sessionParams.participantId = QStringLiteral("candidate-view");
        auto *viewSession = document->createSession(sessionParams);
        require(viewSession && document->sessions() == QList<Markoff::Session *>{viewSession}
                    && document->sessionForParticipant(QStringLiteral("candidate-view"))
                        == viewSession,
                "provider-backed document did not create and register its view session");
        document->destroySession(viewSession);
        require(document->sessions().isEmpty(),
                "provider-backed document did not unregister its destroyed view session");
        const auto blocks = document->iterateBlocks();
        require(blocks.size() == 1
                    && document->blockText(blocks.front()) == original,
                "MarkoffDocument reads did not reflect the accepted list/text projection");

        auto peerProvider = take(makeEphemeralDocumentStorageProvider());
        auto peerSession = take(peerProvider->open(scope, StorageMode::ephemeral));
        const auto blockIdentity = BlockIdentity{
            CollabText::ActorId::from_bytes(blocks.front().actorBytes()),
            blocks.front().counter()};
        const auto textRoute = take(blockTextStreamName(blockIdentity));
        auto peerText = take(ScopedTextStream::create(*peerSession,
            CollabText::PositionScope{"markoff-document-edit", textRoute,
                                      CollabText::Primitive::text, 1}));
        const auto initialBundle = take(ActionBundleCodec::decode(
            initialImportMab, "markoff-document-edit", 1));
        const auto initialTextOperation = std::find_if(initialBundle.operations.begin(),
            initialBundle.operations.end(), [&](const ActionOperation& operation) {
                return operation.stream == textRoute
                    && operation.primitive == OperationPrimitive::text;
            });
        require(initialTextOperation != initialBundle.operations.end(),
                "initial MAB did not contain the seeded block text operation");
        const auto seededPeerText = peerText->applyRemote(RemoteOperation{
            CollabText::PositionScope{"markoff-document-edit", textRoute,
                                      CollabText::Primitive::text, 1},
            {initialTextOperation->producer, initialTextOperation->counter},
            initialTextOperation->payload});
        require(seededPeerText.ok() && peerText->text() == original,
                "peer text stream did not replay the exact accepted baseline");
        auto remoteTextOperation = take(peerText->replaceUtf8(5, 5,
            QByteArrayLiteral("-peer")));
        auto peerMap = take(ReplicatedMapStream::create(*peerSession,
            MapScope{"markoff-document-edit", "block-attrs", 1}));
        take(peerMap->completeReplay());
        const auto remoteAttrName = QByteArrayLiteral("remote-key");
        const auto remoteAttrValue = Markoff::AttrValue{QStringLiteral("peer-value")};
        auto remoteMapOperation = take(peerMap->set(remoteAttrName,
            take(MarkoffMapValueCodec::encodeAttribute(remoteAttrValue)), blockIdentity));
        ActionStager remoteDocumentAction("markoff-document-edit", 1, 2,
            QByteArrayLiteral("peer-document-action"), ActionKind::edit);
        take(remoteDocumentAction.add(remoteMapOperation));
        take(remoteDocumentAction.add(remoteTextOperation));
        const QByteArray remoteMab = take(ActionBundleCodec::encode(
            remoteDocumentAction.bundle()));
        int remoteDocumentNotifications = 0;
        QList<Markoff::BlockId> remoteChangedBlocks;
        QObject::connect(document.get(), &Markoff::MarkoffDocument::documentChanged,
            [&] { ++remoteDocumentNotifications; });
        QObject::connect(document.get(), &Markoff::MarkoffDocument::blocksChanged,
            [&](const QList<Markoff::BlockId>& changed) { remoteChangedBlocks = changed; });
        const auto remoteAccepted = document->acceptRemoteAction(remoteMab);
        require(remoteAccepted.state == ActionPublicationState::committed
                    && document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer")
                    && document->blockAttrs(blocks.front()).value(remoteAttrName)
                        == remoteAttrValue
                    && remoteDocumentNotifications == 1
                    && remoteChangedBlocks.contains(blocks.front()),
                "MarkoffDocument did not publish, project, and notify an accepted remote MAB");

        const auto exactRemoteRetry = document->acceptRemoteAction(remoteMab);
        require(exactRemoteRetry.state == ActionPublicationState::committed
                    && exactRemoteRetry.committedRevision == remoteAccepted.committedRevision
                    && remoteDocumentNotifications == 1,
                "MarkoffDocument did not preserve an idempotent receipt for an exact remote retry");

        const DocumentStorageScope selectiveUndoScope{
            QStringLiteral("markoff-document-selective-undo"), 1};
        auto selectiveUndo = take(Markoff::MarkoffDocument::open(
            *provider, selectiveUndoScope, StorageMode::ephemeral));
        require(selectiveUndo->importInitialMarkdown(
                    QByteArrayLiteral("Block A\n\nBlock B\n")).state
                    == ActionPublicationState::committed,
                "could not import selective document undo fixture");
        const auto selectiveBlocks = selectiveUndo->iterateBlocks();
        require(selectiveBlocks.size() == 2
                    && selectiveUndo->applyBlockEdit({selectiveBlocks[0], 7, 0,
                        QByteArrayLiteral(" A")}).state == ActionPublicationState::committed
                    && selectiveUndo->applyBlockEdit({selectiveBlocks[1], 7, 0,
                        QByteArrayLiteral(" B")}).state == ActionPublicationState::committed,
                "could not publish independent A and B text actions");
        selectiveUndo->undoForBlock(selectiveBlocks[0]);
        require(selectiveUndo->blockText(selectiveBlocks[0])
                    == QByteArrayLiteral("Block A")
                    && selectiveUndo->blockText(selectiveBlocks[1])
                        == QByteArrayLiteral("Block B B")
                    && selectiveUndo->canUndoForBlock(selectiveBlocks[1]),
                "selective document undo of A altered or hid action B");
        selectiveUndo.reset();
        selectiveUndo = take(Markoff::MarkoffDocument::open(
            *provider, selectiveUndoScope, StorageMode::ephemeral));
        require(selectiveUndo->blockText(selectiveBlocks[0])
                    == QByteArrayLiteral("Block A")
                    && selectiveUndo->blockText(selectiveBlocks[1])
                        == QByteArrayLiteral("Block B B"),
                "selective A undo did not survive accepted-prefix reopen");
        selectiveUndo->redoForBlock(selectiveBlocks[0]);
        require(selectiveUndo->blockText(selectiveBlocks[0])
                    == QByteArrayLiteral("Block A A")
                    && selectiveUndo->blockText(selectiveBlocks[1])
                        == QByteArrayLiteral("Block B B"),
                "selective A redo altered the independent B action");

        const DocumentStorageScope selectiveStructureScope{
            QStringLiteral("markoff-document-selective-structure"), 1};
        auto selectiveStructure = take(Markoff::MarkoffDocument::open(
            *provider, selectiveStructureScope, StorageMode::ephemeral));
        require(selectiveStructure->importInitialMarkdown(
                    QByteArrayLiteral("Base\n")).state == ActionPublicationState::committed,
                "could not import selective structural undo fixture");
        const auto baseBlock = selectiveStructure->iterateBlocks().front();
        require(selectiveStructure->applyStructural({
                    Markoff::StructuralOp::InsertEntry{baseBlock,
                        Markoff::BlockKind::ListItem}}).state
                    == ActionPublicationState::committed,
                "could not publish structural action A");
        const auto afterInsertA = selectiveStructure->iterateBlocks();
        const auto insertedAIt = std::find_if(afterInsertA.begin(), afterInsertA.end(),
            [&](Markoff::BlockId candidate) { return candidate != baseBlock; });
        require(insertedAIt != afterInsertA.end(), "structural action A has no new block");
        const auto insertedA = *insertedAIt;
        require(selectiveStructure->applyStructural({
                    Markoff::StructuralOp::InsertEntry{baseBlock,
                        Markoff::BlockKind::BlockQuote}}).state
                    == ActionPublicationState::committed,
                "could not publish structural action B");
        const auto afterInsertB = selectiveStructure->iterateBlocks();
        const auto insertedBIt = std::find_if(afterInsertB.begin(), afterInsertB.end(),
            [&](Markoff::BlockId candidate) {
                return candidate != baseBlock && candidate != insertedA;
            });
        require(insertedBIt != afterInsertB.end(), "structural action B has no new block");
        const auto insertedB = *insertedBIt;
        selectiveStructure->undoForBlock(insertedA);
        auto afterSelectiveStructuralUndo = selectiveStructure->iterateBlocks();
        if (!(afterSelectiveStructuralUndo.size() == 2
                    && std::find(afterSelectiveStructuralUndo.begin(),
                        afterSelectiveStructuralUndo.end(), insertedB)
                        != afterSelectiveStructuralUndo.end()
                    && std::find(afterSelectiveStructuralUndo.begin(),
                        afterSelectiveStructuralUndo.end(), insertedA)
                        == afterSelectiveStructuralUndo.end()
                    && selectiveStructure->blockKind(insertedB)
                        == Markoff::BlockKind::BlockQuote)) {
            throw std::runtime_error("selective structural undo state size="
                + std::to_string(afterSelectiveStructuralUndo.size())
                + " hasA=" + std::to_string(std::find(afterSelectiveStructuralUndo.begin(),
                    afterSelectiveStructuralUndo.end(), insertedA)
                    != afterSelectiveStructuralUndo.end())
                + " hasB=" + std::to_string(std::find(afterSelectiveStructuralUndo.begin(),
                    afterSelectiveStructuralUndo.end(), insertedB)
                    != afterSelectiveStructuralUndo.end())
                + " kindB=" + std::to_string(static_cast<int>(
                    selectiveStructure->blockKind(insertedB))));
        }
        selectiveStructure.reset();
        selectiveStructure = take(Markoff::MarkoffDocument::open(
            *provider, selectiveStructureScope, StorageMode::ephemeral));
        require(selectiveStructure->iterateBlocks() == afterSelectiveStructuralUndo,
                "selective structural undo did not survive accepted-prefix reopen");
        selectiveStructure->redoForBlock(insertedA);
        const auto afterSelectiveStructuralRedo = selectiveStructure->iterateBlocks();
        require(afterSelectiveStructuralRedo.size() == 3
                    && std::find(afterSelectiveStructuralRedo.begin(),
                        afterSelectiveStructuralRedo.end(), insertedA)
                        != afterSelectiveStructuralRedo.end()
                    && std::find(afterSelectiveStructuralRedo.begin(),
                        afterSelectiveStructuralRedo.end(), insertedB)
                        != afterSelectiveStructuralRedo.end()
                    && selectiveStructure->blockKind(insertedA)
                        == Markoff::BlockKind::ListItem
                    && selectiveStructure->blockKind(insertedB)
                        == Markoff::BlockKind::BlockQuote,
                "selective cross-stream redo of A changed B or lost its original identity");

        int tentative = 0;
        int committed = 0;
        int reverted = 0;
        QObject::connect(document.get(), &Markoff::MarkoffDocument::tentativeBlockChanged,
                         [&](Markoff::BlockId) { ++tentative; });
        QObject::connect(document.get(), &Markoff::MarkoffDocument::tentativeBlockCommitted,
                         [&](Markoff::BlockId) { ++committed; });
        QObject::connect(document.get(), &Markoff::MarkoffDocument::tentativeBlockReverted,
                         [&](Markoff::BlockId) { ++reverted; });

        const auto editOffset = document->blockText(blocks.front()).size();
        const auto publication = document->applyBlockEdit(
            {blocks.front(), uint32_t(editOffset), 0, QByteArrayLiteral("!")});
        if (publication.state != ActionPublicationState::committed
            || tentative != 1 || committed != 1 || reverted != 0
            || document->blockText(blocks.front()) != QByteArrayLiteral("alpha-peer!")
            || document->providerState() != ProviderDocumentState::ready) {
            throw std::runtime_error("edit publication state="
                + std::to_string(static_cast<int>(publication.state))
                + " lifecycle state="
                + std::to_string(static_cast<int>(document->providerState()))
                + " diagnostic=" + (publication.diagnostic
                    ? std::string(publication.diagnostic->message()) : "none")
                + " tentative=" + std::to_string(tentative)
                + " committed=" + std::to_string(committed)
                + " reverted=" + std::to_string(reverted)
                + " text=" + document->blockText(blocks.front()).toStdString());
        }

        require(document->canUndoForBlock(blocks.front()),
                "durable action history did not expose the block edit to undo");
        document->undoD2();
        require(document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer")
                    && !document->canUndoForBlock(blocks.front()),
                "provider-backed undo did not compensate the latest block edit");
        document.reset();
        document = take(Markoff::MarkoffDocument::open(
            *provider, scope, StorageMode::ephemeral));
        require(document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer")
                    && !document->canUndoForBlock(blocks.front()),
                "fresh open did not replay the durable undo action and owner history");
        document->redoD2();
        require(document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer!"),
                "durable text redo did not replay the undone edit group");
        document->undoForBlock(blocks.front());
        require(document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer"),
                "text undo after redo did not preserve the accepted peer bytes");
        const auto redoOffset = document->blockText(blocks.front()).size();
        const auto redoSetup = document->applyBlockEdit(
            {blocks.front(), uint32_t(redoOffset), 0, QByteArrayLiteral("!")});
        require(redoSetup.state == ActionPublicationState::committed
                    && document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer!"),
                "could not restore test edit after durable undo");

        const auto kindPublication = document->applyStructural(
            {Markoff::StructuralOp::ChangeKind{blocks.front(),
                                               Markoff::BlockKind::Heading}});
        require(kindPublication.state == ActionPublicationState::committed
                    && document->blockKind(blocks.front()) == Markoff::BlockKind::Heading,
                "MarkoffDocument did not publish a typed block-kind map action");
        const auto attrPublication = document->setBlockAttribute(
            blocks.front(), Markoff::AttrNames::HeadingForm, QStringLiteral("atx"));
        require(attrPublication.state == ActionPublicationState::committed
                    && std::get<QString>(document->blockAttrs(blocks.front())
                        .value(Markoff::AttrNames::HeadingForm)) == QStringLiteral("atx"),
                "MarkoffDocument did not publish a typed block-attribute map action");
        const auto frontmatterPublication = document->setFrontmatterValue(
            QByteArrayLiteral("raw"), QByteArrayLiteral("title: Demo\n"));
        const auto linkRefPublication = document->setLinkReference(
            QByteArrayLiteral("ref-1"),
            {QStringLiteral("https://example.test/"), QStringLiteral("Example")});
        require(document->setFrontmatterValue(QByteArrayLiteral("global-undo"),
                    QByteArrayLiteral("one")).state == ActionPublicationState::committed,
                "could not create a document-level map action for global undo");
        document->undoD2();
        require(!document->frontmatterValue(QByteArrayLiteral("global-undo")),
                "document-wide undo did not compensate a frontmatter-only action");
        document->redoD2();
        require(document->frontmatterValue(QByteArrayLiteral("global-undo"))
                    == QByteArrayLiteral("one"),
                "document-wide redo did not restore a frontmatter-only action");
        require(document->removeFrontmatterValue(QByteArrayLiteral("global-undo"))
                    .state == ActionPublicationState::committed,
                "could not clear the global map undo test value");
        require(frontmatterPublication.state == ActionPublicationState::committed
                    && document->frontmatterValue(QByteArrayLiteral("raw"))
                        == QByteArrayLiteral("title: Demo\n")
                    && linkRefPublication.state == ActionPublicationState::committed
                    && document->linkReference(QByteArrayLiteral("ref-1"))
                        == std::optional<Markoff::LinkRefValue>({
                            QStringLiteral("https://example.test/"),
                            QStringLiteral("Example")}),
                "MarkoffDocument did not publish frontmatter and link-reference maps");
        require(document->setBlockAttribute(blocks.front(),
                    QByteArrayLiteral("temporary"), true).state
                    == ActionPublicationState::committed
                    && document->setFrontmatterValue(QByteArrayLiteral("temporary"),
                        QByteArrayLiteral("discard me")).state
                        == ActionPublicationState::committed
                    && document->setLinkReference(QByteArrayLiteral("ref-temporary"),
                        {QStringLiteral("https://discard.example/"), QString()}).state
                        == ActionPublicationState::committed,
                "MarkoffDocument did not publish temporary sibling-map entries");
        require(document->canUndoForBlock(blocks.front()),
                "block map action was not selected for durable per-block undo");
        document->undoForBlock(blocks.front());
        require(!document->blockAttrs(blocks.front()).contains(
                    QByteArrayLiteral("temporary")),
                "per-block undo did not restore a map key's absent prior value");
        require(document->setBlockAttribute(blocks.front(),
                    QByteArrayLiteral("undo-value"), true).state
                    == ActionPublicationState::committed
                    && document->setBlockAttribute(blocks.front(),
                        QByteArrayLiteral("undo-value"), false).state
                        == ActionPublicationState::committed,
                "could not create consecutive map writes for prior-value undo");
        document->undoForBlock(blocks.front());
        require(std::get<bool>(document->blockAttrs(blocks.front())
                    .value(QByteArrayLiteral("undo-value"))),
                "per-block map undo did not restore the overwritten prior value");
        document->undoForBlock(blocks.front());
        require(!document->blockAttrs(blocks.front()).contains(
                    QByteArrayLiteral("undo-value")),
                "a second undo did not reach the preceding block map action");
        document->redoForBlock(blocks.front());
        require(std::get<bool>(document->blockAttrs(blocks.front())
                    .value(QByteArrayLiteral("undo-value"))),
                "map redo did not restore the prior set value");
        document->redoForBlock(blocks.front());
        require(!std::get<bool>(document->blockAttrs(blocks.front())
                    .value(QByteArrayLiteral("undo-value"))),
                "map redo did not restore the later overwritten value");
        require(document->removeBlockAttribute(blocks.front(),
                    QByteArrayLiteral("undo-value")).state == ActionPublicationState::committed,
                "could not clear the map redo test value");
        require(document->removeBlockAttribute(blocks.front(),
                    QByteArrayLiteral("temporary")).state == ActionPublicationState::committed
                    && document->removeFrontmatterValue(QByteArrayLiteral("temporary")).state
                        == ActionPublicationState::committed
                    && document->removeLinkReference(QByteArrayLiteral("ref-temporary")).state
                        == ActionPublicationState::committed,
                "MarkoffDocument did not publish sibling-map tombstones");

        const auto insertPublication = document->applyStructural(
            {Markoff::StructuralOp::InsertEntry{blocks.front(),
                                                Markoff::BlockKind::ListItem}});
        auto afterInsert = document->iterateBlocks();
        require(insertPublication.state == ActionPublicationState::committed
                    && afterInsert.size() == 2
                    && afterInsert.front() == blocks.front()
                    && document->blockKind(afterInsert.back())
                        == Markoff::BlockKind::ListItem,
                "MarkoffDocument did not atomically publish list insertion and kind");

        auto insertedBlock = afterInsert.back();
        require(document->canUndoForBlock(insertedBlock),
                "paired list/kind insertion was not selected for per-block undo");
        document->undoD2();
        require(document->iterateBlocks() == std::vector<Markoff::BlockId>{blocks.front()},
                "structural undo did not compensate list insertion and kind mapping together");
        document.reset();
        document = take(Markoff::MarkoffDocument::open(
            *provider, scope, StorageMode::ephemeral));
        require(document->iterateBlocks() == std::vector<Markoff::BlockId>{blocks.front()}
                    && !document->canUndoForBlock(insertedBlock),
                "reopen did not restore structural undo state or retire the inserted block action");
        document->redoD2();
        require(document->iterateBlocks() == std::vector<Markoff::BlockId>{
                        blocks.front(), insertedBlock}
                    && document->blockKind(insertedBlock) == Markoff::BlockKind::ListItem,
                "durable redo did not restore the inserted block identity and kind");
        const auto removeRedone = document->applyStructural(
            {Markoff::StructuralOp::RemoveEntry{insertedBlock}});
        require(removeRedone.state == ActionPublicationState::committed
                    && document->iterateBlocks() == std::vector<Markoff::BlockId>{blocks.front()},
                "could not return to the expected list after structural redo");
        const auto reinsertion = document->applyStructural(
            {Markoff::StructuralOp::InsertEntry{blocks.front(),
                                                Markoff::BlockKind::ListItem}});
        afterInsert = document->iterateBlocks();
        require(reinsertion.state == ActionPublicationState::committed
                    && afterInsert.size() == 2,
                "could not reinsert block after durable structural undo");
        insertedBlock = afterInsert.back();
        const auto markerPublication = document->setBlockAttribute(
            insertedBlock, Markoff::AttrNames::MarkerStyle, QStringLiteral("task"));
        const auto checkedPublication = document->setBlockAttribute(
            insertedBlock, Markoff::AttrNames::Checked, true);
        require(markerPublication.state == ActionPublicationState::committed
                    && checkedPublication.state == ActionPublicationState::committed,
                "MarkoffDocument did not publish a checked-list attribute");
        document->toggleListItemChecked(insertedBlock);
        require(!std::get<bool>(document->blockAttrs(insertedBlock)
                    .value(Markoff::AttrNames::Checked)),
                "toggleListItemChecked did not publish the checked-state inverse");
        require(document->serializeForSave().contains(QByteArrayLiteral("- [ ] ")),
                "edited Markdown export did not reconstruct the task-list marker");
        const auto removePublication = document->applyStructural(
            {Markoff::StructuralOp::RemoveEntry{insertedBlock}});
        require(removePublication.state == ActionPublicationState::committed
                    && document->iterateBlocks() == std::vector<Markoff::BlockId>{blocks.front()},
                "MarkoffDocument did not atomically remove a block and its kind entry");
        require(document->canUndoForBlock(insertedBlock),
                "paired list/kind removal was not selected for per-block undo");
        document->undoForBlock(insertedBlock);
        require(document->iterateBlocks() == std::vector<Markoff::BlockId>{
                        blocks.front(), insertedBlock}
                    && document->blockKind(insertedBlock) == Markoff::BlockKind::ListItem,
                "structural undo did not restore the removed block identity and kind");
        const auto removeAgain = document->applyStructural(
            {Markoff::StructuralOp::RemoveEntry{insertedBlock}});
        require(removeAgain.state == ActionPublicationState::committed
                    && document->iterateBlocks() == std::vector<Markoff::BlockId>{blocks.front()},
                "could not remove the restored block after testing structural undo");

        document.reset();
        document = take(Markoff::MarkoffDocument::open(
            *provider, scope, StorageMode::ephemeral));
        require(document->blockText(blocks.front()) == QByteArrayLiteral("alpha-peer!")
                    && document->blockKind(blocks.front()) == Markoff::BlockKind::Heading
                    && std::get<QString>(document->blockAttrs(blocks.front())
                        .value(Markoff::AttrNames::HeadingForm)) == QStringLiteral("atx")
                    && document->frontmatterValue(QByteArrayLiteral("raw"))
                        == QByteArrayLiteral("title: Demo\n")
                    && document->linkReference(QByteArrayLiteral("ref-1"))
                        == std::optional<Markoff::LinkRefValue>({
                            QStringLiteral("https://example.test/"),
                            QStringLiteral("Example")})
                    && !document->blockAttrs(blocks.front()).contains(
                        QByteArrayLiteral("temporary"))
                    && !document->frontmatterValue(QByteArrayLiteral("temporary"))
                    && !document->linkReference(QByteArrayLiteral("ref-temporary"))
                    && document->iterateBlocks()
                        == std::vector<Markoff::BlockId>{blocks.front()},
                "fresh MarkoffDocument open did not replay text, map and list actions");

        auto peerUndoText = take(peerText->replaceUtf8(5, 10, {}));
        ActionStager peerUndoAction("markoff-document-edit", 1, 3,
            QByteArrayLiteral("peer-undo-action"), ActionKind::undo);
        take(peerUndoAction.add(peerUndoText));
        const QByteArray peerUndoMab = take(ActionBundleCodec::encode(
            peerUndoAction.bundle()));
        const auto peerUndoAccepted = document->acceptRemoteAction(peerUndoMab);
        require(peerUndoAccepted.state == ActionPublicationState::committed
                    && document->blockText(blocks.front())
                        == original + QByteArrayLiteral("!")
                    && document->blockAttrs(blocks.front()).value(remoteAttrName)
                        == remoteAttrValue,
                "MarkoffDocument did not accept peer undo while preserving local text and map state");
        const auto peerUndoRetry = document->acceptRemoteAction(peerUndoMab);
        require(peerUndoRetry.state == ActionPublicationState::committed
                    && peerUndoRetry.committedRevision == peerUndoAccepted.committedRevision
                    && document->blockText(blocks.front())
                        == original + QByteArrayLiteral("!"),
                "exact peer-undo retry changed its committed result or projection");
        auto peerRedoText = take(peerText->replaceUtf8(5, 5,
            QByteArrayLiteral("-peer")));
        ActionStager peerRedoAction("markoff-document-edit", 1, 4,
            QByteArrayLiteral("peer-redo-action"), ActionKind::redo);
        take(peerRedoAction.add(peerRedoText));
        const QByteArray peerRedoMab = take(ActionBundleCodec::encode(
            peerRedoAction.bundle()));
        const auto peerRedoAccepted = document->acceptRemoteAction(peerRedoMab);
        require(peerRedoAccepted.state == ActionPublicationState::committed
                    && document->blockText(blocks.front())
                        == QByteArrayLiteral("alpha-peer!")
                    && document->blockAttrs(blocks.front()).value(remoteAttrName)
                        == remoteAttrValue,
                "MarkoffDocument did not accept peer redo while preserving local text");

        const DocumentStorageScope importScope{
            QStringLiteral("markoff-document-import"), 1};
        auto importedDocument = take(Markoff::MarkoffDocument::open(
            *provider, importScope, StorageMode::ephemeral));
        QByteArray markdown = QByteArrayLiteral(
            "---\ntitle: Import\n---\n\n# Heading\n\nParagraph\n\n"
            "[Long\nReference Label]:\n https://example.test/path\n"
            " \"Reference title\"\n\n"
            "[A\\*B]: https://example.test/a&amp;b \"Title &copy;\"\n"
            "[Numeric &#x41;]: <https://example.test/?a=1&amp;b=2> "
            "'Title \\(v2\\)'\n\n"
            "[Numeric A]: /plain-numeric\n"
            "[foo\\!]: /escaped\n"
            "[foo!]: /plain\n\n"
            "[Duplicate Label]: https://example.test/first\n"
            "[ duplicate   label ]: https://example.test/second\n\n"
            "[Paren]: https://example.test/a_(b) (parenthetical title)\n\n"
            "[Multiline title]: /multi \"line\ncontinued\"\n\n"
            "[CRLF title]: /crlf \"first\r\nsecond\"\r\n");
        markdown += QByteArrayLiteral("[") + QByteArray(999, 'x')
            + QByteArrayLiteral("]: /999\n");
        const QByteArray invalidUtf8("bad\xffsource", 10);
        require(importedDocument->importInitialMarkdown(invalidUtf8).state
                    == ActionPublicationState::rejected
                    && importedDocument->providerState()
                        == ProviderDocumentState::awaitingInitialImport,
                "malformed UTF-8 import authorized or mutated the document");
        const auto importPublication = importedDocument->importInitialMarkdown(markdown);
        require(importPublication.state == ActionPublicationState::committed
                    && importedDocument->providerState() == ProviderDocumentState::ready
                    && importedDocument->iterateBlocks().size() == 2
                    && importedDocument->blockKind(importedDocument->iterateBlocks().front())
                        == Markoff::BlockKind::Heading
                    && importedDocument->blockAttrs(
                        importedDocument->iterateBlocks().front())
                        .value(Markoff::AttrNames::Level) == Markoff::AttrValue{1}
                    && importedDocument->frontmatterValue(QByteArrayLiteral("raw"))
                        == QByteArrayLiteral("title: Import")
                    && importedDocument->linkReference(QByteArrayLiteral("long reference label"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("https://example.test/path"),
                            QStringLiteral("Reference title")}}
                    && importedDocument->linkReference(QByteArrayLiteral("a\\*b"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("https://example.test/a&b"),
                            QStringLiteral("Title ©")}}
                    && importedDocument->linkReference(QByteArrayLiteral(
                        "numeric &#x41;"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("https://example.test/?a=1&b=2"),
                            QStringLiteral("Title (v2)")}}
                    && importedDocument->linkReference(QByteArrayLiteral("numeric a"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/plain-numeric"), QString{}}}
                    && importedDocument->linkReference(QByteArrayLiteral("foo\\!"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/escaped"), QString{}}}
                    && importedDocument->linkReference(QByteArrayLiteral("foo!"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/plain"), QString{}}}
                    && importedDocument->linkReference(QByteArrayLiteral("duplicate label"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("https://example.test/first"), QString{}}}
                    && importedDocument->linkReference(QByteArrayLiteral("paren"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("https://example.test/a_(b)"),
                            QStringLiteral("parenthetical title")}}
                    && importedDocument->linkReference(QByteArrayLiteral("multiline title"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/multi"), QStringLiteral("line continued")}}
                    && importedDocument->linkReference(QByteArrayLiteral("crlf title"))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/crlf"), QStringLiteral("first second")}}
                    && importedDocument->linkReference(QByteArray(999, 'x'))
                        == std::optional<Markoff::LinkRefValue>{Markoff::LinkRefValue{
                            QStringLiteral("/999"), QString{}}},
                "provider-backed initial import did not materialize source blocks and metadata");

        const DocumentStorageScope overlongReferenceScope{
            QStringLiteral("markoff-document-overlong-reference"), 1};
        auto overlongReference = take(Markoff::MarkoffDocument::open(
            *provider, overlongReferenceScope, StorageMode::ephemeral));
        const QByteArray overlongReferenceMarkdown = QByteArrayLiteral("# Label limit\n\n[")
            + QByteArray(1000, 'z') + QByteArrayLiteral("]: /ignored\n");
        require(overlongReference->importInitialMarkdown(overlongReferenceMarkdown).state
                    == ActionPublicationState::committed,
                "overlong CommonMark reference source did not import");
        const auto overlongBlocks = overlongReference->iterateBlocks();
        require(overlongBlocks.size() == 2,
                "overlong reference did not remain a normal paragraph block");
        require(overlongReference->blockKind(overlongBlocks.back())
                    == Markoff::BlockKind::Paragraph,
                "overlong reference did not retain paragraph kind");
        require(!overlongReference->linkReference(QByteArray(1000, 'z')),
                "overlong CommonMark reference label was imported as a definition");

        const DocumentStorageScope forkSourceScope{
            QStringLiteral("markoff-document-fork-source"), 1};
        auto forkSource = take(Markoff::MarkoffDocument::open(
            *provider, forkSourceScope, StorageMode::ephemeral));
        require(forkSource->importInitialMarkdown(
                    QByteArrayLiteral("# Forked note\n\nsource text")).state
                    == ActionPublicationState::committed,
                "could not import Markdown fork source");
        const auto forkSourceBlocks = forkSource->iterateBlocks();
        require(forkSourceBlocks.size() == 2
                    && forkSource->applyBlockEdit({forkSourceBlocks.back(),
                        11, 0, QByteArrayLiteral(" edit")}).state
                        == ActionPublicationState::committed,
                "could not create source edit history before fork");
        const auto forkSourceExport = take(forkSource->snapshotForExport());
        const DocumentStorageScope forkTargetScope{
            QStringLiteral("markoff-document-fork-target"), 1};
        auto forkTarget = take(Markoff::MarkoffDocument::open(
            *provider, forkTargetScope, StorageMode::ephemeral));
        require(forkTarget->importInitialMarkdown(forkSourceExport.markdown).state
                    == ActionPublicationState::committed,
                "could not import Markdown into a new fork scope");
        const auto forkTargetBlocks = forkTarget->iterateBlocks();
        require(forkTargetBlocks.size() == 2
                    && forkTarget->blockText(forkTargetBlocks.back())
                        == QByteArrayLiteral("source text edit")
                    && !forkTarget->canUndoForBlock(forkTargetBlocks.back()),
                "Markdown fork inherited the source document's private undo history");
        forkTarget->undoD2();
        require(forkTarget->blockText(forkTargetBlocks.back())
                    == QByteArrayLiteral("source text edit"),
                "document undo crossed the Markdown fork namespace");

        const DocumentStorageScope editThenStructureScope{
            QStringLiteral("markoff-document-edit-then-structure"), 1};
        auto editThenStructure = take(Markoff::MarkoffDocument::open(
            *provider, editThenStructureScope, StorageMode::ephemeral));
        require(editThenStructure->importInitialMarkdown(
                    QByteArrayLiteral("alpha")).state
                    == ActionPublicationState::committed,
                "edit-then-structure fixture import did not commit");
        const auto editThenStructureBlocks = editThenStructure->iterateBlocks();
        require(editThenStructureBlocks.size() == 1,
                "edit-then-structure fixture did not create one block");
        const auto editThenStructureBlock = editThenStructureBlocks.front();
        require(editThenStructure->applyBlockEdit({editThenStructureBlock, 5, 0,
                    QByteArrayLiteral("-edit")}).state
                    == ActionPublicationState::committed,
                "text edit before structural insertion did not commit");
        const auto editThenStructurePaste = editThenStructure->pasteMarkdownAt(
            editThenStructureBlock, 10,
            QByteArrayLiteral("next paragraph\n\nfinal paragraph"));
        require(editThenStructurePaste.publication.state
                    == ActionPublicationState::committed
                    && editThenStructure->iterateBlocks().size() == 3,
                "structured paste after text edit did not commit");
        editThenStructure.reset();
        editThenStructure = take(Markoff::MarkoffDocument::open(
            *provider, editThenStructureScope, StorageMode::ephemeral));
        require(editThenStructure->iterateBlocks().size() == 3
                    && editThenStructure->blockText(editThenStructureBlock)
                        == QByteArrayLiteral("alpha-edit"),
                "edit-then-structure sequence did not survive provider reopen");
        QByteArray largeImport;
        for (int i = 0; i < 1000; ++i) {
            largeImport += QByteArrayLiteral("Paragraph ") + QByteArray::number(i)
                + QByteArrayLiteral(" with enough text to model a realistic block.\n\n");
        }
        const DocumentStorageScope largeImportScope{
            QStringLiteral("markoff-document-large-import-counters"), 1};
        auto largeImportDocument = take(Markoff::MarkoffDocument::open(
            *provider, largeImportScope, StorageMode::ephemeral));
        require(largeImportDocument->importInitialMarkdown(largeImport).state
                    == ActionPublicationState::committed
                    && largeImportDocument->iterateBlocks().size() == 1000,
                "large provider-backed import did not create the expected blocks");
        const auto largeImportFirst = largeImportDocument->iterateBlocks().front();
        const auto largeFirstText = largeImportDocument->blockText(largeImportFirst);
        require(largeImportDocument->applyBlockEdit({largeImportFirst,
                    static_cast<uint32_t>(largeFirstText.size()), 0,
                    QByteArrayLiteral("-edited")}).state
                    == ActionPublicationState::committed,
                "large-import text edit did not commit");
        const auto largePaste = largeImportDocument->pasteMarkdownAt(
            largeImportFirst, static_cast<uint32_t>(largeFirstText.size() + 7),
            QByteArrayLiteral("pasted one\n\npasted two"));
        require(largePaste.publication.state == ActionPublicationState::committed
                    && largeImportDocument->iterateBlocks().size() == 1002,
                "large-import structured paste reused an accepted order identity");
        largeImportDocument.reset();
        largeImportDocument = take(Markoff::MarkoffDocument::open(
            *provider, largeImportScope, StorageMode::ephemeral));
        require(largeImportDocument->iterateBlocks().size() == 1002
                    && largeImportDocument->blockText(largeImportFirst)
                        == largeFirstText + QByteArrayLiteral("-edited"),
                "large-import edit and paste did not survive provider reopen");
        const auto originalExport = take(importedDocument->snapshotForExport());
        require(originalExport.markdown == markdown,
                "pristine export did not preserve exact imported Markdown bytes");
        const auto metadataEdit = importedDocument->setFrontmatterValue(
            QByteArrayLiteral("raw"), QByteArrayLiteral("title: Exported"));
        require(metadataEdit.state == ActionPublicationState::committed
                    && !importedDocument->acknowledgeExport(originalExport).ok(),
                "a stale export revision was acknowledged after a metadata edit");
        const auto currentExport = take(importedDocument->snapshotForExport());
        auto forgedExport = currentExport;
        forgedExport.markdown.append("stale");
        require(currentExport.revision != originalExport.revision
                    && currentExport.markdown.contains(QByteArrayLiteral("title: Exported"))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[Long\nReference Label]:\n https://example.test/path\n"
                        " \"Reference title\""))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[A\\*B]: https://example.test/a&amp;b \"Title &copy;\""))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[ duplicate   label ]: https://example.test/second"))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[Paren]: https://example.test/a_(b) (parenthetical title)"))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[Multiline title]: /multi \"line\ncontinued\""))
                    && currentExport.markdown.contains(QByteArrayLiteral(
                        "[CRLF title]: /crlf \"first\r\nsecond\""))
                    && !importedDocument->acknowledgeExport(forgedExport).ok()
                    && importedDocument->acknowledgeExport(currentExport).ok(),
                "current Markdown export did not bind and acknowledge its accepted revision");
        QTemporaryDir exportDir;
        require(exportDir.isValid(), "could not create export test directory");
        const QString exportPath = exportDir.filePath(QStringLiteral("note.md"));
        require(importedDocument->save(exportPath),
                "revision-bound QSaveFile export failed for a writable destination");
        QFile exportedFile(exportPath);
        require(exportedFile.open(QIODevice::ReadOnly)
                    && exportedFile.readAll() == currentExport.markdown
                    && !importedDocument->save(exportDir.filePath(
                        QStringLiteral("missing/note.md"))),
                "file save did not preserve export bytes or report an open failure");
        importedDocument.reset();
        auto importSession = take(provider->open(importScope, StorageMode::ephemeral));
        const auto importRecovery = take(importSession->readRecoveryState());
        const auto importBundle = take(ActionBundleCodec::decode(
            importRecovery.acceptedRecords.front().bytes,
            "markoff-document-import", importScope.epoch));
        const auto sourceSnapshot = take(SourceSnapshotCodec::decode(
            importBundle.sourceSnapshot, "markoff-document-import", importScope.epoch));
        require(sourceSnapshot.originalMarkdown == markdown,
                "initial import did not retain the exact original Markdown bytes");
        require(importRecovery.exportedRevision == currentExport.revision,
                "provider did not persist the exact export revision watermark");
        importSession.reset();
        importedDocument = take(Markoff::MarkoffDocument::open(
            *provider, importScope, StorageMode::ephemeral));
        require(importedDocument->iterateBlocks().size() == 2
                    && importedDocument->frontmatterValue(QByteArrayLiteral("raw"))
                        == QByteArrayLiteral("title: Exported"),
                "fresh open did not replay the provider-backed initial import");
        const auto oldImportBlocks = importedDocument->iterateBlocks();
        const QByteArray replacementMarkdown = QByteArrayLiteral(
            "---\r\ntitle: Replacement\r\n---\r\n\r\nNew heading\r\n===========\r\n\r\nNew body\r\n");
        const auto replacementPublication = importedDocument->replaceMarkdown(
            replacementMarkdown);
        const auto replacementBlocks = importedDocument->iterateBlocks();
        require(replacementPublication.state == ActionPublicationState::committed
                    && replacementBlocks.size() == 2
                    && replacementBlocks != oldImportBlocks
                    && importedDocument->blockKind(replacementBlocks.front())
                        == Markoff::BlockKind::Heading
                    && importedDocument->frontmatterValue(QByteArrayLiteral("raw"))
                        == QByteArrayLiteral("title: Replacement")
                    && take(importedDocument->snapshotForExport()).markdown
                        == replacementMarkdown,
                "accepted external replacement did not replace content with fresh identities");
        importedDocument.reset();
        importedDocument = take(Markoff::MarkoffDocument::open(
            *provider, importScope, StorageMode::ephemeral));
        require(importedDocument->iterateBlocks() == replacementBlocks
                    && take(importedDocument->snapshotForExport()).markdown
                        == replacementMarkdown,
                "replacement source or identities did not survive accepted-prefix reopen");
        importedDocument->undoD2();
        require(importedDocument->iterateBlocks() == oldImportBlocks
                    && take(importedDocument->snapshotForExport()).markdown
                        == currentExport.markdown,
                "replacement undo did not restore prior identities and exact edited source");
        importedDocument.reset();
        importedDocument = take(Markoff::MarkoffDocument::open(
            *provider, importScope, StorageMode::ephemeral));
        require(importedDocument->iterateBlocks() == oldImportBlocks
                    && take(importedDocument->snapshotForExport()).markdown
                        == currentExport.markdown,
                "replacement undo source did not survive accepted-prefix reopen");
        importedDocument->redoD2();
        require(importedDocument->iterateBlocks() == replacementBlocks
                    && take(importedDocument->snapshotForExport()).markdown
                        == replacementMarkdown,
                "replacement redo did not restore replacement identities and source");
        importedDocument.reset();
        importedDocument = take(Markoff::MarkoffDocument::open(
            *provider, importScope, StorageMode::ephemeral));
        require(importedDocument->iterateBlocks() == replacementBlocks
                    && take(importedDocument->snapshotForExport()).markdown
                        == replacementMarkdown,
                "replacement redo did not survive accepted-prefix reopen");
        const QByteArray replacedHeading = importedDocument->blockText(
            replacementBlocks.front());
        const auto replacementTextEdit = importedDocument->applyBlockEdit(
            {replacementBlocks.front(), 0, uint32_t(replacedHeading.size()),
             QByteArrayLiteral("Changed heading")});
        const QByteArray replacementAfterTextEdit =
            QByteArrayLiteral("---\r\ntitle: Replacement\r\n---\r\n\r\n")
            + QByteArrayLiteral("Changed heading\n") + QByteArray(15, '=')
            + QByteArrayLiteral("\r\n\r\nNew body\r\n");
        require(replacementTextEdit.state == ActionPublicationState::committed
                    && take(importedDocument->snapshotForExport()).markdown
                        == replacementAfterTextEdit,
                "text edit after replacement did not patch only the changed source block");

        const DocumentStorageScope frontmatterFidelityScope{
            QStringLiteral("markoff-frontmatter-source-fidelity"), 1};
        auto frontmatterFidelity = take(Markoff::MarkoffDocument::open(
            *provider, frontmatterFidelityScope, StorageMode::ephemeral));
        const QByteArray frontmatterFidelitySource = QByteArrayLiteral(
            "---\r\ntitle: Before\r\n---\r\n\r\nTitle\r\n-----\r\n\r\nbody  \r\n");
        require(frontmatterFidelity->importInitialMarkdown(
                    frontmatterFidelitySource).state == ActionPublicationState::committed
                    && frontmatterFidelity->setFrontmatterValue(
                        QByteArrayLiteral("raw"), QByteArrayLiteral("title: After"))
                        .state == ActionPublicationState::committed,
                "could not create frontmatter-only source fidelity fixture");
        const auto frontmatterPatched = take(
            frontmatterFidelity->snapshotForExport());
        const QByteArray expectedFrontmatterPatched = QByteArrayLiteral(
            "---\r\ntitle: After\r\n---\r\n\r\nTitle\r\n-----\r\n\r\nbody  \r\n");
        require(frontmatterPatched.markdown == expectedFrontmatterPatched,
                "frontmatter-only export rewrote untouched Markdown body bytes");
        frontmatterFidelity.reset();
        frontmatterFidelity = take(Markoff::MarkoffDocument::open(
            *provider, frontmatterFidelityScope, StorageMode::ephemeral));
        require(take(frontmatterFidelity->snapshotForExport()).markdown
                    == expectedFrontmatterPatched,
                "fresh replay did not preserve the body after a frontmatter-only edit");

        const std::vector<QByteArray> untouchedSources{
            QByteArrayLiteral("Title\n-----\n"),
            QByteArrayLiteral("> quoted line\n> second line\n"),
            QByteArray("# CRLF\r\n\r\nline\r\n", 16),
            QByteArrayLiteral("trailing spaces   \nnext\t \n"),
            QByteArrayLiteral("no final newline")};
        for (size_t i = 0; i < untouchedSources.size(); ++i) {
            const auto fidelityScope = DocumentStorageScope{
                QStringLiteral("markoff-source-fidelity-%1").arg(i + 1), 1};
            auto fidelityDocument = take(Markoff::MarkoffDocument::open(
                *provider, fidelityScope, StorageMode::ephemeral));
            require(fidelityDocument->importInitialMarkdown(untouchedSources[i]).state
                        == ActionPublicationState::committed
                        && take(fidelityDocument->snapshotForExport()).markdown
                            == untouchedSources[i],
                    "pristine export changed an untouched Markdown fidelity case");
            fidelityDocument.reset();
            fidelityDocument = take(Markoff::MarkoffDocument::open(
                *provider, fidelityScope, StorageMode::ephemeral));
            require(take(fidelityDocument->snapshotForExport()).markdown
                        == untouchedSources[i],
                    "reopen changed exact untouched Markdown source bytes");
        }

        const DocumentStorageScope editedFidelityScope{
            QStringLiteral("markoff-source-fidelity-edited"), 1};
        const QByteArray editedSource = QByteArrayLiteral(
            "Title\n-----\n\nfirst   \n\nsecond\t \n");
        auto editedFidelityDocument = take(Markoff::MarkoffDocument::open(
            *provider, editedFidelityScope, StorageMode::ephemeral));
        require(editedFidelityDocument->importInitialMarkdown(editedSource).state
                    == ActionPublicationState::committed,
                "could not import edited-source fidelity fixture");
        const auto editedBlocks = editedFidelityDocument->iterateBlocks();
        require(editedBlocks.size() == 3
                    && editedFidelityDocument->blockText(editedBlocks[1])
                        == QByteArrayLiteral("first   "),
                "edited-source fixture did not retain its paragraph text");
        const auto fidelityEdit = editedFidelityDocument->applyBlockEdit(
            {editedBlocks[1], 0, 8, QByteArrayLiteral("changed")});
        const QByteArray expectedEditedSource = QByteArrayLiteral(
            "Title\n-----\n\nchanged\n\nsecond\t \n");
        require(fidelityEdit.state == ActionPublicationState::committed
                    && take(editedFidelityDocument->snapshotForExport()).markdown
                        == expectedEditedSource,
                "editing one block did not preserve untouched Markdown source formatting");
        editedFidelityDocument.reset();
        editedFidelityDocument = take(Markoff::MarkoffDocument::open(
            *provider, editedFidelityScope, StorageMode::ephemeral));
        require(take(editedFidelityDocument->snapshotForExport()).markdown
                    == expectedEditedSource,
                "reopened edited-source export did not preserve untouched Markdown formatting");

        const DocumentStorageScope crlfEditScope{
            QStringLiteral("markoff-source-fidelity-crlf-edit"), 1};
        const QByteArray crlfSource("# CRLF\r\n\r\nline\r\n", 16);
        auto crlfEditDocument = take(Markoff::MarkoffDocument::open(
            *provider, crlfEditScope, StorageMode::ephemeral));
        require(crlfEditDocument->importInitialMarkdown(crlfSource).state
                    == ActionPublicationState::committed,
                "could not import edited-CRLF fidelity fixture");
        const auto crlfBlocks = crlfEditDocument->iterateBlocks();
        require(crlfBlocks.size() == 2,
                "edited-CRLF fidelity fixture did not produce two blocks");
        const auto crlfEdit = crlfEditDocument->applyBlockEdit(
            {crlfBlocks.back(), 4, 0, QByteArrayLiteral("!")});
        const QByteArray expectedCrlfEdit("# CRLF\r\n\r\nline!\r\n", 17);
        require(crlfEdit.state == ActionPublicationState::committed
                    && take(crlfEditDocument->snapshotForExport()).markdown
                        == expectedCrlfEdit,
                "editing a CRLF document changed untouched syntax or line endings");
        crlfEditDocument.reset();
        crlfEditDocument = take(Markoff::MarkoffDocument::open(
            *provider, crlfEditScope, StorageMode::ephemeral));
        require(take(crlfEditDocument->snapshotForExport()).markdown
                    == expectedCrlfEdit,
                "reopened edited-CRLF export changed source line endings");

        QTemporaryDir peerAStore;
        QTemporaryDir peerBStore;
        require(peerAStore.isValid() && peerBStore.isValid(),
                "could not create concurrent text-edit fixtures");
        auto peerAProvider = take(makeReferenceDocumentStorageProvider(
            peerAStore.path().toStdString()));
        auto peerBProvider = take(makeReferenceDocumentStorageProvider(
            peerBStore.path().toStdString()));
        const DocumentStorageScope peerScope{
            QStringLiteral("markoff-peer-edit-undo"), 1};
        auto peerASession = take(peerAProvider->open(peerScope, StorageMode::durable));
        auto peerBSession = take(peerBProvider->open(peerScope, StorageMode::durable));
        const std::string peerDocument = peerScope.documentKey.toUtf8().toStdString();
        const auto peerStream = take(blockTextStreamName(
            {peerASession->actor(), 77}));
        const CollabText::PositionScope peerTextScope{
            peerDocument, peerStream, CollabText::Primitive::text, peerScope.epoch};
        auto peerAText = take(ScopedTextStream::create(*peerASession, peerTextScope));
        auto peerBText = take(ScopedTextStream::create(*peerBSession, peerTextScope));
        const auto baseText = take(peerAText->replaceUtf8(
            0, 0, QByteArrayLiteral("base")));
        require(take(peerBText->applyRemote(
                    {baseText.scope, baseText.identity, baseText.bytes})).kind()
                    == CollabText::ApplyOutcomeKind::applied,
                "peer did not receive the shared text baseline");
        const auto localEdit = take(peerAText->replaceUtf8(
            4, 4, QByteArrayLiteral(" A")));
        const auto peerEdit = take(peerBText->replaceUtf8(
            4, 4, QByteArrayLiteral(" B")));
        require(take(peerAText->applyRemote(
                    {peerEdit.scope, peerEdit.identity, peerEdit.bytes})).kind()
                    == CollabText::ApplyOutcomeKind::applied
                    && take(peerBText->applyRemote(
                        {localEdit.scope, localEdit.identity, localEdit.bytes})).kind()
                        == CollabText::ApplyOutcomeKind::applied
                    && peerAText->text() == peerBText->text(),
                "concurrent local and peer text edits did not converge");
        const auto peerUndo = take(peerAText->undo());
        require(peerUndo.has_value()
                    && take(peerBText->applyRemote(
                        {peerUndo->scope, peerUndo->identity, peerUndo->bytes})).kind()
                        == CollabText::ApplyOutcomeKind::applied
                    && peerAText->text() == QByteArrayLiteral("base B")
                    && peerBText->text() == peerAText->text(),
                "undo after a peer edit removed peer text or diverged after delivery");
        const auto peerRedo = take(peerAText->redo());
        require(peerRedo.has_value()
                    && take(peerBText->applyRemote(
                        {peerRedo->scope, peerRedo->identity, peerRedo->bytes})).kind()
                        == CollabText::ApplyOutcomeKind::applied
                    && peerAText->text() == peerBText->text()
                    && peerAText->text().startsWith("base")
                    && peerAText->text().contains("B")
                    && peerAText->text().contains("A"),
                "redo after a peer edit did not restore the local text while preserving the peer edit");

        QTemporaryDir failedEditDir;
        require(failedEditDir.isValid(),
                "could not create durable edit-failure fixture");
        auto durableProvider = take(makeReferenceDocumentStorageProvider(
            failedEditDir.path().toStdString()));
        const DocumentStorageScope failedEditScope{
            QStringLiteral("markoff-document-edit-failure"), 1};
        auto failedEditDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedEditScope, StorageMode::durable));
        const QByteArray failureSource = QByteArrayLiteral("stable\n");
        require(failedEditDocument->importInitialMarkdown(failureSource).state
                    == ActionPublicationState::committed,
                "could not import durable edit-failure fixture");
        const auto failureBlock = failedEditDocument->iterateBlocks().front();
        int tentativeChanges = 0;
        int tentativeReverts = 0;
        int tentativeCommits = 0;
        int providerStateChanges = 0;
        QObject::connect(failedEditDocument.get(),
                         &Markoff::MarkoffDocument::providerStateChanged,
                         [&] { ++providerStateChanges; });
        QObject::connect(failedEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockChanged,
                         [&](Markoff::BlockId) { ++tentativeChanges; });
        QObject::connect(failedEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockReverted,
                         [&](Markoff::BlockId) { ++tentativeReverts; });
        QObject::connect(failedEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockCommitted,
                         [&](Markoff::BlockId) { ++tentativeCommits; });
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
        const auto failedEdit = failedEditDocument->applyBlockEdit(
            {failureBlock, uint32_t(failureSource.size() - 1), 0,
             QByteArrayLiteral("! ")});
        require(failedEdit.state == ActionPublicationState::absent
                    && failedEditDocument->blockText(failureBlock)
                        == QByteArrayLiteral("stable")
                    && tentativeChanges == 1 && tentativeReverts == 1
                    && tentativeCommits == 0
                    && providerStateChanges == 1
                    && !failedEditDocument->canStartOrdinaryAction(),
                "definitely absent durable edit leaked tentative text or reopened writes");
        require(failedEditDocument->recoverPendingAllocations().ok()
                    && failedEditDocument->canStartOrdinaryAction()
                    && providerStateChanges == 2
                    && failedEditDocument->providerState() == ProviderDocumentState::ready
                    && failedEditDocument->blockText(failureBlock)
                        == QByteArrayLiteral("stable")
                    && failedEditDocument->applyBlockEdit(
                        {failureBlock, uint32_t(failureSource.size() - 1), 0,
                         QByteArrayLiteral("! ")}).state
                        == ActionPublicationState::committed
                    && failedEditDocument->blockText(failureBlock)
                        == QByteArrayLiteral("stable! "),
                "in-session recovery did not gap the absent edit before allowing retry");

        const DocumentStorageScope uncertainEditScope{
            QStringLiteral("markoff-document-edit-uncertain"), 1};
        auto uncertainEditDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainEditScope, StorageMode::durable));
        require(uncertainEditDocument->importInitialMarkdown(failureSource).state
                    == ActionPublicationState::committed,
                "could not import uncertain edit fixture");
        const auto uncertainBlock = uncertainEditDocument->iterateBlocks().front();
        int uncertainChanges = 0;
        int uncertainReverts = 0;
        int uncertainCommits = 0;
        QObject::connect(uncertainEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockChanged,
                         [&](Markoff::BlockId) { ++uncertainChanges; });
        QObject::connect(uncertainEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockReverted,
                         [&](Markoff::BlockId) { ++uncertainReverts; });
        QObject::connect(uncertainEditDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockCommitted,
                         [&](Markoff::BlockId) { ++uncertainCommits; });
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
        const auto uncertainEdit = uncertainEditDocument->applyBlockEdit(
            {uncertainBlock, uint32_t(failureSource.size() - 1), 0,
             QByteArrayLiteral("! ")});
        require(uncertainEdit.state == ActionPublicationState::uncertain
                    && uncertainEditDocument->blockText(uncertainBlock)
                        == QByteArrayLiteral("stable! ")
                    && uncertainChanges == 1 && uncertainReverts == 0
                    && uncertainCommits == 0
                    && !uncertainEditDocument->canStartOrdinaryAction(),
                "uncertain durable edit lost tentative state or allowed a follow-up action");
        uncertainEditDocument.reset();
        uncertainEditDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainEditScope, StorageMode::durable));
        require(uncertainEditDocument->providerState() == ProviderDocumentState::ready
                    && uncertainEditDocument->blockText(uncertainBlock)
                        == QByteArrayLiteral("stable! ")
                    && uncertainEditDocument->canUndoForBlock(uncertainBlock),
                "fresh durable open did not reconcile the uncertain edit and its undo history");

        const DocumentStorageScope failedUndoScope{
            QStringLiteral("markoff-document-undo-failure"), 1};
        auto failedUndoDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedUndoScope, StorageMode::durable));
        require(failedUndoDocument->importInitialMarkdown(failureSource).state
                    == ActionPublicationState::committed,
                "could not import durable undo-failure fixture");
        const auto undoBaseBlock = failedUndoDocument->iterateBlocks().front();
        const auto undoInsert = failedUndoDocument->applyStructural(
            {Markoff::StructuralOp::InsertEntry{undoBaseBlock,
                                                Markoff::BlockKind::ListItem}});
        const auto undoInsertedBlock = failedUndoDocument->iterateBlocks().back();
        require(undoInsert.state == ActionPublicationState::committed
                    && failedUndoDocument->blockKind(undoInsertedBlock)
                        == Markoff::BlockKind::ListItem,
                "could not create paired list/kind action for undo-failure fixture");
        int failedUndoTentative = 0;
        int failedUndoReverted = 0;
        int failedUndoCommitted = 0;
        QObject::connect(failedUndoDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockChanged,
                         [&](Markoff::BlockId) { ++failedUndoTentative; });
        QObject::connect(failedUndoDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockReverted,
                         [&](Markoff::BlockId) { ++failedUndoReverted; });
        QObject::connect(failedUndoDocument.get(),
                         &Markoff::MarkoffDocument::tentativeBlockCommitted,
                         [&](Markoff::BlockId) { ++failedUndoCommitted; });
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
        failedUndoDocument->undoD2();
        require(failedUndoDocument->iterateBlocks()
                        == std::vector<Markoff::BlockId>{undoBaseBlock, undoInsertedBlock}
                    && failedUndoDocument->blockKind(undoInsertedBlock)
                        == Markoff::BlockKind::ListItem
                    && failedUndoTentative == 1 && failedUndoReverted == 1
                    && failedUndoCommitted == 0
                    && !failedUndoDocument->canStartOrdinaryAction(),
                "failed cross-stream undo accepted only part of its list/kind compensation");

        const DocumentStorageScope failedUndoAllocationScope{
            QStringLiteral("markoff-document-undo-allocation-failure"), 1};
        auto failedUndoAllocation = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedUndoAllocationScope, StorageMode::durable));
        require(failedUndoAllocation->importInitialMarkdown(failureSource).state
                    == ActionPublicationState::committed,
                "could not import durable undo-allocation fixture");
        const auto allocationBase = failedUndoAllocation->iterateBlocks().front();
        require(failedUndoAllocation->applyStructural({
                    Markoff::StructuralOp::InsertEntry{allocationBase,
                        Markoff::BlockKind::ListItem}}).state
                    == ActionPublicationState::committed,
                "could not publish paired action for undo-allocation fixture");
        const auto allocationAfterInsert = failedUndoAllocation->iterateBlocks();
        const auto allocationInserted = std::find_if(
            allocationAfterInsert.begin(), allocationAfterInsert.end(),
            [&](Markoff::BlockId candidate) { return candidate != allocationBase; });
        require(allocationInserted != allocationAfterInsert.end(),
                "paired undo-allocation action has no inserted identity");
        const auto allocationInsertedBlock = *allocationInserted;
        Testing::failReferenceProviderCommitAfter(
            Testing::ReferenceProviderFaultPoint::reservationBeforeAtomicRename, 1);
        failedUndoAllocation->undoForBlock(allocationInsertedBlock);
        const auto allocationAfterUndo = failedUndoAllocation->iterateBlocks();
        require(allocationAfterUndo.size() == 2
                    && std::find(allocationAfterUndo.begin(), allocationAfterUndo.end(),
                        allocationInsertedBlock) != allocationAfterUndo.end()
                    && failedUndoAllocation->blockKind(allocationInsertedBlock)
                        == Markoff::BlockKind::ListItem
                    && failedUndoAllocation->providerState()
                        == ProviderDocumentState::recoveryRequired,
                "mid-compensation reservation failure did not roll back the whole undo");
        require(failedUndoAllocation->recoverPendingAllocations().ok()
                    && failedUndoAllocation->providerState() == ProviderDocumentState::ready
                    && failedUndoAllocation->canStartOrdinaryAction()
                    && failedUndoAllocation->iterateBlocks().size() == 2
                    && failedUndoAllocation->blockKind(allocationInsertedBlock)
                        == Markoff::BlockKind::ListItem,
                "in-session allocation recovery did not finalize gaps after rebuilding");
        failedUndoAllocation.reset();
        failedUndoAllocation = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedUndoAllocationScope, StorageMode::durable));
        require(failedUndoAllocation->providerState() == ProviderDocumentState::ready
                    && failedUndoAllocation->canStartOrdinaryAction()
                    && failedUndoAllocation->iterateBlocks().size() == 2
                    && failedUndoAllocation->blockKind(allocationInsertedBlock)
                        == Markoff::BlockKind::ListItem,
                "fresh MarkoffDocument open did not rebuild accepted state and finalize gaps");

        const DocumentStorageScope failedMixedUndoScope{
            QStringLiteral("markoff-document-mixed-undo-failure"), 1};
        auto failedMixedUndo = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedMixedUndoScope, StorageMode::durable));
        require(failedMixedUndo->importInitialMarkdown(
                    QByteArrayLiteral("alpha\n\nbeta\n\ngamma")).state
                    == ActionPublicationState::committed,
                "could not import mixed-undo fixture");
        const auto mixedUndoInitialBlocks = failedMixedUndo->iterateBlocks();
        require(mixedUndoInitialBlocks.size() == 3
                    && failedMixedUndo->applyBlockMerge(
                        {mixedUndoInitialBlocks[0], 5, 0, QByteArrayLiteral("!")},
                        {mixedUndoInitialBlocks[1]}).state
                        == ActionPublicationState::committed
                    && failedMixedUndo->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedUndoInitialBlocks[0],
                                                         mixedUndoInitialBlocks[2]}
                    && failedMixedUndo->blockText(mixedUndoInitialBlocks[0])
                        == QByteArrayLiteral("alpha!"),
                "could not publish mixed text/list/map merge action");
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
        failedMixedUndo->undoD2();
        require(failedMixedUndo->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedUndoInitialBlocks[0],
                                                         mixedUndoInitialBlocks[2]}
                    && failedMixedUndo->blockText(mixedUndoInitialBlocks[0])
                        == QByteArrayLiteral("alpha!")
                    && !failedMixedUndo->canStartOrdinaryAction(),
                "failed mixed text/list/map compensation accepted partial state");
        require(failedMixedUndo->recoverPendingAllocations().ok()
                    && failedMixedUndo->canStartOrdinaryAction()
                    && failedMixedUndo->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedUndoInitialBlocks[0],
                                                         mixedUndoInitialBlocks[2]}
                    && failedMixedUndo->blockText(mixedUndoInitialBlocks[0])
                        == QByteArrayLiteral("alpha!"),
                "recovery retained a partial mixed compensation");
        failedMixedUndo->undoD2();
        require(failedMixedUndo->iterateBlocks() == mixedUndoInitialBlocks
                    && failedMixedUndo->blockText(mixedUndoInitialBlocks[0])
                        == QByteArrayLiteral("alpha"),
                "retry after mixed compensation recovery did not restore all groups");
        failedMixedUndo->redoD2();
        require(failedMixedUndo->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedUndoInitialBlocks[0],
                                                         mixedUndoInitialBlocks[2]}
                    && failedMixedUndo->blockText(mixedUndoInitialBlocks[0])
                        == QByteArrayLiteral("alpha!")
                    && failedMixedUndo->blockKind(mixedUndoInitialBlocks[0])
                        == Markoff::BlockKind::Paragraph,
                "redo did not restore the complete mixed text/list/map action");

        const DocumentStorageScope failedMixedAllocationScope{
            QStringLiteral("markoff-document-mixed-undo-allocation-failure"), 1};
        auto failedMixedAllocation = take(Markoff::MarkoffDocument::open(
            *durableProvider, failedMixedAllocationScope, StorageMode::durable));
        require(failedMixedAllocation->importInitialMarkdown(
                    QByteArrayLiteral("alpha\n\nbeta\n\ngamma")).state
                    == ActionPublicationState::committed,
                "could not import mixed-allocation fixture");
        const auto mixedAllocationBlocks = failedMixedAllocation->iterateBlocks();
        require(mixedAllocationBlocks.size() == 3
                    && failedMixedAllocation->applyBlockMerge(
                        {mixedAllocationBlocks[0], 5, 0, QByteArrayLiteral("!")},
                        {mixedAllocationBlocks[1]}).state
                        == ActionPublicationState::committed,
                "could not publish mixed action for allocation fault");
        Testing::failReferenceProviderCommitAfter(
            Testing::ReferenceProviderFaultPoint::reservationBeforeAtomicRename, 1);
        failedMixedAllocation->undoD2();
        require(failedMixedAllocation->providerState()
                        == ProviderDocumentState::recoveryRequired
                    && failedMixedAllocation->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedAllocationBlocks[0],
                                                         mixedAllocationBlocks[2]}
                    && failedMixedAllocation->blockText(mixedAllocationBlocks[0])
                        == QByteArrayLiteral("alpha!"),
                "partial mixed compensation reservation changed accepted projection");
        require(failedMixedAllocation->recoverPendingAllocations().ok()
                    && failedMixedAllocation->providerState() == ProviderDocumentState::ready
                    && failedMixedAllocation->iterateBlocks()
                        == std::vector<Markoff::BlockId>{mixedAllocationBlocks[0],
                                                         mixedAllocationBlocks[2]}
                    && failedMixedAllocation->blockText(mixedAllocationBlocks[0])
                        == QByteArrayLiteral("alpha!"),
                "mixed compensation allocation recovery retained a partial action");
        failedMixedAllocation->undoD2();
        require(failedMixedAllocation->iterateBlocks() == mixedAllocationBlocks
                    && failedMixedAllocation->blockText(mixedAllocationBlocks[0])
                        == QByteArrayLiteral("alpha"),
                "mixed undo retry after partial reservation did not restore all groups");

        const DocumentStorageScope uncertainMixedUndoScope{
            QStringLiteral("markoff-document-mixed-undo-uncertain"), 1};
        auto uncertainMixedUndo = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainMixedUndoScope, StorageMode::durable));
        require(uncertainMixedUndo->importInitialMarkdown(
                    QByteArrayLiteral("alpha\n\nbeta\n\ngamma")).state
                    == ActionPublicationState::committed,
                "could not import uncertain mixed-undo fixture");
        const auto uncertainMixedInitialBlocks = uncertainMixedUndo->iterateBlocks();
        require(uncertainMixedInitialBlocks.size() == 3
                    && uncertainMixedUndo->applyBlockMerge(
                        {uncertainMixedInitialBlocks[0], 5, 0, QByteArrayLiteral("!")},
                        {uncertainMixedInitialBlocks[1]}).state
                        == ActionPublicationState::committed,
                "could not publish uncertain mixed-undo action");
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
        uncertainMixedUndo->undoD2();
        require(!uncertainMixedUndo->canStartOrdinaryAction(),
                "uncertain mixed compensation left ordinary writes enabled");
        uncertainMixedUndo.reset();
        uncertainMixedUndo = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainMixedUndoScope, StorageMode::durable));
        require(uncertainMixedUndo->canStartOrdinaryAction()
                    && uncertainMixedUndo->iterateBlocks() == uncertainMixedInitialBlocks
                    && uncertainMixedUndo->blockText(uncertainMixedInitialBlocks[0])
                        == QByteArrayLiteral("alpha"),
                "fresh open did not resolve uncertain mixed compensation from accepted replay");

        const DocumentStorageScope selectiveMixedScope{
            QStringLiteral("markoff-document-selective-mixed-action"), 1};
        auto selectiveMixed = take(Markoff::MarkoffDocument::open(
            *durableProvider, selectiveMixedScope, StorageMode::durable));
        require(selectiveMixed->importInitialMarkdown(
                    QByteArrayLiteral("alpha\n\nbeta")).state
                    == ActionPublicationState::committed,
                "could not import selective mixed-action fixture");
        const auto selectiveMixedBlocks = selectiveMixed->iterateBlocks();
        require(selectiveMixedBlocks.size() == 2
                    && selectiveMixed->applyBlockMerge(
                        {selectiveMixedBlocks[0], 5, 0, QByteArrayLiteral("!")},
                        {selectiveMixedBlocks[1]}).state
                        == ActionPublicationState::committed,
                "could not publish text/list/map mixed edit action");
        const auto survivorSequenceBeforeUndo =
            selectiveMixed->blockEditSequence(selectiveMixedBlocks[0]);
        const auto removedSequenceBeforeUndo =
            selectiveMixed->blockEditSequence(selectiveMixedBlocks[1]);
        const auto structureSequenceBeforeUndo = selectiveMixed->structuralEditSequence();
        selectiveMixed->undoForBlock(selectiveMixedBlocks[0]);
        require(selectiveMixed->iterateBlocks() == selectiveMixedBlocks
                    && selectiveMixed->blockText(selectiveMixedBlocks[0])
                        == QByteArrayLiteral("alpha")
                    && selectiveMixed->blockText(selectiveMixedBlocks[1])
                        == QByteArrayLiteral("beta")
                    && selectiveMixed->blockKind(selectiveMixedBlocks[1])
                        == Markoff::BlockKind::Paragraph,
                "selective undo did not compensate every group in the mixed action");
        require(selectiveMixed->blockEditSequence(selectiveMixedBlocks[0])
                    > survivorSequenceBeforeUndo
                    && selectiveMixed->blockEditSequence(selectiveMixedBlocks[1])
                        > removedSequenceBeforeUndo
                    && selectiveMixed->structuralEditSequence() > structureSequenceBeforeUndo,
                "selective mixed undo did not advance touched block and structure sequences");
        selectiveMixed.reset();
        selectiveMixed = take(Markoff::MarkoffDocument::open(
            *durableProvider, selectiveMixedScope, StorageMode::durable));
        const auto survivorSequenceBeforeRedo =
            selectiveMixed->blockEditSequence(selectiveMixedBlocks[0]);
        const auto removedSequenceBeforeRedo =
            selectiveMixed->blockEditSequence(selectiveMixedBlocks[1]);
        const auto structureSequenceBeforeRedo = selectiveMixed->structuralEditSequence();
        selectiveMixed->redoForBlock(selectiveMixedBlocks[0]);
        const auto afterSelectiveMixedRedo = selectiveMixed->iterateBlocks();
        require(selectiveMixed->iterateBlocks().size() == 1
                    && selectiveMixed->blockText(selectiveMixedBlocks[0])
                        == QByteArrayLiteral("alpha!")
                    && std::find(afterSelectiveMixedRedo.begin(),
                        afterSelectiveMixedRedo.end(), selectiveMixedBlocks[1])
                        == afterSelectiveMixedRedo.end(),
                "selective redo did not restore every group in the mixed action");
        require(selectiveMixed->blockEditSequence(selectiveMixedBlocks[0])
                    > survivorSequenceBeforeRedo
                    && selectiveMixed->blockEditSequence(selectiveMixedBlocks[1])
                        > removedSequenceBeforeRedo
                    && selectiveMixed->structuralEditSequence() > structureSequenceBeforeRedo,
                "selective mixed redo did not advance touched block and structure sequences");

        const DocumentStorageScope absentReplacementScope{
            QStringLiteral("markoff-document-replacement-absent"), 1};
        auto absentReplacement = take(Markoff::MarkoffDocument::open(
            *durableProvider, absentReplacementScope, StorageMode::durable));
        const QByteArray beforeReplacement = QByteArrayLiteral(
            "# Before\n\nOriginal paragraph\n");
        const QByteArray rejectedReplacement = QByteArrayLiteral(
            "# After\n\nReplacement paragraph\n");
        require(absentReplacement->importInitialMarkdown(beforeReplacement).state
                    == ActionPublicationState::committed,
                "could not create absent replacement fixture");
        const auto beforeReplacementIds = absentReplacement->iterateBlocks();
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
        const auto absentReplacementResult = absentReplacement->replaceMarkdown(
            rejectedReplacement);
        require(absentReplacementResult.state == ActionPublicationState::absent
                    && absentReplacement->iterateBlocks() == beforeReplacementIds
                    && take(absentReplacement->snapshotForExport()).markdown
                        == beforeReplacement,
                "definitely absent replacement changed accepted source or identities");
        absentReplacement.reset();
        absentReplacement = take(Markoff::MarkoffDocument::open(
            *durableProvider, absentReplacementScope, StorageMode::durable));
        require(absentReplacement->iterateBlocks() == beforeReplacementIds
                    && take(absentReplacement->snapshotForExport()).markdown
                        == beforeReplacement,
                "fresh open did not recover the old source after absent replacement");

        const DocumentStorageScope uncertainReplacementScope{
            QStringLiteral("markoff-document-replacement-uncertain"), 1};
        auto uncertainReplacement = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainReplacementScope, StorageMode::durable));
        require(uncertainReplacement->importInitialMarkdown(beforeReplacement).state
                    == ActionPublicationState::committed,
                "could not create uncertain replacement fixture");
        Testing::failNextReferenceProviderCommitAt(
            Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename);
        const auto uncertainReplacementResult = uncertainReplacement->replaceMarkdown(
            rejectedReplacement);
        require(uncertainReplacementResult.state == ActionPublicationState::uncertain
                    && !uncertainReplacement->canStartOrdinaryAction(),
                "uncertain replacement did not close the ordinary-action barrier");
        uncertainReplacement.reset();
        uncertainReplacement = take(Markoff::MarkoffDocument::open(
            *durableProvider, uncertainReplacementScope, StorageMode::durable));
        require(uncertainReplacement->canStartOrdinaryAction()
                    && take(uncertainReplacement->snapshotForExport()).markdown
                        == rejectedReplacement
                    && uncertainReplacement->iterateBlocks() != beforeReplacementIds,
                "fresh open did not resolve uncertain replacement from accepted replay");

        const DocumentStorageScope outboxScope{
            QStringLiteral("markoff-document-transport-outbox"), 1};
        auto outboxDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, outboxScope, StorageMode::durable));
        require(outboxDocument->importInitialMarkdown(QByteArrayLiteral("outbox")).state
                    == ActionPublicationState::committed,
                "could not create durable transport outbox fixture");
        auto pendingOutbox = take(outboxDocument->pendingOutboundActions());
        require(pendingOutbox.size() == 1
                    && pendingOutbox.front().mabBytes.startsWith("MAB")
                    && pendingOutbox.front().localRevision
                        == take(outboxDocument->snapshotForExport()).revision,
                "local initial import was not exposed as a pending outbound action");
        const auto outboxBlock = outboxDocument->iterateBlocks().front();
        require(outboxDocument->applyBlockEdit({outboxBlock, 6, 0,
                    QByteArrayLiteral("!")}).state == ActionPublicationState::committed,
                "could not append a second outbox action");
        pendingOutbox = take(outboxDocument->pendingOutboundActions());
        require(pendingOutbox.size() == 2,
                "unacknowledged local actions were not retained together");
        outboxDocument.reset();
        outboxDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, outboxScope, StorageMode::durable));
        pendingOutbox = take(outboxDocument->pendingOutboundActions());
        require(pendingOutbox.size() == 2,
                "unacknowledged actions were lost when reopening before receipt");
        const auto firstOutbound = pendingOutbox.front();
        const auto secondOutbound = pendingOutbox.back();
        take(outboxDocument->acknowledgeOutboundActionReceipt(
            firstOutbound.actionId, QByteArrayLiteral("peer-revision-1")));
        take(outboxDocument->acknowledgeOutboundActionReceipt(
            firstOutbound.actionId, QByteArrayLiteral("peer-revision-1")));
        const auto changedReceipt = outboxDocument->acknowledgeOutboundActionReceipt(
            firstOutbound.actionId, QByteArrayLiteral("different-peer-revision"));
        require(!changedReceipt.ok()
                    && take(outboxDocument->pendingOutboundActions()).size() == 1,
                "outbox receipt retry was not idempotent or accepted changed receipt bytes");
        outboxDocument.reset();
        outboxDocument = take(Markoff::MarkoffDocument::open(
            *durableProvider, outboxScope, StorageMode::durable));
        pendingOutbox = take(outboxDocument->pendingOutboundActions());
        require(pendingOutbox == std::vector<PendingOutboundAction>{secondOutbound},
                "transport outbox did not survive provider reopen with exact action bytes");
        take(outboxDocument->acknowledgeOutboundActionReceipt(
            secondOutbound.actionId, QByteArrayLiteral("peer-revision-2")));
        require(take(outboxDocument->pendingOutboundActions()).empty(),
                "acknowledged actions remained in the durable transport outbox");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
