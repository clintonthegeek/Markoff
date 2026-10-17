// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/OwnerHistoryCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <utility>

namespace Markoff::VNext {
namespace {
using namespace CollabText;
constexpr size_t kMaxBytes = 8U * 1024U * 1024U;
constexpr size_t kMaxRecordField = 8U * 1024U * 1024U;
constexpr size_t kMaxDocument = 256;
constexpr size_t kMaxRevision = 4096;
constexpr size_t kMaxTransactionId = 256;
constexpr uint32_t kMaxStreams = 4096;

Error malformed(const char* message) { return Error(ErrorCode::malformed, message); }

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size > kMaxBytes - static_cast<size_t>(m_bytes.size())) return false;
        if (size) m_bytes.append(data, static_cast<qsizetype>(size));
        return true;
    }
    bool u8(uint8_t value) { const char c = static_cast<char>(value); return bytes(&c, 1); }
    bool u16(uint16_t value)
    {
        const char raw[] = {static_cast<char>(value >> 8), static_cast<char>(value)};
        return bytes(raw, sizeof(raw));
    }
    bool u32(uint32_t value)
    {
        char raw[4];
        for (int i = 0; i < 4; ++i) raw[i] = static_cast<char>(value >> (24 - i * 8));
        return bytes(raw, sizeof(raw));
    }
    bool u64(uint64_t value)
    {
        char raw[8];
        for (int i = 0; i < 8; ++i) raw[i] = static_cast<char>(value >> (56 - i * 8));
        return bytes(raw, sizeof(raw));
    }
    bool string(const std::string& value, size_t max)
    {
        return !value.empty() && value.size() <= max
            && u16(static_cast<uint16_t>(value.size())) && bytes(value.data(), value.size());
    }
    bool field(const QByteArray& value, size_t max)
    {
        return !value.isEmpty() && static_cast<size_t>(value.size()) <= max
            && u32(static_cast<uint32_t>(value.size()))
            && bytes(value.constData(), static_cast<size_t>(value.size()));
    }
    bool actor(const ActorId& actor)
    {
        const auto raw = actor.bytes();
        return actor.valid() && bytes(reinterpret_cast<const char*>(raw.data()), raw.size());
    }
    QByteArray take() && { return std::move(m_bytes); }
private:
    QByteArray m_bytes;
};

class Reader {
public:
    explicit Reader(const QByteArray& bytes) : m_bytes(bytes) {}
    bool bytes(char* out, size_t size)
    {
        if (size > remaining()) return false;
        std::copy_n(m_bytes.constData() + m_offset, size, out);
        m_offset += static_cast<qsizetype>(size);
        return true;
    }
    bool u8(uint8_t& value)
    {
        char c;
        if (!bytes(&c, 1)) return false;
        value = static_cast<uint8_t>(static_cast<unsigned char>(c));
        return true;
    }
    bool u16(uint16_t& value)
    {
        char raw[2];
        if (!bytes(raw, 2)) return false;
        value = (static_cast<uint16_t>(static_cast<unsigned char>(raw[0])) << 8)
              | static_cast<uint16_t>(static_cast<unsigned char>(raw[1]));
        return true;
    }
    bool u32(uint32_t& value)
    {
        char raw[4];
        if (!bytes(raw, 4)) return false;
        value = 0;
        for (char c : raw) value = (value << 8) | static_cast<unsigned char>(c);
        return true;
    }
    bool u64(uint64_t& value)
    {
        char raw[8];
        if (!bytes(raw, 8)) return false;
        value = 0;
        for (char c : raw) value = (value << 8) | static_cast<unsigned char>(c);
        return true;
    }
    bool string(std::string& value, size_t max)
    {
        uint16_t size;
        if (!u16(size) || size == 0 || size > max || size > remaining()) return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += size;
        return TextUnits::checkedByteToQtPos(
            QByteArray(value.data(), static_cast<qsizetype>(value.size())),
            static_cast<qsizetype>(value.size())).has_value();
    }
    bool field(QByteArray& value, size_t max)
    {
        uint32_t size;
        if (!u32(size) || size == 0 || size > max || size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, size);
        m_offset += size;
        return true;
    }
    bool actor(ActorId& actor)
    {
        std::array<uint8_t, ActorId::kBytes> raw{};
        if (!bytes(reinterpret_cast<char*>(raw.data()), raw.size())) return false;
        actor = ActorId::from_bytes(raw);
        return actor.valid();
    }
    size_t remaining() const { return static_cast<size_t>(m_bytes.size() - m_offset); }
    bool atEnd() const { return m_offset == m_bytes.size(); }
private:
    const QByteArray& m_bytes;
    qsizetype m_offset = 0;
};

