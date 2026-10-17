// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/MarkoffMapValueCodec.h>
#include <markoff/vnext/MarkoffMapCodec.h>
#include <markoff/vnext/StreamNames.h>
#include <markoff/vnext/SourceSnapshotCodec.h>

#include <markoff/core/TextUnits.h>

#include <collabtext/Serialization.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace Markoff::VNext {
namespace {

using namespace CollabText;

constexpr size_t kMaxBundleBytes = 16U * 1024U * 1024U;
constexpr uint32_t kMaxOperations = 65536;
constexpr uint32_t kMaxPayloadBytes = 8U * 1024U * 1024U;
constexpr size_t kMaxRouteBytes = 256;
constexpr size_t kMaxActionIdBytes = 256;

Error malformed(const char* message)
{
    return Error(ErrorCode::malformed, message);
}

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool known(ActionKind kind)
{
    return kind == ActionKind::edit || kind == ActionKind::undo
        || kind == ActionKind::redo || kind == ActionKind::initialImport
        || kind == ActionKind::replacement;
}

bool known(OperationPrimitive primitive)
{
    return primitive == OperationPrimitive::text
        || primitive == OperationPrimitive::idList
        || primitive == OperationPrimitive::markoffMap;
}

bool known(OperationCodec codec)
{
    return codec == OperationCodec::ctbo3
        || codec == OperationCodec::ctlo2 || codec == OperationCodec::markoffOwned;
}

bool compatible(OperationPrimitive primitive, OperationCodec codec)
{
    switch (primitive) {
    case OperationPrimitive::text:
        return codec == OperationCodec::ctbo3;
    case OperationPrimitive::idList:
        return codec == OperationCodec::ctlo2;
    case OperationPrimitive::markoffMap:
        return codec == OperationCodec::markoffOwned;
    }
    return false;
}

bool validBlock(const std::optional<BlockIdentity>& block)
{
    return !block || (block->actor.valid() && block->counter != 0);
}

bool validOperation(const ActionOperation& operation)
{
    if (operation.stream.empty() || operation.stream.size() > kMaxRouteBytes
        || !validUtf8(operation.stream) || !known(operation.primitive)
        || !operation.producer.valid() || operation.counter == 0
        || !known(operation.codec) || !compatible(operation.primitive, operation.codec)
        || !validBlock(operation.targetBlock))
        return false;
    if (operation.primitive == OperationPrimitive::text && !operation.targetBlock)
        return false;
    if (operation.primitive == OperationPrimitive::idList && operation.targetBlock)
        return false;
    if (operation.primitive == OperationPrimitive::text) {
        const auto namedBlock = parseBlockTextStreamName(operation.stream);
        if (!namedBlock || namedBlock.value() != *operation.targetBlock) return false;
    }
    if (operation.primitive == OperationPrimitive::idList
        && operation.stream != kOrderStreamName)
        return false;
    return !operation.payload.isEmpty()
        && static_cast<size_t>(operation.payload.size()) <= kMaxPayloadBytes;
}

bool hasUniqueOperationIdentities(const std::vector<ActionOperation>& operations)
{
    std::set<std::tuple<std::string, ActorId, uint64_t>> identities;
    for (const auto& operation : operations) {
        if (!identities.emplace(operation.stream, operation.producer,
                                operation.counter).second)
            return false;
    }
    return true;
}

class Writer {
public:
    bool bytes(const char* data, size_t size)
    {
        if (size == 0) return true;
        if (size > kMaxBundleBytes - static_cast<size_t>(m_bytes.size())) return false;
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
        const char encoded[] = {static_cast<char>(value >> 8),
                                static_cast<char>(value)};
        return bytes(encoded, sizeof(encoded));
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
        return value.size() <= kMaxRouteBytes
            && u16(static_cast<uint16_t>(value.size()))
            && bytes(value.data(), value.size());
    }

