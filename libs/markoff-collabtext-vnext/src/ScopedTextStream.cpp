// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/StreamNames.h>

#include <collabtext/Serialization.h>

#include <markoff/core/TextUnits.h>

#include <limits>
#include <string>
#include <utility>

namespace Markoff::VNext {

namespace {

bool validUtf8Document(const std::string& document)
{
    const QByteArray bytes(document.data(), static_cast<qsizetype>(document.size()));
    return Markoff::TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

} // namespace

class ScopedTextStream::Allocator final : public CollabText::Crdt::IdentityAllocator {
public:
    static CollabText::Result<std::shared_ptr<Allocator>> create(
        DocumentStorageSession& session, const CollabText::PositionScope& scope)
    {
        const auto documentKey = QString::fromUtf8(
            scope.document.data(), static_cast<qsizetype>(scope.document.size()));
        if (documentKey.toUtf8() != QByteArray::fromStdString(scope.document)
            || session.scope().documentKey != documentKey
            || session.scope().epoch != scope.epoch)
            return CollabText::Error(CollabText::ErrorCode::wrong_document,
                                     "provider session does not match text stream scope");
        return std::shared_ptr<Allocator>(new Allocator(
            &session, scope));
    }

    CollabText::Result<uint64_t> reserve_run(uint64_t count) override
    {
        if (m_observationError) return *m_observationError;
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto reservation = m_session->reserve(stream, count);
        if (!reservation.ok()) return reservation.error();
        if (reservation.value().stream != stream
            || reservation.value().actor != m_session->actor()
            || reservation.value().count != count || reservation.value().first == 0)
            return CollabText::Error(CollabText::ErrorCode::invalid_identity,
                                     "provider returned a mismatched scoped reservation");
        m_pendingReservations.push_back(reservation.value());
        return reservation.value().first;
    }

    void observe(uint64_t counter) override
    {
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto observed = m_session->observeCounter(stream, counter);
        if (!observed.ok()) m_observationError = observed.error();
    }

    uint64_t peek() const noexcept override
    {
        if (m_observationError) return 0;
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        auto next = m_session->nextCounter(stream);
        if (!next.ok()) {
            m_observationError = next.error();
            return 0;
        }
        return next.value();
    }

    const CollabText::ActorId& actor() const noexcept override
    {
        return m_session->actor();
    }

    CollabText::Result<void> observeRemote(uint64_t counter)
    {
        const QString stream = QString::fromUtf8(
            m_scope.stream.data(), static_cast<qsizetype>(m_scope.stream.size()));
        return m_session->observeCounter(stream, counter);
    }

    std::vector<ScopedReservation> takeReservations()
    {
        std::vector<ScopedReservation> result;
        result.swap(m_pendingReservations);
        return result;
    }

private:
    Allocator(DocumentStorageSession* session, CollabText::PositionScope scope)
        : m_scope(std::move(scope)), m_session(session) {}

