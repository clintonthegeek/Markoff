// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/AllocCounter.h>

namespace Markoff::Bench {

namespace {
thread_local bool    g_enabled = false;
thread_local quint32 g_count   = 0;
thread_local quint64 g_bytes   = 0;
}

namespace Detail {
void recordAlloc(std::size_t bytes) noexcept {
    if (!g_enabled) return;
    ++g_count;
    g_bytes += bytes;
}
void recordDealloc(std::size_t /*bytes*/) noexcept {
    // We track only outbound allocations (peak pressure), not net retained.
    // Deltas of paired alloc+free still show up in `count` and `bytes`.
}
}  // namespace Detail

AllocSnapshot currentAllocSnapshot() {
    AllocSnapshot s;
    s.count = g_count;
    s.bytes = g_bytes;
    return s;
}

AllocCounterScope::AllocCounterScope() {
    g_count = 0;
    g_bytes = 0;
    g_enabled = true;
}

AllocCounterScope::~AllocCounterScope() {
    g_enabled = false;
    g_count   = 0;
    g_bytes   = 0;
}

}  // namespace Markoff::Bench
