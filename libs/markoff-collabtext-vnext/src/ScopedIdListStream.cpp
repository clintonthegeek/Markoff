// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ScopedIdListStream.h>
#include <markoff/vnext/StreamNames.h>
#include <markoff/vnext/UndoDepth.h>

#include <collabtext/Serialization.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <string>
#include <utility>

namespace Markoff::VNext {

namespace {
bool validUtf8Document(const std::string& document)
{
    const QByteArray bytes(document.data(), static_cast<qsizetype>(document.size()));
    return Markoff::TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool sameIdentity(const CollabText::Crdt::Identity& left,
                  const CollabText::Crdt::Identity& right)
{
    return left.actor == right.actor && left.counter == right.counter;
}
} // namespace

class ScopedIdListStream::Allocator final : public CollabText::Crdt::IdentityAllocator {
public:
    Allocator(DocumentStorageSession& session, CollabText::PositionScope scope)
        : m_session(session), m_scope(std::move(scope)) {}

    CollabText::Result<uint64_t> reserve_run(uint64_t count) override
    {
        if (m_observationError) return *m_observationError;
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto reservation = m_session.reserve(stream, count);
        if (!reservation.ok()) return reservation.error();
        if (reservation.value().stream != stream
            || reservation.value().actor != m_session.actor()
            || reservation.value().count != count || reservation.value().first == 0)
            return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                     "provider returned a mismatched list reservation");
        m_pendingReservations.push_back(reservation.value());
        return reservation.value().first;
    }

    void observe(uint64_t counter) override
    {
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto result = m_session.observeCounter(stream, counter);
        if (!result.ok()) m_observationError = result.error();
    }

    uint64_t peek() const noexcept override
    {
        if (m_observationError) return 0;
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto result = m_session.nextCounter(stream);
        if (!result.ok()) {
            m_observationError = result.error();
            return 0;
        }
        return result.value();
    }

    const CollabText::ActorId& actor() const noexcept override
    {
        return m_session.actor();
    }

    std::vector<ScopedReservation> takeReservations()
    {
        std::vector<ScopedReservation> result;
        result.swap(m_pendingReservations);
        return result;
    }

private:
    DocumentStorageSession& m_session;
    CollabText::PositionScope m_scope;
    mutable std::optional<CollabText::Error> m_observationError;
    std::vector<ScopedReservation> m_pendingReservations;
};

ScopedIdListStream::ScopedIdListStream(
    DocumentStorageSession& session, CollabText::PositionScope scope,
    std::shared_ptr<Allocator> allocator,
    std::unique_ptr<CollabText::IdListEngine> engine)
    : m_session(&session), m_scope(std::move(scope)),
      m_allocator(std::move(allocator)), m_engine(std::move(engine)) {}

ScopedIdListStream::~ScopedIdListStream() = default;

CollabText::Result<std::unique_ptr<ScopedIdListStream>> ScopedIdListStream::create(
    DocumentStorageSession& session, CollabText::PositionScope scope)
{
    using namespace CollabText;
    if (scope.document.empty() || scope.document.size() > 256
        || !validUtf8Document(scope.document) || scope.epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "list stream requires a valid document key and epoch");
    if (scope.primitive != Primitive::list)
        return Error(ErrorCode::wrong_primitive,
                     "scoped IdList stream requires list primitive");
    if (scope.stream != std::string(kOrderStreamName))
        return Error(ErrorCode::unknown_stream,
                     "scoped IdList stream requires the canonical order stream");
    const auto documentKey = QString::fromUtf8(
        scope.document.data(), static_cast<qsizetype>(scope.document.size()));
    if (documentKey.toUtf8() != QByteArray::fromStdString(scope.document)
        || session.scope().documentKey != documentKey
        || session.scope().epoch != scope.epoch)
        return Error(ErrorCode::wrong_document,
                     "provider session does not match list stream scope");
    auto allocator = std::make_shared<Allocator>(session, scope);
    auto engine = IdListEngine::create(allocator, scope);
    if (!engine.ok()) return engine.error();
    auto bounded = engine.value()->set_max_undo_depth(kEngineUndoGroupCeiling);
    if (!bounded.ok()) return bounded.error();
    return std::unique_ptr<ScopedIdListStream>(new ScopedIdListStream(
        session, std::move(scope), std::move(allocator), std::move(engine).value()));
}

const CollabText::PositionScope& ScopedIdListStream::scope() const noexcept
{
    return m_scope;
}

const CollabText::ActorId& ScopedIdListStream::actor() const noexcept
{
    return m_allocator->actor();
}

CollabText::Result<LocalListOperation> ScopedIdListStream::wrap(
    CollabText::Crdt::IdListOperation operation,
    std::optional<BlockIdentity> insertedBlock)
{
    const auto identity = CollabText::Crdt::idlist_op_identity(operation);
    if (!identity.actor.valid() || identity.counter == 0)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "list operation has no valid identity");
    auto encoded = CollabText::Crdt::encode_idlist_operation(operation);
    if (!encoded.ok()) return encoded.error();
    auto reservations = m_allocator->takeReservations();
    return LocalListOperation{
        m_scope, identity, std::move(operation),
        QByteArray(encoded.value().data(), static_cast<qsizetype>(encoded.value().size())),
        insertedBlock, std::move(reservations)};
}