    CollabText::PositionScope m_scope;
    DocumentStorageSession* m_session;
    mutable std::optional<CollabText::Error> m_observationError;
    std::vector<ScopedReservation> m_pendingReservations;
};

ScopedTextStream::ScopedTextStream(
    CollabText::PositionScope scope,
    std::shared_ptr<Allocator> allocator,
    std::unique_ptr<CollabText::CrdtEngine> engine)
    : m_scope(std::move(scope)), m_allocator(std::move(allocator)),
      m_engine(std::move(engine)) {}

ScopedTextStream::~ScopedTextStream() = default;

CollabText::Result<std::unique_ptr<ScopedTextStream>> ScopedTextStream::create(
    DocumentStorageSession& session,
    CollabText::PositionScope scope)
{
    using namespace CollabText;
    if (scope.document.empty() || scope.document.size() > 256
        || !validUtf8Document(scope.document) || scope.epoch == 0)
        return Error(ErrorCode::invalid_identity,
                     "text stream requires a valid document key and epoch");
    if (scope.primitive != Primitive::text)
        return Error(ErrorCode::wrong_primitive,
                     "scoped text stream requires text primitive");
    if (!parseBlockTextStreamName(scope.stream).ok())
        return Error(ErrorCode::unknown_stream,
                     "scoped text stream name is not a canonical block identity");

    auto allocator = Allocator::create(session, scope);
    if (!allocator.ok()) return allocator.error();
    auto engine = CrdtEngine::create(allocator.value(), scope);
    if (!engine.ok()) return engine.error();
    return std::unique_ptr<ScopedTextStream>(new ScopedTextStream(
        std::move(scope), std::move(allocator).value(),
        std::move(engine).value()));
}

QByteArray ScopedTextStream::text() const
{
    const std::string current = m_engine->text();
    return QByteArray(current.data(), static_cast<qsizetype>(current.size()));
}

CollabText::Result<LocalOperation> ScopedTextStream::replaceUtf8(
    qsizetype byteStart, qsizetype byteEnd, const QByteArray& replacement)
{
    using namespace CollabText;
    if (m_initialImportPending)
        return Error(ErrorCode::awaiting_state,
                     "initial import must be durably committed before editing");
    const QByteArray current = text();
    const auto start = TextUnits::checkedByteToQtPos(current, byteStart);
    const auto end = TextUnits::checkedByteToQtPos(current, byteEnd);
    if (!start || !end || byteEnd < byteStart)
        return Error(ErrorCode::not_scalar_boundary,
                     "Markoff edit range is invalid UTF-8 or splits a scalar");
    if (!TextUnits::checkedByteToQtPos(replacement, replacement.size()))
        return Error(ErrorCode::invalid_unicode,
                     "Markoff replacement is not valid UTF-8");
    if (*start > std::numeric_limits<int>::max()
        || *end - *start > std::numeric_limits<int>::max())
        return Error(ErrorCode::limit_exceeded,
                     "Markoff edit exceeds CollabText UTF-16 offset range");

    auto operation = m_engine->try_replace(
        static_cast<int>(*start), static_cast<int>(*end - *start),
        std::string(replacement.constData(), static_cast<size_t>(replacement.size())));
    if (!operation.ok()) return operation.error();
    m_touched = true;
    return encodeLocal(std::move(operation).value());
}

CollabText::Result<std::optional<LocalOperation>> ScopedTextStream::seedInitialImport(
    const QByteArray& initialUtf8)
{
    using namespace CollabText;
    if (m_touched || m_initialImportPending || !text().isEmpty()
        || m_engine->undo_depth() != 0 || m_engine->deferred_operation_count() != 0)
        return Error(ErrorCode::history_unavailable,
                     "initial import requires an untouched fresh text stream");
    if (!TextUnits::checkedByteToQtPos(initialUtf8, initialUtf8.size()))
        return Error(ErrorCode::invalid_unicode,
                     "initial Markdown text is not valid UTF-8");

    if (initialUtf8.isEmpty()) {
        m_touched = true;
        m_initialImportPending = true;
        return std::optional<LocalOperation>{};
    }

    auto seeded = replaceUtf8(0, 0, initialUtf8);
    if (!seeded.ok()) return seeded.error();
    m_initialImportPending = true;
    return std::optional<LocalOperation>(std::move(seeded).value());
}

CollabText::Result<void> ScopedTextStream::commitInitialImport()
{
    using namespace CollabText;
    if (!m_initialImportPending)
        return Error(ErrorCode::invalid_identity,
                     "text stream has no pending initial import");

    Crdt::LocalHistorySnapshot emptyHistory;
    auto restored = m_engine->restore_local_history(emptyHistory);
    if (!restored.ok()) return restored.error();
    m_initialImportPending = false;
    return {};
}

CollabText::Result<QByteArray> ScopedTextStream::boundaryTokenAt(
    uint32_t utf8ByteOffset, CollabText::Affinity affinity) const
{
    auto boundary = m_engine->boundary_at(utf8ByteOffset, affinity);
    if (!boundary.ok()) return boundary.error();
    auto encoded = CollabText::encode_position(
        CollabText::PositionToken{std::move(boundary).value()});
    if (!encoded.ok()) return encoded.error();
    return QByteArray(encoded.value().data(),
                      static_cast<qsizetype>(encoded.value().size()));
}

CollabText::Result<CollabText::ResolvedTextPosition>
ScopedTextStream::resolveBoundary(const QByteArray& tokenBytes) const
{
    const std::string bytes(tokenBytes.constData(),
                            static_cast<size_t>(tokenBytes.size()));
    auto token = CollabText::decode_position_as<CollabText::TextBoundary>(bytes);
    if (!token.ok()) return token.error();
    return m_engine->resolve(token.value());
}

LocalOperation ScopedTextStream::encodeLocal(
    CollabText::Crdt::Operation operation)
{
    const auto identity = CollabText::Crdt::op_identity(operation);
    auto reservations = m_allocator->takeReservations();
    auto encoded = CollabText::Crdt::encode_operation(operation);
    if (!encoded.ok())
        return LocalOperation{m_scope, identity, std::move(operation), {},
                              encoded.error(), std::move(reservations)};
    return LocalOperation{
        m_scope, identity, std::move(operation),
        QByteArray(encoded.value().data(),
                   static_cast<qsizetype>(encoded.value().size())),
        std::nullopt, std::move(reservations)};
}

CollabText::Result<std::optional<LocalOperation>> ScopedTextStream::undo()
{
    if (m_initialImportPending)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "initial import must be committed before undo");
    auto result = m_engine->try_undo();
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalOperation>{};
    m_touched = true;
    return std::optional<LocalOperation>(encodeLocal(std::move(*result.value())));
}

CollabText::Result<std::optional<LocalOperation>> ScopedTextStream::redo()
{
    if (m_initialImportPending)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "initial import must be committed before redo");
    auto result = m_engine->try_redo();
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalOperation>{};
    m_touched = true;
    return std::optional<LocalOperation>(encodeLocal(std::move(*result.value())));
}

