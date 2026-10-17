// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/ActionBundleCodec.h>

#include <algorithm>
#include <set>

namespace Markoff::VNext {
namespace {

ProviderDocumentState stateFor(const StorageRecoveryState& recovery)
{
    if (!recovery.unresolvedTransactions.empty()
        || !recovery.unresolvedReservations.empty())
        return ProviderDocumentState::recoveryRequired;
    if (recovery.initialImport)
        return ProviderDocumentState::initialImportAuthorized;
    if (recovery.acceptedRecords.empty())
        return ProviderDocumentState::awaitingInitialImport;
    return ProviderDocumentState::ready;
}

bool sameRecord(const AcceptedStorageRecord& left,
                const AcceptedStorageRecord& right)
{
    return left.transactionId == right.transactionId
        && left.format == right.format
        && left.bytes == right.bytes
        && left.priorRevision == right.priorRevision
        && left.committedRevision == right.committedRevision;
}

} // namespace

using namespace CollabText;

ProviderBackedDocumentState::ProviderBackedDocumentState(
    std::unique_ptr<DocumentStorageSession> storage,
    std::unique_ptr<ActionPublicationCoordinator> coordinator,
    StorageRecoveryState recovery,
    std::unique_ptr<AcceptedPrefixProjection> projection,
    ProviderDocumentState state)
    : m_storage(std::move(storage))
    , m_coordinator(std::move(coordinator))
    , m_recovery(std::move(recovery))
    , m_projection(std::move(projection))
    , m_state(state)
    , m_ownerThread(std::this_thread::get_id())
{}

Result<std::unique_ptr<ProviderBackedDocumentState>>
ProviderBackedDocumentState::open(
    DocumentStorageProvider& provider, const DocumentStorageScope& scope,
    StorageMode mode)
{
    auto opened = provider.open(scope, mode);
    if (!opened.ok()) return opened.error();
    auto storage = std::move(opened).value();

    auto recovered = storage->readRecoveryState();
    if (!recovered.ok()) return recovered.error();
    auto recovery = std::move(recovered).value();

    auto coordinator = std::make_unique<ActionPublicationCoordinator>(*storage);
    if (!recovery.unresolvedTransactions.empty()) {
        for (const auto& transaction : recovery.unresolvedTransactions) {
            const auto outcome = coordinator->recover(transaction);
            if (outcome.state == ActionPublicationState::rejected
                && outcome.diagnostic)
                return *outcome.diagnostic;
            if (outcome.state == ActionPublicationState::absent) {
                bool rebuilt = false;
                const auto recoveredAbsence = coordinator->recoverAndRebuild(
                    transaction, [&](const StorageRecoveryState& state) -> Result<void> {
                        auto accepted = AcceptedPrefixProjection::replay(*storage, state);
                        if (!accepted.ok()) return accepted.error();
                        rebuilt = true;
                        return {};
                    });
                if (!rebuilt && recoveredAbsence.diagnostic)
                    return *recoveredAbsence.diagnostic;
            }
        }
        auto refreshed = storage->readRecoveryState();
        if (!refreshed.ok()) return refreshed.error();
        recovery = std::move(refreshed).value();
    }

    auto projection = AcceptedPrefixProjection::replay(*storage, recovery);
    if (!projection.ok()) return projection.error();

    const auto state = stateFor(recovery);
    return std::unique_ptr<ProviderBackedDocumentState>(
        new ProviderBackedDocumentState(std::move(storage), std::move(coordinator),
            std::move(recovery), std::move(projection).value(), state));
}

Result<ScopedIdListStream*> ProviderBackedDocumentState::stagingIdList()
{
    if (std::this_thread::get_id() != m_ownerThread)
        return Error(ErrorCode::unsupported_operation,
                     "document lifecycle calls must run on its owner thread");
    if (m_callActive)
        return Error(ErrorCode::reentrant_mutation,
                     "reentrant document lifecycle mutation is refused");
    if (m_state != ProviderDocumentState::ready
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return Error(ErrorCode::awaiting_state,
                     "document is not ready to stage an action");
    return m_projection->ensureIdList(*m_storage);
}

Result<ScopedTextStream*> ProviderBackedDocumentState::stagingTextStream(
    const BlockIdentity& block)
{
    if (std::this_thread::get_id() != m_ownerThread)
        return Error(ErrorCode::unsupported_operation,
                     "document lifecycle calls must run on its owner thread");
    if (m_callActive)
        return Error(ErrorCode::reentrant_mutation,
                     "reentrant document lifecycle mutation is refused");
    if (m_state != ProviderDocumentState::ready
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return Error(ErrorCode::awaiting_state,
                     "document is not ready to stage an action");
    return m_projection->ensureTextStream(*m_storage, block);
}

Result<ReplicatedMapStream*> ProviderBackedDocumentState::stagingMapStream(
    const MapScope& scope)
{
    if (std::this_thread::get_id() != m_ownerThread)
        return Error(ErrorCode::unsupported_operation,
                     "document lifecycle calls must run on its owner thread");
    if (m_callActive)
        return Error(ErrorCode::reentrant_mutation,
                     "reentrant document lifecycle mutation is refused");
    if (m_state != ProviderDocumentState::ready
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return Error(ErrorCode::awaiting_state,
                     "document is not ready to stage an action");
    return m_projection->ensureMapStream(*m_storage, scope);
}

void ProviderBackedDocumentState::setState(ProviderDocumentState state)
{
    if (m_state == state)
        return;
    m_state = state;
    if (m_onStateChanged)
        m_onStateChanged();
}

Result<void> ProviderBackedDocumentState::restoreAcceptedProjection()
{
    auto recovery = m_storage->readRecoveryState();
    if (!recovery.ok()) return recovery.error();
    auto projection = AcceptedPrefixProjection::replay(*m_storage, recovery.value());
    if (!projection.ok()) return projection.error();
    auto refreshed = m_projection->refreshFrom(*m_storage, *projection.value());
    if (!refreshed.ok()) return refreshed.error();
    m_recovery = std::move(recovery).value();
    setState(stateFor(m_recovery));
    return {};
}

Result<void> ProviderBackedDocumentState::authorizeInitialImport(
    const PendingImportIntent& intent)
{
    if (std::this_thread::get_id() != m_ownerThread)
        return Error(ErrorCode::unsupported_operation,
                     "document lifecycle calls must run on its owner thread");
    if (m_callActive)
        return Error(ErrorCode::reentrant_mutation,
                     "reentrant document lifecycle mutation is refused");
    struct CallGuard {
        bool& active;
        explicit CallGuard(bool& value) : active(value) { active = true; }
        ~CallGuard() { active = false; }
    } guard(m_callActive);

    if (m_state != ProviderDocumentState::awaitingInitialImport
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return Error(ErrorCode::awaiting_state,
                     "document is not awaiting an initial import");
    auto authorized = m_storage->beginInitialImport(intent);
    if (!authorized.ok()) return authorized.error();
    m_recovery.initialImport = intent;
    setState(ProviderDocumentState::initialImportAuthorized);
    return {};
}

ActionPublicationResult ProviderBackedDocumentState::publishAction(
    const PreparedStorageTransaction& transaction)
{
    return publishAction(transaction, nullptr);
}

ActionPublicationResult ProviderBackedDocumentState::publishAction(
    const PreparedStorageTransaction& transaction,
    const OwnerHistorySnapshot* stagedOwnerHistory)
{
    using namespace CollabText;
    if (std::this_thread::get_id() != m_ownerThread)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::unsupported_operation,
                  "document lifecycle calls must run on its owner thread")};
    if (m_callActive)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::reentrant_mutation,
                  "reentrant document lifecycle mutation is refused")};
    if (m_state != ProviderDocumentState::ready
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::awaiting_state,
                  "document must be ready or have an authorized initial import")};

