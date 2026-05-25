> **STATUS: FIXED upstream on 2026-05-09** — collabtext commit `d02cca6`
> *(`fix(crdt): make Global sparse to remove O(replica_id) edit cost`)*.
>
> **Root cause:** `Crdt::Global` stored a dense `uint32_t` array indexed by
> `replica_id`. `FragmentSummary` carried **three** Globals; the SumTree
> copied and joined them up the tree on every edit. With `replica_id=60000`
> that allocated and zeroed ~720 kB per fragment summary even though only
> one replica had ever been observed — exactly the linear-in-`replica_id`
> shape this report described.
>
> **Fix:** rewrote `Global` to a sorted, packed `(replica_id, value)` sparse
> layout (`(replica_id << 32) | value` per `uint64_t`, 4-pair SBO).
> Per-edit cost is now flat at ~0.14 ms across the entire `uint16_t`
> replica space (verified with the reproducer in this commit).
> No public-API or wire-format change; `size()`/`operator[]` keep their
> dense-view semantics for the encoder, and `pair_count()`/`pair()` are
> new sparse-iteration accessors. No `schema_version` bump.
>
> **Regression test:** `tst_clock` —
> `buffer_apply_local_edit_cost_independent_of_replica_id` asserts that the
> 400-edit append workload at `replica_id=60000` stays within 10× of the
> same workload at `replica_id=1`.
>
> Markoff can drop the `replica_id=1` workaround once you bump the
> vendored collabtext to or past `d02cca6`.
>
> — collabtext, 2026-05-09

---

# Bug report — `CollabText::Crdt::Buffer`: per-edit cost is O(replica_id)

**Reporter:** Markoff project (consumer of `CollabText::Crdt::Buffer`).
**Discovered:** 2026-05-09 during dogfood of a markdown editor built on CollabText.
**Severity:** **Critical** for any consumer that uses non-small `replica_id`s
(notably anything that derives `replica_id` from a hash, UUID truncation, or
random uint16 — the obvious "give every replica a fresh id" pattern).
**Tested against:** `libs/collabtext` as vendored in
`Markoff/.worktrees/foundation-exploration/libs/collabtext` on
2026-05-09. Commit hash on your end: please match against your tree.

---

## TL;DR

`CollabText::Crdt::Buffer::apply_local_edit`'s per-call cost — both
wall time and resident memory — scales **linearly with the buffer's
`replica_id`** for a workload as simple as "append 180-byte chunks at
end of buffer 400 times." Per-edit cost goes from 0.14 ms at
`replica_id=1` to **27.8 ms at `replica_id=60000` (200× slower)** on
the same hardware, same input, same code path. Memory likewise scales
linearly with `replica_id`.

This is invisible to any test or example that uses a small fixed
`replica_id` (e.g. `1`, `2`), but **catastrophic** in real applications
that pick `replica_id` randomly across the uint16 range.

---

## Reproducer (standalone, no Markoff dependency)

Source: `2026-05-09-collabtext-bug-report-repro.cpp` (in this commit).

```cpp
#include "crdt/Buffer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char *argv[])
{
    const uint16_t replica_id =
        argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 1;
    const int n_edits     = argc > 2 ? std::atoi(argv[2]) : 400;
    const int chunk_bytes = argc > 3 ? std::atoi(argv[3]) : 180;

    CollabText::Crdt::Buffer buf(replica_id);
    const std::string chunk(chunk_bytes, 'a');
    uint32_t cursor = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n_edits; ++i) {
        buf.apply_local_edit({{cursor, cursor}}, {chunk});
        cursor += chunk.size();
    }
    auto t1 = std::chrono::steady_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf("replica_id=%u  edits=%d  total=%ld ms  per-edit=%.3f ms\n",
                replica_id, n_edits, (long)ms, double(ms)/n_edits);
}
```

Build:

