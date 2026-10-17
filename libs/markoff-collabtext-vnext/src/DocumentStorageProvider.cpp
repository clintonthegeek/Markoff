// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/DocumentStorageProvider.h>
#include "ReferenceProviderTestHooks.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QCryptographicHash>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Markoff::VNext {

namespace Testing {
namespace {
std::atomic<ReferenceProviderFaultPoint> g_referenceProviderFault{
    ReferenceProviderFaultPoint::none};
std::atomic<unsigned> g_referenceProviderCommitsToSkip{0};
std::atomic<bool> g_failNextHistoryRetirement{false};
}

void failNextReferenceProviderCommitAt(ReferenceProviderFaultPoint point) noexcept
{
    g_referenceProviderCommitsToSkip.store(0, std::memory_order_release);
    g_referenceProviderFault.store(point, std::memory_order_release);
}

void failReferenceProviderCommitAfter(ReferenceProviderFaultPoint point,
                                      unsigned matchingCommitsToSkip) noexcept
{
    g_referenceProviderCommitsToSkip.store(matchingCommitsToSkip,
                                           std::memory_order_release);
    g_referenceProviderFault.store(point, std::memory_order_release);
}

void failNextHistoryRetirement() noexcept
{
    g_failNextHistoryRetirement.store(true, std::memory_order_release);
}

bool consumeHistoryRetirementFailure() noexcept
{
    return g_failNextHistoryRetirement.exchange(false, std::memory_order_acq_rel);
}

bool consumeReferenceProviderFault(ReferenceProviderFaultPoint point) noexcept
{
    if (g_referenceProviderFault.load(std::memory_order_acquire) != point)
        return false;
    auto skip = g_referenceProviderCommitsToSkip.load(std::memory_order_acquire);
    while (skip != 0) {
        if (g_referenceProviderCommitsToSkip.compare_exchange_weak(
                skip, skip - 1, std::memory_order_acq_rel,
                std::memory_order_acquire))
            return false;
    }
    auto expected = point;
    return g_referenceProviderFault.compare_exchange_strong(
        expected, ReferenceProviderFaultPoint::none,
        std::memory_order_acq_rel, std::memory_order_acquire);
}
} // namespace Testing

DocumentStorageSession::~DocumentStorageSession() = default;
DocumentStorageProvider::~DocumentStorageProvider() = default;

namespace {

using CollabText::Error;
using CollabText::ErrorCode;

bool validScope(const DocumentStorageScope& scope)
{
    if (scope.documentKey.isEmpty() || scope.epoch == 0)
        return false;
    const auto utf8 = scope.documentKey.toUtf8();
    return utf8.size() <= 256 && QString::fromUtf8(utf8) == scope.documentKey;
}

QString documentLeaseKey(const DocumentStorageScope& scope)
{
    // The service lease is document-wide, not epoch-wide. A transition to a
    // new epoch cannot open a second writer while an old epoch remains active.
    return scope.documentKey;
}

QString documentStateKey(const DocumentStorageScope& scope)
{
    const QByteArray key = scope.documentKey.toUtf8();
    return QString::number(key.size()) + QLatin1Char(':') + scope.documentKey
        + QLatin1Char(':') + QString::number(scope.epoch);
}

struct EphemeralDocument {
    AcceptedRevision revision{QByteArrayLiteral("ephemeral:0")};
    QByteArray revisionPrefix = QByteArrayLiteral("ephemeral");
    quint64 revisionNumber = 0;
    std::vector<AcceptedStorageRecord> accepted;
    std::vector<PreparedStorageTransaction> prepared;
    std::set<QByteArray> resolvedAbsentTransactions;
    std::map<QByteArray, PreparedStorageTransaction> transactionsById;
    std::vector<ScopedReservation> reservations;
    std::optional<PrivateHistoryRecord> localHistory;
    quint64 localHistoryRevisionNumber = 0;
    std::optional<AcceptedRevision> exportedRevision;
    quint64 exportedRevisionNumber = 0;
    std::map<QString, quint64> counters;
    std::map<QByteArray, QByteArray> identityOwners;
    std::map<QByteArray, AcceptedRevision> transactionRevisions;
    std::map<QByteArray, PrivateHistoryRecord> completedHistories;
    std::optional<PendingImportIntent> initialImport;
    std::vector<QByteArray> quarantinedOperations;
    std::map<QByteArray, QByteArray> transportReceipts;
};

struct EphemeralState {
    CollabText::ActorId actor;
    std::shared_ptr<CollabText::ReplicaState> identityState;
    QString referenceRoot;
    bool durable = false;
    std::mutex mutex;
    std::set<QString> activeDocuments;
    std::map<QString, EphemeralDocument> documents;
};

bool samePrepared(const PreparedStorageTransaction& a,
                  const PreparedStorageTransaction& b)
{
    if (a.transactionId != b.transactionId
        || a.expectedRevision != b.expectedRevision
        || a.sharedRecordFormat != b.sharedRecordFormat
        || a.sharedRecord != b.sharedRecord
        || a.reservations != b.reservations
        || a.operationIdentities != b.operationIdentities
        || a.localHistoryAfterCommit != b.localHistoryAfterCommit
        || a.remoteAcceptance != b.remoteAcceptance)
        return false;
    return true;
}

bool sameClaim(const OperationIdentityClaim& a, const OperationIdentityClaim& b)
{
    return a.stream == b.stream && a.actor == b.actor && a.counter == b.counter;
}

QByteArray identityKey(const OperationIdentityClaim& claim)
{
    const auto actor = claim.actor.bytes();
    const QByteArray stream = claim.stream.toUtf8();
    QByteArray key;
    const quint32 streamSize = static_cast<quint32>(stream.size());
    for (int i = 3; i >= 0; --i)
        key.append(static_cast<char>(streamSize >> (i * 8)));
    key.append(stream);
    key.append(reinterpret_cast<const char*>(actor.data()),
               static_cast<qsizetype>(actor.size()));
    for (int i = 7; i >= 0; --i)
        key.append(static_cast<char>(claim.counter >> (i * 8)));
    return key;
}

constexpr quint32 kMaxStoredItems = 500'000;
constexpr qint64 kMaxReferenceFileSize = 256 * 1024 * 1024;

AcceptedRevision makeRevision(const EphemeralDocument& document, quint64 number)
{
    return {document.revisionPrefix + ':' + QByteArray::number(number)};
}

bool parseRevision(const QByteArray& token, const QByteArray& prefix,
                   quint64& number)
{
    const QByteArray start = prefix + ':';
    if (!token.startsWith(start) || token.size() == start.size()) return false;
    bool ok = false;
    number = token.mid(start.size()).toULongLong(&ok);
    return ok && makeRevision(EphemeralDocument{.revisionPrefix = prefix}, number).token
        == token;
}

bool validUtf8(const QString& value, qsizetype maxBytes)
{
    const QByteArray utf8 = value.toUtf8();
    return !value.isEmpty() && utf8.size() <= maxBytes
        && QString::fromUtf8(utf8) == value;
}

void writeActor(QDataStream& out, const CollabText::ActorId& actor)
{
    const auto bytes = actor.bytes();
    out << QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qsizetype>(bytes.size()));
}

bool readActor(QDataStream& in, CollabText::ActorId& actor)
{
    QByteArray bytes;
    in >> bytes;
    if (in.status() != QDataStream::Ok || bytes.size() != 16) return false;
    std::array<uint8_t, CollabText::ActorId::kBytes> raw{};
    std::copy(bytes.begin(), bytes.end(), raw.begin());
    actor = CollabText::ActorId::from_bytes(raw);
    return actor.valid();
}

void writeReservation(QDataStream& out, const ScopedReservation& reservation)
{
    out << reservation.stream;
    writeActor(out, reservation.actor);
    out << reservation.first << reservation.count;
}

bool readReservation(QDataStream& in, ScopedReservation& reservation)
{
    in >> reservation.stream;
    return in.status() == QDataStream::Ok && readActor(in, reservation.actor)
        && (in >> reservation.first >> reservation.count).status() == QDataStream::Ok;
}

void writeClaim(QDataStream& out, const OperationIdentityClaim& claim)
{
    out << claim.stream;
    writeActor(out, claim.actor);
    out << claim.counter;
}

bool readClaim(QDataStream& in, OperationIdentityClaim& claim)
{
    in >> claim.stream;
    return in.status() == QDataStream::Ok && readActor(in, claim.actor)
        && (in >> claim.counter).status() == QDataStream::Ok;
}

