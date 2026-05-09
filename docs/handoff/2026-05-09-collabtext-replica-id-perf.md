# CollabText perf bug: load is O(replicaId)

**Date:** 2026-05-09
**Status:** Bug confirmed empirically. Single-user workaround landed
in `libs/markoff-live/app/main.cpp` (replicaId pinned to 1). Upstream
fix in `libs/collabtext/libs/collabtext/src/crdt/Buffer.cpp` (or its
dependents) is required before D5 (collab) ships.
**Severity:** Disqualifying — for random uint16 replicaIds, a 73 kB
document took 50–263 seconds to load and consumed up to 2 GB of RSS.

---

## 1. The symptom

User reported (paraphrased): "this widget takes a minute to load a
~20 kB document, and consumes gigabytes of RAM. completely
disqualifying."

Confirmed end-to-end measurement of `markoff-live-app` on a
72,657-byte real-world spec document:

| Metric | random uint16 replicaId | replicaId=1 | Change |
|---|---|---|---|
| `MarkoffDocument::loadFromMarkdown` | 50,098 ms (one run) — 263,591 ms (another run) | **506 ms** | 99×–520× faster |
| Time-to-stable (full app) | 71.9 s | **1.3 s** | 55× faster |
| Peak RSS (full app) | 1.02 GB | **78 MB** | 13× smaller |

The user's reported magnitude was accurate. The codebase is **not**
fundamentally slow; **the load path is O(replicaId)** in both time and
memory.

## 2. Reproduction

```bash
cd .worktrees/foundation-exploration
cmake --build build-dev --target perf_load_bench -j 8

# Fast (replicaId=1)
QT_QPA_PLATFORM=offscreen MARKOFF_REPLICA_ID=1 \
    ./build-dev/bin/perf_load_bench docs/specs/2026-04-28-foundation-design.md

# Slow (replicaId=10000) — about 25× slower than replicaId=1
QT_QPA_PLATFORM=offscreen MARKOFF_REPLICA_ID=10000 \
    ./build-dev/bin/perf_load_bench docs/specs/2026-04-28-foundation-design.md

# Catastrophic (replicaId near uint16 max) — minutes
QT_QPA_PLATFORM=offscreen MARKOFF_REPLICA_ID=60000 \
    ./build-dev/bin/perf_load_bench docs/specs/2026-04-28-foundation-design.md
```

Output is sent to stderr → systemd journal under Wayland sessions.
Tail with:

```bash
journalctl --user --since "10 seconds ago" | grep "bench\]"
```

## 3. The shape of the regression

Sweep over `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md`
(24,386 bytes, 142 blocks):

| replicaId | loadFromMarkdown | RSS delta | RSS / source ratio |
|---:|---:|---:|---:|
| 1 | 77 ms | 5.9 MB | 247× |
| 2 | 77 ms | 6.0 MB | 252× |
| 10 | 83 ms | 6.1 MB | 256× |
| 100 | 115 ms | 6.3 MB | 264× |
| 256 | 170 ms | 10.6 MB | 445× |
| 500 | 251 ms | 10.6 MB | 443× |
| 1,000 | 426 ms | 16.0 MB | 670× |
| 5,000 | 1,853 ms | 87 MB | 3,672× |
| 10,000 | 3,631 ms | 166 MB | 6,972× |

Time and memory are both **roughly linear in `replicaId`**.
Extrapolating to replicaId=65,535 (max uint16): ~25–30 s load + ~1 GB
RSS, matching the live-app's worst-case observation.

## 4. Where the bug lives

The CRDT engine `CollabText::Crdt::Buffer` (in
`libs/collabtext/libs/collabtext/src/crdt/Buffer.{h,cpp}`) is where
the cost lives. The fragment locator algorithm
(`libs/collabtext/libs/collabtext/src/crdt/Locator.cpp`) doesn't
reference `replicaId`, so the cost is unlikely to be in `between()`
itself. Suspect locations (need a profiler run to confirm):

