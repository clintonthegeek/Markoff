// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/HistoryRetirement.h>

#include <markoff/vnext/AcceptedPrefixProjection.h>
#include <markoff/vnext/UndoDepth.h>

#include "ReferenceProviderTestHooks.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Markoff::VNext {
namespace {

using CollabText::ActorId;
using CollabText::Crdt::Identity;
using CollabText::Error;
using CollabText::ErrorCode;
using CollabText::Primitive;
using CollabText::Result;

bool actionIsCurrentlyUndone(const std::vector<ActionHistoryEvent>& events,
                             const QByteArray& actionId)
{
    std::map<QByteArray, QByteArray> undoParents;
    bool undone = false;
    for (const auto& event : events) {
        if (event.kind == ActionKind::undo && event.parentActionId == actionId) {
            undoParents.emplace(event.actionId, event.parentActionId);
            undone = true;
        } else if (event.kind == ActionKind::redo) {
            if (undoParents.contains(event.parentActionId))
                undone = false;
        }
    }
    return undone;
}

std::string actorKey(const ActorId& actor)
{
    const auto bytes = actor.bytes();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

using GroupKey = std::tuple<std::string, uint8_t, std::string, uint64_t>;

GroupKey groupKey(const std::string& stream, OperationPrimitive primitive,
                  const Identity& id)
{
    return {stream, static_cast<uint8_t>(primitive), actorKey(id.actor), id.counter};
}

GroupKey groupKey(const HistoryGroupAssociation& group)
{
    return groupKey(group.stream, group.primitive, group.groupId);
}

GroupKey groupKey(const RetiredHistoryAssociation& retirement)
{
    return groupKey(retirement.stream, retirement.primitive, retirement.groupId);
}

bool sameIdentity(const Identity& left, const Identity& right)
{
    return left.actor == right.actor && left.counter == right.counter;
}

bool retirementNames(const CrossStreamHistoryIndex& index, const GroupKey& key)
{
    return std::any_of(index.retirements.begin(), index.retirements.end(),
        [&](const RetiredHistoryAssociation& item) { return groupKey(item) == key; });
}

std::optional<size_t> originIndex(const CrossStreamHistoryIndex& index,
                                  const GroupKey& key)
{
    for (size_t i = 0; i < index.events.size(); ++i) {
        for (const auto& group : index.events[i].groups)
            if (groupKey(group) == key)
                return i;
    }
    return std::nullopt;
}

struct LiveStream {
    LocalHistoryRecord* record = nullptr;
    ScopedTextStream* text = nullptr;
    ScopedIdListStream* list = nullptr;
    CollabText::Crdt::LocalHistorySnapshot snapshot;
    bool suppressed = false;
};

OperationPrimitive primitiveOf(const LiveStream& live)
{
    return live.record->scope.primitive == Primitive::text
        ? OperationPrimitive::text : OperationPrimitive::idList;
}

} // namespace

Result<void> enforceActorUndoDepth(AcceptedPrefixProjection& projection,
                                    OwnerHistorySnapshot& history)
{
    auto& index = history.actionIndex;
    if (index.events.empty())
        return {};
    const size_t committing = index.events.size() - 1;
    if (index.events[committing].actionId != history.transactionId)
        return Error(ErrorCode::invalid_identity,
                     "retirement record could not be attached");

    std::vector<size_t> undoable;
    for (size_t i = 0; i < index.events.size(); ++i) {
        const auto& event = index.events[i];
        if (event.kind != ActionKind::edit && event.kind != ActionKind::replacement)
            continue;
        if (actionIsCurrentlyUndone(index.events, event.actionId))
            continue;
        const bool retired = std::any_of(event.groups.begin(), event.groups.end(),
            [&](const HistoryGroupAssociation& group) {
                return retirementNames(index, groupKey(group));
            });
        if (!retired)
            undoable.push_back(i);
    }

    std::vector<size_t> toRetire;
    if (undoable.size() > kActorUndoDepth) {
        const size_t excess = undoable.size() - kActorUndoDepth;
        toRetire.assign(undoable.begin(),
                        undoable.begin() + static_cast<std::ptrdiff_t>(excess));
        if (std::find(toRetire.begin(), toRetire.end(), committing) != toRetire.end())
            return Error(ErrorCode::history_unavailable,
                         "retirement record could not be attached");
    }

    std::vector<LiveStream> streams;
    streams.reserve(history.streams.size());
    for (auto& record : history.streams) {
        LiveStream live;
        live.record = &record;
        if (record.scope.primitive == Primitive::text) {
            live.text = projection.mutableTextStream(record.scope.stream);
            if (!live.text)
                return Error(ErrorCode::unknown_stream,
                             "retirement record could not be attached");
            live.suppressed = live.text->initialImportPending();
            if (!live.suppressed)
                live.snapshot = live.text->localHistorySnapshot();
        } else if (record.scope.primitive == Primitive::list) {
            live.list = projection.mutableIdList();
            if (!live.list || live.list->scope() != record.scope)
                return Error(ErrorCode::unknown_stream,
                             "retirement record could not be attached");
            live.snapshot = live.list->localHistorySnapshot();
        } else {
            return Error(ErrorCode::wrong_primitive,
                         "retirement record could not be attached");
        }
        streams.push_back(std::move(live));
    }

    auto findLive = [&](const std::string& stream, OperationPrimitive primitive) -> LiveStream* {
        for (auto& live : streams) {
            if (live.record->scope.stream != stream)
                continue;
            if (primitive == OperationPrimitive::text
                && live.record->scope.primitive == Primitive::text)
                return &live;
            if (primitive == OperationPrimitive::idList
                && live.record->scope.primitive == Primitive::list)
                return &live;
        }
        return nullptr;
    };

    std::set<GroupKey> planned;
    std::vector<HistoryGroupAssociation> directRetirements;
    std::map<std::pair<std::string, uint8_t>, std::vector<Identity>> prefixes;
    for (const size_t eventIndex : toRetire) {
        for (const auto& group : index.events[eventIndex].groups) {
            const auto key = groupKey(group);
            if (retirementNames(index, key) || !planned.insert(key).second)
                continue;
            if (group.primitive == OperationPrimitive::markoffMap) {
                directRetirements.push_back(group);
                continue;
            }
            auto* live = findLive(group.stream, group.primitive);
            if (!live || live->suppressed)
                return Error(ErrorCode::history_unavailable,
                             "retirement record could not be attached");
            const bool alreadyPending = std::any_of(
                live->snapshot.retired_entries.begin(),
                live->snapshot.retired_entries.end(),
                [&](const auto& entry) {
                    return sameIdentity(entry.group_id, group.groupId);
                });
            if (alreadyPending) {
                directRetirements.push_back(group);
                continue;
            }
            prefixes[{group.stream, static_cast<uint8_t>(group.primitive)}]
                .push_back(group.groupId);
        }
    }

    for (const auto& [route, ids] : prefixes) {
        auto* live = findLive(route.first, static_cast<OperationPrimitive>(route.second));
        if (!live)
            return Error(ErrorCode::history_unavailable,
                         "retirement record could not be attached");
        if (ids.size() > live->snapshot.cursor || ids.size() > live->snapshot.entries.size())
            return Error(ErrorCode::history_unavailable,
                         "retirement record could not be attached");
        for (size_t i = 0; i < ids.size(); ++i) {
            if (!sameIdentity(live->snapshot.entries[i].group_id, ids[i]))
                return Error(ErrorCode::history_unavailable,
                             "retirement record could not be attached");
        }
    }

    bool unrecordedPending = false;
    for (const auto& live : streams) {
        if (live.suppressed)
            continue;
        for (const auto& entry : live.snapshot.retired_entries) {
            const auto key = groupKey(live.record->scope.stream, primitiveOf(live),
                                      entry.group_id);
            if (!retirementNames(index, key) && !planned.contains(key))
                unrecordedPending = true;
        }
    }
    if ((!planned.empty() || unrecordedPending)
        && Testing::consumeHistoryRetirementFailure())
        return Error(ErrorCode::history_unavailable,
                     "retirement record could not be attached");

    for (auto& [route, ids] : prefixes) {
        auto* live = findLive(route.first, static_cast<OperationPrimitive>(route.second));
        if (!live)
            return Error(ErrorCode::history_unavailable,
                         "retirement record could not be attached");
        Result<void> retired = live->text
            ? live->text->retireOldestUndoGroups(ids.size())
            : live->list->retireOldestUndoGroups(ids.size());
        if (!retired.ok())
            return retired.error();
    }

    auto appendRetirement = [&](const std::string& stream, OperationPrimitive primitive,
                                const Identity& id) -> Result<void> {
        const auto key = groupKey(stream, primitive, id);
        if (retirementNames(index, key))
            return {};
        const auto origin = originIndex(index, key);
        if (!origin || *origin >= committing)
            return Error(ErrorCode::history_unavailable,
                         "retirement record could not be attached");
        index.retirements.push_back(
            {stream, primitive, id, index.events[committing].actionId});
        return {};
    };

    for (const auto& group : directRetirements) {
        auto recorded = appendRetirement(group.stream, group.primitive, group.groupId);
        if (!recorded.ok())
            return recorded.error();
    }
    for (auto& live : streams) {
        if (live.suppressed)
            continue;
        if (live.text)
            live.snapshot = live.text->localHistorySnapshot();
        else if (live.list)
            live.snapshot = live.list->localHistorySnapshot();
        else
            continue;
        for (const auto& entry : live.snapshot.retired_entries) {
            auto recorded = appendRetirement(live.record->scope.stream, primitiveOf(live),
                                             entry.group_id);
            if (!recorded.ok())
                return recorded.error();
        }
        live.record->snapshot = std::move(live.snapshot);
    }
    return {};
}

Result<void> acknowledgeDurableRetirements(AcceptedPrefixProjection& projection,
                                            const OwnerHistorySnapshot& history)
{
    std::set<GroupKey> named;
    for (const auto& item : history.actionIndex.retirements)
        named.insert(groupKey(item));
    if (named.empty())
        return {};

    for (const auto& record : history.streams) {
        const OperationPrimitive primitive = record.scope.primitive == Primitive::text
            ? OperationPrimitive::text : OperationPrimitive::idList;
        CollabText::Crdt::LocalHistorySnapshot snapshot;
        ScopedTextStream* text = nullptr;
        ScopedIdListStream* list = nullptr;
        if (record.scope.primitive == Primitive::text) {
            text = projection.mutableTextStream(record.scope.stream);
            if (!text || text->initialImportPending())
                continue;
            snapshot = text->localHistorySnapshot();
        } else if (record.scope.primitive == Primitive::list) {
            list = projection.mutableIdList();
            if (!list || list->scope() != record.scope)
                continue;
            snapshot = list->localHistorySnapshot();
        } else {
            continue;
        }
        std::vector<Identity> pending;
        for (const auto& entry : snapshot.retired_entries) {
            if (named.contains(groupKey(record.scope.stream, primitive, entry.group_id)))
                pending.push_back(entry.group_id);
        }
        if (pending.empty())
            continue;
        auto acked = text ? text->acknowledgeUndoRetirements(pending)
                          : list->acknowledgeUndoRetirements(pending);
        if (!acked.ok())
            return acked.error();
    }
    return {};
}

} // namespace Markoff::VNext
