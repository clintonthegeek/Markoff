// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/CounterGapCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace Markoff::VNext {
namespace {
using namespace CollabText;
constexpr size_t kMaxScopeField = 256;
constexpr size_t kMaxPriorToken = 4096;
constexpr size_t kMaxRecord = 16U * 1024U;

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size > kMaxRecord - static_cast<size_t>(m_bytes.size())) return false;
        if (size) m_bytes.append(data, static_cast<qsizetype>(size));
        return true;
    }
    bool u8(uint8_t value) { const char c = static_cast<char>(value); return bytes(&c, 1); }
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
    bool string(const std::string& value)
    {
        return value.size() <= kMaxScopeField
            && value.size() <= std::numeric_limits<uint32_t>::max()
            && u32(static_cast<uint32_t>(value.size())) && bytes(value.data(), value.size());
    }
    bool field(const QByteArray& value, size_t limit)
    {
        return static_cast<size_t>(value.size()) <= limit
            && u32(static_cast<uint32_t>(value.size()))
            && bytes(value.constData(), static_cast<size_t>(value.size()));
    }
    bool actor(const ActorId& value)
    {
        const auto raw = value.bytes();
        return value.valid() && bytes(reinterpret_cast<const char*>(raw.data()), raw.size());
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
        char raw;
        if (!bytes(&raw, 1)) return false;
        value = static_cast<uint8_t>(static_cast<unsigned char>(raw));
        return true;
    }
    bool u32(uint32_t& value)
    {
        char raw[4];
        if (!bytes(raw, sizeof(raw))) return false;
        value = 0;
        for (char c : raw) value = (value << 8) | static_cast<unsigned char>(c);
        return true;
    }
    bool u64(uint64_t& value)
    {
        char raw[8];
        if (!bytes(raw, sizeof(raw))) return false;
        value = 0;
        for (char c : raw) value = (value << 8) | static_cast<unsigned char>(c);
        return true;
    }
    bool string(std::string& value)
    {
        uint32_t size;
        if (!u32(size) || size > kMaxScopeField || size > remaining()) return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += static_cast<qsizetype>(size);
        return validUtf8(value);
    }
    bool field(QByteArray& value, size_t limit)
    {
        uint32_t size;
        if (!u32(size) || size > limit || size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, static_cast<qsizetype>(size));
        m_offset += static_cast<qsizetype>(size);
        return true;
    }
    bool actor(ActorId& value)
    {
        std::array<uint8_t, ActorId::kBytes> raw{};
        if (!bytes(reinterpret_cast<char*>(raw.data()), raw.size())) return false;
        value = ActorId::from_bytes(raw);
        return value.valid();
    }
    size_t remaining() const { return static_cast<size_t>(m_bytes.size() - m_offset); }
    bool atEnd() const { return m_offset == m_bytes.size(); }
private:
    const QByteArray& m_bytes;
    qsizetype m_offset = 0;
};

Error malformed(const char* message) { return Error(ErrorCode::malformed, message); }
uint8_t primitiveByte(Primitive primitive) { return primitive == Primitive::text ? 1 : 2; }
}

Result<QByteArray> CounterGapCodec::encode(const ScopedCounterGap& record)
{
    using namespace CollabText;
    const auto& scope = record.scope;
    if (scope.document.empty() || scope.stream.empty() || scope.epoch == 0
        || (scope.primitive != Primitive::text && scope.primitive != Primitive::list)
        || !validUtf8(scope.document) || !validUtf8(scope.stream)
        || !record.gap.actor.valid() || record.gap.first == 0 || record.gap.count == 0
        || record.acceptedPrior.isEmpty()
        || static_cast<size_t>(record.acceptedPrior.size()) > kMaxPriorToken)
        return Error(ErrorCode::invalid_identity, "invalid scoped counter-gap record");
    const auto encodedGap = Crdt::encode_counter_gap(record.gap);
    if (!encodedGap.ok()) return encodedGap.error();
    Writer writer;
    constexpr char magic[] = {'M','T','G',1};
    const QByteArray gapBytes = QByteArray::fromStdString(encodedGap.value());
    if (!writer.bytes(magic, sizeof(magic)) || !writer.string(scope.document)
        || !writer.string(scope.stream) || !writer.u8(primitiveByte(scope.primitive))
        || !writer.u64(scope.epoch) || !writer.field(record.acceptedPrior, kMaxPriorToken)
        || !writer.field(gapBytes, kMaxRecord))
        return Error(ErrorCode::limit_exceeded, "scoped counter-gap record exceeds its limit");
    return std::move(writer).take();
}

Result<ScopedCounterGap> CounterGapCodec::decodeRecord(const QByteArray& bytes)
{
    using namespace CollabText;
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxRecord)
        return Error(ErrorCode::limit_exceeded, "scoped counter-gap size is invalid");
    if (bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'G')
        return Error(ErrorCode::unknown_format, "unknown scoped counter-gap format");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported scoped counter-gap version");
    Reader reader(bytes);
    char magic[4];
    uint8_t primitive;
    uint64_t epoch;
    QByteArray prior, gapBytes;
    std::string document, stream;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(document)
        || !reader.string(stream) || !reader.u8(primitive) || !reader.u64(epoch)
        || !reader.field(prior, kMaxPriorToken) || !reader.field(gapBytes, kMaxRecord)
        || !reader.atEnd() || prior.isEmpty())
        return malformed("truncated or malformed scoped counter-gap record");
    if (primitive != 1 && primitive != 2)
        return Error(ErrorCode::wrong_primitive, "unsupported scoped counter-gap primitive");
    const PositionScope scope{document, stream,
        primitive == 1 ? Primitive::text : Primitive::list, epoch};
    if (scope.document.empty() || scope.stream.empty() || scope.epoch == 0)
        return Error(ErrorCode::invalid_identity, "scoped counter-gap identity is invalid");
    auto gap = Crdt::decode_counter_gap(std::string_view(
        gapBytes.constData(), static_cast<size_t>(gapBytes.size())));
    if (!gap.ok()) return gap.error();
    if (!gap.value().actor.valid() || gap.value().first == 0 || gap.value().count == 0)
        return Error(ErrorCode::invalid_identity, "invalid scoped counter-gap identity interval");
    return ScopedCounterGap{scope, std::move(gap).value(), std::move(prior)};
}

