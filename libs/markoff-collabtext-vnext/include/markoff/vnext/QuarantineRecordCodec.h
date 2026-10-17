// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/vnext/BlockIdentity.h>

#include <QByteArray>

#include <optional>
#include <string>

namespace Markoff::VNext {

enum class QuarantinePrimitive : uint8_t { text = 1, list = 2, map = 3 };

/// Complete, scope-bound rejected envelope retained for durable recovery.
struct QuarantineRecord {
    std::string document;
    uint64_t epoch = 0;
    std::string stream;
    QuarantinePrimitive primitive = QuarantinePrimitive::text;
    CollabText::ActorId actor;
    uint64_t counter = 0;
    std::optional<BlockIdentity> targetBlock;
    QByteArray payload;
    uint64_t deliverySequence = 0;
    QByteArray actionId;
    /// Exact enclosing MAB/3 bytes when the host quarantines a whole action.
    /// Primitive routers may leave this empty when retaining one operation.
    QByteArray originalActionEnvelope;
    uint32_t bundleIndex = 0;
    uint32_t bundleOperationCount = 0;
    CollabText::Error reason;
    friend bool operator==(const QuarantineRecord&, const QuarantineRecord&) = default;
};

class QuarantineRecordCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const QuarantineRecord& record);
    [[nodiscard]] static CollabText::Result<QuarantineRecord> decode(
        const QByteArray& bytes);
};

} // namespace Markoff::VNext