    struct CallGuard {
        bool& active;
        explicit CallGuard(bool& value) : active(value) { active = true; }
        ~CallGuard() { active = false; }
    } guard(m_callActive);

    auto result = m_coordinator->publish(transaction);
    if (result.state != ActionPublicationState::committed
        && result.state != ActionPublicationState::committedHistoryPending) {
        if (result.state == ActionPublicationState::uncertain) {
            setState(ProviderDocumentState::recoveryRequired);
            return result;
        }
        if (result.state == ActionPublicationState::absent) {
            std::unique_ptr<AcceptedPrefixProjection> rebuiltProjection;
            auto rebuilt = m_coordinator->recoverAndRebuild(transaction,
                [&](const StorageRecoveryState& accepted) -> Result<void> {
                    auto projection = AcceptedPrefixProjection::replay(*m_storage, accepted);
                    if (!projection.ok()) return projection.error();
                    rebuiltProjection = std::move(projection).value();
                    return {};
                });
            if (rebuilt.state != ActionPublicationState::absent || !rebuiltProjection) {
                setState(ProviderDocumentState::recoveryRequired);
                return rebuilt;
            }
            auto recovery = m_storage->readRecoveryState();
            if (!recovery.ok()) {
                setState(ProviderDocumentState::recoveryRequired);
                result.diagnostic = recovery.error();
                return result;
            }
            auto refreshed = m_projection->refreshFrom(*m_storage, *rebuiltProjection);
            if (!refreshed.ok()) {
                setState(ProviderDocumentState::recoveryRequired);
                result.diagnostic = refreshed.error();
                return result;
            }
            m_recovery = std::move(recovery).value();
            setState(stateFor(m_recovery));
            return result;
        }
        // Definite absence/rejection must discard every tentative engine
        // mutation before returning. Reconcile the reservation barrier from
        // the same authoritative snapshot; only uncertainty keeps the staged
        // view visible.
        auto restored = restoreAcceptedProjection();
        if (!restored.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = restored.error();
            return result;
        }
        if (m_coordinator->blocked())
            setState(ProviderDocumentState::recoveryRequired);
        return result;
    }