CollabText::Result<std::optional<LocalOperation>> ScopedTextStream::undoGroup(
    const CollabText::Crdt::Identity& groupId)
{
    if (m_initialImportPending)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "initial import must be committed before undo");
    auto result = m_engine->try_undo_group(groupId);
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalOperation>{};
    m_touched = true;
    return std::optional<LocalOperation>(encodeLocal(std::move(*result.value())));
}

CollabText::Result<std::optional<LocalOperation>> ScopedTextStream::redoGroup(
    const CollabText::Crdt::Identity& groupId)
{
    if (m_initialImportPending)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "initial import must be committed before redo");
    auto result = m_engine->try_redo_group(groupId);
    if (!result.ok()) return result.error();
    if (!result.value()) return std::optional<LocalOperation>{};
    m_touched = true;
    return std::optional<LocalOperation>(encodeLocal(std::move(*result.value())));
}

CollabText::Crdt::LocalHistorySnapshot ScopedTextStream::localHistorySnapshot() const
{
    if (m_initialImportPending) return {};
    return m_engine->local_history_snapshot();
}

CollabText::Result<void> ScopedTextStream::restoreLocalHistory(
    const CollabText::Crdt::LocalHistorySnapshot& snapshot)
{
    if (m_initialImportPending)
        return CollabText::Error(CollabText::ErrorCode::awaiting_state,
                                 "initial import must be committed before history restore");
    auto restored = m_engine->restore_local_history(snapshot);
    if (restored.ok()) m_touched = true;
    return restored;
}

const CollabText::ActorId& ScopedTextStream::actor() const noexcept
{
    return m_allocator->actor();
}

CollabText::Result<CollabText::ApplyOutcome> ScopedTextStream::applyRemote(
    const RemoteOperation& remote)
{
    using namespace CollabText;
    if (m_initialImportPending)
        return Error(ErrorCode::awaiting_state,
                     "initial import must be durably committed before remote apply");
    if (remote.scope.document != m_scope.document)
        return Error(ErrorCode::wrong_document, "remote text operation targets another document");
    if (remote.scope.stream != m_scope.stream)
        return Error(ErrorCode::wrong_stream, "remote text operation targets another stream");
    if (remote.scope.epoch != m_scope.epoch)
        return Error(ErrorCode::wrong_epoch, "remote text operation targets another epoch");
    if (remote.scope.primitive != Primitive::text)
        return Error(ErrorCode::wrong_primitive, "remote operation is not a text operation");

    const std::string bytes(remote.bytes.constData(),
                            static_cast<size_t>(remote.bytes.size()));
    auto decoded = Crdt::decode_operation(bytes);
    if (!decoded.ok()) return decoded.error();
    if (Crdt::op_identity(decoded.value()) != remote.identity)
        return Error(ErrorCode::actor_conflict,
                     "remote envelope identity does not match operation bytes");
    if (remote.identity.actor == m_allocator->actor()) {
        auto observed = m_allocator->observeRemote(remote.identity.counter);
        if (!observed.ok()) return observed.error();
    }
    auto applied = m_engine->apply_remote(decoded.value());
    m_touched = true;
    return applied;
}

CollabText::Result<CollabText::ApplyOutcome>
ScopedTextStream::applyCommittedCounterGap(
    const QByteArray& scopedRecord, const QByteArray& expectedAcceptedPrior)
{
    auto decoded = CounterGapCodec::decode(scopedRecord, m_scope, expectedAcceptedPrior);
    if (!decoded.ok()) return decoded.error();
    const auto bytes = CollabText::Crdt::encode_counter_gap(decoded.value().gap);
    if (!bytes.ok()) return bytes.error();
    auto applied = m_engine->apply_counter_gap(m_scope, bytes.value());
    if (applied.kind() != CollabText::ApplyOutcomeKind::rejected)
        m_touched = true;
    return applied;
}

const CollabText::PositionScope& ScopedTextStream::scope() const noexcept
{
    return m_scope;
}

CollabText::Result<void> ScopedTextStream::replaceStateFrom(
    ScopedTextStream& replayed)
{
    if (m_scope != replayed.m_scope)
        return CollabText::Error(CollabText::ErrorCode::wrong_stream,
                                 "cannot refresh a text stream from another scope");
    m_allocator.swap(replayed.m_allocator);
    m_engine.swap(replayed.m_engine);
    std::swap(m_touched, replayed.m_touched);
    std::swap(m_initialImportPending, replayed.m_initialImportPending);
    return {};
}

} // namespace Markoff::VNext