```bash
g++ -std=c++20 -O2 \
    -I libs/collabtext/libs/collabtext/src \
    -I libs/collabtext/libs/collabtext/include \
    repro.cpp \
    build-dev/libs/collabtext/libs/collabtext/libcollabtext.a \
    -o repro
```

Run sweep:

```bash
for r in 1 100 1000 5000 10000 30000 60000; do ./repro $r; done
```

---

## Empirical results

### Standalone reproducer (above) — append-only workload

400 edits of 180 bytes appended to end-of-buffer. Same input, only
`replica_id` varies:

| `replica_id` | total | per-edit | slowdown vs `replica_id=1` |
|---:|---:|---:|---:|
| 1 | 55 ms | 0.138 ms | 1× |
| 100 | 78 ms | 0.195 ms | 1.4× |
| 1,000 | 192 ms | 0.480 ms | 3.5× |
| 5,000 | 902 ms | 2.255 ms | 16× |
| 10,000 | 1,926 ms | 4.815 ms | 35× |
| 30,000 | 5,452 ms | 13.630 ms | 99× |
| 60,000 | 11,114 ms | 27.785 ms | **201×** |

Per-edit cost vs `replica_id` is **strikingly linear**:

```
per-edit (ms) ≈ 0.13 + replica_id * 4.6e-4
```

(At `replica_id` = 65,535 we'd extrapolate ~30 ms per simple end-append
edit on a buffer that holds only 72 kB of text.)

### End-to-end Markoff measurement

Loading the same 72,657-byte markdown file via Markoff's
`MarkoffDocument::loadFromMarkdown` (which decomposes into many
`Buffer::apply_local_edit` calls across many `Buffer` instances —
one per markdown block, 392 in this case):

| `replica_id` | `loadFromMarkdown` | RSS-after | RSS-delta vs `replica_id=1` |
|---:|---:|---:|---:|
| 1 | 506 ms | 35 MB | 0 |
| 60,000 (random) | **50,098 ms** | **531 MB** | +496 MB |
| 62,000 (worse draw) | **263,591 ms** | **2,033 MB** | +2,000 MB |

Both wall-time and memory scale linearly with `replica_id` here too.
Variance between "60000" and "62000" reproduces because the random
draw doesn't strictly monotonically order the cost — but the trend is
unambiguous over a sweep.

---

## Where it bites Markoff

Markoff picks `replica_id` randomly from the uint16 range at app
startup (the obvious "every replica gets a unique id" approach for an
upcoming collab feature). End users land somewhere in the 65k-wide
distribution every launch. The slow tail is disqualifying: a 73 kB
markdown document took **>4 minutes to load and consumed 2 GB of RSS**
on a worst-case draw. Workaround in Markoff today: pin
`replica_id=1`. Single-user only — collab cannot ship until this is
fixed because real users in collab need distinct replica IDs.

---

## What we ruled out

Before pinning the cause to CollabText we eliminated:

- **Tree-sitter parsing.** Markoff's parser path takes ~50 ms for the
  same 72 kB document; total `inlineSpansFor` across 392 blocks is
  47 ms. Nowhere near the 50–263 s we observed.
- **QML / view layer.** Under offscreen QPA, ListView doesn't realise
  any delegates yet RSS still grows to 1 GB and CPU stays at 99% for
  60+ seconds. The slowness happens entirely **before** any QML
  delegate is constructed. Specifically inside `loadFromMarkdown`,
  before the engine even loads `Main.qml`.
- **Markoff's per-block model rebuild.** `LiveListModelBinding::onD2Changed`
  takes 73 ms total for 392 rows.
- **`QApplication` vs `QCoreApplication`.** Same load path, same time.
- **The minimal standalone reproducer above** removes Markoff entirely
  and still reproduces the linear-in-`replica_id` per-edit cost.

The only variable that varies the cost is `replica_id`.

---

## Suspect locations in your codebase

(Speculation; no source-level fix verified yet — but starting points
for your investigation.)

