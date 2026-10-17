// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/MarkoffMapValueCodec.h>

#include <markoff/core/TextUnits.h>

#include <bit>
#include <cstdint>
#include <limits>

namespace Markoff::VNext {
namespace {

using namespace CollabText;
constexpr qsizetype kMaxAttributeStringBytes = 1024 * 1024 - 9;
constexpr qsizetype kMaxLinkRefFieldBytes = 1024 * 1024 - 12;

bool hasMagic(const QByteArray& bytes, char a, char b, char c)
{
    return bytes.size() >= 4 && bytes[0] == a && bytes[1] == b && bytes[2] == c;
}

Error malformed(const char* message)
{
    return Error(ErrorCode::malformed, message);
}

void appendU32(QByteArray& bytes, uint32_t value)
{
    for (int i = 0; i != 4; ++i)
        bytes.append(static_cast<char>(value >> (24 - i * 8)));
}

uint32_t readU32(const char* bytes)
{
    uint32_t value = 0;
    for (int i = 0; i != 4; ++i)
        value = (value << 8) | static_cast<unsigned char>(bytes[i]);
    return value;
}

bool validBlockKind(uint8_t raw)
{
    return raw <= static_cast<uint8_t>(Markoff::BlockKind::Table);
}

Result<QByteArray> encodeQStringField(const QString& value, qsizetype maxBytes,
                                     const char* description)
{
    const QByteArray utf8 = value.toUtf8();
    if (utf8.size() > maxBytes
        || !TextUnits::checkedByteToQtPos(utf8, utf8.size())
        || QString::fromUtf8(utf8.constData(), utf8.size()) != value)
        return Error(ErrorCode::invalid_unicode, description);
    return utf8;
}

Result<QString> decodeQStringField(const QByteArray& utf8, const char* description)
{
    if (!TextUnits::checkedByteToQtPos(utf8, utf8.size()))
        return Error(ErrorCode::invalid_unicode, description);
    const QString value = QString::fromUtf8(utf8.constData(), utf8.size());
    if (value.toUtf8() != utf8)
        return Error(ErrorCode::invalid_unicode, description);
    return value;
}

} // namespace

Result<QByteArray> MarkoffMapValueCodec::encodeBlockKind(Markoff::BlockKind value)
{
    const auto raw = static_cast<uint8_t>(value);
    if (!validBlockKind(raw))
        return Error(ErrorCode::malformed, "unknown Markoff block kind");
    QByteArray bytes("MBK\x01", 4);
    bytes.append(static_cast<char>(raw));
    return bytes;
}

Result<Markoff::BlockKind> MarkoffMapValueCodec::decodeBlockKind(
    const QByteArray& bytes)
{
    if (bytes.size() != 5 || !hasMagic(bytes, 'M', 'B', 'K'))
        return malformed("malformed Markoff block-kind value");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported block-kind value version");
    const auto raw = static_cast<uint8_t>(static_cast<unsigned char>(bytes[4]));
    if (!validBlockKind(raw)) return malformed("unknown Markoff block kind");
    return static_cast<Markoff::BlockKind>(raw);
}

Result<QByteArray> MarkoffMapValueCodec::encodeAttribute(const Markoff::AttrValue& value)
{
    QByteArray bytes("MAV\x01", 4);
    if (const auto* integer = std::get_if<int>(&value)) {
        static_assert(sizeof(int) <= sizeof(int32_t));
        bytes.append(char(1));
        appendU32(bytes, std::bit_cast<uint32_t>(static_cast<int32_t>(*integer)));
        return bytes;
    }
    if (const auto* string = std::get_if<QString>(&value)) {
        const QByteArray utf8 = string->toUtf8();
        if (utf8.size() > kMaxAttributeStringBytes
            || !TextUnits::checkedByteToQtPos(utf8, utf8.size())
            || QString::fromUtf8(utf8.constData(), utf8.size()) != *string)
            return Error(ErrorCode::invalid_unicode,
                         "Markoff attribute string is malformed or exceeds 1 MiB");
        bytes.append(char(2));
        appendU32(bytes, static_cast<uint32_t>(utf8.size()));
        bytes.append(utf8);
        return bytes;
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        bytes.append(char(3));
        bytes.append(*boolean ? char(1) : char(0));
        return bytes;
    }
    return Error(ErrorCode::malformed, "unsupported Markoff attribute value type");
}

Result<Markoff::AttrValue> MarkoffMapValueCodec::decodeAttribute(
    const QByteArray& bytes)
{
    if (!hasMagic(bytes, 'M', 'A', 'V'))
        return malformed("not a Markoff attribute value");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version, "unsupported Markoff attribute version");
    if (bytes.size() < 5) return malformed("truncated Markoff attribute value");

