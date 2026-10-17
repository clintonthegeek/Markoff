// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/ActorId.h>

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Markoff::VNext {

/// Draft provider boundary; see docs/specs/2026-10-12-document-storage-provider.md.
/// Opaque bytes belong to Markoff codecs. Providers implement durability and
/// leases; Markoff implements editing, replay and recovery orchestration.
struct DocumentStorageScope {
    QString documentKey;
    quint64 epoch = 0;
    friend bool operator==(const DocumentStorageScope&,
                           const DocumentStorageScope&) = default;
};

enum class StorageMode { durable, ephemeral };

/// Equality token for an accepted prefix. Always nonempty, including an empty
/// prefix. It is neither a Core causal frontier nor a numeric edit sequence.
struct AcceptedRevision {
    QByteArray token;
    friend bool operator==(const AcceptedRevision&, const AcceptedRevision&) = default;
};

struct AcceptedStorageRecord {
    QByteArray transactionId;
    QByteArray format;
    QByteArray bytes;
    /// Provider-derived prefix tokens for replay validation; these values are
    /// not embedded in the shared record bytes.
    AcceptedRevision priorRevision;
    AcceptedRevision committedRevision;
};

/// A locally committed MAB awaiting an authenticated transport receipt. The
/// provider derives these from the accepted prefix and durable receipt ledger.
struct PendingOutboundAction {
    QByteArray actionId;
    QByteArray mabBytes;
    AcceptedRevision localRevision;
    friend bool operator==(const PendingOutboundAction&,
                           const PendingOutboundAction&) = default;
};

struct ScopedReservation {
    QString stream;
    CollabText::ActorId actor;
    quint64 first = 0;
    quint64 count = 0;
    friend bool operator==(const ScopedReservation&, const ScopedReservation&) = default;
};

/// Markoff validates these claims against its encoded operations. The host
/// atomically rejects reuse by a different transaction or different bytes.
struct OperationIdentityClaim {
    QString stream;
    CollabText::ActorId actor;
    quint64 counter = 0;
    friend bool operator==(const OperationIdentityClaim&,
                           const OperationIdentityClaim&) = default;
};

struct PreparedStorageTransaction {
    QByteArray transactionId;
    AcceptedRevision expectedRevision;
    QByteArray sharedRecordFormat;
    QByteArray sharedRecord;
    std::vector<ScopedReservation> reservations;
    std::vector<OperationIdentityClaim> operationIdentities;
    /// Complete intended owner-local history after an action, captured before
    /// append. Empty for terminal metadata such as a counter-gap record, which
    /// finalizes its transaction without replacing owner undo history.
    QByteArray localHistoryAfterCommit;
    /// True only for a Markoff-validated incoming MAB accepted into this
    /// provider's shared prefix. Remote actions have no local reservations or
    /// writer-private history update.
    bool remoteAcceptance = false;
};

enum class PublicationDisposition { committed, absent, uncertain, rejected };

struct PublicationResult {
    PublicationDisposition disposition = PublicationDisposition::uncertain;
    /// Present only for committed; identifies the prefix ending at this
    /// transaction, which can be older than the current accepted prefix.
    std::optional<AcceptedRevision> committedRevision;
    std::optional<CollabText::Error> diagnostic;
};

struct PrivateHistoryRecord {
    QByteArray committedTransactionId;
    AcceptedRevision acceptedRevision;
    QByteArray bytes;
    friend bool operator==(const PrivateHistoryRecord&,
                           const PrivateHistoryRecord&) = default;
};

struct PendingImportIntent {
    QByteArray transactionId;
    QByteArray sourceMarkdown;
    friend bool operator==(const PendingImportIntent&,
                           const PendingImportIntent&) = default;
};

struct StorageRecoveryState {
    AcceptedRevision acceptedRevision;
    /// Complete ordered prefix for the initial implementation, including gaps.
    /// Each item carries the exact host prefix before and after its commit so
    /// replay can validate scope-bound gap tokens without ordering opaque tokens.
    std::vector<AcceptedStorageRecord> acceptedRecords;
    /// Locally accepted MABs not yet acknowledged by the transport host.
    std::vector<PendingOutboundAction> pendingOutboundActions;
    /// Includes preparations with incomplete private-history persistence even
    /// when their shared transaction is committed.
    std::vector<PreparedStorageTransaction> unresolvedTransactions;
    /// Successful reservations not associated with a prepared transaction;
    /// includes reservations made before a preparation could be persisted.
    std::vector<ScopedReservation> unresolvedReservations;
    std::optional<PrivateHistoryRecord> localHistory;
    std::optional<PendingImportIntent> initialImport;
    /// Opaque Markoff quarantine records acknowledged only after this provider
    /// has durably retained them. Records are append-only recovery evidence.
    std::vector<QByteArray> quarantinedOperations;
    /// Last host-acknowledged Markdown export, if any. This watermark is only
    /// an optimization for detecting pending exports; accepted history is
    /// always retained and remains the recovery authority.
    std::optional<AcceptedRevision> exportedRevision;
};

struct MarkdownExportSnapshot {
    QByteArray markdown;
    AcceptedRevision acceptedRevision;
};

/// Owns the exclusive document-service lease for its entire lifetime. All
/// methods are synchronous, run on the session owner's thread, and invoke no
/// consumer callbacks. Opening storage does not make an editor writable:
/// Markoff must complete recovery/replay and restore accepted local history.
class DocumentStorageSession {
public:
    virtual ~DocumentStorageSession();
    [[nodiscard]] virtual const DocumentStorageScope& scope() const noexcept = 0;
    [[nodiscard]] virtual StorageMode mode() const noexcept = 0;
    [[nodiscard]] virtual const CollabText::ActorId& actor() const noexcept = 0;

    [[nodiscard]] virtual CollabText::Result<StorageRecoveryState> readRecoveryState() = 0;
    /// Durably bind the one authorized initial-import transaction and exact
    /// source bytes before allocating or publishing its generated operations.
    [[nodiscard]] virtual CollabText::Result<void> beginInitialImport(
        const PendingImportIntent& intent) = 0;
    [[nodiscard]] virtual CollabText::Result<ScopedReservation> reserve(
        const QString& stream, quint64 count) = 0;
    [[nodiscard]] virtual CollabText::Result<void> observeCounter(
        const QString& stream, quint64 counter) = 0;
    [[nodiscard]] virtual CollabText::Result<quint64> nextCounter(
        const QString& stream) const = 0;

    [[nodiscard]] virtual CollabText::Result<void> prepare(
        const PreparedStorageTransaction& transaction) = 0;
    /// No bare I/O-error return: publication certainty is always explicit.
    [[nodiscard]] virtual PublicationResult append(const QByteArray& transactionId) = 0;
    /// Atomically append an exact, Markoff-validated remote MAB and claim its
    /// scoped operation identities. The provider does not interpret MAB bytes;
    /// Markoff must validate them and supply matching identity claims first.
    /// Exact retry after an uncertain result returns the original commit.
    [[nodiscard]] virtual PublicationResult acceptRemoteAction(
        const PreparedStorageTransaction& transaction) = 0;
    [[nodiscard]] virtual PublicationResult inspect(
        const PreparedStorageTransaction& expectedTransaction) = 0;
    /// Once inspect proves an exact transaction absent, durably tombstone its
    /// ID against later prepare/append and return its full reservations to gap
    /// recovery. This also records a valid transaction whose prepare failed
    /// before the provider could persist it.
    [[nodiscard]] virtual CollabText::Result<void> resolveAbsentPrepared(
        const PreparedStorageTransaction& expectedTransaction) = 0;
    [[nodiscard]] virtual CollabText::Result<void> storeLocalHistory(
        const PrivateHistoryRecord& record) = 0;
    /// Durably retain one complete Markoff-encoded rejected remote envelope.
    /// Exact retries are idempotent; providers do not interpret the bytes.
    [[nodiscard]] virtual CollabText::Result<void> storeQuarantinedOperation(
        const QByteArray& record) = 0;
    /// Acknowledge an export only for the exact accepted prefix written by
    /// the host. A stale revision is rejected and must not clear newer dirty
    /// state. The provider records this watermark independently from history;
    /// it does not compact or remove accepted records.
    [[nodiscard]] virtual CollabText::Result<void> acknowledgeMarkdownExport(
        const AcceptedRevision& revision) = 0;
    /// Record a host-verified receipt for one locally accepted action. Exact
    /// retries are idempotent; a changed receipt for the same action conflicts.
    [[nodiscard]] virtual CollabText::Result<void> acknowledgeTransportReceipt(
        const QByteArray& actionId, const QByteArray& acceptedRevision) = 0;
};

class DocumentStorageProvider {
public:
    virtual ~DocumentStorageProvider();
    /// Missing registered history, authority corruption, or lease conflict
    /// fails opening. An empty prefix is recoverable but does not authorize
    /// edits: the host must persist its exact first-import decision with
    /// beginInitialImport() before reserving operation identities. Recovery
    /// returns that intent when one is pending.
    [[nodiscard]] virtual CollabText::Result<std::unique_ptr<DocumentStorageSession>> open(
        const DocumentStorageScope& scope, StorageMode mode) = 0;
};

/// Create an explicitly non-durable provider for previews and tests. It keeps
/// records only for the provider object's lifetime and refuses durable opens;
/// callers must never use it as a fallback for a failed durable open.
[[nodiscard]] CollabText::Result<std::unique_ptr<DocumentStorageProvider>>
makeEphemeralDocumentStorageProvider();

/// Create the restartable local reference provider used by integration and
/// fault-recovery tests. Its files are an isolated Markoff reference store,
/// not Corbomite's production journal. The root must be private to this
/// provider and outside synchronized document storage.
[[nodiscard]] CollabText::Result<std::unique_ptr<DocumentStorageProvider>>
makeReferenceDocumentStorageProvider(const std::filesystem::path& root);

} // namespace Markoff::VNext
