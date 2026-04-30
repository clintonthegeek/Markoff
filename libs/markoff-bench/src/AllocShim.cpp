// SPDX-License-Identifier: GPL-3.0-or-later
//
// Global operator new / delete override. Compiled into the markoff_bench
// static library. Counters are disabled by default per-thread; enable via
// AllocCounterScope. When disabled, this is one branch + one indirect call
// per allocation — measurable but small.
//
// We deliberately do NOT override the nothrow / aligned / placement variants
// — the foundation/parser don't use them on the hot path.

#include <markoff-bench/AllocCounter.h>

#include <cstdlib>
#include <new>

using Markoff::Bench::Detail::recordAlloc;
using Markoff::Bench::Detail::recordDealloc;

void *operator new(std::size_t n) {
    if (void *p = std::malloc(n)) {
        recordAlloc(n);
        return p;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t n) {
    if (void *p = std::malloc(n)) {
        recordAlloc(n);
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void *p) noexcept {
    recordDealloc(0);
    std::free(p);
}

void operator delete[](void *p) noexcept {
    recordDealloc(0);
    std::free(p);
}

void operator delete(void *p, std::size_t n) noexcept {
    recordDealloc(n);
    std::free(p);
}

void operator delete[](void *p, std::size_t n) noexcept {
    recordDealloc(n);
    std::free(p);
}