void writePrepared(QDataStream& out, const PreparedStorageTransaction& transaction)
{
    out << transaction.transactionId << transaction.expectedRevision.token
        << transaction.sharedRecordFormat << transaction.sharedRecord
        << quint32(transaction.reservations.size());
    for (const auto& reservation : transaction.reservations)
        writeReservation(out, reservation);
    out << quint32(transaction.operationIdentities.size());
    for (const auto& claim : transaction.operationIdentities)
        writeClaim(out, claim);
    out << transaction.localHistoryAfterCommit << quint8(transaction.remoteAcceptance);
}

bool readCount(QDataStream& in, quint32& count)
{
    in >> count;
    return in.status() == QDataStream::Ok && count <= kMaxStoredItems;
}

bool readPrepared(QDataStream& in, PreparedStorageTransaction& transaction)
{
    in >> transaction.transactionId >> transaction.expectedRevision.token
       >> transaction.sharedRecordFormat >> transaction.sharedRecord;
    quint32 reservationCount = 0;
    if (in.status() != QDataStream::Ok || !readCount(in, reservationCount)) return false;
    transaction.reservations.resize(reservationCount);
    for (auto& reservation : transaction.reservations)
        if (!readReservation(in, reservation)) return false;
    quint32 claimCount = 0;
    if (!readCount(in, claimCount)) return false;
    transaction.operationIdentities.resize(claimCount);
    for (auto& claim : transaction.operationIdentities)
        if (!readClaim(in, claim)) return false;
    quint8 remoteAcceptance = 0;
    in >> transaction.localHistoryAfterCommit >> remoteAcceptance;
    if (in.status() != QDataStream::Ok || remoteAcceptance > 1) return false;
    transaction.remoteAcceptance = remoteAcceptance != 0;
    return true;
}

void writePrivateHistory(QDataStream& out, const PrivateHistoryRecord& record)
{
    out << record.committedTransactionId << record.acceptedRevision.token << record.bytes;
}

void readPrivateHistory(QDataStream& in, PrivateHistoryRecord& record)
{
    in >> record.committedTransactionId >> record.acceptedRevision.token >> record.bytes;
}

CollabText::Result<QByteArray> encodeReferenceDocument(
    const DocumentStorageScope& scope, const EphemeralDocument& document,
    const CollabText::ActorId& actor)
{
    if (document.accepted.size() > kMaxStoredItems
        || document.transactionsById.size() > kMaxStoredItems
        || document.prepared.size() > kMaxStoredItems
        || document.resolvedAbsentTransactions.size() > kMaxStoredItems
        || document.reservations.size() > kMaxStoredItems
        || document.counters.size() > kMaxStoredItems
        || document.completedHistories.size() > kMaxStoredItems
        || document.quarantinedOperations.size() > kMaxStoredItems
        || document.transportReceipts.size() > kMaxStoredItems)
        return Error(ErrorCode::limit_exceeded,
                     "reference provider state has too many entries");
    for (const auto& [transactionId, transaction] : document.transactionsById) {
        Q_UNUSED(transactionId)
        if (transaction.reservations.size() > 4096
            || transaction.operationIdentities.size() > 65'536)
            return Error(ErrorCode::limit_exceeded,
                         "reference transaction exceeds its entry limits");
    }
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_8);
    out.setByteOrder(QDataStream::BigEndian);
    out << QByteArrayLiteral("MREF") << quint16(5) << scope.documentKey << scope.epoch;
    writeActor(out, actor);
    out
        << document.revisionNumber << quint32(document.accepted.size());
    for (const auto& record : document.accepted)
        out << record.transactionId << record.format << record.bytes;
    out << quint32(document.transactionsById.size());
    for (const auto& [transactionId, transaction] : document.transactionsById) {
        Q_UNUSED(transactionId)
        writePrepared(out, transaction);
    }
    out << quint32(document.prepared.size());
    for (const auto& transaction : document.prepared)
        out << transaction.transactionId;
    out << quint32(document.resolvedAbsentTransactions.size());
    for (const auto& transactionId : document.resolvedAbsentTransactions)
        out << transactionId;
    out << quint32(document.reservations.size());
    for (const auto& reservation : document.reservations)
        writeReservation(out, reservation);
    out << quint32(document.counters.size());
    for (const auto& [stream, next] : document.counters)
        out << stream << next;
    out << quint8(document.localHistory.has_value());
    if (document.localHistory) writePrivateHistory(out, *document.localHistory);
    out << document.localHistoryRevisionNumber
        << quint8(document.exportedRevision.has_value())
        << document.exportedRevisionNumber
        << quint32(document.completedHistories.size());
    for (const auto& [transactionId, record] : document.completedHistories) {
        Q_UNUSED(transactionId)
        writePrivateHistory(out, record);
    }
    out << quint8(document.initialImport.has_value());
    if (document.initialImport)
        out << document.initialImport->transactionId << document.initialImport->sourceMarkdown;
    out << quint32(document.quarantinedOperations.size());
    for (const auto& record : document.quarantinedOperations) out << record;
    out << quint32(document.transportReceipts.size());
    for (const auto& [actionId, revision] : document.transportReceipts)
        out << actionId << revision;
    if (out.status() != QDataStream::Ok)
        return Error(ErrorCode::io_error, "could not encode reference provider state");
    if (bytes.size() > kMaxReferenceFileSize)
        return Error(ErrorCode::limit_exceeded,
                     "reference provider state exceeds its file size limit");
    return bytes;
}