Result<ScopedCounterGap> CounterGapCodec::decode(
    const QByteArray& bytes, const CollabText::PositionScope& expectedScope,
    const QByteArray& expectedAcceptedPrior)
{
    using namespace CollabText;
    auto decoded = decodeRecord(bytes);
    if (!decoded.ok()) return decoded.error();
    const auto& scope = decoded.value().scope;
    if (scope.document != expectedScope.document)
        return Error(ErrorCode::wrong_document, "counter-gap belongs to another document");
    if (scope.stream != expectedScope.stream)
        return Error(ErrorCode::wrong_stream, "counter-gap belongs to another stream");
    if (scope.epoch != expectedScope.epoch)
        return Error(ErrorCode::wrong_epoch, "counter-gap belongs to another epoch");
    if (scope.primitive != expectedScope.primitive)
        return Error(ErrorCode::wrong_primitive, "counter-gap belongs to another primitive");
    if (decoded.value().acceptedPrior != expectedAcceptedPrior)
        return Error(ErrorCode::pin_conflict, "counter-gap accepted prior does not match");
    return decoded;
}

Result<QByteArray> MapCounterGapCodec::encode(const MapCounterGap& record)
{
    using namespace CollabText;
    if (record.document.empty() || record.document.size() > kMaxScopeField
        || !validUtf8(record.document) || record.epoch == 0
        || !mapStreamName(record.map).ok() || !record.actor.valid()
        || record.first == 0 || record.count == 0
        || record.count - 1 > std::numeric_limits<uint64_t>::max() - record.first
        || record.acceptedPrior.isEmpty()
        || static_cast<size_t>(record.acceptedPrior.size()) > kMaxPriorToken)
        return Error(ErrorCode::invalid_identity, "invalid Markoff map counter-gap record");
    Writer writer;
    constexpr char magic[] = {'M','M','G',1};
    if (!writer.bytes(magic, sizeof(magic)) || !writer.string(record.document)
        || !writer.string(record.map) || !writer.u64(record.epoch)
        || !writer.actor(record.actor) || !writer.u64(record.first)
        || !writer.u64(record.count) || !writer.field(record.acceptedPrior, kMaxPriorToken))
        return Error(ErrorCode::limit_exceeded, "Markoff map gap exceeds its record limit");
    return std::move(writer).take();
}

Result<MapCounterGap> MapCounterGapCodec::decodeRecord(const QByteArray& bytes)
{
    using namespace CollabText;
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxRecord)
        return Error(ErrorCode::limit_exceeded, "Markoff map gap size is invalid");
    if (bytes[0] != 'M' || bytes[1] != 'M' || bytes[2] != 'G')
        return Error(ErrorCode::unknown_format, "unknown Markoff map gap format");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported Markoff map gap version");
    Reader reader(bytes);
    char magic[4];
    MapCounterGap record;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(record.document)
        || !reader.string(record.map) || !reader.u64(record.epoch)
        || !reader.actor(record.actor) || !reader.u64(record.first)
        || !reader.u64(record.count) || !reader.field(record.acceptedPrior, kMaxPriorToken)
        || !reader.atEnd())
        return malformed("truncated or malformed Markoff map gap record");
    if (record.document.empty() || record.epoch == 0
        || !mapStreamName(record.map).ok())
        return Error(ErrorCode::invalid_identity, "Markoff map gap scope is invalid");
    if (record.first == 0 || record.count == 0
        || record.count - 1 > std::numeric_limits<uint64_t>::max() - record.first)
        return Error(ErrorCode::invalid_identity, "invalid Markoff map gap interval");
    if (record.acceptedPrior.isEmpty())
        return Error(ErrorCode::malformed, "Markoff map gap accepted prior is empty");
    return record;
}

Result<MapCounterGap> MapCounterGapCodec::decode(
    const QByteArray& bytes, const std::string& expectedDocument,
    const std::string& expectedMap, uint64_t expectedEpoch,
    const QByteArray& expectedAcceptedPrior)
{
    auto decoded = decodeRecord(bytes);
    if (!decoded.ok()) return decoded.error();
    const auto& record = decoded.value();
    if (record.document != expectedDocument)
        return Error(ErrorCode::wrong_document, "Markoff map gap belongs to another document");
    if (record.map != expectedMap)
        return Error(ErrorCode::wrong_stream, "Markoff map gap belongs to another map");
    if (record.epoch != expectedEpoch)
        return Error(ErrorCode::wrong_epoch, "Markoff map gap belongs to another epoch");
    if (record.acceptedPrior != expectedAcceptedPrior)
        return Error(ErrorCode::pin_conflict, "Markoff map gap accepted prior does not match");
    return record;
}

} // namespace Markoff::VNext