    auto recovery = m_storage->readRecoveryState();
    if (!recovery.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = recovery.error();
        return result;
    }
    bool advancedStagedProjection = false;
    if (stagedOwnerHistory
        && result.state == ActionPublicationState::committed
        && m_recovery.acceptedRevision == transaction.expectedRevision
        && recovery.value().acceptedRecords.size()
            == m_recovery.acceptedRecords.size() + 1
        && recovery.value().unresolvedTransactions.empty()
        && recovery.value().unresolvedReservations.empty()
        && recovery.value().localHistory
        && recovery.value().localHistory->committedTransactionId
            == transaction.transactionId
        && recovery.value().localHistory->bytes == transaction.localHistoryAfterCommit
        && result.committedRevision
        && recovery.value().localHistory->acceptedRevision == *result.committedRevision) {
        bool exactPrefix = true;
        for (size_t i = 0; i < m_recovery.acceptedRecords.size(); ++i) {
            if (!sameRecord(m_recovery.acceptedRecords[i],
                            recovery.value().acceptedRecords[i])) {
                exactPrefix = false;
                break;
            }
        }
        const auto& accepted = recovery.value().acceptedRecords.back();
        if (exactPrefix
            && accepted.transactionId == transaction.transactionId
            && accepted.format == transaction.sharedRecordFormat
            && accepted.bytes == transaction.sharedRecord
            && accepted.priorRevision == transaction.expectedRevision
            && accepted.committedRevision == *result.committedRevision) {
            auto encodedHistory = OwnerHistoryCodec::encode(*stagedOwnerHistory);
            if (encodedHistory.ok()
                && encodedHistory.value() == transaction.localHistoryAfterCommit) {
                auto advanced = m_projection->acceptStagedLocalCommit(
                    accepted, *result.committedRevision, *stagedOwnerHistory);
                advancedStagedProjection = advanced.ok();
            }
        }
    }

    if (!advancedStagedProjection) {
        auto projection = AcceptedPrefixProjection::replay(*m_storage, recovery.value());
        if (!projection.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = projection.error();
            return result;
        }
        auto refreshed = m_projection->refreshFrom(*m_storage, *projection.value());
        if (!refreshed.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = refreshed.error();
            return result;
        }
    }
    m_recovery = std::move(recovery).value();
    setState(stateFor(m_recovery));
    if (m_onLocalActionCommitted
        && (result.state == ActionPublicationState::committed
            || result.state == ActionPublicationState::committedHistoryPending)
        && transaction.sharedRecordFormat == QByteArrayLiteral("MAB/3"))
        m_onLocalActionCommitted(transaction.transactionId, transaction.sharedRecord);
    return result;
}

