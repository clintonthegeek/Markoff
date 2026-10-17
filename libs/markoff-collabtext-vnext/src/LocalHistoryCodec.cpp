// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/LocalHistoryCodec.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

constexpr size_t kMaxRecordBytes = 8U * 1024U * 1024U;
constexpr uint32_t kMaxGroups = 100000;
constexpr uint32_t kMaxIdentities = 400000;
constexpr size_t kMaxScopeStringBytes = 256;

Error malformed(const char* message)
{
    return Error(ErrorCode::malformed, message);
}

bool supportedPrimitive(Primitive primitive)
{
    return primitive == Primitive::text || primitive == Primitive::list;
}

uint8_t wirePrimitive(Primitive primitive)
{
    return primitive == Primitive::text ? 1 : 2;
}

std::optional<Primitive> readPrimitive(uint8_t value)
{
    if (value == 1) return Primitive::text;
    if (value == 2) return Primitive::list;
    return std::nullopt;
}

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool validIdentity(const Crdt::Identity& identity, const ActorId& owner)
{
    return identity.actor == owner && identity.counter != 0;
}

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size > kMaxRecordBytes - static_cast<size_t>(m_bytes.size()))
            return false;
        m_bytes.append(data, static_cast<qsizetype>(size));
        return true;
    }

    bool u8(uint8_t value)
    {
        const char byte = static_cast<char>(value);
        return bytes(&byte, 1);
    }

    bool u32(uint32_t value)
    {
        char encoded[4];
        for (int i = 0; i != 4; ++i)
            encoded[i] = static_cast<char>(value >> (24 - i * 8));
        return bytes(encoded, sizeof(encoded));
    }

    bool u64(uint64_t value)
    {
        char encoded[8];
        for (int i = 0; i != 8; ++i)
            encoded[i] = static_cast<char>(value >> (56 - i * 8));
        return bytes(encoded, sizeof(encoded));
    }

    bool string(const std::string& value)
    {
        return value.size() <= kMaxScopeStringBytes
            && value.size() <= std::numeric_limits<uint32_t>::max()
            && u32(static_cast<uint32_t>(value.size()))
            && bytes(value.data(), value.size());
    }

    bool identity(const Crdt::Identity& id, const ActorId& owner)
    {
        if (!validIdentity(id, owner)) return false;
        const auto actorBytes = id.actor.bytes();
        return bytes(reinterpret_cast<const char*>(actorBytes.data()), actorBytes.size())
            && u64(id.counter);
    }

    QByteArray take() && { return std::move(m_bytes); }

private:
    QByteArray m_bytes;
};

class Reader {
public:
    explicit Reader(const QByteArray& bytes) : m_bytes(bytes) {}

    bool bytes(char* destination, size_t size)
    {
        if (size > remaining()) return false;
        std::copy_n(m_bytes.constData() + m_offset, size, destination);
        m_offset += static_cast<qsizetype>(size);
        return true;
    }

    bool u8(uint8_t& value)
    {
        char byte;
        if (!bytes(&byte, 1)) return false;
        value = static_cast<uint8_t>(static_cast<unsigned char>(byte));
        return true;
    }

    bool u32(uint32_t& value)
    {
        char encoded[4];
        if (!bytes(encoded, sizeof(encoded))) return false;
        value = 0;
        for (const char byte : encoded)
            value = (value << 8) | static_cast<unsigned char>(byte);
        return true;
    }

    bool u64(uint64_t& value)
    {
        char encoded[8];
        if (!bytes(encoded, sizeof(encoded))) return false;
        value = 0;
        for (const char byte : encoded)
            value = (value << 8) | static_cast<unsigned char>(byte);
        return true;
    }

    bool string(std::string& value)
    {
        uint32_t size;
        if (!u32(size) || size > kMaxScopeStringBytes || size > remaining())
            return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += static_cast<qsizetype>(size);
        return validUtf8(value);
    }

    bool identity(Crdt::Identity& id, const ActorId& owner)
    {
        std::array<uint8_t, ActorId::kBytes> actorBytes{};
        uint64_t counter;
        if (!bytes(reinterpret_cast<char*>(actorBytes.data()), actorBytes.size())
            || !u64(counter))
            return false;
        id = Crdt::Identity(ActorId::from_bytes(actorBytes), counter);
        return validIdentity(id, owner);
    }

    size_t remaining() const noexcept
    {
        return static_cast<size_t>(m_bytes.size() - m_offset);
    }

