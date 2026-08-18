// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <QHashFunctions>

namespace Markoff {

class BlockId {
public:
    BlockId() noexcept = default;
    static BlockId fromRaw(uint64_t raw) noexcept { BlockId b; b.m_raw = raw; return b; }
    bool isNull() const noexcept { return m_raw == 0; }
    uint64_t raw() const noexcept { return m_raw; }
    bool operator==(const BlockId &o) const noexcept { return m_raw == o.m_raw; }
    bool operator!=(const BlockId &o) const noexcept { return m_raw != o.m_raw; }
private:
    uint64_t m_raw = 0;
};

// seed defaults to 0 so Qt 6.9+ qHashMulti (1-arg qHash calls) still compiles.
inline size_t qHash(const BlockId &id, size_t seed = 0) noexcept {
    return ::qHash(id.raw(), seed);
}

}  // namespace Markoff
