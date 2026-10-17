// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/BlockIdentity.h>
#include <markoff/vnext/MarkoffMapCodec.h>

#include <collabtext/ActorId.h>
#include <collabtext/Engine.h>

#include <QByteArray>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Markoff::VNext {

enum class ActionKind : uint8_t {
    edit = 1, undo = 2, redo = 3, initialImport = 4, replacement = 5
};
enum class OperationPrimitive : uint8_t { text = 1, idList = 2, markoffMap = 3 };
enum class OperationCodec : uint8_t { ctbo3 = 2, ctlo2 = 3, markoffOwned = 4 };

struct ActionOperation {
    std::string stream;
    OperationPrimitive primitive = OperationPrimitive::text;
    std::optional<BlockIdentity> targetBlock;
    CollabText::ActorId producer;
    uint64_t counter = 0;
    OperationCodec codec = OperationCodec::ctbo3;
    QByteArray payload;
};

/// Markoff-owned MAB/3 binary action envelope. This codec validates operation
/// routes and carries the optional initial-source record; consumers must
/// decode operation payloads and compare checked identities before apply.
class ActionBundleCodec final {
public:
    struct Bundle {
        std::string document;
        uint64_t epoch = 0;
        uint64_t deliverySequence = 0;
        QByteArray actionId;
        ActionKind kind = ActionKind::edit;
        std::vector<ActionOperation> operations;
        /// MDS/1 exact source record. Required on initial import and external
        /// replacement; undo/redo may carry the source snapshot restored by
        /// compensating a replacement.
        QByteArray sourceSnapshot;
    };

    [[nodiscard]] static CollabText::Result<QByteArray> encode(const Bundle& bundle);
    [[nodiscard]] static CollabText::Result<Bundle> decode(
        const QByteArray& bytes, const std::string& expectedDocument,
        uint64_t expectedEpoch);

    /// Check redundant envelope producer/counter fields against each opaque
    /// operation's checked decoded identity.
    [[nodiscard]] static CollabText::Result<void> validateOperationIdentities(
        const Bundle& bundle);
};

} // namespace Markoff::VNext
