// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/ActorId.h>

#include <QByteArray>

#include <cstdint>
#include <string>

namespace Markoff::VNext {

struct MarkoffMapOperation {
    std::string map;
    QByteArray key;
    QByteArray value;
    CollabText::ActorId producer;
    uint64_t counter = 0;
    bool tombstone = false;
};

/// Bounded, layout-independent codec for Markoff-owned sibling-map operations.
/// Map-specific key/value interpretation remains with Markoff.
class MarkoffMapCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const MarkoffMapOperation& operation);
    [[nodiscard]] static CollabText::Result<MarkoffMapOperation> decode(
        const QByteArray& bytes);
};

} // namespace Markoff::VNext