    bool atEnd() const noexcept { return m_offset == m_bytes.size(); }

private:
    const QByteArray& m_bytes;
    qsizetype m_offset = 0;
};

bool writeIdentities(Writer& writer, const std::vector<Crdt::Identity>& identities,
                     const ActorId& owner, uint32_t& total)
{
    if (identities.size() > kMaxIdentities
        || identities.size() > kMaxIdentities - total
        || !writer.u32(static_cast<uint32_t>(identities.size())))
        return false;
    total += static_cast<uint32_t>(identities.size());
    for (const auto& identity : identities)
        if (!writer.identity(identity, owner)) return false;
    return true;
}

bool readIdentities(Reader& reader, std::vector<Crdt::Identity>& identities,
                    const ActorId& owner, uint32_t& total)
{
    uint32_t count;
    if (!reader.u32(count) || count > kMaxIdentities
        || count > kMaxIdentities - total)
        return false;
    total += count;
    identities.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Crdt::Identity identity;
        if (!reader.identity(identity, owner)) return false;
        identities.push_back(std::move(identity));
    }
    return true;
}

bool writeEntry(Writer& writer, const Crdt::LocalUndoEntrySnapshot& entry,
                const ActorId& owner, uint32_t& totalIdentities)
{
    static_assert(sizeof(size_t) <= sizeof(uint64_t));
    if (totalIdentities == kMaxIdentities) return false;
    ++totalIdentities;
    return writer.identity(entry.group_id, owner)
        && writeIdentities(writer, entry.operation_ids, owner, totalIdentities)
        && writeIdentities(writer, entry.inserted_items, owner, totalIdentities)
        && writeIdentities(writer, entry.deletion_operations, owner, totalIdentities)
        && writer.u64(static_cast<uint64_t>(entry.retained_bytes));
}

bool readEntry(Reader& reader, Crdt::LocalUndoEntrySnapshot& entry,
               const ActorId& owner, uint32_t& totalIdentities)
{
    uint64_t retainedBytes;
    if (totalIdentities == kMaxIdentities) return false;
    ++totalIdentities;
    if (!reader.identity(entry.group_id, owner)
        || !readIdentities(reader, entry.operation_ids, owner, totalIdentities)
        || !readIdentities(reader, entry.inserted_items, owner, totalIdentities)
        || !readIdentities(reader, entry.deletion_operations, owner, totalIdentities)
        || !reader.u64(retainedBytes)
        || retainedBytes > std::numeric_limits<size_t>::max())
        return false;
    entry.retained_bytes = static_cast<size_t>(retainedBytes);
    return true;
}

} // namespace

CollabText::Result<QByteArray> LocalHistoryCodec::encode(
    const LocalHistoryRecord& record)
{
    using namespace CollabText;
    if (record.scope.document.empty() || record.scope.stream.empty()
        || !supportedPrimitive(record.scope.primitive) || record.scope.epoch == 0
        || !record.actor.valid())
        return Error(ErrorCode::invalid_identity, "invalid local-history record scope or actor");
    if (record.scope.document.size() > kMaxScopeStringBytes
        || record.scope.stream.size() > kMaxScopeStringBytes)
        return Error(ErrorCode::limit_exceeded, "local-history scope string exceeds 256 bytes");
    if (!validUtf8(record.scope.document) || !validUtf8(record.scope.stream))
        return Error(ErrorCode::invalid_unicode, "local-history scope is not valid UTF-8");

    const auto& snapshot = record.snapshot;
    if (snapshot.entries.size() > kMaxGroups
        || snapshot.retired_entries.size() > kMaxGroups - snapshot.entries.size()
        || snapshot.cursor > snapshot.entries.size())
        return Error(ErrorCode::limit_exceeded, "local-history group count or cursor is invalid");

    Writer writer;
    constexpr char magic[] = {'M', 'A', 'H', 1};
    const auto actorBytes = record.actor.bytes();
    if (!writer.bytes(magic, sizeof(magic))
        || !writer.string(record.scope.document)
        || !writer.string(record.scope.stream)
        || !writer.u8(wirePrimitive(record.scope.primitive))
        || !writer.u64(record.scope.epoch)
        || !writer.bytes(reinterpret_cast<const char*>(actorBytes.data()), actorBytes.size())
        || !writer.u64(static_cast<uint64_t>(snapshot.cursor))
        || !writer.u32(static_cast<uint32_t>(snapshot.entries.size()))
        || !writer.u32(static_cast<uint32_t>(snapshot.retired_entries.size())))
        return Error(ErrorCode::limit_exceeded, "local-history record exceeds 8 MiB");

    uint32_t totalIdentities = 0;
    for (const auto& entry : snapshot.entries)
        if (!writeEntry(writer, entry, record.actor, totalIdentities))
            return Error(ErrorCode::malformed,
                         "active history contains invalid or excessive identities");
    for (const auto& entry : snapshot.retired_entries)
        if (!writeEntry(writer, entry, record.actor, totalIdentities))
            return Error(ErrorCode::malformed,
                         "retired history contains invalid or excessive identities");
    return std::move(writer).take();
}