CollabText::Result<LocalListOperation> ScopedIdListStream::insertAfter(
    std::optional<BlockIdentity> after, uint64_t applicationValue)
{
    CollabText::Crdt::Anchor anchor = CollabText::Crdt::Anchor::min();
    if (after) {
        auto resolved = anchorOf(*after);
        if (!resolved.ok()) return resolved.error();
        anchor = std::move(resolved).value();
    }
    auto before = m_engine->visible_entries();
    if (!before.ok()) return before.error();
    auto operation = m_engine->try_insert_after(anchor, applicationValue);
    if (!operation.ok()) return operation.error();
    auto afterEntries = m_engine->visible_entries();
    if (!afterEntries.ok()) return afterEntries.error();
    std::optional<BlockIdentity> inserted;
    for (const auto& entry : afterEntries.value()) {
        const bool existed = std::any_of(before.value().begin(), before.value().end(),
            [&](const auto& prior) { return prior.identity == entry.identity; });
        if (existed) continue;
        if (inserted)
            return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                     "list insertion produced multiple new elements");
        inserted = BlockIdentity{entry.identity.actor, entry.identity.counter};
    }
    if (!inserted)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "list insertion produced no structural element");
    return wrap(std::move(operation).value(), inserted);
}

CollabText::Result<LocalListOperation> ScopedIdListStream::remove(
    const BlockIdentity& identity)
{
    auto anchor = anchorOf(identity);
    if (!anchor.ok()) return anchor.error();
    auto operation = m_engine->try_remove_at(anchor.value());
    if (!operation.ok()) return operation.error();
    return wrap(std::move(operation).value(), std::nullopt);
}

CollabText::Result<std::optional<LocalListOperation>> ScopedIdListStream::undo()
{
    auto result = m_engine->try_undo();
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalListOperation>{};
    auto operation = wrap(std::move(*result.value()), std::nullopt);
    if (!operation.ok()) return operation.error();
    return std::optional<LocalListOperation>(std::move(operation).value());
}

CollabText::Result<std::optional<LocalListOperation>> ScopedIdListStream::redo()
{
    auto result = m_engine->try_redo();
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalListOperation>{};
    auto operation = wrap(std::move(*result.value()), std::nullopt);
    if (!operation.ok()) return operation.error();
    return std::optional<LocalListOperation>(std::move(operation).value());
}

CollabText::Result<std::optional<LocalListOperation>>
ScopedIdListStream::undoGroup(const CollabText::Crdt::Identity& groupId)
{
    auto result = m_engine->try_undo_group(groupId);
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalListOperation>{};
    auto operation = wrap(std::move(*result.value()), std::nullopt);
    if (!operation.ok()) return operation.error();
    return std::optional<LocalListOperation>(std::move(operation).value());
}

CollabText::Result<std::optional<LocalListOperation>>
ScopedIdListStream::redoGroup(const CollabText::Crdt::Identity& groupId)
{
    auto result = m_engine->try_redo_group(groupId);
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalListOperation>{};
    auto operation = wrap(std::move(*result.value()), std::nullopt);
    if (!operation.ok()) return operation.error();
    return std::optional<LocalListOperation>(std::move(operation).value());
}

CollabText::Crdt::LocalHistorySnapshot
ScopedIdListStream::localHistorySnapshot() const
{
    return m_engine->local_history_snapshot();
}

CollabText::Result<void> ScopedIdListStream::restoreLocalHistory(
    const CollabText::Crdt::LocalHistorySnapshot& snapshot)
{
    return m_engine->restore_local_history(snapshot);
}

