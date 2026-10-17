// SPDX-License-Identifier: GPL-3.0-or-later
// Markoff-owned smoke of CollabText's installed public Core contract.
// Build against a relocated package; this source must not include CollabText
// implementation headers or depend on its build tree.

#include <collabtext/ActorId.h>
#include <collabtext/Anchor.h>
#include <markoff/vnext/LocalHistoryCodec.h>
#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/ScopedOperationRouter.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/StreamNames.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace CollabText;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T take(Result<T> result)
{
    if (!result.ok()) {
        throw std::runtime_error(std::string(to_string(result.error().code()))
                                 + ": " + std::string(result.error().message()));
    }
    return std::move(result).value();
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view label)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path()
               / ("markoff-collabtext-vnext-" + std::string(label) + "-"
                  + std::to_string(nonce));
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

struct TestSession {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::VNext::DocumentStorageSession> session;

    const ActorId& actor() const noexcept { return session->actor(); }
    void reset() { session.reset(); provider.reset(); }
};

TestSession open_replica(const std::filesystem::path& path,
                         const Markoff::VNext::DocumentStorageScope& scope)
{
    auto provider = take(Markoff::VNext::makeReferenceDocumentStorageProvider(path));
    auto session = take(provider->open(scope, Markoff::VNext::StorageMode::durable));
    return {std::move(provider), std::move(session)};
}