CollabText::Result<LocalHistoryRecord> LocalHistoryCodec::decode(
    const QByteArray& bytes, const CollabText::PositionScope& expectedScope,
    const CollabText::ActorId& expectedActor)
{
    using namespace CollabText;
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxRecordBytes)
        return Error(ErrorCode::limit_exceeded, "local-history record size is invalid");
    if (expectedScope.document.empty() || expectedScope.stream.empty()
        || expectedScope.epoch == 0 || !supportedPrimitive(expectedScope.primitive)
        || !expectedActor.valid())
        return Error(ErrorCode::invalid_identity, "invalid expected local-history owner");

    Reader reader(bytes);
    char magic[4];
    if (!reader.bytes(magic, sizeof(magic))) return malformed("truncated local-history header");
    if (magic[0] != 'M' || magic[1] != 'A' || magic[2] != 'H')
        return Error(ErrorCode::unknown_format, "unknown local-history record format");
    if (static_cast<uint8_t>(static_cast<unsigned char>(magic[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported local-history version");

    LocalHistoryRecord record;
    uint8_t primitive;
    uint64_t cursor;
    uint32_t activeCount, retiredCount;
    std::array<uint8_t, ActorId::kBytes> actorBytes{};
    if (!reader.string(record.scope.document) || !reader.string(record.scope.stream)
        || !reader.u8(primitive) || !reader.u64(record.scope.epoch)
        || !reader.bytes(reinterpret_cast<char*>(actorBytes.data()), actorBytes.size())
        || !reader.u64(cursor) || !reader.u32(activeCount) || !reader.u32(retiredCount))
        return malformed("truncated or invalid local-history header fields");
    const auto parsedPrimitive = readPrimitive(primitive);
    if (!parsedPrimitive) return Error(ErrorCode::wrong_primitive,
                                       "unsupported local-history primitive");
    record.scope.primitive = *parsedPrimitive;
    record.actor = ActorId::from_bytes(actorBytes);

    if (record.scope.document != expectedScope.document)
        return Error(ErrorCode::wrong_document, "local-history record belongs to another document");
    if (record.scope.stream != expectedScope.stream)
        return Error(ErrorCode::wrong_stream, "local-history record belongs to another stream");
    if (record.scope.epoch != expectedScope.epoch)
        return Error(ErrorCode::wrong_epoch, "local-history record belongs to another epoch");
    if (record.scope.primitive != expectedScope.primitive)
        return Error(ErrorCode::wrong_primitive, "local-history record belongs to another primitive");
    if (record.actor != expectedActor)
        return Error(ErrorCode::actor_conflict, "local-history record belongs to another actor");
    if (!record.actor.valid()) return Error(ErrorCode::invalid_identity,
                                            "local-history actor is invalid");
    if (activeCount > kMaxGroups || retiredCount > kMaxGroups - activeCount
        || cursor > activeCount)
        return Error(ErrorCode::limit_exceeded, "local-history counts or cursor are invalid");

    record.snapshot.cursor = static_cast<size_t>(cursor);
    record.snapshot.entries.reserve(activeCount);
    record.snapshot.retired_entries.reserve(retiredCount);
    uint32_t totalIdentities = 0;
    for (uint32_t i = 0; i < activeCount; ++i) {
        Crdt::LocalUndoEntrySnapshot entry;
        if (!readEntry(reader, entry, record.actor, totalIdentities))
            return malformed("invalid active local-history entry");
        record.snapshot.entries.push_back(std::move(entry));
    }
    for (uint32_t i = 0; i < retiredCount; ++i) {
        Crdt::LocalUndoEntrySnapshot entry;
        if (!readEntry(reader, entry, record.actor, totalIdentities))
            return malformed("invalid retired local-history entry");
        record.snapshot.retired_entries.push_back(std::move(entry));
    }
    if (!reader.atEnd()) return malformed("trailing bytes in local-history record");
    return record;
}

} // namespace Markoff::VNext