CollabText::Result<EphemeralDocument> decodeReferenceDocument(
    const QByteArray& bytes, const DocumentStorageScope& expectedScope,
    const CollabText::ActorId& actor)
{
    if (bytes.isEmpty() || bytes.size() > kMaxReferenceFileSize)
        return Error(ErrorCode::corrupt_record,
                     "reference provider state has an invalid file size");
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_8);
    in.setByteOrder(QDataStream::BigEndian);
    QByteArray magic;
    quint16 version = 0;
    DocumentStorageScope storedScope;
    CollabText::ActorId storedActor;
    EphemeralDocument document;
    document.revisionPrefix = QByteArrayLiteral("reference");
    in >> magic >> version >> storedScope.documentKey >> storedScope.epoch;
    const bool actorRead = readActor(in, storedActor);
    in >> document.revisionNumber;
    quint32 count = 0;
    if (in.status() != QDataStream::Ok || magic != QByteArrayLiteral("MREF")
        || version != 5 || storedScope != expectedScope || !actorRead
        || storedActor != actor
        || !readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider state header or scope is invalid");

    document.accepted.resize(count);
    for (auto& record : document.accepted)
        in >> record.transactionId >> record.format >> record.bytes;
    if (in.status() != QDataStream::Ok || !readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider accepted prefix is truncated");
    for (quint32 i = 0; i < count; ++i) {
        PreparedStorageTransaction transaction;
        if (!readPrepared(in, transaction) || transaction.transactionId.isEmpty()
            || document.transactionsById.contains(transaction.transactionId))
            return Error(ErrorCode::corrupt_record,
                         "reference provider transaction table is invalid");
        document.transactionsById.emplace(transaction.transactionId,
                                           std::move(transaction));
    }
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider preparation table is invalid");
    for (quint32 i = 0; i < count; ++i) {
        QByteArray transactionId;
        in >> transactionId;
        const auto found = document.transactionsById.find(transactionId);
        if (in.status() != QDataStream::Ok || found == document.transactionsById.end())
            return Error(ErrorCode::corrupt_record,
                         "reference provider preparation has no transaction metadata");
        document.prepared.push_back(found->second);
    }
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider resolved-absence table is invalid");
    for (quint32 i = 0; i < count; ++i) {
        QByteArray transactionId;
        in >> transactionId;
        if (in.status() != QDataStream::Ok || transactionId.isEmpty()
            || !document.resolvedAbsentTransactions.insert(transactionId).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider resolved-absence entry is invalid");
    }
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider reservation table is invalid");
    document.reservations.resize(count);
    for (auto& reservation : document.reservations)
        if (!readReservation(in, reservation))
            return Error(ErrorCode::corrupt_record,
                         "reference provider reservation is truncated");
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider counter table is invalid");
    for (quint32 i = 0; i < count; ++i) {
        QString stream;
        quint64 next = 0;
        in >> stream >> next;
        if (in.status() != QDataStream::Ok || !validUtf8(stream, 256)
            || next == 0 || !document.counters.emplace(stream, next).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider counter table has invalid entries");
    }
    quint8 hasHistory = 0;
    in >> hasHistory;
    if (in.status() != QDataStream::Ok || hasHistory > 1)
        return Error(ErrorCode::corrupt_record,
                     "reference provider private history flag is invalid");
    if (hasHistory) {
        PrivateHistoryRecord record;
        readPrivateHistory(in, record);
        if (in.status() != QDataStream::Ok)
            return Error(ErrorCode::corrupt_record,
                         "reference provider private history is truncated");
        document.localHistory = std::move(record);
    }
    quint8 hasExport = 0;
    in >> document.localHistoryRevisionNumber >> hasExport
       >> document.exportedRevisionNumber;
    if (in.status() != QDataStream::Ok || hasExport > 1)
        return Error(ErrorCode::corrupt_record,
                     "reference provider revision metadata is invalid");
    if (hasExport)
        document.exportedRevision = makeRevision(document, document.exportedRevisionNumber);
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider completed-history table is invalid");
    for (quint32 i = 0; i < count; ++i) {
        PrivateHistoryRecord record;
        readPrivateHistory(in, record);
        if (in.status() != QDataStream::Ok || record.committedTransactionId.isEmpty()
            || !document.completedHistories.emplace(
                    record.committedTransactionId, record).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider completed-history entry is invalid");
    }
    quint8 hasImport = 0;
    in >> hasImport;
    if (in.status() != QDataStream::Ok || hasImport > 1)
        return Error(ErrorCode::corrupt_record,
                     "reference provider import flag is invalid");
    if (hasImport) {
        PendingImportIntent intent;
        in >> intent.transactionId >> intent.sourceMarkdown;
        if (in.status() != QDataStream::Ok || intent.transactionId.isEmpty()
            || intent.transactionId.size() > 256
            || intent.sourceMarkdown.size() > 16 * 1024 * 1024)
            return Error(ErrorCode::corrupt_record,
                         "reference provider import intent is invalid");
        document.initialImport = std::move(intent);
    }
    if (!readCount(in, count))
        return Error(ErrorCode::corrupt_record,
                     "reference provider quarantine table is invalid");
    document.quarantinedOperations.resize(count);
    std::set<QByteArray> uniqueQuarantineRecords;
    for (auto& record : document.quarantinedOperations) {
        in >> record;
        if (in.status() != QDataStream::Ok || record.isEmpty()
            || record.size() > 26 * 1024 * 1024
            || !uniqueQuarantineRecords.insert(record).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider quarantine record is invalid");
    }
    if (version >= 5) {
        if (!readCount(in, count))
            return Error(ErrorCode::corrupt_record,
                         "reference provider transport receipt table is invalid");
        for (quint32 i = 0; i < count; ++i) {
            QByteArray actionId;
            QByteArray revision;
            in >> actionId >> revision;
            if (in.status() != QDataStream::Ok || actionId.isEmpty()
                || actionId.size() > 256 || revision.isEmpty() || revision.size() > 256
                || !document.transportReceipts.emplace(actionId, revision).second)
                return Error(ErrorCode::corrupt_record,
                             "reference provider transport receipt entry is invalid");
        }
    }
    if (!in.atEnd() || document.accepted.size() != document.revisionNumber)
        return Error(ErrorCode::corrupt_record,
                     "reference provider state has trailing bytes or inconsistent revision");
    document.revision = makeRevision(document, document.revisionNumber);

    for (const auto& [actionId, revision] : document.transportReceipts) {
        Q_UNUSED(revision)
        const auto accepted = std::find_if(document.accepted.begin(),
            document.accepted.end(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == actionId
                    && record.format == QByteArrayLiteral("MAB/3");
            });
        const auto transaction = document.transactionsById.find(actionId);
        if (accepted == document.accepted.end()
            || transaction == document.transactionsById.end()
            || transaction->second.remoteAcceptance)
            return Error(ErrorCode::corrupt_record,
                         "reference provider receipt does not name a local accepted MAB");
    }

    for (const auto& [transactionId, transaction] : document.transactionsById) {
        quint64 expectedNumber = 0;
        if (transactionId.isEmpty() || transactionId.size() > 256
            || transaction.transactionId != transactionId
            || !parseRevision(transaction.expectedRevision.token,
                             document.revisionPrefix, expectedNumber)
            || expectedNumber > document.revisionNumber
            || transaction.sharedRecordFormat.isEmpty()
            || transaction.sharedRecordFormat.size() > 256
            || transaction.sharedRecord.isEmpty()
            || transaction.sharedRecord.size() > 16 * 1024 * 1024
            || transaction.localHistoryAfterCommit.size() > 32 * 1024 * 1024
            || transaction.reservations.size() > 4096
            || transaction.operationIdentities.size() > 65'536)
            return Error(ErrorCode::corrupt_record,
                         "reference provider transaction metadata is out of bounds");
        std::set<QByteArray> transactionClaims;
        for (const auto& claim : transaction.operationIdentities) {
            if (!validUtf8(claim.stream, 256) || !claim.actor.valid() || claim.counter == 0
                || !transactionClaims.insert(identityKey(claim)).second)
                return Error(ErrorCode::corrupt_record,
                             "reference provider transaction identity claim is invalid");
            const bool reserved = std::any_of(
                transaction.reservations.begin(), transaction.reservations.end(),
                [&](const ScopedReservation& reservation) {
                    return reservation.stream == claim.stream
                        && reservation.actor == claim.actor
                        && claim.counter >= reservation.first
                        && claim.counter - reservation.first < reservation.count;
                });
            if (transaction.remoteAcceptance) {
                if (!transaction.reservations.empty()
                    || !transaction.localHistoryAfterCommit.isEmpty())
                    return Error(ErrorCode::corrupt_record,
                                 "remote transaction carries local reservations or history");
            } else if (claim.actor != actor || !reserved)
                return Error(ErrorCode::corrupt_record,
                             "reference provider identity claim lacks a reservation");
        }
    }

    std::set<QByteArray> acceptedIds;
    std::set<QByteArray> allClaimKeys;
    quint64 prefix = 0;
    for (const auto& record : document.accepted) {
        ++prefix;
        if (record.transactionId.isEmpty() || record.format.isEmpty()
            || record.bytes.isEmpty() || !acceptedIds.insert(record.transactionId).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider accepted record is invalid or duplicated");
        const auto transaction = document.transactionsById.find(record.transactionId);
        if (transaction == document.transactionsById.end()
            || transaction->second.sharedRecordFormat != record.format
            || transaction->second.sharedRecord != record.bytes
            || transaction->second.expectedRevision != makeRevision(document, prefix - 1))
            return Error(ErrorCode::corrupt_record,
                         "reference provider accepted transaction binding is inconsistent");
        document.transactionRevisions.emplace(record.transactionId,
                                              makeRevision(document, prefix));
        for (const auto& claim : transaction->second.operationIdentities) {
            if ((!transaction->second.remoteAcceptance && claim.actor != actor)
                || !validUtf8(claim.stream, 256)
                || claim.counter == 0
                || !allClaimKeys.insert(identityKey(claim)).second)
                return Error(ErrorCode::corrupt_record,
                             "reference provider accepted identity index is inconsistent");
            document.identityOwners.emplace(identityKey(claim), record.transactionId);
        }
    }
    std::set<QByteArray> unresolvedIds;
    for (const auto& transaction : document.prepared) {
        if (!unresolvedIds.insert(transaction.transactionId).second)
            return Error(ErrorCode::corrupt_record,
                         "reference provider repeats an unresolved transaction");
        if (document.completedHistories.contains(transaction.transactionId))
            return Error(ErrorCode::corrupt_record,
                         "reference provider transaction completion state is inconsistent");
        const bool accepted = acceptedIds.contains(transaction.transactionId);
        for (const auto& claim : transaction.operationIdentities) {
            if (!validUtf8(claim.stream, 256) || !claim.actor.valid() || claim.counter == 0)
                return Error(ErrorCode::corrupt_record,
                             "reference provider transaction has an invalid identity claim");
            if (!accepted && !allClaimKeys.insert(identityKey(claim)).second)
                return Error(ErrorCode::corrupt_record,
                             "reference provider pending identity claim conflicts");
        }
    }
    for (const auto& [transactionId, transaction] : document.transactionsById) {
        Q_UNUSED(transaction)
        if (!acceptedIds.contains(transactionId) && !unresolvedIds.contains(transactionId)
            && !document.resolvedAbsentTransactions.contains(transactionId))
            return Error(ErrorCode::corrupt_record,
                         "reference provider has orphan transaction metadata");
    }
    for (const auto& transactionId : document.resolvedAbsentTransactions)
        if (!document.transactionsById.contains(transactionId)
            || acceptedIds.contains(transactionId) || unresolvedIds.contains(transactionId))
            return Error(ErrorCode::corrupt_record,
                         "reference provider absence resolution is inconsistent");
    for (const auto& [transactionId, history] : document.completedHistories) {
        const auto transaction = document.transactionsById.find(transactionId);
        const auto revision = document.transactionRevisions.find(transactionId);
        if (transaction == document.transactionsById.end()
            || revision == document.transactionRevisions.end()
            || history.acceptedRevision != revision->second
            || history.bytes != transaction->second.localHistoryAfterCommit)
            return Error(ErrorCode::corrupt_record,
                         "reference provider private history is not transaction-bound");
    }
    if (document.localHistory) {
        const auto found = document.completedHistories.find(
            document.localHistory->committedTransactionId);
        quint64 localHistoryNumber = 0;
        if (found == document.completedHistories.end()
            || found->second != *document.localHistory
            || !parseRevision(document.localHistory->acceptedRevision.token,
                              document.revisionPrefix, localHistoryNumber)
            || localHistoryNumber != document.localHistoryRevisionNumber)
            return Error(ErrorCode::corrupt_record,
                         "reference provider current private history is unbound");
    }
    if ((!document.localHistory && document.localHistoryRevisionNumber != 0)
        || document.localHistoryRevisionNumber > document.revisionNumber
        || (hasExport && document.exportedRevisionNumber > document.revisionNumber)
        || (!hasExport && document.exportedRevisionNumber != 0))
        return Error(ErrorCode::corrupt_record,
                     "reference provider revision watermark exceeds the accepted prefix");

    std::map<QString, std::vector<std::pair<quint64, quint64>>> intervals;
    auto accountReservation = [&](const ScopedReservation& reservation) -> bool {
        if (!validUtf8(reservation.stream, 256) || reservation.actor != actor
            || reservation.first == 0 || reservation.count == 0
            || reservation.count > std::numeric_limits<quint64>::max() - reservation.first)
            return false;
        const quint64 last = reservation.first + reservation.count - 1;
        const auto counter = document.counters.find(reservation.stream);
        if (counter == document.counters.end() || counter->second <= last) return false;
        intervals[reservation.stream].emplace_back(reservation.first, last);
        return true;
    };
    for (const auto& reservation : document.reservations)
        if (!accountReservation(reservation))
            return Error(ErrorCode::corrupt_record,
                         "reference provider unresolved reservation is invalid");
    for (const auto& [transactionId, transaction] : document.transactionsById) {
        // A definitively absent preparation retains its metadata for audit and
        // append refusal, while its reservation has returned to the unresolved
        // reservation table. Count that interval only once.
        if (document.resolvedAbsentTransactions.contains(transactionId)) continue;
        for (const auto& reservation : transaction.reservations)
            if (!accountReservation(reservation))
                return Error(ErrorCode::corrupt_record,
                             "reference provider transaction reservation is invalid");
    }
    for (auto& [stream, streamIntervals] : intervals) {
        Q_UNUSED(stream)
        std::sort(streamIntervals.begin(), streamIntervals.end());
        for (size_t i = 1; i < streamIntervals.size(); ++i)
            if (streamIntervals[i].first <= streamIntervals[i - 1].second)
                return Error(ErrorCode::corrupt_record,
                             "reference provider reservation intervals overlap");
    }
    return document;
}

QString referenceDocumentPath(const QString& root,
                              const DocumentStorageScope& scope)
{
    QByteArray material = scope.documentKey.toUtf8();
    material.append('\0');
    for (int i = 7; i >= 0; --i)
        material.append(static_cast<char>(scope.epoch >> (i * 8)));
    const QByteArray digest = QCryptographicHash::hash(
        material, QCryptographicHash::Sha256).toHex();
    return root + QStringLiteral("/documents/") + QString::fromLatin1(digest)
        + QStringLiteral(".mref");
}

QString referenceLeasePath(const QString& root,
                           const DocumentStorageScope& scope)
{
    const QByteArray digest = QCryptographicHash::hash(
        scope.documentKey.toUtf8(), QCryptographicHash::Sha256).toHex();
    return root + QStringLiteral("/leases/") + QString::fromLatin1(digest)
        + QStringLiteral(".lock");
}

CollabText::Result<void> syncReferenceFileAndDirectory(const QString& path)
{
#if defined(Q_OS_UNIX)
    const QByteArray nativeFile = QFile::encodeName(path);
    const int file = ::open(nativeFile.constData(), O_RDONLY);
    if (file < 0)
        return Error(ErrorCode::commit_uncertain,
                     "reference state committed but could not reopen it for sync");
    if (Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::fileSync)) {
        ::close(file);
        return Error(ErrorCode::commit_uncertain,
                     "injected reference state file sync failure");
    }
    const int fileResult = ::fsync(file);
    ::close(file);
    if (fileResult != 0)
        return Error(ErrorCode::commit_uncertain, "reference state file sync failed");
    const QByteArray nativeDir = QFile::encodeName(QFileInfo(path).absolutePath());
    const int directory = ::open(nativeDir.constData(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
        return Error(ErrorCode::commit_uncertain,
                     "reference state committed but its directory could not be synced");
    if (Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::directorySync)) {
        ::close(directory);
        return Error(ErrorCode::commit_uncertain,
                     "injected reference state directory sync failure");
    }
    const int directoryResult = ::fsync(directory);
    ::close(directory);
    if (directoryResult != 0)
        return Error(ErrorCode::commit_uncertain, "reference state directory sync failed");
    return {};
#else
    Q_UNUSED(path)
    return Error(ErrorCode::unsupported_operation,
                 "reference storage requires file and directory sync support");
#endif
}

CollabText::Result<void> persistReferenceDocument(
    const QString& path, const DocumentStorageScope& scope,
    const EphemeralDocument& document, const CollabText::ActorId& actor,
    bool appendingAcceptedTransaction = false,
    bool preparingTransaction = false,
    bool storingLocalHistory = false,
    bool reservingAllocation = false)
{
    auto encoded = encodeReferenceDocument(scope, document, actor);
    if (!encoded.ok()) return encoded.error();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return Error(ErrorCode::io_error, "could not open reference state for writing",
                     path.toStdString());
    const QByteArray bytes = std::move(encoded).value();
    if (file.write(bytes) != bytes.size())
        return Error(ErrorCode::io_error, "could not write complete reference state",
                     path.toStdString());
    if (appendingAcceptedTransaction
        && Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename))
        return Error(ErrorCode::io_error,
                     "injected failure before accepted append atomic rename",
                     path.toStdString());
    if (preparingTransaction
        && Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::prepareBeforeAtomicRename))
        return Error(ErrorCode::io_error,
                     "injected failure before preparation atomic rename",
                     path.toStdString());
    if (reservingAllocation
        && Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::reservationBeforeAtomicRename))
        return Error(ErrorCode::io_error,
                     "injected failure before reservation atomic rename",
                     path.toStdString());
    if (storingLocalHistory
        && Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::historyBeforeAtomicRename))
        return Error(ErrorCode::io_error,
                     "injected failure before local-history atomic rename",
                     path.toStdString());
    if (!file.commit())
        return Error(ErrorCode::commit_uncertain, "reference state commit is uncertain",
                     path.toStdString());
    if (appendingAcceptedTransaction
        && Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::appendAfterAtomicRename))
        return Error(ErrorCode::commit_uncertain,
                     "injected uncertainty after accepted append atomic rename",
                     path.toStdString());
    if (Testing::consumeReferenceProviderFault(
            Testing::ReferenceProviderFaultPoint::afterAtomicRename))
        return Error(ErrorCode::commit_uncertain,
                     "injected failure after reference state atomic rename",
                     path.toStdString());
    return syncReferenceFileAndDirectory(path);
}

CollabText::Result<EphemeralDocument> loadReferenceDocument(
    const QString& path, const DocumentStorageScope& scope,
    const CollabText::ActorId& actor)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::io_error, "could not open reference state for reading",
                     path.toStdString());
    if (file.size() <= 0 || file.size() > kMaxReferenceFileSize)
        return Error(ErrorCode::corrupt_record, "reference state file size is invalid",
                     path.toStdString());
    return decodeReferenceDocument(file.readAll(), scope, actor);
}

