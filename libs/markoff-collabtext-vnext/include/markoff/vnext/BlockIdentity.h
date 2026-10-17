// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <collabtext/ActorId.h>

#include <QHashFunctions>

#include <compare>
#include <cstdint>
#include <functional>

namespace Markoff::VNext {

/// Lossless identity for a block created by one structural IdList operation.
/// It is never packed into CollabText's uint64_t application-value field.
struct BlockIdentity {
    CollabText::ActorId actor;
    uint64_t counter = 0;

    friend bool operator==(const BlockIdentity&, const BlockIdentity&) = default;
    friend std::strong_ordering operator<=>(const BlockIdentity& left,
                                            const BlockIdentity& right) noexcept
    {
        const auto leftActor = left.actor.bytes();
        const auto rightActor = right.actor.bytes();
        if (const auto actorOrder = leftActor <=> rightActor;
            actorOrder != std::strong_ordering::equal)
            return actorOrder;
        return left.counter <=> right.counter;
    }
};

/// Stable hash for caches keyed by full insertion identity. Application values
/// are intentionally absent: equal counters from different actors stay distinct.
struct BlockIdentityHash {
    size_t operator()(const BlockIdentity& identity) const noexcept
    {
        size_t seed = 0;
        for (const uint8_t byte : identity.actor.bytes())
            seed = ::qHash(byte, seed);
        return ::qHash(identity.counter, seed);
    }
};

inline size_t qHash(const BlockIdentity& identity, size_t seed = 0) noexcept
{
    for (const uint8_t byte : identity.actor.bytes())
        seed = ::qHash(byte, seed);
    return ::qHash(identity.counter, seed);
}

} // namespace Markoff::VNext