ActionPublicationResult ProviderBackedDocumentState::acceptRemoteAction(
    const PreparedStorageTransaction& transaction)
{
    using namespace CollabText;
    auto refuse = [](Error error) {
        return ActionPublicationResult{ActionPublicationState::rejected,
                                       std::nullopt, std::move(error)};
    };
    if (std::this_thread::get_id() != m_ownerThread)
        return refuse(Error(ErrorCode::unsupported_operation,
                            "document lifecycle calls must run on its owner thread"));
    if (m_callActive)
        return refuse(Error(ErrorCode::reentrant_mutation,
                            "reentrant document lifecycle mutation is refused"));
    const auto previous = std::find_if(m_recovery.acceptedRecords.begin(),
        m_recovery.acceptedRecords.end(), [&](const AcceptedStorageRecord& record) {
            return record.transactionId == transaction.transactionId;
        });
    const AcceptedRevision expectedPrior = previous == m_recovery.acceptedRecords.end()
        ? m_recovery.acceptedRevision : previous->priorRevision;
    if (!transaction.remoteAcceptance || transaction.expectedRevision != expectedPrior)
        return refuse(Error(ErrorCode::counter_regression,
                            "remote action has the wrong mode or transaction prefix"));

    const QByteArray documentBytes = m_storage->scope().documentKey.toUtf8();
    const std::string document(documentBytes.constData(),
                               static_cast<size_t>(documentBytes.size()));
    auto bundle = ActionBundleCodec::decode(transaction.sharedRecord, document,
                                             m_storage->scope().epoch);
    if (!bundle.ok()) return refuse(bundle.error());
    if (transaction.sharedRecordFormat != QByteArrayLiteral("MAB/3")
        || bundle.value().actionId != transaction.transactionId
        || bundle.value().operations.size() != transaction.operationIdentities.size())
        return refuse(Error(ErrorCode::invalid_identity,
                            "remote MAB metadata does not match its transaction"));
    const bool registeringImport = m_state == ProviderDocumentState::awaitingInitialImport
        && bundle.value().kind == ActionKind::initialImport;
    if (!registeringImport && m_state != ProviderDocumentState::ready)
        return refuse(Error(ErrorCode::awaiting_state,
                            "remote actions require an imported, recovered document"));
    if (bundle.value().kind == ActionKind::initialImport && !registeringImport)
        return refuse(Error(ErrorCode::awaiting_state,
                            "remote initial import is only valid for an empty document"));
    auto identitiesValid = ActionBundleCodec::validateOperationIdentities(bundle.value());
    if (!identitiesValid.ok()) return refuse(identitiesValid.error());
    for (size_t i = 0; i < bundle.value().operations.size(); ++i) {
        const auto& operation = bundle.value().operations[i];
        const OperationIdentityClaim expected{
            QString::fromUtf8(operation.stream.data(),
                              static_cast<qsizetype>(operation.stream.size())),
            operation.producer, operation.counter};
        if (transaction.operationIdentities[i] != expected)
            return refuse(Error(ErrorCode::invalid_identity,
                                "remote MAB operation differs from its identity claim"));
    }
    if (previous != m_recovery.acceptedRecords.end()) {
        if (previous->format == transaction.sharedRecordFormat
            && previous->bytes == transaction.sharedRecord)
            return {ActionPublicationState::committed,
                    previous->committedRevision, std::nullopt};
        return refuse(Error(ErrorCode::pin_conflict,
                            "remote action ID was already accepted with different bytes"));
    }

    // Validate the complete candidate against the recovered prefix before it
    // becomes durable. In particular, replay rejects causal gaps and lets a
    // same-bundle IdList insertion establish a target before its text/map ops.
    StorageRecoveryState candidateRecovery = m_recovery;
    AcceptedStorageRecord candidateRecord;
    candidateRecord.transactionId = transaction.transactionId;
    candidateRecord.format = transaction.sharedRecordFormat;
    candidateRecord.bytes = transaction.sharedRecord;
    candidateRecord.priorRevision = m_recovery.acceptedRevision;
    candidateRecord.committedRevision.token = m_recovery.acceptedRevision.token
        + QByteArrayLiteral("/preflight/") + transaction.transactionId.toHex();
    candidateRecovery.acceptedRecords.push_back(candidateRecord);
    candidateRecovery.acceptedRevision = candidateRecord.committedRevision;
    auto candidateProjection = AcceptedPrefixProjection::replay(
        *m_storage, candidateRecovery);
    if (!candidateProjection.ok()) return refuse(candidateProjection.error());

    std::set<std::pair<std::string, uint64_t>> visibleBlocks;
    auto collectVisibleBlocks = [&](const ScopedIdListStream* idList)
        -> Result<void> {
        if (!idList) return {};
        auto entries = idList->visibleEntries();
        if (!entries.ok()) return entries.error();
        for (const auto& entry : entries.value()) {
            const auto actorBytes = entry.identity.actor.bytes();
            visibleBlocks.emplace(std::string(
                reinterpret_cast<const char*>(actorBytes.data()), actorBytes.size()),
                entry.identity.counter);
        }
        return {};
    };
    auto currentBlocks = collectVisibleBlocks(m_projection->idList());
    if (!currentBlocks.ok()) return refuse(currentBlocks.error());
    auto candidateBlocks = collectVisibleBlocks(candidateProjection.value()->idList());
    if (!candidateBlocks.ok()) return refuse(candidateBlocks.error());
    for (const auto& operation : bundle.value().operations) {
        if (!operation.targetBlock) continue;
        const auto actorBytes = operation.targetBlock->actor.bytes();
        const std::string actor(reinterpret_cast<const char*>(actorBytes.data()),
                                actorBytes.size());
        if (!visibleBlocks.contains({actor, operation.targetBlock->counter}))
            return refuse(Error(ErrorCode::unknown_stream,
                                "remote MAB targets a block absent from its accepted prefix"));
    }

    struct CallGuard {
        bool& active;
        explicit CallGuard(bool& value) : active(value) { active = true; }
        ~CallGuard() { active = false; }
    } guard(m_callActive);

    auto accepted = m_storage->acceptRemoteAction(transaction);
    if (accepted.disposition != PublicationDisposition::committed) {
        if (accepted.disposition == PublicationDisposition::uncertain)
            setState(ProviderDocumentState::recoveryRequired);
        const auto state = accepted.disposition == PublicationDisposition::absent
            ? ActionPublicationState::absent
            : accepted.disposition == PublicationDisposition::uncertain
                ? ActionPublicationState::uncertain
                : ActionPublicationState::rejected;
        return {state, accepted.committedRevision, accepted.diagnostic};
    }

    auto recovery = m_storage->readRecoveryState();
    if (!recovery.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        return {ActionPublicationState::uncertain, accepted.committedRevision,
                recovery.error()};
    }
    auto projection = AcceptedPrefixProjection::replay(*m_storage, recovery.value());
    if (!projection.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        return {ActionPublicationState::uncertain, accepted.committedRevision,
                projection.error()};
    }
    auto refreshed = m_projection->refreshFrom(*m_storage, *projection.value());
    if (!refreshed.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        return {ActionPublicationState::uncertain, accepted.committedRevision,
                refreshed.error()};
    }
    m_recovery = std::move(recovery).value();
    setState(stateFor(m_recovery));
    return {ActionPublicationState::committed, accepted.committedRevision,
            accepted.diagnostic};
}

