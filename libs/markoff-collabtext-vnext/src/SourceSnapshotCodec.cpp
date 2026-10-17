// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/SourceSnapshotCodec.h>

#include <markoff/core/TextUnits.h>

#include <QCryptographicHash>

#include <algorithm>
#include <limits>
#include <utility>

namespace Markoff::VNext {
namespace {
using namespace CollabText;
constexpr size_t kMaxRecord = 16U * 1024U * 1024U;
constexpr size_t kMaxSource = 8U * 1024U * 1024U;
constexpr size_t kMaxCache = 4U * 1024U * 1024U;
constexpr size_t kMaxDocument = 256;
constexpr size_t kMaxParserVersion = 128;
constexpr uint32_t kMaxSeparators = 65536;

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

QByteArray sourceDigest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool validSeparators(const QByteArray& source,
                     const std::vector<SourceSeparator>& separators)
{
    uint64_t previousEnd = 0;
    for (const auto& separator : separators) {
        if (separator.bytes.isEmpty() || separator.offset < previousEnd
            || separator.offset > static_cast<uint64_t>(source.size())
            || static_cast<uint64_t>(separator.bytes.size())
                > static_cast<uint64_t>(source.size()) - separator.offset)
            return false;
        const auto offset = static_cast<qsizetype>(separator.offset);
        if (source.mid(offset, separator.bytes.size()) != separator.bytes) return false;
        previousEnd = separator.offset + static_cast<uint64_t>(separator.bytes.size());
    }
    return true;
}

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size > kMaxRecord - static_cast<size_t>(m_bytes.size())) return false;
        if (size) m_bytes.append(data, static_cast<qsizetype>(size));
        return true;
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
    bool string(const std::string& value, size_t maximum)
    {
        return value.size() <= maximum && value.size() <= std::numeric_limits<uint32_t>::max()
            && u32(static_cast<uint32_t>(value.size())) && bytes(value.data(), value.size());
    }
    bool field(const QByteArray& value, size_t maximum)
    {
        return static_cast<size_t>(value.size()) <= maximum
            && u32(static_cast<uint32_t>(value.size()))
            && bytes(value.constData(), static_cast<size_t>(value.size()));
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
    bool string(std::string& value, size_t maximum)
    {
        uint32_t size;
        if (!u32(size) || size > maximum || size > remaining()) return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += static_cast<qsizetype>(size);
        return validUtf8(value);
    }
    bool field(QByteArray& value, size_t maximum)
    {
        uint32_t size;
        if (!u32(size) || size > maximum || size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, static_cast<qsizetype>(size));
        m_offset += static_cast<qsizetype>(size);
        return true;
    }
    size_t remaining() const { return static_cast<size_t>(m_bytes.size() - m_offset); }
    bool atEnd() const { return m_offset == m_bytes.size(); }
private:
    const QByteArray& m_bytes;
    qsizetype m_offset = 0;
};

Error malformed(const char* message) { return Error(ErrorCode::malformed, message); }
}

