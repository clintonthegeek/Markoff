// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/CrossStreamHistoryCodec.h>
#include <markoff/vnext/SourceSnapshotCodec.h>
#include <markoff/vnext/StreamNames.h>

#include <markoff/core/TextUnits.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace Markoff::VNext {
namespace {
using namespace CollabText;
constexpr size_t kMaxBytes = 32U * 1024U * 1024U;
constexpr size_t kMaxSourceSnapshot = 16U * 1024U * 1024U;
constexpr uint32_t kMaxEvents = 100000;
constexpr uint32_t kMaxGroups = 200000;
constexpr uint32_t kMaxIdentities = 400000;
constexpr size_t kMaxRoute = 256;
constexpr size_t kMaxAction = 256;
constexpr size_t kMaxRevision = 4096;
constexpr size_t kMaxMapUndoValue = 1024U * 1024U;

Error malformed(const char* message) { return Error(ErrorCode::malformed, message); }

bool validUtf8(const std::string& value)
{
    const QByteArray bytes(value.data(), static_cast<qsizetype>(value.size()));
    return Markoff::TextUnits::checkedByteToQtPos(bytes, bytes.size()).has_value();
}

bool knownKind(ActionKind kind)
{
    return kind == ActionKind::edit || kind == ActionKind::undo
        || kind == ActionKind::redo || kind == ActionKind::initialImport
        || kind == ActionKind::replacement;
}

bool validRoute(const std::string& stream, OperationPrimitive primitive)
{
    if (stream.empty() || stream.size() > kMaxRoute || !validUtf8(stream)) return false;
    if (primitive == OperationPrimitive::idList)
        return stream == kOrderStreamName;
    if (primitive == OperationPrimitive::text)
        return parseBlockTextStreamName(stream).ok();
    if (primitive == OperationPrimitive::markoffMap) {
        constexpr std::string_view prefix = "markoff/map/v1/";
        return stream.starts_with(prefix) && mapStreamName(stream.substr(prefix.size())).ok();
    }
    return false;
}

bool validId(const Crdt::Identity& id, const ActorId& owner)
{
    return id.actor == owner && id.counter != 0;
}

bool validIndex(const CrossStreamHistoryIndex& index, bool requireMapPriorState = false)
{
    if (index.document.empty() || index.document.size() > kMaxRoute
        || !validUtf8(index.document) || index.epoch == 0 || !index.owner.valid()
        || index.acceptedPrior.isEmpty()
        || static_cast<size_t>(index.acceptedPrior.size()) > kMaxRevision
        || index.events.size() > kMaxEvents || index.retirements.size() > kMaxGroups)
        return false;

    std::set<QByteArray> actionIds;
    std::set<std::tuple<std::string, OperationPrimitive, ActorId, uint64_t>> knownGroups;
    std::map<std::tuple<std::string, OperationPrimitive, ActorId, uint64_t>, QByteArray>
        groupOrigins;
    std::map<QByteArray, const ActionHistoryEvent*> eventsById;
    std::set<std::tuple<std::string, ActorId, uint64_t>> operationClaims;
    uint64_t groupCount = 0;
    uint64_t identityCount = 0;
    for (const auto& event : index.events) {
        if (event.actionId.isEmpty() || static_cast<size_t>(event.actionId.size()) > kMaxAction
            || !knownKind(event.kind) || !actionIds.insert(event.actionId).second)
            return false;
        if (event.kind == ActionKind::replacement) {
            if (event.sourceSnapshotBefore.isEmpty()
                || !SourceSnapshotCodec::decode(event.sourceSnapshotBefore,
                    index.document, index.epoch).ok())
                return false;
        } else if (!event.sourceSnapshotBefore.isEmpty()) {
            return false;
        }
        eventsById.emplace(event.actionId, &event);
        if (event.kind == ActionKind::undo || event.kind == ActionKind::redo) {
            if (event.parentActionId.isEmpty() || event.parentActionId == event.actionId
                || !actionIds.contains(event.parentActionId))
                return false;
            const auto parent = eventsById.find(event.parentActionId);
            if (parent == eventsById.end()
                || (event.kind == ActionKind::redo
                    && parent->second->kind != ActionKind::undo)
                || (event.kind == ActionKind::undo
                    && parent->second->kind == ActionKind::undo))
                return false;
        } else if (!event.parentActionId.isEmpty()) {
            return false;
        }
        groupCount += event.groups.size();
        if (groupCount > kMaxGroups) return false;
        std::set<std::tuple<std::string, ActorId, uint64_t>> eventGroups;
        for (const auto& group : event.groups) {
            if (!validRoute(group.stream, group.primitive)
                || !validId(group.groupId, index.owner) || group.operationIds.empty()
                || !eventGroups.emplace(group.stream, group.groupId.actor,
                                        group.groupId.counter).second)
                return false;
            if (event.kind == ActionKind::undo || event.kind == ActionKind::redo) {
                const auto parent = eventsById.find(event.parentActionId);
                const ActionHistoryEvent* ancestor = parent == eventsById.end()
                    ? nullptr : parent->second;
                bool belongsToAncestry = false;
                while (ancestor && !belongsToAncestry) {
                    belongsToAncestry = std::any_of(ancestor->groups.begin(),
                        ancestor->groups.end(), [&](const HistoryGroupAssociation& candidate) {
                            if (candidate.stream != group.stream
                                || candidate.primitive != group.primitive)
                                return false;
                            // Markoff map compensation emits a fresh singleton group;
                            // the route is stable while its group identity is new.
                            return group.primitive == OperationPrimitive::markoffMap
                                || candidate.groupId == group.groupId;
                        });
                    if (belongsToAncestry || ancestor->parentActionId.isEmpty()) break;
                    const auto next = eventsById.find(ancestor->parentActionId);
                    ancestor = next == eventsById.end() ? nullptr : next->second;
                }
                if (!belongsToAncestry) return false;
            }
            if (group.primitive == OperationPrimitive::markoffMap) {
                if (requireMapPriorState
                    && (!group.hasMapPriorState || group.operationIds.size() != 1
                        || group.groupId != group.operationIds.front()))
                    return false;
                if (group.hasMapPriorState
                    && (!group.mapPriorValueExisted && !group.mapPriorValue.isEmpty()))
                    return false;
                if (static_cast<size_t>(group.mapPriorValue.size()) > kMaxMapUndoValue)
                    return false;
            } else if (group.hasMapPriorState || group.mapPriorValueExisted
                       || !group.mapPriorValue.isEmpty()) {
                return false;
            }
            const auto groupKey = std::make_tuple(group.stream, group.primitive,
                group.groupId.actor, group.groupId.counter);
            knownGroups.insert(groupKey);
            groupOrigins.try_emplace(groupKey, event.actionId);
            identityCount += 1 + group.operationIds.size();
            if (identityCount > kMaxIdentities) return false;
            std::set<std::pair<ActorId, uint64_t>> operationIds;
            for (const auto& id : group.operationIds)
                if (!validId(id, index.owner)
                    || !operationIds.emplace(id.actor, id.counter).second
                    || !operationClaims.emplace(group.stream, id.actor, id.counter).second)
                    return false;
        }
    }

    std::set<std::tuple<std::string, OperationPrimitive, ActorId, uint64_t>> retired;
    for (const auto& item : index.retirements) {
        const auto groupKey = std::make_tuple(item.stream, item.primitive,
            item.groupId.actor, item.groupId.counter);
        const auto event = eventsById.find(item.retirementActionId);
        const auto origin = groupOrigins.find(groupKey);
        if (!validRoute(item.stream, item.primitive) || !validId(item.groupId, index.owner)
            || event == eventsById.end() || event->second->kind != ActionKind::redo
            || origin == groupOrigins.end() || !knownGroups.contains(groupKey)
            || !retired.emplace(item.stream, item.primitive,
                                item.groupId.actor, item.groupId.counter).second)
            return false;

        // A retirement is part of the redo branch that restores the original
        // action. Merely naming an existing action ID is insufficient: an
        // unrelated redo must not retire another branch's undo group.
        const ActionHistoryEvent* ancestor = event->second;
        bool reachesOrigin = false;
        while (ancestor) {
            if (ancestor->actionId == origin->second) {
                reachesOrigin = true;
                break;
            }
            if (ancestor->parentActionId.isEmpty()) break;
            const auto parent = eventsById.find(ancestor->parentActionId);
            ancestor = parent == eventsById.end() ? nullptr : parent->second;
        }
        if (!reachesOrigin) return false;
    }
    return identityCount + index.retirements.size() <= kMaxIdentities;
}

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
    bool string(const std::string& value)
    {
        return value.size() <= kMaxRoute && u16(static_cast<uint16_t>(value.size()))
            && bytes(value.data(), value.size());
    }
    bool field(const QByteArray& value, size_t limit)
    {
        return !value.isEmpty() && static_cast<size_t>(value.size()) <= limit
            && u16(static_cast<uint16_t>(value.size()))
            && bytes(value.constData(), static_cast<size_t>(value.size()));
    }
    bool blob(const QByteArray& value, size_t limit)
    {
        return static_cast<size_t>(value.size()) <= limit
            && u32(static_cast<uint32_t>(value.size()))
            && bytes(value.constData(), static_cast<size_t>(value.size()));
    }
    bool actor(const ActorId& actor)
    {
        const auto raw = actor.bytes();
        return actor.valid() && bytes(reinterpret_cast<const char*>(raw.data()), raw.size());
    }
    bool identity(const Crdt::Identity& id)
    {
        return actor(id.actor) && id.counter != 0 && u64(id.counter);
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
    bool string(std::string& value)
    {
        uint16_t size;
        if (!u16(size) || size == 0 || size > kMaxRoute || size > remaining()) return false;
        value.assign(m_bytes.constData() + m_offset, size);
        m_offset += size;
        return validUtf8(value);
    }
    bool field(QByteArray& value, size_t limit)
    {
        uint16_t size;
        if (!u16(size) || size == 0 || size > limit || size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, size);
        m_offset += size;
        return true;
    }
    bool blob(QByteArray& value, size_t limit)
    {
        uint32_t size;
        if (!u32(size) || size > limit || size > remaining()) return false;
        value = QByteArray(m_bytes.constData() + m_offset, static_cast<qsizetype>(size));
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
    bool identity(Crdt::Identity& id)
    {
        return actor(id.actor) && u64(id.counter) && id.counter != 0;
    }
    size_t remaining() const { return static_cast<size_t>(m_bytes.size() - m_offset); }
    bool atEnd() const { return m_offset == m_bytes.size(); }
private:
    const QByteArray& m_bytes;
    qsizetype m_offset = 0;
};
}

Result<QByteArray> CrossStreamHistoryCodec::encode(const CrossStreamHistoryIndex& index)
{
    using namespace CollabText;
    if (!validIndex(index, true))
        return Error(ErrorCode::malformed, "invalid cross-stream history index");
    Writer writer;
    constexpr char magic[] = {'M','A','I',3};
    const auto owner = index.owner.bytes();
    if (!writer.bytes(magic, sizeof(magic)) || !writer.string(index.document)
        || !writer.u64(index.epoch)
        || !writer.bytes(reinterpret_cast<const char*>(owner.data()), owner.size())
        || !writer.field(index.acceptedPrior, kMaxRevision)
        || !writer.u32(static_cast<uint32_t>(index.events.size())))
        return Error(ErrorCode::limit_exceeded, "cross-stream history exceeds 8 MiB");
    for (const auto& event : index.events) {
        if (!writer.field(event.actionId, kMaxAction)
            || !writer.u8(static_cast<uint8_t>(event.kind))
            || !writer.u16(static_cast<uint16_t>(event.parentActionId.size()))
            || !writer.bytes(event.parentActionId.constData(),
                             static_cast<size_t>(event.parentActionId.size()))
            || !writer.u32(static_cast<uint32_t>(event.groups.size())))
            return Error(ErrorCode::limit_exceeded, "cross-stream history exceeds 8 MiB");
        if (!writer.blob(event.sourceSnapshotBefore, kMaxSourceSnapshot))
            return Error(ErrorCode::limit_exceeded,
                         "replacement prior source snapshot exceeds its limit");
        for (const auto& group : event.groups) {
            if (!writer.string(group.stream)
                || !writer.u8(static_cast<uint8_t>(group.primitive))
                || !writer.identity(group.groupId)
                || !writer.u32(static_cast<uint32_t>(group.operationIds.size())))
                return Error(ErrorCode::limit_exceeded, "cross-stream history exceeds 8 MiB");
            for (const auto& id : group.operationIds)
                if (!writer.identity(id))
                    return Error(ErrorCode::malformed, "invalid history operation identity");
            if (!writer.u8(group.hasMapPriorState ? 1 : 0)
                || (group.hasMapPriorState
                    && (!writer.u8(group.mapPriorValueExisted ? 1 : 0)
                        || !writer.blob(group.mapPriorValue, kMaxMapUndoValue))))
                return Error(ErrorCode::limit_exceeded,
                             "cross-stream map undo state exceeds its limit");
        }
    }
    if (!writer.u32(static_cast<uint32_t>(index.retirements.size())))
        return Error(ErrorCode::limit_exceeded, "cross-stream history exceeds 8 MiB");
    for (const auto& item : index.retirements)
        if (!writer.string(item.stream) || !writer.u8(static_cast<uint8_t>(item.primitive))
            || !writer.identity(item.groupId)
            || !writer.field(item.retirementActionId, kMaxAction))
            return Error(ErrorCode::limit_exceeded, "cross-stream history exceeds 8 MiB");
    return std::move(writer).take();
}

Result<CrossStreamHistoryIndex> CrossStreamHistoryCodec::decode(
    const QByteArray& bytes, const std::string& expectedDocument, uint64_t expectedEpoch,
    const ActorId& expectedOwner, const QByteArray& expectedAcceptedPrior)
{
    using namespace CollabText;
    if (bytes.size() < 4 || static_cast<size_t>(bytes.size()) > kMaxBytes)
        return Error(ErrorCode::limit_exceeded, "cross-stream history size is invalid");
    if (bytes[0] != 'M' || bytes[1] != 'A' || bytes[2] != 'I')
        return Error(ErrorCode::unknown_format, "unknown cross-stream history format");
    const auto version = static_cast<uint8_t>(static_cast<unsigned char>(bytes[3]));
    if (version != 3)
        return Error(ErrorCode::unsupported_version, "unsupported cross-stream history version");
    Reader reader(bytes);
    char magic[4];
    CrossStreamHistoryIndex index;
    std::array<uint8_t, ActorId::kBytes> ownerBytes{};
    uint32_t eventCount;
    if (!reader.bytes(magic, sizeof(magic)) || !reader.string(index.document)
        || !reader.u64(index.epoch)
        || !reader.bytes(reinterpret_cast<char*>(ownerBytes.data()), ownerBytes.size())
        || !reader.field(index.acceptedPrior, kMaxRevision)
        || !reader.u32(eventCount) || eventCount > kMaxEvents)
        return malformed("truncated cross-stream history header");
    index.owner = ActorId::from_bytes(ownerBytes);
    if (index.document != expectedDocument)
        return Error(ErrorCode::wrong_document, "history index belongs to another document");
    if (index.epoch != expectedEpoch)
        return Error(ErrorCode::wrong_epoch, "history index belongs to another epoch");
    if (index.owner != expectedOwner)
        return Error(ErrorCode::actor_conflict, "history index belongs to another actor");
    if (index.acceptedPrior != expectedAcceptedPrior)
        return Error(ErrorCode::pin_conflict, "history index belongs to another accepted prefix");

    index.events.reserve(eventCount);
    uint64_t groupCount = 0, identityCount = 0;
    for (uint32_t i = 0; i < eventCount; ++i) {
        ActionHistoryEvent event;
        uint8_t kind;
        uint16_t parentLength;
        uint32_t groups;
        if (!reader.field(event.actionId, kMaxAction) || !reader.u8(kind)
            || !reader.u16(parentLength) || parentLength > kMaxAction
            || parentLength > reader.remaining())
            return malformed("truncated action-history event");
        event.kind = static_cast<ActionKind>(kind);
        if (parentLength) {
            std::array<char, kMaxAction> parent{};
            if (!reader.bytes(parent.data(), parentLength))
                return malformed("truncated parent action ID");
            event.parentActionId = QByteArray(parent.data(), parentLength);
        }
        if (!reader.u32(groups) || groups > kMaxGroups - groupCount)
            return malformed("invalid action-history group count");
        groupCount += groups;
        if (!reader.blob(event.sourceSnapshotBefore, kMaxSourceSnapshot))
            return malformed("truncated replacement prior source snapshot");
        event.groups.reserve(groups);
        for (uint32_t g = 0; g < groups; ++g) {
            HistoryGroupAssociation group;
            uint8_t primitive;
            uint32_t operationCount;
            if (!reader.string(group.stream) || !reader.u8(primitive)
                || !reader.identity(group.groupId) || !reader.u32(operationCount)
                || operationCount == 0 || operationCount > kMaxIdentities - identityCount)
                return malformed("truncated action-history group");
            group.primitive = static_cast<OperationPrimitive>(primitive);
            identityCount += 1 + operationCount;
            group.operationIds.reserve(operationCount);
            for (uint32_t op = 0; op < operationCount; ++op) {
                Crdt::Identity id;
                if (!reader.identity(id)) return malformed("invalid action-history operation ID");
                group.operationIds.push_back(std::move(id));
            }
            uint8_t hasPrior;
            if (!reader.u8(hasPrior) || hasPrior > 1)
                return malformed("invalid map undo state flag");
            group.hasMapPriorState = hasPrior != 0;
            if (group.hasMapPriorState) {
                uint8_t existed;
                if (!reader.u8(existed) || existed > 1
                    || !reader.blob(group.mapPriorValue, kMaxMapUndoValue))
                    return malformed("truncated map undo prior value");
                group.mapPriorValueExisted = existed != 0;
            }
            event.groups.push_back(std::move(group));
        }
        index.events.push_back(std::move(event));
    }
    uint32_t retirementCount;
    if (!reader.u32(retirementCount) || retirementCount > kMaxGroups)
        return malformed("invalid retirement association count");
    index.retirements.reserve(retirementCount);
    for (uint32_t i = 0; i < retirementCount; ++i) {
        RetiredHistoryAssociation item;
        uint8_t primitive;
        if (!reader.string(item.stream) || !reader.u8(primitive)
            || !reader.identity(item.groupId)
            || !reader.field(item.retirementActionId, kMaxAction))
            return malformed("truncated history retirement association");
        item.primitive = static_cast<OperationPrimitive>(primitive);
        index.retirements.push_back(std::move(item));
    }
    if (!reader.atEnd()) return malformed("trailing bytes in cross-stream history index");
    if (!index.owner.valid() || !validIndex(index, true))
        return malformed("invalid cross-stream history identities or associations");
    return index;
}

} // namespace Markoff::VNext