ActionPublicationResult ProviderBackedDocumentState::publishStagedAction(
    const ActionStager& stager, const OwnerHistorySnapshot& ownerHistory)
{
    using namespace CollabText;
    if (std::this_thread::get_id() != m_ownerThread)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::unsupported_operation,
                  "document lifecycle calls must run on its owner thread")};
    if (m_callActive)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::reentrant_mutation,
                  "reentrant document lifecycle mutation is refused")};
    if (m_state != ProviderDocumentState::ready
        && m_state != ProviderDocumentState::initialImportAuthorized)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::awaiting_state,
                  "document must be ready or have an authorized initial import")};

    auto transaction = ActionTransactionBuilder::build(
        *m_storage, stager.bundle(), ownerHistory, stager.operationReservations());
    if (!transaction.ok()) {
        auto restored = restoreAcceptedProjection();
        if (!restored.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            return {ActionPublicationState::rejected, std::nullopt, restored.error()};
        }
        if (m_coordinator->blocked())
            setState(ProviderDocumentState::recoveryRequired);
        return {ActionPublicationState::rejected, std::nullopt, transaction.error()};
    }
    return publishAction(transaction.value(), &ownerHistory);
}

ActionPublicationResult ProviderBackedDocumentState::recoverActionAndRebuild(
    const PreparedStorageTransaction& transaction)
{
    if (std::this_thread::get_id() != m_ownerThread)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::unsupported_operation,
                  "document lifecycle calls must run on its owner thread")};
    if (m_callActive)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::reentrant_mutation,
                  "reentrant document lifecycle mutation is refused")};
    if (m_state != ProviderDocumentState::recoveryRequired)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::awaiting_state,
                  "document has no unresolved action recovery")};

    struct CallGuard {
        bool& active;
        explicit CallGuard(bool& value) : active(value) { active = true; }
        ~CallGuard() { active = false; }
    } guard(m_callActive);

    std::unique_ptr<AcceptedPrefixProjection> rebuiltProjection;
    auto result = m_coordinator->recoverAndRebuild(transaction,
        [&](const StorageRecoveryState& accepted) -> Result<void> {
            auto rebuilt = AcceptedPrefixProjection::replay(*m_storage, accepted);
            if (!rebuilt.ok()) return rebuilt.error();
            rebuiltProjection = std::move(rebuilt).value();
            return {};
        });
    const bool actionCommitted = result.state == ActionPublicationState::committed
        || result.state == ActionPublicationState::committedHistoryPending;
    if (actionCommitted) {
        auto recovery = m_storage->readRecoveryState();
        if (!recovery.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = recovery.error();
            return result;
        }
        auto projection = AcceptedPrefixProjection::replay(*m_storage, recovery.value());
        if (!projection.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = projection.error();
            return result;
        }
        auto refreshed = m_projection->refreshFrom(*m_storage, *projection.value());
        if (!refreshed.ok()) {
            setState(ProviderDocumentState::recoveryRequired);
            result.diagnostic = refreshed.error();
            return result;
        }
        m_recovery = std::move(recovery).value();
        setState(stateFor(m_recovery));
        return result;
    }
    if (result.state != ActionPublicationState::absent || !rebuiltProjection) {
        setState(ProviderDocumentState::recoveryRequired);
        return result;
    }

    auto recovery = m_storage->readRecoveryState();
    if (!recovery.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = recovery.error();
        return result;
    }
    auto refreshed = m_projection->refreshFrom(*m_storage, *rebuiltProjection);
    if (!refreshed.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = refreshed.error();
        return result;
    }
    m_recovery = std::move(recovery).value();
    setState(stateFor(m_recovery));
    return result;
}