PublicationResult rejected(const char* message, ErrorCode code = ErrorCode::pin_conflict)
{
    PublicationResult result;
    result.disposition = PublicationDisposition::rejected;
    result.diagnostic.emplace(code, message);
    return result;
}

class EphemeralSession final : public DocumentStorageSession {
public:
    EphemeralSession(std::shared_ptr<EphemeralState> state,
                     DocumentStorageScope scope, StorageMode mode,
                     QString documentPath = {},
                     std::shared_ptr<QLockFile> documentLease = {})
        : m_state(std::move(state)), m_scope(std::move(scope)), m_mode(mode),
          m_documentPath(std::move(documentPath)),
          m_documentLease(std::move(documentLease)) {}

    ~EphemeralSession() override
    {
        std::lock_guard lock(m_state->mutex);
        m_state->activeDocuments.erase(documentLeaseKey(m_scope));
    }

    const DocumentStorageScope& scope() const noexcept override { return m_scope; }
    StorageMode mode() const noexcept override { return m_mode; }
    const CollabText::ActorId& actor() const noexcept override { return m_state->actor; }

    CollabText::Result<StorageRecoveryState> readRecoveryState() override
    {
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        const auto& document = state();
        StorageRecoveryState result;
        result.acceptedRevision = document.revision;
        result.acceptedRecords = document.accepted;
        for (auto& record : result.acceptedRecords) {
            const auto transaction = document.transactionsById.find(record.transactionId);
            const auto revision = document.transactionRevisions.find(record.transactionId);
            if (transaction == document.transactionsById.end()
                || revision == document.transactionRevisions.end())
                return Error(ErrorCode::history_unavailable,
                             "accepted record is missing its exact prefix revisions");
            record.priorRevision = transaction->second.expectedRevision;
            record.committedRevision = revision->second;
        }
        result.unresolvedTransactions = document.prepared;
        result.unresolvedReservations = document.reservations;
        result.localHistory = document.localHistory;
        result.initialImport = document.initialImport;
        result.quarantinedOperations = document.quarantinedOperations;
        result.exportedRevision = document.exportedRevision;
        for (const auto& record : result.acceptedRecords) {
            if (record.format != QByteArrayLiteral("MAB/3")
                || document.transportReceipts.contains(record.transactionId))
                continue;
            const auto transaction = document.transactionsById.find(record.transactionId);
            if (transaction == document.transactionsById.end())
                return Error(ErrorCode::history_unavailable,
                             "accepted action is missing transaction metadata");
            if (transaction->second.remoteAcceptance) continue;
            result.pendingOutboundActions.push_back({record.transactionId,
                record.bytes, record.committedRevision});
        }
        return result;
    }

