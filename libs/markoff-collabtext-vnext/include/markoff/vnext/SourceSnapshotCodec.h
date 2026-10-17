// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/ActorId.h>

#include <QByteArray>

#include <cstdint>
#include <string>
#include <vector>

namespace Markoff::VNext {

/// Exact source bytes are authoritative. Separator slices preserve the raw
/// bytes between parsed blocks; parserCache is disposable and source-digested.
struct SourceSeparator {
    uint64_t offset = 0;
    QByteArray bytes;
    friend bool operator==(const SourceSeparator&, const SourceSeparator&) = default;
};

struct SourceSnapshotRecord {
    std::string document;
    uint64_t epoch = 0;
    QByteArray originalMarkdown;
    std::vector<SourceSeparator> separators;
    std::string parserVersion;
    QByteArray parserCache;
};

/// Versioned MDS/1 payload embedded in the initial MAB action. Decoding keeps
/// source bytes exact; parser cache validity is checked against their SHA-256.
class SourceSnapshotCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const SourceSnapshotRecord& record);
    [[nodiscard]] static CollabText::Result<SourceSnapshotRecord> decode(
        const QByteArray& bytes, const std::string& expectedDocument,
        uint64_t expectedEpoch);
};

} // namespace Markoff::VNext
