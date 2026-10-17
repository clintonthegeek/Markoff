// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>

#include <QHashFunctions>
#include <QList>
#include <QMetaType>

namespace Markoff {

/// Lossless identity of a block insertion, formed by the creating actor and
/// that actor's list-operation counter. Application values are never identity.
class BlockId {
public:
    BlockId() noexcept = default;

    static std::optional<BlockId> fromStructuralIdentity(
        const std::array<uint8_t, 16> &actor, uint64_t counter) noexcept
    {
        if (counter == 0 || std::all_of(actor.begin(), actor.end(),
                                        [](uint8_t byte) { return byte == 0; }))
            return std::nullopt;
        BlockId id;
        id.m_actor = actor;
        id.m_counter = counter;
        return id;
    }

    bool isNull() const noexcept { return m_counter == 0; }
    const std::array<uint8_t, 16> &actorBytes() const noexcept { return m_actor; }
    uint64_t counter() const noexcept { return m_counter; }

    bool operator==(const BlockId &) const noexcept = default;
    bool operator!=(const BlockId &other) const noexcept { return !(*this == other); }
    bool operator<(const BlockId &other) const noexcept
    {
        return m_actor != other.m_actor ? m_actor < other.m_actor
                                        : m_counter < other.m_counter;
    }

    size_t identityHash(size_t seed = 0) const noexcept
    {
        for (uint8_t byte : m_actor)
            seed = ::qHash(byte, seed);
        return ::qHash(m_counter, seed);
    }

private:
    std::array<uint8_t, 16> m_actor{};
    uint64_t m_counter = 0;
};

inline size_t qHash(const BlockId &id, size_t seed = 0) noexcept
{
    return id.identityHash(seed);
}

} // namespace Markoff

Q_DECLARE_METATYPE(Markoff::BlockId)
Q_DECLARE_METATYPE(QList<Markoff::BlockId>)