    CollabText::Result<void> beginInitialImport(
        const PendingImportIntent& intent) override
    {
        if (intent.transactionId.isEmpty() || intent.transactionId.size() > 256
            || intent.sourceMarkdown.size() > 16 * 1024 * 1024)
            return Error(ErrorCode::malformed,
                         "initial import intent requires a bounded transaction ID and source");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        if (document.initialImport) {
            if (*document.initialImport == intent) return {};
            const bool sameSource = document.initialImport->sourceMarkdown
                == intent.sourceMarkdown;
            const bool oldAttemptWasProvenAbsent =
                document.resolvedAbsentTransactions.contains(
                    document.initialImport->transactionId);
            const bool prefixContainsOnlyTerminalGaps = std::all_of(
                document.accepted.begin(), document.accepted.end(),
                [](const AcceptedStorageRecord& record) {
                    return record.format == QByteArrayLiteral("MTG/1")
                        || record.format == QByteArrayLiteral("MMG/1");
                });
            if (sameSource && intent.transactionId != document.initialImport->transactionId
                && !document.transactionsById.contains(intent.transactionId)
                && oldAttemptWasProvenAbsent && document.prepared.empty()
                && document.reservations.empty() && prefixContainsOnlyTerminalGaps) {
                const auto before = document;
                document.initialImport = intent;
                auto saved = persist(document);
                if (!saved.ok()) {
                    document = before;
                    return saved.error();
                }
                return {};
            }
            return Error(ErrorCode::pin_conflict,
                         "document already has a different or unresolved initial import intent");
        }
        if (!document.accepted.empty() || !document.prepared.empty()
            || !document.reservations.empty() || !document.transactionsById.empty()
            || !document.counters.empty())
            return Error(ErrorCode::history_unavailable,
                         "initial import intent requires an empty, unreserved history");
        const auto before = document;
        document.initialImport = intent;
        auto saved = persist(document);
        if (!saved.ok()) {
            if (saved.error().code() == ErrorCode::commit_uncertain)
                m_poisoned = true;
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<ScopedReservation> reserve(
        const QString& stream, quint64 count) override
    {
        if (!validUtf8(stream, 256) || count == 0)
            return Error(ErrorCode::invalid_identity,
                         "reservation requires a bounded stream and nonzero count");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        const auto found = document.counters.find(stream);
        const quint64 next = found == document.counters.end() ? 1 : found->second;
        if (count > std::numeric_limits<quint64>::max() - next)
            return Error(ErrorCode::counter_exhausted,
                         "ephemeral stream counter exhausted");
        const quint64 first = next;
        ScopedReservation reservation{stream, m_state->actor, first, count};
        const auto before = document;
        document.counters[stream] = next + count;
        document.reservations.push_back(reservation);
        auto saved = persist(document, false, false, false, true);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return reservation;
    }

    CollabText::Result<void> observeCounter(
        const QString& stream, quint64 counter) override
    {
        if (!validUtf8(stream, 256) || counter == 0)
            return Error(ErrorCode::invalid_identity,
                         "counter observation requires a bounded stream and nonzero value");
        if (counter == std::numeric_limits<quint64>::max())
            return Error(ErrorCode::counter_exhausted,
                         "observed counter leaves no representable successor");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        const auto before = document;
        auto& next = document.counters[stream];
        if (next == 0) next = 1;
        next = std::max(next, counter + 1);
        auto saved = persist(document);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<quint64> nextCounter(const QString& stream) const override
    {
        if (!validUtf8(stream, 256))
            return Error(ErrorCode::invalid_identity, "invalid counter stream");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        const auto& counters = state().counters;
        const auto found = counters.find(stream);
        return found == counters.end() ? quint64(1) : found->second;
    }

    CollabText::Result<void> prepare(
        const PreparedStorageTransaction& transaction) override
    {
        if (transaction.remoteAcceptance)
            return Error(CollabText::ErrorCode::invalid_identity,
                         "remote acceptance cannot use local reservation preparation");
        if (transaction.transactionId.isEmpty()
            || transaction.sharedRecordFormat.isEmpty()
            || transaction.sharedRecord.isEmpty())
            return Error(ErrorCode::malformed,
                         "prepared transaction requires ID, format and record bytes");
        if (transaction.transactionId.size() > 256
            || transaction.sharedRecordFormat.size() > 256
            || transaction.sharedRecord.size() > 16 * 1024 * 1024
            || transaction.localHistoryAfterCommit.size() > 32 * 1024 * 1024
            || transaction.reservations.size() > 4096
            || transaction.operationIdentities.size() > 65'536)
            return Error(ErrorCode::limit_exceeded,
                         "prepared transaction exceeds provider limits");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        for (const auto& item : document.prepared) {
            if (item.transactionId == transaction.transactionId)
                return samePrepared(item, transaction)
                    ? CollabText::Result<void>{}
                    : CollabText::Result<void>{Error(
                          ErrorCode::pin_conflict,
                          "transaction ID was prepared with different bytes or metadata")};
        }
        const auto prior = document.transactionsById.find(transaction.transactionId);
        if (prior != document.transactionsById.end()) {
            if (samePrepared(prior->second, transaction)) {
                const bool wasAccepted = std::any_of(
                    document.accepted.begin(), document.accepted.end(),
                    [&](const AcceptedStorageRecord& record) {
                        return record.transactionId == transaction.transactionId;
                    });
                return wasAccepted
                    ? CollabText::Result<void>{}
                    : CollabText::Result<void>{Error(
                          ErrorCode::pin_conflict,
                          "transaction was resolved absent and cannot be prepared again")};
            }
            return Error(ErrorCode::pin_conflict,
                         "transaction ID was rebound to different bytes or metadata");
        }
        if (transaction.expectedRevision != document.revision)
            return Error(ErrorCode::counter_regression,
                         "prepared transaction has a stale accepted revision");
        std::set<std::tuple<QString, quint64, quint64>> seenReservations;
        for (const auto& reservation : transaction.reservations) {
            if (!validUtf8(reservation.stream, 256)
                || reservation.actor != m_state->actor || reservation.first == 0
                || reservation.count == 0
                || reservation.count > std::numeric_limits<quint64>::max()
                                     - reservation.first
                || std::find(document.reservations.begin(), document.reservations.end(),
                             reservation) == document.reservations.end()
                || !seenReservations.emplace(reservation.stream, reservation.first,
                                             reservation.count).second)
                return Error(ErrorCode::invalid_identity,
                             "transaction references an unknown reservation");
        }
        std::set<QByteArray> seen;
        for (const auto& claim : transaction.operationIdentities) {
            if (!validUtf8(claim.stream, 256) || claim.actor != m_state->actor
                || claim.counter == 0 || !seen.insert(identityKey(claim)).second)
                return Error(ErrorCode::invalid_identity,
                             "transaction contains an invalid or duplicate identity claim");
            if (document.identityOwners.contains(identityKey(claim)))
                return Error(ErrorCode::pin_conflict,
                    "operation identity is already claimed by accepted history: stream "
                    + claim.stream.toUtf8().toStdString() + " counter "
                    + std::to_string(claim.counter));
            const bool reserved = std::any_of(
                transaction.reservations.begin(), transaction.reservations.end(),
                [&](const ScopedReservation& item) {
                    return item.stream == claim.stream && item.actor == claim.actor
                        && claim.counter >= item.first
                        && claim.counter - item.first < item.count;
                });
            if (!reserved)
                return Error(ErrorCode::invalid_identity,
                             "operation identity is outside its transaction reservations");
            for (const auto& pending : document.prepared)
                for (const auto& other : pending.operationIdentities)
                    if (sameClaim(claim, other))
                        return Error(ErrorCode::pin_conflict,
                                     "operation identity is already claimed by a preparation");
        }
        const auto before = document;
        document.transactionsById.emplace(transaction.transactionId, transaction);
        document.prepared.push_back(transaction);
        for (const auto& reservation : transaction.reservations) {
            const auto found = std::find(document.reservations.begin(),
                                         document.reservations.end(), reservation);
            document.reservations.erase(found);
        }
        auto saved = persist(document, false, true);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return {};
    }

    PublicationResult append(const QByteArray& transactionId) override
    {
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned) {
            PublicationResult result;
            result.disposition = PublicationDisposition::uncertain;
            result.diagnostic.emplace(ErrorCode::commit_uncertain,
                                      "reference session requires close and recovery");
            return result;
        }
        auto& document = state();
        const auto committed = std::find_if(document.accepted.begin(), document.accepted.end(),
            [&](const AcceptedStorageRecord& record) {
                return record.transactionId == transactionId;
            });
        if (committed != document.accepted.end())
            return {PublicationDisposition::committed,
                    document.transactionRevisions.at(transactionId), std::nullopt};
        const auto prepared = std::find_if(document.prepared.begin(), document.prepared.end(),
            [&](const PreparedStorageTransaction& item) {
                return item.transactionId == transactionId;
            });
        if (prepared == document.prepared.end())
            return rejected("append requires a prepared transaction", ErrorCode::not_found);
        if (prepared->expectedRevision != document.revision)
            return rejected("accepted prefix advanced after prepare", ErrorCode::counter_regression);
        for (const auto& claim : prepared->operationIdentities) {
            if (document.identityOwners.contains(identityKey(claim)))
                return rejected("operation identity conflicts with accepted history");
        }
        const auto before = document;
        AcceptedStorageRecord record{prepared->transactionId,
                                     prepared->sharedRecordFormat,
                                     prepared->sharedRecord};
        document.accepted.push_back(std::move(record));
        for (const auto& claim : prepared->operationIdentities)
            document.identityOwners.emplace(identityKey(claim), transactionId);
        ++document.revisionNumber;
        document.revision = makeRevision(document, document.revisionNumber);
        const auto revision = document.revision;
        document.transactionRevisions.emplace(transactionId, revision);
        // Keep the exact preparation until its associated private history is
        // stored. Recovery must be able to finish this step after shared commit.
        auto saved = persist(document, true);
        if (!saved.ok()) {
            document = before;
            if (saved.error().code() != ErrorCode::io_error)
                m_poisoned = true; // The file may contain the new prefix; reopen before inspection.
            PublicationResult result;
            result.disposition = saved.error().code() == ErrorCode::io_error
                ? PublicationDisposition::absent
                : PublicationDisposition::uncertain;
            result.diagnostic = saved.error();
            return result;
        }
        if (Testing::consumeReferenceProviderFault(
                Testing::ReferenceProviderFaultPoint::appendAfterCommit)) {
            m_poisoned = true;
            PublicationResult result;
            result.disposition = PublicationDisposition::uncertain;
            result.diagnostic.emplace(ErrorCode::commit_uncertain,
                                      "injected uncertainty after accepted append commit");
            return result;
        }
        return {PublicationDisposition::committed, revision, std::nullopt};
    }

    PublicationResult acceptRemoteAction(
        const PreparedStorageTransaction& transaction) override
    {
        using namespace CollabText;
        auto rejectRemote = [](const char* message, ErrorCode code) {
            PublicationResult result;
            result.disposition = PublicationDisposition::rejected;
            result.diagnostic.emplace(code, message);
            return result;
        };
        if (!transaction.remoteAcceptance
            || transaction.transactionId.isEmpty()
            || transaction.transactionId.size() > 256
            || transaction.sharedRecordFormat != QByteArrayLiteral("MAB/3")
            || transaction.sharedRecord.isEmpty()
            || transaction.sharedRecord.size() > 16 * 1024 * 1024
            || !transaction.reservations.empty()
            || !transaction.localHistoryAfterCommit.isEmpty()
            || transaction.operationIdentities.empty()
            || transaction.operationIdentities.size() > 65'536)
            return rejectRemote("remote acceptance metadata is invalid", ErrorCode::malformed);
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned) {
            PublicationResult result;
            result.disposition = PublicationDisposition::uncertain;
            result.diagnostic.emplace(ErrorCode::commit_uncertain,
                                      "reference session requires close and recovery");
            return result;
        }
        auto& document = state();
        const auto existing = document.transactionsById.find(transaction.transactionId);
        if (existing != document.transactionsById.end()) {
            if (!samePrepared(existing->second, transaction))
                return rejectRemote("remote action ID was rebound to different metadata",
                                    ErrorCode::pin_conflict);
            const auto accepted = std::find_if(document.accepted.begin(), document.accepted.end(),
                [&](const AcceptedStorageRecord& record) {
                    return record.transactionId == transaction.transactionId;
                });
            if (accepted != document.accepted.end())
                return {PublicationDisposition::committed,
                        document.transactionRevisions.at(transaction.transactionId),
                        std::nullopt};
            return rejectRemote("remote action ID is already locally prepared or retired",
                                ErrorCode::pin_conflict);
        }
        if (transaction.expectedRevision != document.revision)
            return rejectRemote("remote action was staged against a stale accepted prefix",
                                ErrorCode::counter_regression);

        std::set<QByteArray> identities;
        for (const auto& claim : transaction.operationIdentities) {
            if (!validUtf8(claim.stream, 256) || !claim.actor.valid()
                || claim.counter == 0 || !identities.insert(identityKey(claim)).second)
                return rejectRemote("remote action contains an invalid or duplicate identity",
                                    ErrorCode::invalid_identity);
            if (document.identityOwners.contains(identityKey(claim)))
                return rejectRemote("remote operation identity is already accepted",
                                    ErrorCode::pin_conflict);
        }
        const auto before = document;
        document.transactionsById.emplace(transaction.transactionId, transaction);
        document.accepted.push_back({transaction.transactionId,
                                     transaction.sharedRecordFormat,
                                     transaction.sharedRecord});
        for (const auto& claim : transaction.operationIdentities)
            document.identityOwners.emplace(identityKey(claim), transaction.transactionId);
        ++document.revisionNumber;
        document.revision = makeRevision(document, document.revisionNumber);
        const auto revision = document.revision;
        document.transactionRevisions.emplace(transaction.transactionId, revision);
        auto saved = persist(document, true);
        if (!saved.ok()) {
            document = before;
            PublicationResult result;
            result.disposition = saved.error().code() == ErrorCode::io_error
                ? PublicationDisposition::absent : PublicationDisposition::uncertain;
            result.diagnostic = saved.error();
            if (result.disposition == PublicationDisposition::uncertain) m_poisoned = true;
            return result;
        }
        if (Testing::consumeReferenceProviderFault(
                Testing::ReferenceProviderFaultPoint::appendAfterCommit)) {
            m_poisoned = true;
            PublicationResult result;
            result.disposition = PublicationDisposition::uncertain;
            result.diagnostic.emplace(ErrorCode::commit_uncertain,
                                      "injected uncertainty after remote acceptance commit");
            return result;
        }
        return {PublicationDisposition::committed, revision, std::nullopt};
    }

    PublicationResult inspect(
        const PreparedStorageTransaction& expectedTransaction) override
    {
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned) {
            PublicationResult result;
            result.disposition = PublicationDisposition::uncertain;
            result.diagnostic.emplace(ErrorCode::commit_uncertain,
                                      "reference session requires close and recovery");
            return result;
        }
        const auto& document = state();
        for (const auto& record : document.accepted) {
            if (record.transactionId != expectedTransaction.transactionId) continue;
            const auto committed = document.transactionsById.find(record.transactionId);
            const bool exact = committed != document.transactionsById.end()
                && samePrepared(committed->second, expectedTransaction);
            if (!exact) return rejected("transaction ID is committed with different bytes");
            return {PublicationDisposition::committed,
                    document.transactionRevisions.at(expectedTransaction.transactionId),
                    std::nullopt};
        }
        for (const auto& prepared : document.prepared) {
            if (prepared.transactionId == expectedTransaction.transactionId)
                return samePrepared(prepared, expectedTransaction)
                    ? PublicationResult{PublicationDisposition::absent, std::nullopt, std::nullopt}
                    : rejected("transaction ID is prepared with different metadata");
        }
        return {PublicationDisposition::absent, std::nullopt, std::nullopt};
    }

    CollabText::Result<void> resolveAbsentPrepared(
        const PreparedStorageTransaction& expectedTransaction) override
    {
        using namespace CollabText;
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        const auto stored = document.transactionsById.find(expectedTransaction.transactionId);
        if (stored != document.transactionsById.end()
            && !samePrepared(stored->second, expectedTransaction))
            return Error(ErrorCode::pin_conflict,
                         "absence resolution does not match the exact preparation");
        const bool accepted = std::any_of(document.accepted.begin(), document.accepted.end(),
            [&](const AcceptedStorageRecord& record) {
                return record.transactionId == expectedTransaction.transactionId;
            });
        if (accepted)
            return Error(ErrorCode::commit_uncertain,
                         "cannot resolve an accepted transaction as absent");
        if (document.resolvedAbsentTransactions.contains(expectedTransaction.transactionId))
            return {};
        if (stored == document.transactionsById.end()) {
            if (expectedTransaction.transactionId.isEmpty()
                || expectedTransaction.transactionId.size() > 256
                || expectedTransaction.sharedRecordFormat.isEmpty()
                || expectedTransaction.sharedRecordFormat.size() > 256
                || expectedTransaction.sharedRecord.isEmpty()
                || expectedTransaction.sharedRecord.size() > 16 * 1024 * 1024
                || expectedTransaction.reservations.size() > 4096
                || expectedTransaction.operationIdentities.size() > 65'536)
                return Error(ErrorCode::malformed,
                             "absent transaction metadata is invalid");
            if (expectedTransaction.expectedRevision != document.revision)
                return Error(ErrorCode::counter_regression,
                             "absent transaction was staged against a stale accepted prefix");
            for (const auto& reservation : expectedTransaction.reservations)
                if (!validUtf8(reservation.stream, 256)
                    || reservation.actor != m_state->actor || reservation.first == 0
                    || reservation.count == 0
                    || reservation.count > std::numeric_limits<quint64>::max()
                                               - reservation.first
                    || std::find(document.reservations.begin(), document.reservations.end(),
                              reservation) == document.reservations.end())
                    return Error(ErrorCode::invalid_identity,
                                 "absent transaction does not own every unresolved reservation");
            for (const auto& claim : expectedTransaction.operationIdentities) {
                const bool reserved = std::any_of(
                    expectedTransaction.reservations.begin(),
                    expectedTransaction.reservations.end(),
                    [&](const ScopedReservation& reservation) {
                        return reservation.stream == claim.stream
                            && reservation.actor == claim.actor
                            && claim.counter >= reservation.first
                            && claim.counter - reservation.first < reservation.count;
                    });
                if (!validUtf8(claim.stream, 256) || claim.actor != m_state->actor
                    || claim.counter == 0 || !reserved)
                    return Error(ErrorCode::invalid_identity,
                                 "absent transaction has an invalid operation claim");
            }
        }
        const auto prepared = std::find_if(document.prepared.begin(), document.prepared.end(),
            [&](const PreparedStorageTransaction& item) {
                return item.transactionId == expectedTransaction.transactionId;
            });
        const auto before = document;
        if (stored == document.transactionsById.end())
            document.transactionsById.emplace(expectedTransaction.transactionId,
                                               expectedTransaction);
        if (prepared != document.prepared.end()) {
            if (!samePrepared(*prepared, expectedTransaction))
                return Error(ErrorCode::pin_conflict,
                             "prepared transaction changed before absence resolution");
            document.prepared.erase(prepared);
        }
        document.resolvedAbsentTransactions.insert(expectedTransaction.transactionId);
        for (const auto& reservation : expectedTransaction.reservations)
            if (std::find(document.reservations.begin(), document.reservations.end(), reservation)
                == document.reservations.end())
                document.reservations.push_back(reservation);
        auto saved = persist(document);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<void> storeLocalHistory(
        const PrivateHistoryRecord& record) override
    {
        if (record.committedTransactionId.isEmpty() || record.acceptedRevision.token.isEmpty())
            return Error(ErrorCode::malformed, "private history requires transaction and revision");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        const auto accepted = std::find_if(document.accepted.begin(), document.accepted.end(),
            [&](const AcceptedStorageRecord& item) {
                return item.transactionId == record.committedTransactionId;
            });
        if (accepted == document.accepted.end())
            return Error(ErrorCode::not_found,
                         "private history transaction is not in accepted history");
        const auto completed = document.completedHistories.find(record.committedTransactionId);
        if (completed != document.completedHistories.end())
            return completed->second == record
                ? CollabText::Result<void>{}
                : CollabText::Result<void>{Error(
                      ErrorCode::pin_conflict,
                      "private history transaction was retried with different bytes")};
        const auto prepared = std::find_if(document.prepared.begin(), document.prepared.end(),
            [&](const PreparedStorageTransaction& item) {
                return item.transactionId == record.committedTransactionId;
            });
        if (prepared == document.prepared.end())
            return Error(ErrorCode::not_found,
                         "private history preparation is no longer recoverable");
        const auto intendedRevision = document.transactionRevisions.find(
            record.committedTransactionId);
        if (intendedRevision == document.transactionRevisions.end()
            || record.acceptedRevision != intendedRevision->second
            || record.bytes != prepared->localHistoryAfterCommit)
            return Error(ErrorCode::pin_conflict,
                         "private history differs from its committed preparation");
        const quint64 revisionNumber = record.acceptedRevision.token
            .mid(document.revisionPrefix.size() + 1).toULongLong();
        const auto before = document;
        if (!record.bytes.isEmpty()
            && revisionNumber >= document.localHistoryRevisionNumber) {
            document.localHistory = record;
            document.localHistoryRevisionNumber = revisionNumber;
        }
        document.completedHistories.emplace(record.committedTransactionId, record);
        document.prepared.erase(prepared);
        if (document.initialImport
            && document.initialImport->transactionId == record.committedTransactionId)
            document.initialImport.reset();
        auto saved = persist(document, false, false, true);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<void> storeQuarantinedOperation(
        const QByteArray& record) override
    {
        if (record.isEmpty() || record.size() > 26 * 1024 * 1024)
            return Error(ErrorCode::malformed,
                         "quarantined operation record must be nonempty and bounded");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        if (std::find(document.quarantinedOperations.begin(),
                      document.quarantinedOperations.end(), record)
            != document.quarantinedOperations.end())
            return {};
        if (document.quarantinedOperations.size() >= kMaxStoredItems)
            return Error(ErrorCode::limit_exceeded,
                         "reference provider quarantine table is full");
        const auto before = document;
        document.quarantinedOperations.push_back(record);
        auto saved = persist(document);
        if (!saved.ok()) {
            if (saved.error().code() == ErrorCode::commit_uncertain)
                m_poisoned = true;
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<void> acknowledgeMarkdownExport(
        const AcceptedRevision& revision) override
    {
        if (revision.token.isEmpty())
            return Error(ErrorCode::malformed, "export revision token is empty");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        quint64 revisionNumber = 0;
        bool known = revision == makeRevision(document, 0);
        if (!known) {
            for (const auto& [transaction, acceptedRevision] : document.transactionRevisions) {
                Q_UNUSED(transaction)
                if (acceptedRevision == revision) {
                    known = true;
                    revisionNumber = revision.token.mid(
                        document.revisionPrefix.size() + 1).toULongLong();
                    break;
                }
            }
        }
        if (!known)
            return Error(ErrorCode::not_found,
                         "export acknowledgment does not name an accepted prefix");
        if (document.exportedRevision && document.exportedRevisionNumber > revisionNumber)
            return Error(ErrorCode::counter_regression,
                         "older export acknowledgment cannot replace a newer one");
        const auto before = document;
        document.exportedRevision = revision;
        document.exportedRevisionNumber = revisionNumber;
        auto saved = persist(document);
        if (!saved.ok()) {
            document = before;
            return saved.error();
        }
        return {};
    }

    CollabText::Result<void> acknowledgeTransportReceipt(
        const QByteArray& actionId, const QByteArray& acceptedRevision) override
    {
        if (actionId.isEmpty() || actionId.size() > 256
            || acceptedRevision.isEmpty() || acceptedRevision.size() > 256)
            return Error(ErrorCode::malformed,
                         "transport receipt requires bounded action and revision IDs");
        std::lock_guard lock(m_state->mutex);
        if (m_poisoned)
            return Error(ErrorCode::commit_uncertain,
                         "reference session requires close and recovery");
        auto& document = state();
        const auto accepted = std::find_if(document.accepted.begin(),
            document.accepted.end(), [&](const AcceptedStorageRecord& record) {
                return record.transactionId == actionId
                    && record.format == QByteArrayLiteral("MAB/3");
            });
        const auto transaction = document.transactionsById.find(actionId);
        if (accepted == document.accepted.end()
            || transaction == document.transactionsById.end()
            || transaction->second.remoteAcceptance)
            return Error(ErrorCode::not_found,
                         "transport receipt does not name a local accepted MAB");
        const auto prior = document.transportReceipts.find(actionId);
        if (prior != document.transportReceipts.end()) {
            if (prior->second == acceptedRevision) return {};
            return Error(ErrorCode::pin_conflict,
                         "transport receipt changed for an acknowledged action");
        }
        if (document.transportReceipts.size() >= kMaxStoredItems)
            return Error(ErrorCode::limit_exceeded,
                         "reference provider transport receipt table is full");
        const auto before = document;
        document.transportReceipts.emplace(actionId, acceptedRevision);
        auto saved = persist(document);
        if (!saved.ok()) {
            if (saved.error().code() == ErrorCode::commit_uncertain)
                m_poisoned = true;
            document = before;
            return saved.error();
        }
        return {};
    }

private:
    EphemeralDocument& state() const
    {
        return m_state->documents[documentStateKey(m_scope)];
    }

    CollabText::Result<void> persist(const EphemeralDocument& document,
                                     bool appendingAcceptedTransaction = false,
                                     bool preparingTransaction = false,
                                     bool storingLocalHistory = false,
                                     bool reservingAllocation = false)
    {
        if (m_mode == StorageMode::ephemeral) return {};
        if (m_documentPath.isEmpty()) {
            m_poisoned = true;
            return Error(ErrorCode::io_error,
                         "durable reference session has no document file");
        }
        auto saved = persistReferenceDocument(
            m_documentPath, m_scope, document, m_state->actor,
            appendingAcceptedTransaction, preparingTransaction,
            storingLocalHistory, reservingAllocation);
        if (!saved.ok() && saved.error().code() != CollabText::ErrorCode::io_error)
            m_poisoned = true;
        return saved;
    }

    std::shared_ptr<EphemeralState> m_state;
    DocumentStorageScope m_scope;
    StorageMode m_mode;
    QString m_documentPath;
    std::shared_ptr<QLockFile> m_documentLease;
    bool m_poisoned = false;
};

class EphemeralProvider final : public DocumentStorageProvider {
public:
    explicit EphemeralProvider(CollabText::ActorId actor)
        : m_state(std::make_shared<EphemeralState>())
    {
        m_state->actor = actor;
    }

    CollabText::Result<std::unique_ptr<DocumentStorageSession>> open(
        const DocumentStorageScope& scope, StorageMode mode) override
    {
        if (mode != StorageMode::ephemeral)
            return Error(ErrorCode::unsupported_operation,
                         "ephemeral provider cannot open durable storage");
        if (!validScope(scope))
            return Error(ErrorCode::invalid_identity,
                         "storage scope requires a bounded UTF-8 document key and nonzero epoch");
        std::lock_guard lock(m_state->mutex);
        const QString lease = documentLeaseKey(scope);
        if (m_state->activeDocuments.contains(lease))
            return Error(ErrorCode::writer_conflict,
                         "ephemeral document already has an active session");
        m_state->activeDocuments.insert(lease);
        return std::unique_ptr<DocumentStorageSession>(
            new EphemeralSession(m_state, scope, StorageMode::ephemeral));
    }

private:
    std::shared_ptr<EphemeralState> m_state;
};

class ReferenceProvider final : public DocumentStorageProvider {
public:
    ReferenceProvider(std::shared_ptr<EphemeralState> state, QString root)
        : m_state(std::move(state)), m_root(std::move(root)) {}

    CollabText::Result<std::unique_ptr<DocumentStorageSession>> open(
        const DocumentStorageScope& scope, StorageMode mode) override
    {
        if (mode != StorageMode::durable)
            return Error(ErrorCode::unsupported_operation,
                         "reference provider only opens durable sessions");
        if (!validScope(scope))
            return Error(ErrorCode::invalid_identity,
                         "storage scope requires a bounded UTF-8 document key and nonzero epoch");

        const QString activeKey = documentLeaseKey(scope);
        const QString stateKey = documentStateKey(scope);
        std::lock_guard lock(m_state->mutex);
        if (m_state->activeDocuments.contains(activeKey))
            return Error(ErrorCode::writer_conflict,
                         "reference document already has an active session");

        auto lease = std::make_shared<QLockFile>(referenceLeasePath(m_root, scope));
        lease->setStaleLockTime(0);
        if (!lease->tryLock(0))
            return Error(ErrorCode::writer_conflict,
                         "reference document is leased by another process");

        const QString path = referenceDocumentPath(m_root, scope);
        EphemeralDocument document;
        const QFileInfo stateInfo(path);
        if (stateInfo.exists()) {
            auto loaded = loadReferenceDocument(path, scope, m_state->actor);
            if (!loaded.ok()) return loaded.error();
            document = std::move(loaded).value();
        } else {
            document.revisionPrefix = QByteArrayLiteral("reference");
            document.revision = makeRevision(document, 0);
            auto saved = persistReferenceDocument(path, scope, document, m_state->actor);
            if (!saved.ok()) return saved.error();
        }

        m_state->documents[stateKey] = std::move(document);
        m_state->activeDocuments.insert(activeKey);
        return std::unique_ptr<DocumentStorageSession>(new EphemeralSession(
            m_state, scope, StorageMode::durable, path, std::move(lease)));
    }

private:
    std::shared_ptr<EphemeralState> m_state;
    QString m_root;
};

} // namespace

CollabText::Result<std::unique_ptr<DocumentStorageProvider>>
makeEphemeralDocumentStorageProvider()
{
    auto actor = CollabText::ActorId::generate();
    if (!actor.ok()) return actor.error();
    std::unique_ptr<DocumentStorageProvider> provider(
        new EphemeralProvider(std::move(actor).value()));
    return provider;
}

CollabText::Result<std::unique_ptr<DocumentStorageProvider>>
makeReferenceDocumentStorageProvider(const std::filesystem::path& root)
{
    using namespace CollabText;
#if !defined(Q_OS_UNIX)
    Q_UNUSED(root)
    return Error(ErrorCode::unsupported_operation,
                 "reference provider requires Unix file and directory sync support");
#else
    if (root.empty())
        return Error(ErrorCode::invalid_identity,
                     "reference provider requires a nonempty isolated root");
    std::error_code ec;
    const auto absoluteRoot = std::filesystem::absolute(root, ec).lexically_normal();
    if (ec)
        return Error(ErrorCode::io_error,
                     "could not resolve reference provider root", ec.message());
    std::filesystem::create_directories(absoluteRoot / "documents", ec);
    if (ec)
        return Error(ErrorCode::io_error,
                     "could not create reference document directory", ec.message());
    std::filesystem::create_directories(absoluteRoot / "leases", ec);
    if (ec)
        return Error(ErrorCode::io_error,
                     "could not create reference lease directory", ec.message());
    std::filesystem::create_directories(absoluteRoot / "identity", ec);
    if (ec)
        return Error(ErrorCode::io_error,
                     "could not create reference identity directory", ec.message());

    auto identity = ReplicaState::open(absoluteRoot / "identity");
    if (!identity.ok()) return identity.error();
    auto state = std::make_shared<EphemeralState>();
    state->identityState = std::make_shared<ReplicaState>(std::move(identity).value());
    state->actor = state->identityState->actor();
    state->referenceRoot = QString::fromStdString(absoluteRoot.string());
    state->durable = true;
    std::unique_ptr<DocumentStorageProvider> provider(new ReferenceProvider(
        std::move(state), QString::fromStdString(absoluteRoot.string())));
    return provider;
#endif
}

} // namespace Markoff::VNext