`libs/collabtext/src/crdt/Buffer.{h,cpp}`:

- The fragment locator algorithm in `Locator::between`
  (`crdt/Locator.cpp`) doesn't appear to reference `replica_id`, so
  the cost likely isn't inside locator generation itself.
- `m_origin_index: std::unordered_map<uint16_t, std::map<uint32_t, Locator>>`
  (`Buffer.h:210`) is keyed by `replica_id`. For a single-replica buffer
  the outer map should have one entry independent of the magnitude of
  the key — but worth confirming the lookup / iteration paths don't
  treat the key as a magnitude rather than an opaque id.
- The Lamport encoding `origin_key(const Lamport&)` in `Buffer.cpp:25`.
  If this concatenates `(replica_id << N) | counter` and any
  downstream structure is sized to `max_origin_key + 1`, that would
  be a direct match to the observed scaling.
- Any `std::vector<…>` whose `resize()` argument is derived from
  `m_replica_id` or from a `Lamport` whose `replica_id` field is the
  buffer's own id. A grep for `resize` and `reserve` against
  `replica_id` is the fastest first check.
- `rebuild_origin_index` (`Buffer.h:215`) and the surrounding insertion
  paths.

The shape of the scaling — strictly linear in `replica_id`,
independent of content size for a fixed edit pattern — suggests **a
loop or an allocation whose count or capacity is `replica_id` itself,
not `replica_id`'s cardinality in the data.** That narrows the hunt.

---

## What would help confirm root cause

A `perf record -g ./repro 60000` (or `valgrind --tool=callgrind`)
against the standalone reproducer with `replica_id=60000` and
`n_edits=400` should give a clean flame graph dominated by the
offending call site. The reproducer is small and isolated; the
profile shouldn't be noisy.

If you want a workload that hits multiple buffers (matching Markoff's
actual loadFromMarkdown shape, 392 separate buffers), modify the
reproducer to construct `n` `Buffer` instances each receiving one
edit. We confirmed the regression also reproduces in that pattern;
the simpler one-buffer-many-edits pattern in the snippet above was
chosen for diagnostic clarity.

---

## Environment

- OS: Linux 6.12.84-1-MANJARO
- Compiler: g++ (Arch GCC 15.2.1) — also reproduced under clang 20.x
- Build: `-O2 -std=c++20`
- Qt: 6.11 (via Arch's `qt6-base`)
- CollabText: as vendored in `markoff/foundation-exploration` on
  2026-05-09. Please cross-check against your tip — if the suspect
  code paths above have changed, this report may need re-targeting.

---

## Asks (ordered by what would unblock us fastest)

1. **Confirm reproduction** on your hardware with the reproducer
   above. If the linear-in-`replica_id` curve doesn't show on your
   machine, something about our build is anomalous and we'd want to
   know that fast.
2. **Profiler attribution** to a specific function and (ideally) a
   specific data structure.
3. **A targeted fix.** This blocks shipping any collab-enabled
   consumer that uses random `replica_id`s.
4. **A test added to your suite** that exercises a non-trivial
   `replica_id` (e.g. `42000`) on `Buffer::apply_local_edit` and
   asserts a sane per-edit budget. The current absence of such a
   test is presumably how this reached us.

Happy to provide more reproducer variants, larger sweeps, or run
any test you propose against our consumer setup. Reply on this
issue or ping the Markoff dogfood thread.

---

## Cross-references

- Markoff-side findings (consumer perspective):
  `docs/handoff/2026-05-09-collabtext-replica-id-perf.md`
- Markoff workaround commit:
  `fa57cbc — markoff: workaround CollabText O(replicaId) load regression + diagnostic bench`
- Standalone reproducer source (this report's companion):
  `docs/handoff/2026-05-09-collabtext-bug-report-repro.cpp`
- Bench harness used to build the sweep:
  `libs/markoff-core/tests/perf_load_bench.cpp`
