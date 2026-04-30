// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

namespace Markoff::Bench {

struct AllocSnapshot {
    quint32 count = 0;
    quint64 bytes = 0;
};

/// Snapshot the current thread's allocation counters. Returns zeros when
/// the counter is not enabled on this thread.
AllocSnapshot currentAllocSnapshot();

/// RAII scope: zeros and enables the per-thread allocation counter on
/// construction; restores the previous enabled state on destruction.
/// Nesting is supported (inner scope sees only its own delta because
/// it zeros on entry; outer scope's count is lost — by design).
class AllocCounterScope {
public:
    AllocCounterScope();
    ~AllocCounterScope();
    AllocCounterScope(const AllocCounterScope &) = delete;
    AllocCounterScope &operator=(const AllocCounterScope &) = delete;
};

namespace Detail {
/// Internal: called from the operator new/delete shim. Linked into every
/// binary that depends on markoff_bench; shim is no-op when disabled.
void recordAlloc(std::size_t bytes) noexcept;
void recordDealloc(std::size_t bytes) noexcept;
}

}  // namespace Markoff::Bench
