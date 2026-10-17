// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ActionTransactionBuilder.h>

#include <algorithm>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

Error invalidAction(const char* message)
{
    return Error(ErrorCode::malformed, message);
}

QByteArray routeBytes(const std::string& route)
{
    return QByteArray(route.data(), static_cast<qsizetype>(route.size()));
}

bool reservationCovers(const ScopedReservation& reservation,
                       const ActionOperation& operation)
{
    const QByteArray route = routeBytes(operation.stream);
    if (reservation.stream.toUtf8() != route
        || reservation.actor != operation.producer
        || reservation.first == 0
        || reservation.count == 0
        || operation.counter < reservation.first)
        return false;
    const quint64 offset = operation.counter - reservation.first;
    return offset < reservation.count;
}

} // namespace

Result<PreparedStorageTransaction> ActionTransactionBuilder::build(
    DocumentStorageSession& session,
    const ActionBundleCodec::Bundle& bundle,
    const OwnerHistorySnapshot& ownerHistory,
    const std::vector<OperationReservationBinding>& operationReservations)
{
    auto currentState = session.readRecoveryState();
    if (!currentState.ok()) return currentState.error();
    const StorageRecoveryState& recovery = currentState.value();

    const QByteArray documentBytes = bundle.document.empty()
        ? QByteArray{} : QByteArray(bundle.document.data(),
                                    static_cast<qsizetype>(bundle.document.size()));
    if (documentBytes.isEmpty()
        || QString::fromUtf8(documentBytes) != session.scope().documentKey
        || bundle.epoch != session.scope().epoch
        || bundle.actionId.isEmpty()
        || !recovery.unresolvedTransactions.empty()
        || recovery.acceptedRevision.token.isEmpty())
        return invalidAction("action scope, revision, or lifecycle state is invalid");

    auto reservationOrder = [](const ScopedReservation& left,
                               const ScopedReservation& right) {
        if (left.stream != right.stream) return left.stream < right.stream;
        const auto leftActor = left.actor.bytes();
        const auto rightActor = right.actor.bytes();
        if (leftActor != rightActor) return leftActor < rightActor;
        if (left.first != right.first) return left.first < right.first;
        return left.count < right.count;
    };
    if (operationReservations.size() != bundle.operations.size())
        return invalidAction("every MAB operation must bind its provider reservations");
    std::vector<bool> boundOperations(bundle.operations.size(), false);
    std::vector<ScopedReservation> staged;
    for (const auto& binding : operationReservations) {
        if (binding.operationIndex >= bundle.operations.size()
            || boundOperations[binding.operationIndex])
            return invalidAction("operation reservation binding index is invalid or repeated");
        boundOperations[binding.operationIndex] = true;
        const auto& operation = bundle.operations[binding.operationIndex];
        const QByteArray operationRoute = routeBytes(operation.stream);
        if (std::any_of(binding.reservations.cbegin(), binding.reservations.cend(),
                [&](const ScopedReservation& reservation) {
                    return reservation.stream.toUtf8() != operationRoute
                        || reservation.actor != operation.producer;
                }))
            return invalidAction("operation is bound to another stream's reservation");
        const auto covering = std::count_if(
            binding.reservations.cbegin(), binding.reservations.cend(),
            [&](const ScopedReservation& reservation) {
                return reservationCovers(reservation, operation);
            });
        if (covering != 1)
            return invalidAction("operation identity is not covered by its own reservation");
        staged.insert(staged.end(), binding.reservations.cbegin(),
                      binding.reservations.cend());
    }
    if (std::find(boundOperations.cbegin(), boundOperations.cend(), false)
        != boundOperations.cend())
        return invalidAction("MAB operation has no reservation binding");
    auto unresolved = recovery.unresolvedReservations;
    std::sort(staged.begin(), staged.end(), reservationOrder);
    std::sort(unresolved.begin(), unresolved.end(), reservationOrder);
    if (staged != unresolved)
        return invalidAction("staged operations do not account for every provider reservation");

    if (ownerHistory.document != bundle.document
        || ownerHistory.epoch != bundle.epoch
        || ownerHistory.owner != session.actor()
        || ownerHistory.transactionId != bundle.actionId
        || ownerHistory.acceptedPrior != recovery.acceptedRevision.token
        || ownerHistory.actionIndex.document != bundle.document
        || ownerHistory.actionIndex.epoch != bundle.epoch
        || ownerHistory.actionIndex.owner != session.actor()
        || ownerHistory.actionIndex.acceptedPrior != recovery.acceptedRevision.token)
        return invalidAction("owner history does not bind to this action and accepted prefix");

    const auto matchingEvents = std::count_if(
        ownerHistory.actionIndex.events.cbegin(), ownerHistory.actionIndex.events.cend(),
        [&](const ActionHistoryEvent& event) {
            return event.actionId == bundle.actionId && event.kind == bundle.kind;
        });
    if (matchingEvents != 1)
        return invalidAction("owner history must contain exactly one matching action event");

    auto validIdentities = ActionBundleCodec::validateOperationIdentities(bundle);
    if (!validIdentities.ok()) return validIdentities.error();
    auto sharedBytes = ActionBundleCodec::encode(bundle);
    if (!sharedBytes.ok()) return sharedBytes.error();
    auto privateBytes = OwnerHistoryCodec::encode(ownerHistory);
    if (!privateBytes.ok()) return privateBytes.error();

    PreparedStorageTransaction transaction;
    transaction.transactionId = bundle.actionId;
    transaction.expectedRevision = recovery.acceptedRevision;
    transaction.sharedRecordFormat = QByteArrayLiteral("MAB/3");
    transaction.sharedRecord = std::move(sharedBytes).value();
    transaction.reservations = std::move(unresolved);
    transaction.localHistoryAfterCommit = std::move(privateBytes).value();
    transaction.operationIdentities.reserve(bundle.operations.size());
    for (const auto& operation : bundle.operations) {
        const QByteArray route = routeBytes(operation.stream);
        transaction.operationIdentities.push_back({
            QString::fromUtf8(route), operation.producer, operation.counter});
    }
    return transaction;
}

} // namespace Markoff::VNext