uint8_t primitiveCode(Primitive primitive)
{
    return primitive == Primitive::text ? 1 : 2;
}

std::optional<Primitive> primitiveFromCode(uint8_t raw)
{
    if (raw == 1) return Primitive::text;
    if (raw == 2) return Primitive::list;
    return std::nullopt;
}

bool validHistoryRoute(Primitive primitive, const std::string& stream)
{
    if (primitive == Primitive::list)
        return stream == kOrderStreamName;
    return primitive == Primitive::text && parseBlockTextStreamName(stream).ok();
}

bool coversIndexedStreamHistories(
    const CrossStreamHistoryIndex& index,
    const std::set<std::pair<uint8_t, std::string>>& routes)
{
    for (const auto& event : index.events) {
        for (const auto& group : event.groups) {
            if (group.primitive == OperationPrimitive::markoffMap) continue;
            const uint8_t primitive = group.primitive == OperationPrimitive::text ? 1 : 2;
            if (!routes.contains({primitive, group.stream})) return false;
        }
    }
    return true;
}
}

Result<QByteArray> OwnerHistoryCodec::encode(const OwnerHistorySnapshot& snapshot)
{
    using namespace CollabText;
    if (snapshot.document.empty() || snapshot.document.size() > kMaxDocument
        || snapshot.epoch == 0 || !snapshot.owner.valid()
        || snapshot.transactionId.isEmpty()
        || static_cast<size_t>(snapshot.transactionId.size()) > kMaxTransactionId
        || snapshot.acceptedPrior.isEmpty()
        || static_cast<size_t>(snapshot.acceptedPrior.size()) > kMaxRevision
        || snapshot.streams.size() > kMaxStreams)
        return Error(ErrorCode::invalid_identity, "invalid owner-history scope or count");
    const auto& index = snapshot.actionIndex;
    if (index.document != snapshot.document || index.epoch != snapshot.epoch
        || index.owner != snapshot.owner || index.acceptedPrior != snapshot.acceptedPrior
        || index.events.empty() || index.events.back().actionId != snapshot.transactionId)
        return Error(ErrorCode::pin_conflict,
                     "owner-history index does not match its transaction and accepted prior");
    auto indexBytes = CrossStreamHistoryCodec::encode(index);
    if (!indexBytes.ok()) return indexBytes.error();
    std::set<std::pair<uint8_t, std::string>> routes;
    std::vector<QByteArray> encodedStreams;
    encodedStreams.reserve(snapshot.streams.size());
    for (const auto& stream : snapshot.streams) {
        if (stream.scope.document != snapshot.document || stream.scope.epoch != snapshot.epoch
            || stream.actor != snapshot.owner
            || (stream.scope.primitive != Primitive::text
                && stream.scope.primitive != Primitive::list))
            return Error(ErrorCode::wrong_document,
                         "per-stream history does not match owner-history scope");
        if (!validHistoryRoute(stream.scope.primitive, stream.scope.stream))
            return Error(ErrorCode::wrong_stream,
                         "per-stream history names an invalid Markoff stream");
        if (!routes.emplace(primitiveCode(stream.scope.primitive), stream.scope.stream).second)
            return Error(ErrorCode::malformed, "duplicate per-stream history route");
        auto encoded = LocalHistoryCodec::encode(stream);
        if (!encoded.ok()) return encoded.error();
        encodedStreams.push_back(std::move(encoded).value());
    }
    if (!coversIndexedStreamHistories(index, routes))
        return Error(ErrorCode::history_unavailable,
                     "owner history omits a stream referenced by its action index");

    Writer writer;
    constexpr char magic[] = {'M','O','H',1};
    const auto owner = snapshot.owner.bytes();
    if (!writer.bytes(magic, sizeof(magic))
        || !writer.string(snapshot.document, kMaxDocument) || !writer.u64(snapshot.epoch)
        || !writer.bytes(reinterpret_cast<const char*>(owner.data()), owner.size())
        || !writer.field(snapshot.transactionId, kMaxTransactionId)
        || !writer.field(snapshot.acceptedPrior, kMaxRevision)
        || !writer.u32(static_cast<uint32_t>(snapshot.streams.size())))
        return Error(ErrorCode::limit_exceeded, "owner-history record exceeds 8 MiB");
    for (size_t i = 0; i < snapshot.streams.size(); ++i) {
        const auto& stream = snapshot.streams[i];
        if (!writer.string(stream.scope.stream, kMaxDocument)
            || !writer.u8(primitiveCode(stream.scope.primitive))
            || !writer.field(encodedStreams[i], kMaxRecordField))
            return Error(ErrorCode::limit_exceeded, "owner-history record exceeds 8 MiB");
    }
    if (!writer.field(indexBytes.value(), kMaxRecordField))
        return Error(ErrorCode::limit_exceeded, "owner-history record exceeds 8 MiB");
    return std::move(writer).take();
}

