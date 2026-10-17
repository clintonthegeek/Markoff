// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/MarkoffMapCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

constexpr size_t kMaxMapNameBytes = 128;
constexpr size_t kMaxMapKeyBytes = 1024;
constexpr size_t kMaxMapValueBytes = 1024U * 1024U;
constexpr size_t kMaxRecordBytes = kMaxMapValueBytes + kMaxMapKeyBytes + 256;

bool validUtf8(const std::string& text)
{
    const QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
    return TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size == 0) return true;
        if (size > kMaxRecordBytes - static_cast<size_t>(m_bytes.size())) return false;
        m_bytes.append(data, static_cast<qsizetype>(size));
        return true;
    }

    bool u8(uint8_t value)
    {
        const char byte = static_cast<char>(value);
        return bytes(&byte, 1);
    }

    bool u16(uint16_t value)
    {
        const char raw[] = {static_cast<char>(value >> 8), static_cast<char>(value)};
        return bytes(raw, sizeof(raw));
    }

    bool u32(uint32_t value)
    {
        char raw[4];
        for (int i = 0; i != 4; ++i)
            raw[i] = static_cast<char>(value >> (24 - i * 8));
        return bytes(raw, sizeof(raw));
    }

    bool u64(uint64_t value)
    {
        char raw[8];
        for (int i = 0; i != 8; ++i)
            raw[i] = static_cast<char>(value >> (56 - i * 8));
        return bytes(raw, sizeof(raw));
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

    bool bytes(char* destination, size_t size)
    {
        if (size > remaining()) return false;
        std::copy_n(m_bytes.constData() + m_offset, size, destination);
        m_offset += static_cast<qsizetype>(size);
        return true;
    }

    bool byteArray(QByteArray& value, size_t size)
    {
        if (size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, static_cast<qsizetype>(size));
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

    bool u16(uint16_t& value)
    {
        char raw[2];
        if (!bytes(raw, sizeof(raw))) return false;
        value = (static_cast<uint16_t>(static_cast<unsigned char>(raw[0])) << 8)
              | static_cast<uint16_t>(static_cast<unsigned char>(raw[1]));
        return true;
    }

    bool u32(uint32_t& value)
    {
        char raw[4];
        if (!bytes(raw, sizeof(raw))) return false;
        value = 0;
        for (char byte : raw)
            value = (value << 8) | static_cast<unsigned char>(byte);
        return true;
    }

    bool u64(uint64_t& value)
    {
        char raw[8];
        if (!bytes(raw, sizeof(raw))) return false;
        value = 0;
        for (char byte : raw)
            value = (value << 8) | static_cast<unsigned char>(byte);
        return true;
    }

    bool actor(ActorId& actor)
    {
        std::array<uint8_t, ActorId::kBytes> raw{};
        if (!bytes(reinterpret_cast<char*>(raw.data()), raw.size())) return false;
        actor = ActorId::from_bytes(raw);
        return actor.valid();
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

} // namespace

Result<QByteArray> MarkoffMapCodec::encode(const MarkoffMapOperation& operation)
{
    if (!mapStreamName(operation.map) || operation.map.size() > kMaxMapNameBytes
        || !validUtf8(operation.map) || !operation.producer.valid()
        || operation.counter == 0)
        return Error(ErrorCode::invalid_identity, "invalid Markoff map route or identity");
    if (static_cast<size_t>(operation.key.size()) > kMaxMapKeyBytes
        || static_cast<size_t>(operation.value.size()) > kMaxMapValueBytes
        || (operation.tombstone && !operation.value.isEmpty()))
        return Error(ErrorCode::limit_exceeded, "Markoff map key/value exceeds format limits");

    Writer writer;
    constexpr char magic[] = {'M', 'M', 'P', 1};
    if (!writer.bytes(magic, sizeof(magic))
        || !writer.u16(static_cast<uint16_t>(operation.map.size()))
        || !writer.bytes(operation.map.data(), operation.map.size())
        || !writer.u32(static_cast<uint32_t>(operation.key.size()))
        || !writer.bytes(operation.key.constData(), static_cast<size_t>(operation.key.size()))
        || !writer.u8(operation.tombstone ? 1 : 0)
        || !writer.u32(static_cast<uint32_t>(operation.value.size()))
        || !writer.bytes(operation.value.constData(), static_cast<size_t>(operation.value.size()))
        || !writer.actor(operation.producer) || !writer.u64(operation.counter))
        return Error(ErrorCode::limit_exceeded, "Markoff map record exceeds its format budget");
    return std::move(writer).take();
}

Result<MarkoffMapOperation> MarkoffMapCodec::decode(const QByteArray& bytes)
{
    if (bytes.size() < 4) return Error(ErrorCode::malformed, "truncated Markoff map header");
    if (static_cast<size_t>(bytes.size()) > kMaxRecordBytes)
        return Error(ErrorCode::limit_exceeded, "Markoff map record exceeds its format budget");
    if (bytes[0] != 'M' || bytes[1] != 'M' || bytes[2] != 'P')
        return Error(ErrorCode::unknown_format, "not a Markoff map operation");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported Markoff map version");

    Reader reader(bytes);
    char magic[4];
    uint16_t mapLength;
    uint32_t keyLength;
    uint8_t tombstone;
    uint32_t valueLength;
    MarkoffMapOperation operation;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.u16(mapLength)
        || mapLength == 0 || mapLength > kMaxMapNameBytes
        || mapLength > reader.remaining())
        return Error(ErrorCode::malformed, "truncated Markoff map name");
    std::array<char, kMaxMapNameBytes> mapName{};
    if (!reader.bytes(mapName.data(), mapLength))
        return Error(ErrorCode::malformed, "truncated Markoff map name");
    operation.map.assign(mapName.data(), mapLength);
    if (!validUtf8(operation.map) || !mapStreamName(operation.map)
        || !reader.u32(keyLength)
        || keyLength > kMaxMapKeyBytes || keyLength > reader.remaining()
        || !reader.byteArray(operation.key, keyLength) || !reader.u8(tombstone)
        || tombstone > 1 || !reader.u32(valueLength)
        || valueLength > kMaxMapValueBytes || valueLength > reader.remaining()
        || !reader.byteArray(operation.value, valueLength)
        || !reader.actor(operation.producer) || !reader.u64(operation.counter)
        || operation.counter == 0 || !reader.atEnd())
        return Error(ErrorCode::malformed, "malformed Markoff map operation");
    operation.tombstone = tombstone == 1;
    if (operation.tombstone && !operation.value.isEmpty())
        return Error(ErrorCode::malformed, "tombstone Markoff map operation carries a value");
    return operation;
}

} // namespace Markoff::VNext