Result<QByteArray> SourceSnapshotCodec::encode(const SourceSnapshotRecord& record)
{
    if (record.document.empty() || record.document.size() > kMaxDocument
        || !validUtf8(record.document) || record.epoch == 0)
        return Error(ErrorCode::invalid_identity, "invalid source snapshot scope");
    if (static_cast<size_t>(record.originalMarkdown.size()) > kMaxSource
        || !TextUnits::checkedByteToQtPos(record.originalMarkdown,
                                         record.originalMarkdown.size()))
        return Error(ErrorCode::invalid_unicode, "invalid or oversized original Markdown bytes");
    if (record.separators.size() > kMaxSeparators
        || !validSeparators(record.originalMarkdown, record.separators))
        return Error(ErrorCode::malformed, "source separator slices are invalid");
    if (static_cast<size_t>(record.parserCache.size()) > kMaxCache
        || record.parserVersion.size() > kMaxParserVersion
        || !validUtf8(record.parserVersion)
        || (record.parserCache.isEmpty() != record.parserVersion.empty()))
        return Error(ErrorCode::limit_exceeded, "source parser cache metadata is invalid");

    Writer writer;
    constexpr char magic[] = {'M','D','S',1};
    const QByteArray digest = record.parserCache.isEmpty()
        ? QByteArray{} : sourceDigest(record.originalMarkdown);
    const QByteArray cacheDigest = record.parserCache.isEmpty()
        ? QByteArray{} : sourceDigest(record.parserCache);
    if (!writer.bytes(magic, sizeof(magic)) || !writer.string(record.document, kMaxDocument)
        || !writer.u64(record.epoch) || !writer.field(record.originalMarkdown, kMaxSource)
        || !writer.u32(static_cast<uint32_t>(record.separators.size())))
        return Error(ErrorCode::limit_exceeded, "source snapshot exceeds 16 MiB");
    for (const auto& separator : record.separators)
        if (!writer.u64(separator.offset) || !writer.field(separator.bytes, kMaxSource))
            return Error(ErrorCode::limit_exceeded, "source separator record exceeds its limit");
    if (!writer.string(record.parserVersion, kMaxParserVersion)
        || !writer.field(digest, 32) || !writer.field(cacheDigest, 32)
        || !writer.field(record.parserCache, kMaxCache))
        return Error(ErrorCode::limit_exceeded, "source parser cache exceeds its limit");
    return std::move(writer).take();
}

Result<SourceSnapshotRecord> SourceSnapshotCodec::decode(
    const QByteArray& bytes, const std::string& expectedDocument, uint64_t expectedEpoch)
{
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxRecord)
        return Error(ErrorCode::limit_exceeded, "source snapshot record size is invalid");
    if (bytes[0] != 'M' || bytes[1] != 'D' || bytes[2] != 'S')
        return Error(ErrorCode::unknown_format, "unknown source snapshot format");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported source snapshot version");
    Reader reader(bytes);
    char magic[4];
    SourceSnapshotRecord record;
    uint32_t separatorCount;
    QByteArray sourceDigestBytes, cacheDigest;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(record.document, kMaxDocument)
        || !reader.u64(record.epoch) || !reader.field(record.originalMarkdown, kMaxSource)
        || !reader.u32(separatorCount) || separatorCount > kMaxSeparators)
        return malformed("truncated source snapshot header");
    record.separators.reserve(separatorCount);
    for (uint32_t i = 0; i < separatorCount; ++i) {
        SourceSeparator separator;
        if (!reader.u64(separator.offset) || !reader.field(separator.bytes, kMaxSource))
            return malformed("truncated source separator");
        record.separators.push_back(std::move(separator));
    }
    if (!reader.string(record.parserVersion, kMaxParserVersion)
        || !reader.field(sourceDigestBytes, 32) || !reader.field(cacheDigest, 32)
        || !reader.field(record.parserCache, kMaxCache)
        || !reader.atEnd())
        return malformed("truncated source parser cache or trailing bytes");
    if (record.document != expectedDocument)
        return Error(ErrorCode::wrong_document, "source snapshot belongs to another document");
    if (record.epoch != expectedEpoch)
        return Error(ErrorCode::wrong_epoch, "source snapshot belongs to another epoch");
    if (!TextUnits::checkedByteToQtPos(record.originalMarkdown,
                                      record.originalMarkdown.size()))
        return Error(ErrorCode::invalid_unicode, "source snapshot Markdown is not valid UTF-8");
    if (!validSeparators(record.originalMarkdown, record.separators))
        return malformed("source separators do not match exact original bytes");
    if (record.parserCache.isEmpty()) {
        if (!record.parserVersion.empty() || !sourceDigestBytes.isEmpty()
            || !cacheDigest.isEmpty())
            return malformed("empty parser cache carries parser metadata");
    } else if (record.parserVersion.empty() || sourceDigestBytes.size() != 32
               || cacheDigest.size() != 32
               || sourceDigestBytes != sourceDigest(record.originalMarkdown)
               || cacheDigest != sourceDigest(record.parserCache)) {
        return malformed("parser cache source binding or digest is invalid");
    }
    return record;
}

} // namespace Markoff::VNext
