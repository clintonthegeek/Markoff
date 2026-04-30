# collabtext — Global SBO regression: handoff with reproducer

**Date:** 2026-04-30
**Reporter:** Markoff foundation-exploration agent
**Severity:** Crash (SIGSEGV); intermittent; ~70 % per `markoff-bench-render`
invocation on commit `f7acaab` against collabtext `0008577`.
**Suspect commit:** `0008577 perf(crdt): SBO on Global eliminates heap alloc on
summary hot path`. The crash does not reproduce against `715cbff` (the
parent commit).

The performance win cited in `0008577` (−22 % single-replica typing, −37 %
sequential append, etc.) is not in dispute — the bench results are real and
match what the Markoff perf handoff predicted. The regression below is in
the heap-promotion path of the new SBO `Global`, which fires whenever the
first observed `Lamport.replica_id ≥ SBO_CAP (= 4)`. The earlier vector-
backed `Global` did not have this branch, so nothing in the existing
collabtext test suite exercises it under the same conditions a fresh
`MarkoffDocument(replicaId)` does.

## Symptoms

`markoff-bench-render --profile mid_prose --scenario type_end --out X.json`
crashes with SIGSEGV in roughly 70 % of runs. The crash signature is
identical across reproductions:

```
#0  CollabText::Crdt::Global::observe(Lamport)        (libcollabtext + Clock.cpp:95)
#1  CollabText::Crdt::Fragment::summary() const       (libcollabtext + Fragment.h:188)
#2  CollabText::Crdt::SumTree<Fragment, 6>::push_item (libcollabtext + SumTree.h)
#3  CollabText::Crdt::Buffer::apply_local_edit        (libcollabtext + Buffer.cpp)
#4  Markoff::MarkoffDocument::resetContent            (libmarkoff_foundation + .cpp:194)
#5  main                                              (markoff-bench-render)
```

The fault address varies across crashes (sometimes a low pointer, sometimes
mid-heap), which is consistent with reading a stale union member rather than
a deterministic null deref.

## Reproducer (minimal)

The CRDT issue can be exercised without Markoff or QML at all. The pattern
the bench triggers is:

```cpp
#include <crdt/Buffer.h>
#include <crdt/Operations.h>
#include <random>
#include <string>

int main() {
    std::mt19937 rng{0xBEEF};
    for (int trial = 0; trial < 20; ++trial) {
        // Random replicaId in [4, 65535] forces the first `observe()`
        // through the SBO → heap promotion branch (resize_zero with
        // new_size > SBO_CAP=4).
        const uint16_t replicaId = (rng() & 0xFFFC) | 0x0004;  // ≥ 4
        CollabText::Crdt::Buffer buf(replicaId);
        const std::string seed(16'384, 'a');     // any 16 KB-ish string
        buf.apply_local_edit({{0, 0}}, {seed});  // crashes here ~70% of trials
    }
    return 0;
}
```

The critical conditions are:
1. **A freshly constructed `Buffer`** (or `Global` reachable through it).
2. **First `observe(ts)` with `ts.replica_id >= SBO_CAP`** — i.e. enough
   to take the heap-promotion path on the very first observe, not the
   inline path.

In the Markoff bench, `replicaId` was previously generated as
`QRandomGenerator::global()->generate() & 0xFFFF` (so ~99.99 % of values are
≥ 4). The bench frontend now uses a small monotonic replicaId
(`++sReplicaSeq`, starting at 1) as a workaround, which keeps the SumTree's
per-fragment `Global` summaries in the SBO inline buffer. The workaround
sidesteps the regression but does not exercise the production code path —
real apps don't get to choose small replica IDs.

## Smoking gun (one hypothesis)

Reading `libs/collabtext/src/crdt/Clock.cpp` and `Clock.h` at `0008577`,
the most likely culprit is in `resize_zero` when promoting from inline (≤
SBO_CAP) to heap on the first observe of a fresh `Global`:

```cpp
void Global::resize_zero(uint16_t new_size) {
    if (new_size <= m_size) {                          //  m_size == 0 here
        m_size = new_size;
        return;
    }
    if (new_size > m_capacity) {                       //  new_size > 4, true
        uint32_t new_cap = m_capacity;
        while (new_cap < new_size) new_cap *= 2;
        uint32_t *new_data = static_cast<uint32_t *>(std::malloc(new_cap * sizeof(uint32_t)));
        if (m_size > 0)
            std::memcpy(new_data, data(), m_size * sizeof(uint32_t));   //  m_size==0, skipped
        std::memset(new_data + m_size, 0, (new_cap - m_size) * sizeof(uint32_t));
        if (on_heap()) std::free(m_heap_data);          //  on_heap()==false, skipped
        m_heap_data = new_data;                         //  ← writes through the union
        m_capacity = static_cast<uint16_t>(new_cap);    //  now on_heap()==true
    }
    ...
}
```