void run_probe()
{
    constexpr std::string_view document = "markoff-installed-probe";
    TemporaryDirectory writer_dir("writer");
    TemporaryDirectory reader_dir("reader");
    TemporaryDirectory restart_reader_dir("restart-reader");
    const Markoff::VNext::DocumentStorageScope storage_scope{
        QString::fromUtf8(document.data(), static_cast<qsizetype>(document.size())), 1};
    auto writer_state = open_replica(writer_dir.path(), storage_scope);
    auto reader_state = open_replica(reader_dir.path(), storage_scope);
    auto restart_reader_state = open_replica(restart_reader_dir.path(), storage_scope);
    const auto stream = take(Markoff::VNext::blockTextStreamName(
        {writer_state.actor(), 1}));
    const PositionScope scope{std::string(document), stream, Primitive::text, 1};
    auto writer = take(Markoff::VNext::ScopedTextStream::create(*writer_state.session, scope));
    auto reader = take(Markoff::VNext::ScopedTextStream::create(*reader_state.session, scope));
    auto restart_reader = take(Markoff::VNext::ScopedTextStream::create(
        *restart_reader_state.session, scope));

    // Markoff edits arrive in UTF-8 byte coordinates; the adapter converts
    // them to the engine's UTF-16 edit units. 😀 is four bytes and two units.
    const QByteArray source = QByteArrayLiteral("A\xF0\x9F\x98\x80\xC3\xA9");
    const auto inserted = take(writer->replaceUtf8(0, 0, source));
    require(inserted.encodable(), "local edit could not be encoded for durable publication");
    const auto token_bytes = take(writer->boundaryTokenAt(5, Affinity::right));
    require(token_bytes.contains(char(0)),
            "CTPT boundary bytes were not retained as binary data");

    const auto replayed = take(reader->applyRemote(
        {inserted.scope, inserted.identity, inserted.bytes}));
    require(replayed.kind() == ApplyOutcomeKind::applied,
            "fresh peer did not apply the encoded block edit");
    const auto duplicate = take(reader->applyRemote(
        {inserted.scope, inserted.identity, inserted.bytes}));
    require(duplicate.kind() == ApplyOutcomeKind::duplicate,
            "replayed operation was not reported as a duplicate");
    require(reader->text() == source,
            "Unicode block bytes changed during operation replay");

    auto wrong_scope = inserted.scope;
    wrong_scope.document = "another-document";
    require(!reader->applyRemote({wrong_scope, inserted.identity, inserted.bytes}).ok(),
            "operation from another document passed route validation");
    auto wrong_identity = inserted.identity;
    ++wrong_identity.counter;
    require(!reader->applyRemote({inserted.scope, wrong_identity, inserted.bytes}).ok(),
            "operation with a forged identity passed envelope validation");

    // A split inside the four-byte scalar must be rejected without mutation.
    const auto invalid_range = reader->replaceUtf8(3, 4, QByteArrayLiteral("x"));
    require(!invalid_range.ok() && reader->text() == source,
            "split UTF-8 boundary was not rejected atomically");

    auto resolved = take(reader->resolveBoundary(token_bytes));
    require(resolved.byte_offset == 5,
            "original scoped token did not resolve after operation replay");

    // A compensation is another ordinary operation and is delivered once.
    auto undone = take(writer->undo());
    require(undone.has_value(), "local edit produced no undo compensation");
    require(undone->encodable(), "undo compensation could not be encoded");
    const auto undo_outcome = take(reader->applyRemote(
        {undone->scope, undone->identity, undone->bytes}));
    require(undo_outcome.kind() == ApplyOutcomeKind::applied,
            "peer did not apply the encoded undo compensation");
    require(writer->text().isEmpty() && reader->text().isEmpty(),
            "writer and peer diverged after undo compensation");

    auto redone = take(writer->redo());
    require(redone.has_value(), "undo compensation produced no redo operation");
    require(redone->encodable(), "redo compensation could not be encoded");
    const auto redo_outcome = take(reader->applyRemote(
        {redone->scope, redone->identity, redone->bytes}));
    require(redo_outcome.kind() == ApplyOutcomeKind::applied,
            "peer did not apply the encoded redo compensation");
    require(writer->text() == source && reader->text() == source,
            "writer and peer diverged after redo compensation");

    const Markoff::VNext::LocalOperation* replay_operations[] = {
        &inserted, &undone.value(), &redone.value()};
    for (const auto* operation : replay_operations) {
        const auto outcome = take(restart_reader->applyRemote(
            {operation->scope, operation->identity, operation->bytes}));
        require(outcome.kind() == ApplyOutcomeKind::applied,
                "fresh reader did not replay the complete operation prefix");
    }
    require(restart_reader->text() == source,
            "fresh reader diverged after complete operation replay");
    const auto restored_token = take(restart_reader->resolveBoundary(token_bytes));
    require(restored_token.byte_offset == 5,
            "original scoped token did not survive complete replay");

    const ActorId original_actor = writer_state.actor();
    const Markoff::VNext::LocalHistoryRecord history_record{
        scope, original_actor, writer->localHistorySnapshot()};
    const QByteArray encoded_history = take(
        Markoff::VNext::LocalHistoryCodec::encode(history_record));
    const auto decoded_history = take(Markoff::VNext::LocalHistoryCodec::decode(
        encoded_history, scope, original_actor));
    const auto same_history_entry = [](const Crdt::LocalUndoEntrySnapshot& a,
                                       const Crdt::LocalUndoEntrySnapshot& b) {
        return a.group_id == b.group_id
            && a.operation_ids == b.operation_ids
            && a.inserted_items == b.inserted_items
            && a.deletion_operations == b.deletion_operations
            && a.retained_bytes == b.retained_bytes;
    };
    require(decoded_history.snapshot.cursor == history_record.snapshot.cursor
                && !decoded_history.snapshot.entries.empty()
                && decoded_history.snapshot.entries.size() == history_record.snapshot.entries.size()
                && decoded_history.snapshot.retired_entries.size()
                    == history_record.snapshot.retired_entries.size()
                && same_history_entry(decoded_history.snapshot.entries.front(),
                                      history_record.snapshot.entries.front()),
            "local-history codec did not preserve the undo group snapshot");

    auto extended_history = history_record;
    auto retired_entry = extended_history.snapshot.entries.front();
    retired_entry.operation_ids.push_back(retired_entry.group_id);
    retired_entry.inserted_items.push_back(retired_entry.group_id);
    retired_entry.deletion_operations.push_back(retired_entry.group_id);
    retired_entry.retained_bytes += 17;
    extended_history.snapshot.retired_entries.push_back(retired_entry);
    const QByteArray extended_bytes = take(
        Markoff::VNext::LocalHistoryCodec::encode(extended_history));
    const auto extended_decoded = take(Markoff::VNext::LocalHistoryCodec::decode(
        extended_bytes, scope, original_actor));
    require(extended_decoded.snapshot.retired_entries.size() == 1
                && same_history_entry(extended_decoded.snapshot.retired_entries.front(),
                                      retired_entry),
            "local-history codec did not preserve retired entry fields");

    auto foreign_actor_bytes = original_actor.bytes();
    foreign_actor_bytes[0] ^= 1;
    const ActorId foreign_actor = ActorId::from_bytes(foreign_actor_bytes);
    const auto foreign_history = Markoff::VNext::LocalHistoryCodec::decode(
        encoded_history, scope, foreign_actor);
    require(!foreign_history.ok()
                && foreign_history.error().code() == ErrorCode::actor_conflict,
            "local-history codec accepted another actor's history");
    QByteArray bad_version = encoded_history;
    bad_version[3] = char(2);
    const auto unsupported_history = Markoff::VNext::LocalHistoryCodec::decode(
        bad_version, scope, original_actor);
    require(!unsupported_history.ok()
                && unsupported_history.error().code() == ErrorCode::unsupported_version,
            "local-history codec accepted an unsupported version");
    const auto truncated_history = Markoff::VNext::LocalHistoryCodec::decode(
        encoded_history.left(encoded_history.size() - 1), scope, original_actor);
    require(!truncated_history.ok(), "local-history codec accepted a truncated record");
    auto wrong_history_scope = scope;
    wrong_history_scope.stream = "another-stream";
    const auto misrouted_history = Markoff::VNext::LocalHistoryCodec::decode(
        encoded_history, wrong_history_scope, original_actor);
    require(!misrouted_history.ok()
                && misrouted_history.error().code() == ErrorCode::wrong_stream,
            "local-history codec accepted a record for another stream");
    writer.reset();
    writer_state.reset(); // release the provider session before reopening
    writer_state = open_replica(writer_dir.path(), storage_scope);
    require(writer_state.actor() == original_actor,
            "reopened private state changed the writer actor");
    writer = take(Markoff::VNext::ScopedTextStream::create(*writer_state.session, scope));
    for (const auto* operation : replay_operations)
        (void)take(writer->applyRemote(
            {operation->scope, operation->identity, operation->bytes}));
    const auto restored_history = writer->restoreLocalHistory(decoded_history.snapshot);
    require(restored_history.ok(), "reopened writer rejected its persisted history snapshot");
    auto restored_undo = take(writer->undo());
    require(restored_undo.has_value() && restored_undo->encodable()
                && writer->text().isEmpty(),
            "reopened writer did not restore and execute its local undo history");
    const auto restored_undo_apply = take(restart_reader->applyRemote(
        {restored_undo->scope, restored_undo->identity, restored_undo->bytes}));
    require(restored_undo_apply.kind() == ApplyOutcomeKind::applied
                && restart_reader->text().isEmpty(),
            "peer did not apply the restart-restored undo compensation");
    auto restored_redo = take(writer->redo());
    require(restored_redo.has_value() && restored_redo->encodable()
                && writer->text() == source,
            "reopened writer did not redo its restored local history");
    const auto restored_redo_apply = take(restart_reader->applyRemote(
        {restored_redo->scope, restored_redo->identity, restored_redo->bytes}));
    require(restored_redo_apply.kind() == ApplyOutcomeKind::applied
                && restart_reader->text() == source,
            "peer did not apply the restart-restored redo compensation");
    const auto appended = take(writer->replaceUtf8(
        source.size(), source.size(), QByteArrayLiteral("!")));
    require(appended.encodable() && appended.identity.actor == original_actor
                && appended.identity.counter > redone->identity.counter,
            "reopened writer did not continue its durable identity sequence");
    require(writer->text() == source + QByteArrayLiteral("!"),
            "reopened writer failed to continue from replayed state");

    const auto make_envelope = [](const Markoff::VNext::LocalOperation& operation,
                                  uint64_t sequence, QByteArray action_id) {
        return Markoff::VNext::RemoteOperation{
            operation.scope, operation.identity, operation.bytes, sequence,
            std::move(action_id), 0, 1};
    };

    // Unknown streams are retained within a strict budget. At capacity the
    // second envelope remains caller-owned and is reported as backpressure.
    TemporaryDirectory unknown_writer_dir("unknown-writer");
    TemporaryDirectory unknown_reader_dir("unknown-reader");
    auto unknown_writer_state = open_replica(unknown_writer_dir.path(), storage_scope);
    auto unknown_reader_state = open_replica(unknown_reader_dir.path(), storage_scope);
    const PositionScope unknown_scope{std::string(document),
                                      take(Markoff::VNext::blockTextStreamName(
                                          {writer_state.actor(), 2})),
                                      Primitive::text, 1};
    auto unknown_writer = take(Markoff::VNext::ScopedTextStream::create(
        *unknown_writer_state.session, unknown_scope));
    auto unknown_first = take(unknown_writer->replaceUtf8(0, 0, QByteArrayLiteral("u")));
    auto unknown_second = take(unknown_writer->replaceUtf8(1, 1, QByteArrayLiteral("v")));
    Markoff::VNext::ScopedOperationRouter bounded_router(
        std::string(document), 1, {1, 1024 * 1024});
    std::vector<Markoff::VNext::CompletedRemoteOperation> routed_completed;
    auto route_result = bounded_router.offer(
        make_envelope(unknown_first, 20, QByteArrayLiteral("unknown-20")), routed_completed);
    require(route_result.state == Markoff::VNext::RouteState::held_for_unknown_stream
                && bounded_router.heldOperationCount() == 1,
            "router did not retain an operation for an unknown stream");
    route_result = bounded_router.offer(
        make_envelope(unknown_second, 21, QByteArrayLiteral("unknown-21")), routed_completed);
    require(route_result.state == Markoff::VNext::RouteState::backpressured
                && bounded_router.heldOperationCount() == 1,
            "router exceeded its held-operation limit instead of backpressuring");

    auto unknown_reader = take(Markoff::VNext::ScopedTextStream::create(
        *unknown_reader_state.session, unknown_scope));
    require(!unknown_reader->resolveBoundary(token_bytes).ok(),
            "boundary token resolved in a different text stream");
    auto unknown_completed = take(bounded_router.registerStream(*unknown_reader));
    require(unknown_completed.size() == 1
                && unknown_completed[0].outcome == ApplyOutcomeKind::applied
                && unknown_completed[0].operation.delivery_sequence == 20
                && unknown_completed[0].operation.action_id == QByteArrayLiteral("unknown-20")
                && unknown_completed[0].operation.bytes == unknown_first.bytes
                && unknown_reader->text() == QByteArrayLiteral("u")
                && bounded_router.heldOperationCount() == 0,
            "registering an unknown stream did not drain its retained operation");
    route_result = bounded_router.offer(
        make_envelope(unknown_second, 21, QByteArrayLiteral("unknown-21")), routed_completed);
    require(route_result.state == Markoff::VNext::RouteState::applied
                && unknown_reader->text() == QByteArrayLiteral("uv"),
            "caller could not retry its backpressured envelope");

    // Scoped tokens preserve their affinity when another actor inserts at the
    // same UTF-8 boundary, and remain bound to their original stream.
    const auto left_token = take(reader->boundaryTokenAt(5, Affinity::left));
    TemporaryDirectory concurrent_writer_dir("concurrent-writer");
    auto concurrent_writer_state = open_replica(concurrent_writer_dir.path(), storage_scope);
    auto concurrent_writer = take(Markoff::VNext::ScopedTextStream::create(
        *concurrent_writer_state.session, scope));
    const auto concurrent_seed = take(concurrent_writer->applyRemote(
        {inserted.scope, inserted.identity, inserted.bytes}));
    require(concurrent_seed.kind() == ApplyOutcomeKind::applied,
            "concurrent writer did not receive the initial text state");
    const auto concurrent_insert = take(concurrent_writer->replaceUtf8(
        5, 5, QByteArrayLiteral("X")));
    const auto concurrent_apply = take(reader->applyRemote(
        {concurrent_insert.scope, concurrent_insert.identity,
         concurrent_insert.bytes}));
    require(concurrent_apply.kind() == ApplyOutcomeKind::applied,
            "concurrent boundary insertion did not apply");
    require(take(reader->resolveBoundary(token_bytes)).byte_offset == 6
                && take(reader->resolveBoundary(left_token)).byte_offset == 5,
            "left/right boundary affinity changed across concurrent insertion");

    // A causally early operation is held byte-for-byte. When its dependency is
    // applied, the router reports completion for both accepted envelopes.
    TemporaryDirectory causal_writer_dir("causal-writer");
    TemporaryDirectory causal_reader_dir("causal-reader");
    auto causal_writer_state = open_replica(causal_writer_dir.path(), storage_scope);
    auto causal_reader_state = open_replica(causal_reader_dir.path(), storage_scope);
    const PositionScope causal_scope{
        std::string(document),
        take(Markoff::VNext::blockTextStreamName({causal_writer_state.actor(), 3})),
        Primitive::text, 1};
    auto causal_writer = take(Markoff::VNext::ScopedTextStream::create(
        *causal_writer_state.session, causal_scope));
    auto causal_reader = take(Markoff::VNext::ScopedTextStream::create(
        *causal_reader_state.session, causal_scope));
    auto causal_first = take(causal_writer->replaceUtf8(0, 0, QByteArrayLiteral("A")));
    auto causal_second = take(causal_writer->replaceUtf8(1, 1, QByteArrayLiteral("B")));
    Markoff::VNext::ScopedOperationRouter causal_router(std::string(document), 1);
    (void)take(causal_router.registerStream(*causal_reader));
    std::vector<Markoff::VNext::CompletedRemoteOperation> causal_completed;
    route_result = causal_router.offer(
        make_envelope(causal_second, 31, QByteArrayLiteral("causal-31")), causal_completed);
    require(route_result.state == Markoff::VNext::RouteState::deferred
                && causal_router.heldOperationCount() == 1,
            "router did not retain a causally deferred operation");
    route_result = causal_router.offer(
        make_envelope(causal_first, 30, QByteArrayLiteral("causal-30")), causal_completed);
    require(route_result.state == Markoff::VNext::RouteState::applied
                && causal_completed.size() == 2
                && causal_completed[0].operation.delivery_sequence == 30
                && causal_completed[1].operation.delivery_sequence == 31
                && causal_completed[1].operation.bytes == causal_second.bytes
                && causal_reader->text() == QByteArrayLiteral("AB")
                && causal_router.heldOperationCount() == 0,
            "router did not report the deferred operation after its dependency arrived");
}

