# markoff-bench

In-tree benchmark harness for the foundation-exploration parse / render
pipeline. See `docs/specs/2026-04-29-parse-render-bench-design.md` for
design rationale and `docs/plans/2026-04-29-parse-render-bench.md` for
the implementation plan.

## Layout

- `include/markoff-bench/`, `src/` — library sources (STATIC, internal,
  not installed). Public-to-bench-frontends only.
- `tests/` — unit tests for the harness primitives + CTest smoke target.
- `fixtures/` — committed real-doc fixtures (markdown).

Frontends live under `apps/bench/`:

- `markoff-bench-parse` — Tier 1 + Tier 1b (parse direct + via ParsePool).
- `markoff-bench-render` — Tier 2 (offscreen QPA, QML view, applyLocalEdit→frameSwapped).

## Build + run

```bash
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff-bench-parse markoff-bench-render -j 8

# Full parse-tier matrix (every profile × every scenario × both tiers):
./build-dev/bin/markoff-bench-parse --out /tmp/bench.json

# One profile / one scenario:
./build-dev/bin/markoff-bench-parse \
    --profile mid_prose --scenario type_end --tier tier1

# Render tier:
./build-dev/bin/markoff-bench-render \
    --profile mid_prose --scenario type_end --out /tmp/bench-render.json
```

## CTest integration

The harness includes one CTest-registered smoke target:

```bash
ctest --test-dir build-dev -L bench --output-on-failure
```

This runs `tst_bench_smoke` (mid_prose × type_end × 180 iters at Tier 1)
in well under 1 second. It verifies the harness builds and produces
plausible numbers; it does NOT enforce perf thresholds. To skip it on
the fast inner loop:

```bash
ctest --test-dir build-dev -j 8 -LE bench -E "tst_realistic|tst_benchmark" \
    --output-on-failure
```

## Output schema (v1)

```json
{
  "schema_version": 1,
  "git_sha": "…",
  "build_type": "RelWithDebInfo",
  "host": {"cpu": "…", "kernel": "…", "qt": "6.8.x"},
  "results": [
    {
      "tier": "direct_parse",
      "scenario": "type_end",
      "corpus_profile": "mid_prose",
      "iterations": 180,
      "warmup_iterations": 20,
      "metrics": {
        "total_ns":            {"count": …, "min": …, "mean": …, "p50": …, "p95": …, "p99": …, "max": …},
        "phase_extract":       {"…"},
        "phase_diff":          {"…"},
        "phase_parse_block":   {"…"},
        "phase_parse_inline":  {"…"},
        "phase_queries":       {"…"},
        "phase_snapshot":      {"…"},
        "phase_pool_queue":    {"…"},
        "phase_signal_hop":    {"…"},
        "phase_render_frame":  {"…"},
        "block_changed_bytes": {"…"},
        "inline_reuse_count":  {"…"},
        "alloc_bytes":         {"…"},
        "alloc_count":         {"…"}
      }
    }
  ]
}
```

All times are nanoseconds. `block_changed_bytes` and `inline_reuse_count`
are populated on Tier 1 only (we read the parser directly); Tier 1b/2
emit zeros for those fields.

## Caveats

1. **Phase splits are coarse.** Tier 1 currently buckets the entire
   per-iter parse cost into `phase_parse_block`. Finer splits require
   instrumenting `IncrementalParseSession::applyEdit` with PhaseTimer
   guards inside the foundation source — a follow-up task once a
   profile motivates it.
2. **Allocation counter is approximate.** The shim sees `operator new`
   from C++ code in markoff_bench-linked binaries. Calls that bypass
   the global operator (mmap, jemalloc, etc.) are not counted. The
   shim is gated on `-rdynamic` (so libstdc++.so resolves through it)
   and `-fno-allocation-dce` (so GCC's allocation-DCE pass does not
   elide `std::vector::reserve` and similar calls before the shim
   fires) — both are applied as `INTERFACE` build flags by the bench
   library.
3. **Offscreen QPA ≠ real GPU.** Tier 2 numbers are useful for
   relative comparisons across commits, not absolute UX claims. The
   render bench uses an inline QML root that loads `MarkoffEditor`
   from the public `org.markoff.view.qml` module (the app-private
   `org.markoff.view.qml.app` module is not exported).
4. **Cross-host comparisons are not meaningful.** The harness is
   designed for trend-on-same-machine; comparing numbers across CPUs
   or kernels will produce noise.
5. **Bench tests link the global new/delete shim.** Other test
   binaries in this repo do NOT link `markoff_bench`, so they are
   uninstrumented — but if you add a new test that links bench, be
   aware its allocator path is shimmed and its allocations count
   when an `AllocCounterScope` is in effect on that thread.

## Adjusting the synthetic corpus

Profile definitions live in `src/CorpusGen.cpp`'s `kProfiles` table.
Bumping a target size or changing a share is a stable change as long
as the size-tolerance test still passes. To add a new profile:

1. Append to the `CorpusProfile` enum in `include/markoff-bench/CorpusGen.h`.
2. Append a `ProfileSpec` to `kProfiles` in the same order.
3. Bump `kCorpusProfileCount`.
4. Add a row to `tst_bench_corpus_gen.cpp::all_profiles_within_size_tolerance_data`.

Real-doc fixtures live in `fixtures/`. To add one, copy a `.md` file
into the directory and reference it from the CLI as
`--fixture <basename>` (without the `.md` suffix).