- `m_origin_index: unordered_map<uint16_t, map<uint32_t, Locator>>` —
  per-replica map. For a single replica, this should have one outer
  entry; should not depend on `replicaId` magnitude. **Probably not it.**
- Some structure (vector? bitmap?) sized to fit the maximum
  `replica_id` ever observed. Pre-allocating `replicaId` slots and
  iterating them per op would explain linear scaling. **Most likely
  candidate.**
- A hash function or comparison whose performance degrades for high
  uint16 values. Less likely.
- An unintended quadratic interaction between `m_clock.value` and
  `replica_id` in the Lamport key encoding (`origin_key` in
  `Buffer.cpp:25`). Worth checking.

The `markoff-live`, `markoff-core`, and `markoff-parser` paths are
**not** the bottleneck — they're milliseconds and megabytes:

- `TreeSitterParser::parse` for 72 kB: a few tens of ms total.
- `loadFromMarkdown` minus the CRDT cost: tens of ms.
- `LiveListModelBinding::onD2Changed`: 73 ms for 392 rows.
- `inlineSpansFor` for all 392 blocks: 47 ms total.

## 5. Workaround landed today

`libs/markoff-live/app/main.cpp` previously generated a random uint16
replicaId per launch. Pinned to `1` with a comment pointing at this
doc. This makes the test app usable for single-user dogfood of
markoff-live.

```cpp
// libs/markoff-live/app/main.cpp
const quint16 replicaId = 1;  // PERF WORKAROUND — see this doc
```

## 6. Why this is single-user only

CRDT correctness requires distinct `replicaId`s per replica. If two
users both load with `replicaId=1`, their op IDs collide and
convergence fails. The workaround is fine for the single-user test
app; **D5 (collab activation) cannot ship until the upstream
`replicaId`-linear cost is fixed**, because real users get random
uint16s on their first connection.

## 7. Permanent benchmark harness

`libs/markoff-core/tests/perf_load_bench.cpp` is in the build as
target `perf_load_bench`. Not registered with `ctest` because it's a
diagnostic, not a pass/fail test. Runs:

```bash
QT_QPA_PLATFORM=offscreen MARKOFF_REPLICA_ID=<n> \
    ./build-dev/bin/perf_load_bench <path/to/doc.md>
```

Reports per-doc:
- `MarkoffDocument` ctor time
- `loadFromMarkdown` time
- `inlineSpansFor` cost summed across all blocks (separate phase —
  this is what `LiveListModelBinding::onD2Changed` calls)
- RSS before / after / VmPeak
- Block count, total span count
- RSS-to-source ratio

Should grow into a proper gated test once the underlying perf is
fixed: sample 5 doc sizes (1 kB / 10 kB / 50 kB / 200 kB / 1 MB) at
replicaId=1, assert load < N ms and RSS-delta < M MB.

## 8. Next moves

1. **Now (this commit):** workaround landed; bench harness in tree;
   findings doc (this file).
2. **Soon:** profile `CollabText::Crdt::Buffer` with `perf` /
   `valgrind --tool=callgrind` against the slow path to pinpoint the
   O(replicaId) cost. The reproducer above is small and isolated;
   straightforward to instrument.
3. **Before D5:** fix the upstream regression, remove the workaround
   in `markoff-live-app`, and gate the bench in CI.

## 9. What this rules out

The user asked "is it tree-sitter? is it qml?" Answer: **neither.**
Empirically:

- Tree-sitter parse is fast (sub-100 ms even for 73 kB).
- QML / ListView delegate creation is fast (delegates aren't even
  realised under offscreen QPA; the slowness happens before any
  delegate is created).
- The 71-second window of CPU-bound work is **inside
  `loadFromMarkdown`** — specifically the CollabText `Buffer`
  initialisation that runs as `loadFromMarkdown` populates per-block
  buffers. Phase markers in the live-app and the bench both show this
  cleanly.
