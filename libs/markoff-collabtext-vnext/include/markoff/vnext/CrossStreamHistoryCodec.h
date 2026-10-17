// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/ActionBundleCodec.h>

#include <collabtext/LocalHistory.h>

namespace Markoff::VNext {

struct HistoryGroupAssociation {
    std::string stream;
    OperationPrimitive primitive = OperationPrimitive::text;
    CollabText::Crdt::Identity groupId;
    std::vector<CollabText::Crdt::Identity> operationIds;
    /// Present for MAI/3 Markoff-map singleton groups; private undo state.
    bool hasMapPriorState = false;
    bool mapPriorValueExisted = false;
    QByteArray mapPriorValue;
    friend bool operator==(const HistoryGroupAssociation&,
                           const HistoryGroupAssociation&) = default;
};

struct ActionHistoryEvent {
    QByteArray actionId;
    ActionKind kind = ActionKind::edit;
    /// Undo/redo point to the action whose groups they compensate.
    QByteArray parentActionId;
    std::vector<HistoryGroupAssociation> groups;
    /// Exact pre-replacement Markdown bytes, bound as MDS/1 and retained in
    /// owner history so local undo can restore the matching export source.
    QByteArray sourceSnapshotBefore;
    friend bool operator==(const ActionHistoryEvent&, const ActionHistoryEvent&) = default;
};

struct RetiredHistoryAssociation {
    std::string stream;
    OperationPrimitive primitive = OperationPrimitive::text;
    CollabText::Crdt::Identity groupId;
    QByteArray retirementActionId;
    friend bool operator==(const RetiredHistoryAssociation&,
                           const RetiredHistoryAssociation&) = default;
};

/// Owner-private MAI/3 index. It ties the accepted prefix immediately before a
/// prepared action to ordered action/group associations and explicitly
/// identifies which action recorded each engine-group retirement.
struct CrossStreamHistoryIndex {
    std::string document;
    uint64_t epoch = 0;
    CollabText::ActorId owner;
    QByteArray acceptedPrior;
    std::vector<ActionHistoryEvent> events;
    std::vector<RetiredHistoryAssociation> retirements;
};

class CrossStreamHistoryCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const CrossStreamHistoryIndex& index);
    [[nodiscard]] static CollabText::Result<CrossStreamHistoryIndex> decode(
        const QByteArray& bytes, const std::string& expectedDocument,
        uint64_t expectedEpoch, const CollabText::ActorId& expectedOwner,
        const QByteArray& expectedAcceptedPrior);
};

} // namespace Markoff::VNext
