// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <array>
#include <chrono>

namespace Markoff::Bench {

enum class Phase : int {
    Extract     = 0,
    Diff        = 1,
    ParseBlock  = 2,
    ParseInline = 3,
    Queries     = 4,
    Snapshot    = 5,
    PoolQueue   = 6,   // Tier 1b: applyLocalEdit return → worker pickup
    SignalHop   = 7,   // Tier 1b: worker emit → main-thread receipt
    RenderFrame = 8,   // Tier 2 only
    Count       = 9,   // sentinel — keep last
};

constexpr int kPhaseCount = static_cast<int>(Phase::Count);

/// Per-iteration phase totals in nanoseconds, indexed by Phase.
using PhaseTable = std::array<quint64, kPhaseCount>;

/// RAII guard that adds the elapsed wall time (steady_clock nanoseconds)
/// of its lifetime to `table[phase]`. Cheap; no heap traffic, no syscalls
/// beyond clock_gettime.
class PhaseTimer {
public:
    PhaseTimer(PhaseTable &table, Phase phase) noexcept
        : m_table(table), m_phase(phase),
          m_start(std::chrono::steady_clock::now()) {}

    ~PhaseTimer() {
        const auto end = std::chrono::steady_clock::now();
        const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end - m_start).count();
        m_table[static_cast<int>(m_phase)] += static_cast<quint64>(ns);
    }

    PhaseTimer(const PhaseTimer &) = delete;
    PhaseTimer &operator=(const PhaseTimer &) = delete;

private:
    PhaseTable &m_table;
    Phase       m_phase;
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace Markoff::Bench