    bool actor(const ActorId& id)
    {
        const auto raw = id.bytes();
        return id.valid() && bytes(reinterpret_cast<const char*>(raw.data()), raw.size());
    }

    bool block(const std::optional<BlockIdentity>& id)
    {
        if (!u8(id ? 1 : 0)) return false;
        return !id || (actor(id->actor) && u64(id->counter));
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

    bool string(std::string& value)
    {
        uint16_t size;
        if (!u16(size) || size == 0 || size > kMaxRouteBytes || size > remaining())
            return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += size;
        return validUtf8(value);
    }

    bool actor(ActorId& id)
    {
        std::array<uint8_t, ActorId::kBytes> raw{};
        if (!bytes(reinterpret_cast<char*>(raw.data()), raw.size())) return false;
        id = ActorId::from_bytes(raw);
        return id.valid();
    }

    bool block(std::optional<BlockIdentity>& id)
    {
        uint8_t present;
        if (!u8(present) || present > 1) return false;
        if (!present) {
            id.reset();
            return true;
        }
        BlockIdentity value;
        if (!actor(value.actor) || !u64(value.counter) || value.counter == 0) return false;
        id = value;
        return true;
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

Result<QByteArray> ActionBundleCodec::encode(const Bundle& bundle)
{
    if (bundle.document.empty() || bundle.document.size() > kMaxRouteBytes
        || !validUtf8(bundle.document) || bundle.epoch == 0
        || bundle.deliverySequence == 0 || bundle.actionId.isEmpty()
        || static_cast<size_t>(bundle.actionId.size()) > kMaxActionIdBytes
        || !known(bundle.kind))
        return Error(ErrorCode::invalid_identity, "invalid MAB document, sequence, action ID or kind");
    if (bundle.operations.size() > kMaxOperations
        || (bundle.operations.empty() && bundle.kind != ActionKind::initialImport))
        return Error(ErrorCode::limit_exceeded, "invalid MAB operation count");
    const bool requiredSourceAction = bundle.kind == ActionKind::initialImport
        || bundle.kind == ActionKind::replacement;
    const bool mayRestoreSource = bundle.kind == ActionKind::undo
        || bundle.kind == ActionKind::redo;
    if (requiredSourceAction && bundle.sourceSnapshot.isEmpty())
        return Error(ErrorCode::malformed,
                     "source actions require an exact MDS source snapshot");
    if (!bundle.sourceSnapshot.isEmpty()) {
        if (!requiredSourceAction && !mayRestoreSource)
            return Error(ErrorCode::malformed,
                         "MAB source snapshot is only valid on source or compensation actions");
        auto source = SourceSnapshotCodec::decode(
            bundle.sourceSnapshot, bundle.document, bundle.epoch);
        if (!source.ok()) return source.error();
    }
    if (!hasUniqueOperationIdentities(bundle.operations))
        return Error(ErrorCode::malformed,
                     "MAB repeats an operation identity in one stream");
    auto identitiesValid = validateOperationIdentities(bundle);
    if (!identitiesValid.ok()) return identitiesValid.error();

    Writer writer;
    const char magic[] = {'M', 'A', 'B', 3};
    if (!writer.bytes(magic, sizeof(magic)) || !writer.string(bundle.document)
        || !writer.u64(bundle.epoch) || !writer.u64(bundle.deliverySequence)
        || !writer.u16(static_cast<uint16_t>(bundle.actionId.size()))
        || !writer.bytes(bundle.actionId.constData(), static_cast<size_t>(bundle.actionId.size()))
        || !writer.u8(static_cast<uint8_t>(bundle.kind))
        || !writer.u32(static_cast<uint32_t>(bundle.operations.size())))
        return Error(ErrorCode::limit_exceeded, "MAB record exceeds 16 MiB");

    for (const auto& operation : bundle.operations) {
        if (!validOperation(operation))
            return Error(ErrorCode::malformed, "invalid MAB operation metadata");
        if (!writer.string(operation.stream)
            || !writer.u8(static_cast<uint8_t>(operation.primitive))
            || !writer.block(operation.targetBlock)
            || !writer.actor(operation.producer) || !writer.u64(operation.counter)
            || !writer.u8(static_cast<uint8_t>(operation.codec))
            || !writer.u32(static_cast<uint32_t>(operation.payload.size()))
            || !writer.bytes(operation.payload.constData(),
                             static_cast<size_t>(operation.payload.size())))
            return Error(ErrorCode::limit_exceeded, "MAB record exceeds 16 MiB");
    }
    if (!writer.u32(static_cast<uint32_t>(bundle.sourceSnapshot.size()))
        || (!bundle.sourceSnapshot.isEmpty()
            && !writer.bytes(bundle.sourceSnapshot.constData(),
                             static_cast<size_t>(bundle.sourceSnapshot.size()))))
        return Error(ErrorCode::limit_exceeded, "MAB source snapshot exceeds 16 MiB");
    return std::move(writer).take();
}

Result<ActionBundleCodec::Bundle> ActionBundleCodec::decode(
    const QByteArray& bytes, const std::string& expectedDocument,
    uint64_t expectedEpoch)
{
    if (bytes.size() < 4) return malformed("truncated MAB header");
    if (bytes.size() > static_cast<qsizetype>(kMaxBundleBytes))
        return Error(ErrorCode::limit_exceeded, "MAB record exceeds 16 MiB");
    if (bytes[0] != 'M' || bytes[1] != 'A' || bytes[2] != 'B')
        return Error(ErrorCode::unknown_format, "not a Markoff action bundle");
    const uint8_t version = static_cast<uint8_t>(static_cast<unsigned char>(bytes[3]));
    if (version != 3)
        return Error(ErrorCode::unsupported_version, "unsupported MAB version");
    if (expectedDocument.empty() || expectedDocument.size() > kMaxRouteBytes
        || !validUtf8(expectedDocument) || expectedEpoch == 0)
        return Error(ErrorCode::invalid_identity, "invalid expected MAB scope");

    Reader reader(bytes);
    char magic[4];
    Bundle bundle;
    uint8_t kind;
    uint16_t actionIdSize;
    uint32_t operationCount;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(bundle.document)
        || !reader.u64(bundle.epoch) || !reader.u64(bundle.deliverySequence)
        || !reader.u16(actionIdSize) || actionIdSize == 0
        || actionIdSize > kMaxActionIdBytes || !reader.byteArray(bundle.actionId, actionIdSize)
        || !reader.u8(kind) || !reader.u32(operationCount))
        return malformed("truncated or malformed MAB header");
    bundle.kind = static_cast<ActionKind>(kind);
    if (!known(bundle.kind)) return malformed("unknown MAB action kind");
    if (bundle.document != expectedDocument)
        return Error(ErrorCode::wrong_document, "MAB belongs to another document");
    if (bundle.epoch != expectedEpoch)
        return Error(ErrorCode::wrong_epoch, "MAB belongs to another epoch");
    if (bundle.deliverySequence == 0 || operationCount > kMaxOperations
        || (operationCount == 0 && bundle.kind != ActionKind::initialImport))
        return malformed("invalid MAB sequence or operation count");

    bundle.operations.reserve(operationCount);
    for (uint32_t i = 0; i < operationCount; ++i) {
        ActionOperation operation;
        uint8_t primitive;
        uint8_t codec;
        uint32_t payloadSize;
        if (!reader.string(operation.stream) || !reader.u8(primitive)
            || !reader.block(operation.targetBlock) || !reader.actor(operation.producer)
            || !reader.u64(operation.counter) || !reader.u8(codec)
            || !reader.u32(payloadSize) || payloadSize == 0
            || payloadSize > kMaxPayloadBytes || !reader.byteArray(operation.payload, payloadSize))
            return malformed("truncated or malformed MAB operation");
        if (!known(static_cast<OperationCodec>(codec)))
            return Error(ErrorCode::unsupported_version, "unsupported MAB operation codec");
        operation.primitive = static_cast<OperationPrimitive>(primitive);
        operation.codec = static_cast<OperationCodec>(codec);
        if (!validOperation(operation))
            return malformed("invalid MAB operation metadata");
        bundle.operations.push_back(std::move(operation));
    }
    {
        uint32_t sourceSnapshotSize;
        if (!reader.u32(sourceSnapshotSize)
            || sourceSnapshotSize > reader.remaining()
            || (sourceSnapshotSize > 0
                && !reader.byteArray(bundle.sourceSnapshot, sourceSnapshotSize)))
            return malformed("invalid MAB/3 source snapshot field");
        const bool requiredSourceAction = bundle.kind == ActionKind::initialImport
            || bundle.kind == ActionKind::replacement;
        const bool mayRestoreSource = bundle.kind == ActionKind::undo
            || bundle.kind == ActionKind::redo;
        if ((requiredSourceAction && sourceSnapshotSize == 0)
            || (!requiredSourceAction && !mayRestoreSource && sourceSnapshotSize > 0))
            return malformed("MAB/3 source snapshot does not match action kind");
        if (sourceSnapshotSize > 0) {
            auto source = SourceSnapshotCodec::decode(
                bundle.sourceSnapshot, bundle.document, bundle.epoch);
            if (!source.ok()) return source.error();
        }
    }
    if (!reader.atEnd()) return malformed("trailing bytes after MAB bundle");
    if (!hasUniqueOperationIdentities(bundle.operations))
        return Error(ErrorCode::malformed,
                     "MAB repeats an operation identity in one stream");
    auto identitiesValid = validateOperationIdentities(bundle);
    if (!identitiesValid.ok()) return identitiesValid.error();
    return bundle;
}

Result<void> ActionBundleCodec::validateOperationIdentities(
    const Bundle& bundle)
{
    if (!hasUniqueOperationIdentities(bundle.operations))
        return Error(ErrorCode::malformed,
                     "MAB repeats an operation identity in one stream");
    for (const auto& operation : bundle.operations) {
        if (!validOperation(operation))
            return malformed("invalid MAB operation metadata");

        Crdt::Identity identity;
        if (operation.codec == OperationCodec::ctbo3) {
            const std::string_view bytes(operation.payload.constData(),
                                         static_cast<size_t>(operation.payload.size()));
            auto decoded = Crdt::decode_operation(bytes);
            if (!decoded) return decoded.error();
            identity = Crdt::op_identity(decoded.value());
        } else if (operation.codec == OperationCodec::ctlo2) {
            const std::string_view bytes(operation.payload.constData(),
                                         static_cast<size_t>(operation.payload.size()));
            auto decoded = Crdt::decode_idlist_operation(bytes);
            if (!decoded) return decoded.error();
            identity = Crdt::idlist_op_identity(decoded.value());
        } else {
            auto decoded = MarkoffMapCodec::decode(operation.payload);
            if (!decoded) return decoded.error();
            auto canonicalStream = mapStreamName(decoded.value().map);
            if (!canonicalStream || canonicalStream.value() != operation.stream)
                return Error(ErrorCode::wrong_stream,
                             "MAB map stream does not match its payload map name");
            auto binding = MarkoffMapValueCodec::validateBinding(
                decoded.value(), operation.targetBlock);
            if (!binding.ok()) return binding.error();
            identity = Crdt::Identity(decoded.value().producer, decoded.value().counter);
        }

        if (identity.actor != operation.producer || identity.counter != operation.counter)
            return Error(ErrorCode::invalid_identity,
                         "MAB producer/counter does not match decoded operation identity");
    }
    return {};
}

} // namespace Markoff::VNext