    const uint8_t tag = static_cast<uint8_t>(static_cast<unsigned char>(bytes[4]));
    if (tag == 1) {
        if (bytes.size() != 9) return malformed("invalid Markoff integer attribute length");
        const uint32_t raw = readU32(bytes.constData() + 5);
        const int32_t integer = std::bit_cast<int32_t>(raw);
        if (integer < std::numeric_limits<int>::min()
            || integer > std::numeric_limits<int>::max())
            return Error(ErrorCode::limit_exceeded,
                         "Markoff integer attribute is outside the host int range");
        return Markoff::AttrValue{static_cast<int>(integer)};
    }
    if (tag == 2) {
        if (bytes.size() < 9) return malformed("truncated Markoff string attribute");
        const uint32_t size = readU32(bytes.constData() + 5);
        if (size > static_cast<uint32_t>(kMaxAttributeStringBytes)
            || bytes.size() != static_cast<qsizetype>(9 + size))
            return malformed("invalid Markoff string attribute length");
        const QByteArray utf8(bytes.constData() + 9, static_cast<qsizetype>(size));
        if (!TextUnits::checkedByteToQtPos(utf8, utf8.size()))
            return Error(ErrorCode::invalid_unicode,
                         "Markoff attribute string is not valid UTF-8");
        const QString string = QString::fromUtf8(utf8.constData(), utf8.size());
        if (string.toUtf8() != utf8)
            return Error(ErrorCode::invalid_unicode,
                         "Markoff attribute string does not round-trip as UTF-8");
        return Markoff::AttrValue{string};
    }
    if (tag == 3) {
        if (bytes.size() != 6) return malformed("invalid Markoff boolean attribute length");
        const uint8_t raw = static_cast<uint8_t>(static_cast<unsigned char>(bytes[5]));
        if (raw > 1) return malformed("invalid Markoff boolean attribute value");
        return Markoff::AttrValue{raw == 1};
    }
    return malformed("unknown Markoff attribute value tag");
}

Result<QByteArray> MarkoffMapValueCodec::encodeLinkRef(
    const Markoff::LinkRefValue& value)
{
    auto url = encodeQStringField(value.url, kMaxLinkRefFieldBytes,
                                  "link-reference URL is malformed or too large");
    if (!url.ok()) return url.error();
    auto title = encodeQStringField(value.title, kMaxLinkRefFieldBytes,
                                    "link-reference title is malformed or too large");
    if (!title.ok()) return title.error();
    if (url.value().size() > kMaxLinkRefFieldBytes - title.value().size())
        return Error(ErrorCode::limit_exceeded,
                     "link-reference value exceeds 1 MiB");

    QByteArray bytes("MLR\x01", 4);
    appendU32(bytes, static_cast<uint32_t>(url.value().size()));
    bytes.append(url.value());
    appendU32(bytes, static_cast<uint32_t>(title.value().size()));
    bytes.append(title.value());
    return bytes;
}

Result<Markoff::LinkRefValue> MarkoffMapValueCodec::decodeLinkRef(
    const QByteArray& bytes)
{
    if (!hasMagic(bytes, 'M', 'L', 'R'))
        return malformed("not a Markoff link-reference value");
    if (static_cast<uint8_t>(static_cast<unsigned char>(bytes[3])) != 1)
        return Error(ErrorCode::unsupported_version,
                     "unsupported link-reference value version");
    if (bytes.size() < 12 || bytes.size() > 1024 * 1024)
        return malformed("malformed Markoff link-reference value length");

    const uint32_t urlSize = readU32(bytes.constData() + 4);
    if (urlSize > static_cast<uint32_t>(kMaxLinkRefFieldBytes)
        || static_cast<qsizetype>(urlSize) > bytes.size() - 12)
        return malformed("invalid Markoff link-reference URL length");
    const qsizetype titleLengthOffset = 8 + static_cast<qsizetype>(urlSize);
    const uint32_t titleSize = readU32(bytes.constData() + titleLengthOffset);
    if (titleSize > static_cast<uint32_t>(kMaxLinkRefFieldBytes)
        || bytes.size() != titleLengthOffset + 4 + static_cast<qsizetype>(titleSize))
        return malformed("invalid Markoff link-reference title length");

    const QByteArray urlBytes(bytes.constData() + 8, static_cast<qsizetype>(urlSize));
    const QByteArray titleBytes(bytes.constData() + titleLengthOffset + 4,
                                static_cast<qsizetype>(titleSize));
    auto url = decodeQStringField(urlBytes, "link-reference URL is not valid UTF-8");
    if (!url.ok()) return url.error();
    auto title = decodeQStringField(titleBytes,
                                    "link-reference title is not valid UTF-8");
    if (!title.ok()) return title.error();
    if (urlSize + titleSize > static_cast<uint32_t>(kMaxLinkRefFieldBytes))
        return Error(ErrorCode::limit_exceeded,
                     "link-reference value exceeds 1 MiB");
    return Markoff::LinkRefValue{std::move(url).value(), std::move(title).value()};
}

Result<void> MarkoffMapValueCodec::validateBinding(
    const MarkoffMapOperation& operation,
    const std::optional<BlockIdentity>& targetBlock)
{
    if (targetBlock && (!targetBlock->actor.valid() || targetBlock->counter == 0))
        return Error(ErrorCode::invalid_identity,
                     "Markoff map target block identity is invalid");
    if (operation.map == "kind-tag") {
        if (!targetBlock || !operation.key.isEmpty())
            return malformed("kind-tag operations require a target block and empty key");
        if (!operation.tombstone) {
            auto kind = decodeBlockKind(operation.value);
            if (!kind) return kind.error();
        }
    } else if (operation.map == "block-attrs") {
        if (!targetBlock || operation.key.isEmpty()
            || !TextUnits::checkedByteToQtPos(operation.key, operation.key.size()))
            return malformed("block-attrs operations require a target block and UTF-8 attribute key");
        if (!operation.tombstone) {
            auto value = decodeAttribute(operation.value);
            if (!value) return value.error();
        }
    } else if (operation.map == "link-ref") {
        if (targetBlock)
            return malformed("link-ref operations are document-scoped");
        if (!operation.tombstone) {
            auto value = decodeLinkRef(operation.value);
            if (!value) return value.error();
        }
    } else if (operation.map == "frontmatter"
               || operation.map == "footnote-def") {
        if (targetBlock)
            return malformed("frontmatter and footnote operations are document-scoped");
    }
    return {};
}

} // namespace Markoff::VNext