void run_typed_value_codec_contract()
{
    using Markoff::VNext::MarkoffMapValueCodec;
    const QByteArray blockKindFixture = QByteArray::fromHex(QByteArrayLiteral("4d424b0101"));
    require(take(MarkoffMapValueCodec::encodeBlockKind(Markoff::BlockKind::Heading))
                == blockKindFixture,
            "block-kind codec no longer matches its literal wire fixture");
    require(take(MarkoffMapValueCodec::decodeBlockKind(blockKindFixture))
                == Markoff::BlockKind::Heading,
            "block-kind literal fixture did not decode");

    const QByteArray attributeFixture = QByteArray::fromHex(
        QByteArrayLiteral("4d415601020000000368c3a9"));
    const Markoff::AttrValue unicodeAttribute = QString::fromUtf8("h\xc3\xa9");
    require(take(MarkoffMapValueCodec::encodeAttribute(unicodeAttribute))
                == attributeFixture,
            "attribute codec no longer matches its literal UTF-8 fixture");
    const auto decodedAttribute = take(MarkoffMapValueCodec::decodeAttribute(attributeFixture));
    require(std::get<QString>(decodedAttribute) == QString::fromUtf8("h\xc3\xa9"),
            "attribute literal fixture changed its Unicode value");

    const Markoff::LinkRefValue link{QStringLiteral("u"), QString::fromUtf8("t\xc3\xa9")};
    const QByteArray linkFixture = QByteArray::fromHex(
        QByteArrayLiteral("4d4c520100000001750000000374c3a9"));
    require(take(MarkoffMapValueCodec::encodeLinkRef(link)) == linkFixture,
            "link-reference codec no longer matches its literal UTF-8 fixture");
    const auto decodedLink = take(MarkoffMapValueCodec::decodeLinkRef(linkFixture));
    require(decodedLink.url == link.url && decodedLink.title == link.title,
            "link-reference literal fixture changed its fields");

    QByteArray invalidAttribute = attributeFixture;
    invalidAttribute[10] = char(0xff);
    const auto malformedUnicode = MarkoffMapValueCodec::decodeAttribute(invalidAttribute);
    require(!malformedUnicode.ok()
                && malformedUnicode.error().code() == ErrorCode::invalid_unicode,
            "attribute codec accepted malformed UTF-8");
    QByteArray unknownVersion = linkFixture;
    unknownVersion[3] = char(2);
    const auto unsupportedLink = MarkoffMapValueCodec::decodeLinkRef(unknownVersion);
    require(!unsupportedLink.ok()
                && unsupportedLink.error().code() == ErrorCode::unsupported_version,
            "link-reference codec accepted an unknown version");
    const auto truncatedLink = MarkoffMapValueCodec::decodeLinkRef(linkFixture.left(10));
    require(!truncatedLink.ok(), "link-reference codec accepted a truncated record");
}

} // namespace

int main()
{
    try {
        run_probe();
        run_typed_value_codec_contract();
        std::cout << "Markoff installed CollabText Core probe passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Markoff installed CollabText Core probe failed: "
                  << error.what() << '\n';
        return 1;
    }
}
