// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/Anchor.h>
#include <collabtext/ActorId.h>
#include <collabtext/Serialization.h>

#include <QByteArray>

#include <string>

namespace Markoff::VNext {

/// Durable Markoff authority envelope around Core's unscoped CTGP payload.
/// The acceptedPrior token identifies the exact prefix against which absence
/// was established; CTGP's own prior is the stream causal frontier.
struct ScopedCounterGap {
    CollabText::PositionScope scope;
    CollabText::Crdt::CounterGap gap;
    QByteArray acceptedPrior;
};

/// Markoff map streams have no Core primitive. MMG/1 records only terminal
/// coverage for the exact actor counter interval and accepted prefix.
struct MapCounterGap {
    std::string document;
    std::string map;
    uint64_t epoch = 0;
    CollabText::ActorId actor;
    uint64_t first = 0;
    uint64_t count = 0;
    QByteArray acceptedPrior;
};

class CounterGapCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const ScopedCounterGap& record);
    /// Decode the embedded complete scope and prior token. Replay uses this to
    /// route a committed gap before constructing its target stream.
    [[nodiscard]] static CollabText::Result<ScopedCounterGap> decodeRecord(
        const QByteArray& bytes);
    [[nodiscard]] static CollabText::Result<ScopedCounterGap> decode(
        const QByteArray& bytes, const CollabText::PositionScope& expectedScope,
        const QByteArray& expectedAcceptedPrior);
};

class MapCounterGapCodec final {
public:
    [[nodiscard]] static CollabText::Result<QByteArray> encode(
        const MapCounterGap& record);
    [[nodiscard]] static CollabText::Result<MapCounterGap> decodeRecord(
        const QByteArray& bytes);
    [[nodiscard]] static CollabText::Result<MapCounterGap> decode(
        const QByteArray& bytes, const std::string& expectedDocument,
        const std::string& expectedMap, uint64_t expectedEpoch,
        const QByteArray& expectedAcceptedPrior);
};

} // namespace Markoff::VNext
