// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ActionStager.h>

#include <markoff/vnext/StreamNames.h>

#include <algorithm>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

Error invalidStagedOperation(const char* message)
{
    return Error(ErrorCode::invalid_identity, message);
}

bool sameOperationIdentity(const ActionOperation& left,
                           const ActionOperation& right)
{
    return left.stream == right.stream && left.producer == right.producer
        && left.counter == right.counter;
}

} // namespace

ActionStager::ActionStager(std::string document, uint64_t epoch,
                           uint64_t deliverySequence, QByteArray actionId,
                           ActionKind kind)
{
    m_bundle.document = std::move(document);
    m_bundle.epoch = epoch;
    m_bundle.deliverySequence = deliverySequence;
    m_bundle.actionId = std::move(actionId);
    m_bundle.kind = kind;
}

Result<void> ActionStager::append(ActionOperation operation,
                                  std::vector<ScopedReservation> reservations)
{
    if (std::any_of(m_bundle.operations.cbegin(), m_bundle.operations.cend(),
            [&](const ActionOperation& existing) {
                return sameOperationIdentity(existing, operation);
            }))
        return invalidStagedOperation("action repeats a stream operation identity");

    for (const auto& reservation : reservations) {
        if (reservation.stream.toUtf8()
                != QByteArray(operation.stream.data(),
                              static_cast<qsizetype>(operation.stream.size()))
            || reservation.actor != operation.producer
            || reservation.count == 0 || reservation.first == 0)
            return invalidStagedOperation("operation reservation has the wrong stream or actor");
    }
    const auto covering = std::count_if(reservations.cbegin(), reservations.cend(),
        [&](const ScopedReservation& reservation) {
            if (reservation.stream.toUtf8()
                    != QByteArray(operation.stream.data(),
                                  static_cast<qsizetype>(operation.stream.size()))
                || reservation.actor != operation.producer
                || operation.counter < reservation.first || reservation.count == 0)
                return false;
            return operation.counter - reservation.first < reservation.count;
        });
    if (covering != 1)
        return invalidStagedOperation("operation identity lacks one exact reservation binding");

    const size_t index = m_bundle.operations.size();
    m_bundle.operations.push_back(std::move(operation));
    m_operationReservations.push_back({index, std::move(reservations)});
    return {};
}

Result<void> ActionStager::add(const LocalListOperation& local)
{
    if (local.scope.document != m_bundle.document || local.scope.epoch != m_bundle.epoch
        || local.scope.primitive != Primitive::list
        || local.scope.stream != kOrderStreamName || local.bytes.isEmpty())
        return invalidStagedOperation("local list operation has the wrong action scope");
    ActionOperation operation{local.scope.stream, OperationPrimitive::idList,
        std::nullopt, local.identity.actor, local.identity.counter,
        OperationCodec::ctlo2, local.bytes};
    return append(std::move(operation), local.reservations);
}

Result<void> ActionStager::add(const LocalOperation& local)
{
    if (!local.encodable()) return *local.encoding_error;
    if (local.scope.document != m_bundle.document || local.scope.epoch != m_bundle.epoch
        || local.scope.primitive != Primitive::text || local.bytes.isEmpty())
        return invalidStagedOperation("local text operation has the wrong action scope");
    auto block = parseBlockTextStreamName(local.scope.stream);
    if (!block.ok()) return block.error();
    ActionOperation operation{local.scope.stream, OperationPrimitive::text,
        block.value(), local.identity.actor, local.identity.counter,
        OperationCodec::ctbo3, local.bytes};
    return append(std::move(operation), local.reservations);
}

Result<void> ActionStager::add(const LocalMapOperation& local)
{
    if (local.scope.document != m_bundle.document || local.scope.epoch != m_bundle.epoch)
        return invalidStagedOperation("local map operation has the wrong action scope");
    auto operation = toActionOperation(local);
    if (!operation.ok()) return operation.error();
    return append(std::move(operation).value(), {local.reservation});
}

void ActionStager::setSourceSnapshot(QByteArray snapshot)
{
    m_bundle.sourceSnapshot = std::move(snapshot);
}

} // namespace Markoff::VNext