The arithmetic looks correct in isolation. The smoking gun isn't here —
it's *in the caller's frame*. `Fragment::summary()` returns a
`FragmentSummary` by value, which contains *three* `Global` members
(`max_version`, `min_insertion_version`, `max_insertion_version`). Each is
default-constructed, then `observe()`-d, then NRVO'd or copied into
`SumTree::push_item`'s argument. When the `Global` move constructor is
invoked on a heap-promoted instance:

```cpp
Global::Global(Global &&other) noexcept {
    if (other.on_heap()) {
        m_heap_data = other.m_heap_data;
        m_capacity = other.m_capacity;
    } else {
        std::memcpy(m_inline, other.m_inline, other.m_size * sizeof(uint32_t));
    }
    m_size = other.m_size;
    other.m_size = 0;
    other.m_capacity = SBO_CAP;       // ← but other.m_heap_data still aliases this!
}
```

After the move, `other`'s union still has `m_heap_data` pointing to the
just-handed-off heap allocation, but `other.on_heap()` now returns `false`
(`m_capacity == SBO_CAP`). The active union member has effectively been
flipped from `m_heap_data` to `m_inline` *without zeroing the inline
storage*. If the moved-from `other` is then re-used (or its destructor
fires — it won't free, but if the compiler reads `m_inline[0..3]` as part
of a future copy), it observes the high bits of the heap pointer as
"version-vector entries." A subsequent `observe()` against THIS Global
(now in degenerate inline mode with m_size=0 and uninitialized m_inline)
calls `resize_zero(new_size)` which `memset`s only `m_inline + m_size`
onward — but if a code path reads `m_inline[0..m_size-1]` before that
memset for some reason, it's reading stale pointer bits. (See the join /
add_summary call sites — they iterate `m_size` entries.)

Equally plausible alternative: the `FragmentSummary` constructor /
copy-elision interaction with the union may be skipping the default member
initializer for `m_capacity`. C++20 default-member-init on a union member
is one of the known footguns. If `m_capacity` ends up uninitialized, the
first `observe()` reads `on_heap()` against garbage and may take the heap
branch, returning `m_heap_data` which is also garbage.

A quick assert at the top of `Global::observe`:

```cpp
assert(m_capacity >= SBO_CAP && "Global::m_capacity uninitialized");
```

would distinguish the two hypotheses. If it fires, it's the missing
default-init story. If it doesn't, the move-semantics-with-union story is
likelier.

## Suggested fix directions

In rough order of "definitely correct" to "speculative":

1. **Make `Global::Global() noexcept = default;` non-default**, and
   explicitly initialize `m_capacity = SBO_CAP` and `std::memset(m_inline,
   0, sizeof(m_inline))`. Belt-and-suspenders against the union's lack of
   default-member-init.
2. **Move ctor / move assignment**: after handing off the heap pointer,
   explicitly zero the inline storage on the source so a subsequent re-use
   in inline mode starts from a known state, not stale pointer bits:
   ```cpp
   } else {
       // (already in else branch — source was inline; nothing to zero)
   }
   ...
   other.m_size = 0;
   other.m_capacity = SBO_CAP;
   std::memset(other.m_inline, 0, sizeof(other.m_inline));   // <— add
   ```
3. **Run the existing `tst_collabtext_*` suite under valgrind** with the
   reproducer above linked in. Valgrind would catch the uninitialized read
   on the spot.

## Cross-references

- Optimization handoff that triggered the SBO work:
  `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`.
- Markoff render-tier instrumentation work-unit (this branch):
  `docs/handoff/2026-04-30-render-tier-instrumentation-SESSION-BRIEF.md` and
  the commit landing the bench frontend's small-replicaId workaround.
- collabtext suspect commit: `0008577` (`perf(crdt): SBO on Global ...`).
- collabtext last-known-good: `715cbff`.

## What we did NOT do

- Revert `0008577` — the performance gain is real and the brief explicitly
  scoped CRDT work to the collabtext team. The Markoff bench keeps the SBO
  code path active; only the bench frontend's replicaId-generation policy
  changed (small monotonic instead of full-uint16 random).
- Touch any collabtext source. The hypothesis above is annotated reading
  of the public source at `0008577`, not a confirmed root cause.
- File this against a tracker. Drop it wherever the collabtext team's
  current intake lives; happy to reformat if there's a template.