ActionPublicationResult ProviderBackedDocumentState::publishGap(
    const PreparedStorageTransaction& transaction)
{
    using namespace CollabText;
    if (std::this_thread::get_id() != m_ownerThread)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::unsupported_operation,
                  "document lifecycle calls must run on its owner thread")};
    if (m_callActive)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::reentrant_mutation,
                  "reentrant document lifecycle mutation is refused")};
    if (m_state != ProviderDocumentState::recoveryRequired)
        return {ActionPublicationState::rejected, std::nullopt,
            Error(ErrorCode::awaiting_state,
                  "terminal coverage requires an unresolved recovery barrier")};
    struct CallGuard {
        bool& active;
        explicit CallGuard(bool& value) : active(value) { active = true; }
        ~CallGuard() { active = false; }
    } guard(m_callActive);

    auto result = m_coordinator->publishGap(transaction);
    if (result.state != ActionPublicationState::committed
        && result.state != ActionPublicationState::committedHistoryPending) {
        setState(ProviderDocumentState::recoveryRequired);
        return result;
    }
    auto recovery = m_storage->readRecoveryState();
    if (!recovery.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = recovery.error();
        return result;
    }
    auto projection = AcceptedPrefixProjection::replay(*m_storage, recovery.value());
    if (!projection.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = projection.error();
        return result;
    }
    auto refreshed = m_projection->refreshFrom(*m_storage, *projection.value());
    if (!refreshed.ok()) {
        setState(ProviderDocumentState::recoveryRequired);
        result.diagnostic = refreshed.error();
        return result;
    }
    m_recovery = std::move(recovery).value();
    setState(stateFor(m_recovery));
    return result;
}

} // namespace Markoff::VNext