CollabText::Result<void> ScopedIdListStream::retireOldestUndoGroups(std::size_t count)
{
    using namespace CollabText;
    if (count == 0)
        return {};
    const auto before = m_engine->local_history_snapshot();
    if (count > before.entries.size())
        return Error(ErrorCode::history_unavailable,
                     "undo retirement exceeds the recorded group prefix");
    auto lowered = m_engine->set_max_undo_depth(before.entries.size() - count);
    if (!lowered.ok())
        return lowered.error();
    auto raised = m_engine->set_max_undo_depth(kEngineUndoGroupCeiling);
    if (!raised.ok())
        return raised.error();
    const auto after = m_engine->local_history_snapshot();
    if (after.entries.size() + count != before.entries.size()
        || after.retired_entries.size() < before.retired_entries.size() + count)
        return Error(ErrorCode::history_unavailable,
                     "undo retirement did not record the oldest groups");
    return {};
}

CollabText::Result<void> ScopedIdListStream::acknowledgeUndoRetirements(
    const std::vector<CollabText::Crdt::Identity>& groupIds)
{
    if (groupIds.empty())
        return {};
    return m_engine->acknowledge_undo_retirements(groupIds);
}

CollabText::Result<CollabText::Crdt::Anchor> ScopedIdListStream::anchorOf(
    const BlockIdentity& identity, CollabText::Crdt::Bias bias) const
{
    if (!identity.actor.valid() || identity.counter == 0)
        return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                 "block identity is invalid");
    return m_engine->try_anchor_of(
        CollabText::Crdt::Identity{identity.actor, identity.counter}, bias);
}

CollabText::Result<std::vector<ListProjectionEntry>>
ScopedIdListStream::visibleEntries() const
{
    auto visible = m_engine->visible_entries();
    if (!visible.ok()) return visible.error();
    std::vector<ListProjectionEntry> result;
    result.reserve(visible.value().size());
    for (const auto& entry : visible.value())
        result.push_back({{entry.identity.actor, entry.identity.counter},
                          entry.application_value});
    return result;
}

CollabText::Result<CollabText::ApplyOutcome> ScopedIdListStream::applyCommittedCounterGap(
    const QByteArray& scopedRecord, const QByteArray& expectedAcceptedPrior)
{
    auto decoded = CounterGapCodec::decode(scopedRecord, m_scope, expectedAcceptedPrior);
    if (!decoded.ok()) return decoded.error();
    const auto bytes = CollabText::Crdt::encode_counter_gap(decoded.value().gap);
    if (!bytes.ok()) return bytes.error();
    return m_engine->apply_counter_gap(m_scope, bytes.value());
}

CollabText::Result<CollabText::ApplyOutcome> ScopedIdListStream::applyRemote(
    const RemoteListOperation& remote)
{
    using namespace CollabText;
    if (remote.scope != m_scope)
        return Error(ErrorCode::wrong_stream,
                     "remote list operation has a different position scope");
    auto decoded = Crdt::decode_idlist_operation(std::string_view(
        remote.bytes.constData(), static_cast<size_t>(remote.bytes.size())));
    if (!decoded.ok()) return decoded.error();
    if (!sameIdentity(Crdt::idlist_op_identity(decoded.value()), remote.identity))
        return Error(ErrorCode::invalid_identity,
                     "remote list route identity does not match encoded operation");
    if (remote.identity.actor == m_session->actor()) {
        auto observed = m_session->observeCounter(
            QString::fromUtf8(m_scope.stream.data(),
                              static_cast<qsizetype>(m_scope.stream.size())),
            remote.identity.counter);
        if (!observed.ok()) return observed.error();
    }
    auto outcomes = m_engine->apply_ops({std::move(decoded).value()});
    if (outcomes.size() != 1)
        return Error(ErrorCode::malformed,
                     "Core returned an invalid list application result count");
    return outcomes.front();
}

CollabText::Result<void> ScopedIdListStream::replaceStateFrom(
    ScopedIdListStream& replayed)
{
    if (m_session != replayed.m_session || m_scope != replayed.m_scope)
        return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                                 "cannot refresh a list stream from another scope");
    m_allocator.swap(replayed.m_allocator);
    m_engine.swap(replayed.m_engine);
    return {};
}

} // namespace Markoff::VNext
