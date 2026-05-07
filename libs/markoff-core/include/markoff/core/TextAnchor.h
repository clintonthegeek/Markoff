// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <cstdint>
#include <QtGlobal>

namespace Markoff {

class MARKOFF_CORE_EXPORT TextAnchor {
public:
    TextAnchor() noexcept = default;
    static TextAnchor make(BlockId b, uint16_t replicaId, uint64_t charValue, bool rightBias) noexcept {
        TextAnchor a; a.m_block = b; a.m_replicaId = replicaId;
        a.m_charValue = charValue; a.m_rightBias = rightBias;
        return a;
    }

    bool isNull() const noexcept { return m_block.isNull() && m_charValue == 0; }
    BlockId block() const noexcept { return m_block; }
    uint16_t replicaId() const noexcept { return m_replicaId; }
    uint64_t charValue() const noexcept { return m_charValue; }
    bool rightBias() const noexcept { return m_rightBias; }

    bool operator==(const TextAnchor &o) const noexcept {
        return m_block == o.m_block && m_replicaId == o.m_replicaId
            && m_charValue == o.m_charValue && m_rightBias == o.m_rightBias;
    }
    bool operator!=(const TextAnchor &o) const noexcept { return !(*this == o); }

private:
    BlockId  m_block;
    uint16_t m_replicaId = 0;
    uint64_t m_charValue = 0;
    bool     m_rightBias = false;
};

}  // namespace Markoff