Result<OwnerHistorySnapshot> OwnerHistoryCodec::decode(
    const QByteArray& bytes, const std::string& expectedDocument, uint64_t expectedEpoch,
    const CollabText::ActorId& expectedOwner, const QByteArray& expectedTransactionId,
    const QByteArray& expectedAcceptedPrior)
{
    using namespace CollabText;
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxBytes)
        return Error(ErrorCode::limit_exceeded, "owner-history record size is invalid");
    if (bytes[0] != 'M' || bytes[1] != 'O' || bytes[2] != 'H')
        return Error(ErrorCode::unknown_format, "unknown owner-history format");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported owner-history version");

    Reader reader(bytes);
    char magic[4];
    OwnerHistorySnapshot snapshot;
    uint32_t streamCount;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(snapshot.document, kMaxDocument)
        || !reader.u64(snapshot.epoch) || !reader.actor(snapshot.owner)
        || !reader.field(snapshot.transactionId, kMaxTransactionId)
        || !reader.field(snapshot.acceptedPrior, kMaxRevision)
        || !reader.u32(streamCount) || streamCount > kMaxStreams)
        return malformed("truncated owner-history header");
    if (snapshot.document != expectedDocument)
        return Error(ErrorCode::wrong_document, "owner history belongs to another document");
    if (snapshot.epoch != expectedEpoch)
        return Error(ErrorCode::wrong_epoch, "owner history belongs to another epoch");
    if (snapshot.owner != expectedOwner)
        return Error(ErrorCode::actor_conflict, "owner history belongs to another actor");
    if (snapshot.transactionId != expectedTransactionId)
        return Error(ErrorCode::pin_conflict, "owner history belongs to another transaction");
    if (snapshot.acceptedPrior != expectedAcceptedPrior)
        return Error(ErrorCode::pin_conflict, "owner history was prepared against another prefix");

    snapshot.streams.reserve(streamCount);
    std::set<std::pair<uint8_t, std::string>> routes;
    for (uint32_t i = 0; i < streamCount; ++i) {
        std::string streamName;
        uint8_t primitiveCodeRaw;
        QByteArray localHistoryBytes;
        if (!reader.string(streamName, kMaxDocument) || !reader.u8(primitiveCodeRaw)
            || !reader.field(localHistoryBytes, kMaxRecordField))
            return malformed("truncated per-stream owner history");
        const auto primitive = primitiveFromCode(primitiveCodeRaw);
        if (!primitive) return Error(ErrorCode::wrong_primitive,
                                     "unsupported owner-history primitive");
        if (!validHistoryRoute(*primitive, streamName))
            return Error(ErrorCode::wrong_stream,
                         "owner history names an invalid Markoff stream");
        if (!routes.emplace(primitiveCodeRaw, streamName).second)
            return malformed("duplicate per-stream owner-history route");
        const PositionScope scope{snapshot.document, streamName, *primitive, snapshot.epoch};
        auto local = LocalHistoryCodec::decode(localHistoryBytes, scope, snapshot.owner);
        if (!local.ok()) return local.error();
        snapshot.streams.push_back(std::move(local).value());
    }
    QByteArray indexBytes;
    if (!reader.field(indexBytes, kMaxRecordField) || !reader.atEnd())
        return malformed("truncated owner-history index or trailing bytes");
    auto index = CrossStreamHistoryCodec::decode(indexBytes, snapshot.document,
        snapshot.epoch, snapshot.owner, snapshot.acceptedPrior);
    if (!index.ok()) return index.error();
    snapshot.actionIndex = std::move(index).value();
    if (snapshot.actionIndex.events.empty()
        || snapshot.actionIndex.events.back().actionId != snapshot.transactionId)
        return malformed("owner-history action index does not end at its transaction");
    std::set<std::pair<uint8_t, std::string>> historyRoutes;
    for (const auto& stream : snapshot.streams)
        historyRoutes.emplace(primitiveCode(stream.scope.primitive), stream.scope.stream);
    if (!coversIndexedStreamHistories(snapshot.actionIndex, historyRoutes))
        return Error(ErrorCode::history_unavailable,
                     "owner history omits a stream referenced by its action index");
    return snapshot;
}

} // namespace Markoff::VNext
