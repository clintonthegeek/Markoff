// SPDX-License-Identifier: GPL-3.0-or-later
//
// Checks a DocumentStorageProvider must pass. The reference provider is the
// known-good implementation. A host author can point main() at another
// provider without reading Markoff's document implementation. Opaque record
// bytes are supplied by the caller. This file does not decode them.

#include <markoff/vnext/DocumentStorageProvider.h>

#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace Markoff::VNext;
using namespace CollabText;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T take(Result<T> result)
{
    require(result.ok(), "provider call failed");
    return std::move(result).value();
}

void take(Result<void> result)
{
    require(result.ok(), "provider call failed");
}

PreparedStorageTransaction localAction(QByteArray id, QByteArray bytes,
                                       AcceptedRevision expected)
{
    PreparedStorageTransaction transaction;
    transaction.transactionId = std::move(id);
    transaction.expectedRevision = std::move(expected);
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = std::move(bytes);
    return transaction;
}

void runProviderInterfaceConformance(DocumentStorageProvider& provider)
{
    const DocumentStorageScope scope{QStringLiteral("provider-interface-conformance"), 1};
    auto session = take(provider.open(scope, StorageMode::durable));

    const QByteArray firstBytes("exact-local-mab-1");
    const QByteArray secondBytes("exact-local-mab-2");
    auto first = localAction(QByteArrayLiteral("local-1"), firstBytes,
                             take(session->readRecoveryState()).acceptedRevision);
    const auto firstCommit = session->append(first.transactionId);
    require(firstCommit.disposition == PublicationDisposition::rejected,
            "append before prepare was treated as committed");
    take(session->prepare(first));
    const auto firstAccepted = session->append(first.transactionId);
    require(firstAccepted.disposition == PublicationDisposition::committed
                && firstAccepted.committedRevision,
            "local action did not commit");

    auto second = localAction(QByteArrayLiteral("local-2"), secondBytes,
                              *firstAccepted.committedRevision);
    take(session->prepare(second));
    const auto secondAccepted = session->append(second.transactionId);
    require(secondAccepted.disposition == PublicationDisposition::committed,
            "second local action did not commit");

    const auto expectedOutbox = std::vector<PendingOutboundAction>{
        {first.transactionId, firstBytes, *firstAccepted.committedRevision},
        {second.transactionId, secondBytes, *secondAccepted.committedRevision},
    };
    require(take(session->readRecoveryState()).pendingOutboundActions == expectedOutbox,
            "pending outbox did not retain the exact local MAB bytes");

    session.reset();
    session = take(provider.open(scope, StorageMode::durable));
    require(take(session->readRecoveryState()).pendingOutboundActions == expectedOutbox,
            "reopen did not replay the unacknowledged outbox");

    take(session->acknowledgeTransportReceipt(
        first.transactionId, QByteArrayLiteral("peer-revision-1")));
    take(session->acknowledgeTransportReceipt(
        first.transactionId, QByteArrayLiteral("peer-revision-1")));
    const auto changedReceipt = session->acknowledgeTransportReceipt(
        first.transactionId, QByteArrayLiteral("different-peer-revision"));
    require(!changedReceipt.ok()
                && changedReceipt.error().code() == ErrorCode::pin_conflict,
            "a changed receipt for an acknowledged action was accepted");
    const auto afterFirstReceipt = std::vector<PendingOutboundAction>{
        expectedOutbox.back()};
    require(take(session->readRecoveryState()).pendingOutboundActions == afterFirstReceipt,
            "receipt acknowledgment did not clear exactly one outbox action");

    session.reset();
    session = take(provider.open(scope, StorageMode::durable));
    require(take(session->readRecoveryState()).pendingOutboundActions == afterFirstReceipt,
            "reconnect lost the remaining exact outbox bytes");
    take(session->acknowledgeTransportReceipt(
        second.transactionId, QByteArrayLiteral("peer-revision-2")));
    require(take(session->readRecoveryState()).pendingOutboundActions.empty(),
            "acknowledged action remained in the outbox");

    std::array<uint8_t, ActorId::kBytes> peerBytes{};
    peerBytes.back() = 0x44;
    const ActorId peer = ActorId::from_bytes(peerBytes);
    require(peer.valid() && peer != session->actor(),
            "conformance peer did not use an independent actor");
    const QByteArray remoteBytes("exact-remote-mab");
    PreparedStorageTransaction remote;
    remote.transactionId = QByteArrayLiteral("remote-1");
    remote.expectedRevision = take(session->readRecoveryState()).acceptedRevision;
    remote.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    remote.sharedRecord = remoteBytes;
    remote.operationIdentities.push_back(
        {QStringLiteral("markoff/block-text/v1/peer"), peer, 1});
    remote.remoteAcceptance = true;
    const auto remoteAccepted = session->acceptRemoteAction(remote);
    require(remoteAccepted.disposition == PublicationDisposition::committed,
            "provider did not accept a remote action");
    require(take(session->readRecoveryState()).pendingOutboundActions.empty(),
            "a remote accepted action entered the local outbox");

    QByteArray changedRemote = remoteBytes;
    changedRemote.append("-changed");
    auto rebound = remote;
    rebound.sharedRecord = changedRemote;
    const auto reboundResult = session->acceptRemoteAction(rebound);
    require(reboundResult.disposition == PublicationDisposition::rejected
                && reboundResult.diagnostic
                && reboundResult.diagnostic->code() == ErrorCode::pin_conflict
                && changedRemote.endsWith("-changed"),
            "changed duplicate replaced the accepted remote bytes or the caller envelope");
    const auto afterReject = take(session->readRecoveryState());
    const auto stillOriginal = std::find_if(
        afterReject.acceptedRecords.begin(), afterReject.acceptedRecords.end(),
        [&](const AcceptedStorageRecord& record) {
            return record.transactionId == remote.transactionId;
        });
    require(stillOriginal != afterReject.acceptedRecords.end()
                && stillOriginal->bytes == remoteBytes,
            "rejected duplicate changed the accepted remote record");

    take(session->storeQuarantinedOperation(changedRemote));
    take(session->storeQuarantinedOperation(changedRemote));
    session.reset();
    session = take(provider.open(scope, StorageMode::durable));
    require(take(session->readRecoveryState()).quarantinedOperations
                == std::vector<QByteArray>{changedRemote},
            "quarantine did not survive reopen with the exact rejected envelope");

    QByteArray refused(26 * 1024 * 1024 + 1, 'Q');
    const auto refusedStore = session->storeQuarantinedOperation(refused);
    require(!refusedStore.ok()
                && refused.size() == 26 * 1024 * 1024 + 1
                && refused.front() == 'Q'
                && take(session->readRecoveryState()).quarantinedOperations
                    == std::vector<QByteArray>{changedRemote},
            "rejected quarantine write took the envelope from the caller");
}

} // namespace

int main()
{
    QTemporaryDir directory;
    try {
        require(directory.isValid(), "could not create conformance storage root");
        auto provider = take(makeReferenceDocumentStorageProvider(
            std::filesystem::path(directory.path().toStdString())));
        runProviderInterfaceConformance(*provider);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
