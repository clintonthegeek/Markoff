# Parse / render pipeline benchmark — design

**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Status:** Design (awaiting user review).

## Goal

Establish a reproducible, axis-isolated benchmark for the foundation-exploration parse and render pipeline so that:

1. We can attribute future regressions to **parse vs render**, **block tree vs inline tree**, **specific markdown shapes** (code-heavy, table-heavy, footnote-heavy, deeply-nested), and **edit scenarios** (cold parse, end-of-doc typing, mid-doc typing, big paste, large replace, block-boundary edit).
2. Every commit that claims a perf change can cite a number from the same harness on the same corpus.
3. The 04-28 typing-perf baseline gets folded into a structured artifact instead of living only in a free-text plan.

This is informational, not gating. The harness emits numbers; humans decide when a change is a regression.

## Pipeline being measured

```
applyLocalEdit
  └─ ParsePool::schedule(utf8)
        └─ worker thread:
             ├─ Document::extract(raw)              [frontmatter + footnote refs]
             ├─ IncrementalParseSession diff        [prefix/suffix scan → ByteEdit]
             ├─ TreeSitterParser::parseIncremental  [block tree edit + inline tree reuse]
             ├─ parser.buildDocumentQueries()       [bake DocumentQueryResult]
             └─ Document::fromComponents()          [value snapshot]
        └─ ParseUpdated signal
              └─ LiveBlockModel / SourceTextDocumentBinding update
              └─ KSyntaxHighlighter rehighlight (source mode)
              └─ Qt Quick scene graph rebuild + glyph cache
```

The benchmark addresses these in two tiers.

### Tier 1 — parse benchmark (primary, CI-friendly)

Drives the parser components directly **without** a Qt event loop, **without** the QML runtime, and **without** `ParsePool` queuing. Reaches `IncrementalParseSession` and `TreeSitterParser` via foundation-internal headers (the bench library is allowed `PRIVATE` access to `libs/markoff-core/src`).

A second, higher-level parse path (Tier 1b) drives `MarkoffDocument::applyLocalEdit` and waits for `ParseUpdated` synchronously, capturing pool-level cost (queueing, coalescing, signal hop). Both Tier 1 paths share the same corpus and scenarios; the difference is only how deep into the stack the bench reaches.

### Tier 2 — render benchmark (secondary, on-demand)

Spins up the QML view (`markoff-view-qml-app` smoke topology) under `QT_QPA_PLATFORM=offscreen`. Drives keystrokes via `QTest::keyClick` into TextArea, measures end-to-end keystroke→`afterRendering` signal latency, captures frame counts and reachable glyph-cache stats. Less CI-friendly (offscreen QPA ≠ real GPU), so it's a CLI-only frontend, run manually after meaningful UI changes.

## Corpus

### Synthetic generator (primary)

Deterministic, parameterised. Lives in `libs/markoff-bench/src/CorpusGen.cpp`. Axes:

| Axis | Range | Notes |
|---|---|---|
| Total size | 1 KB, 16 KB, 100 KB, 500 KB, 2 MB | Five sizes; biggest mirrors a "bad day" doc. |
| Paragraph count | derived from size + density | |
| Inline density | low / medium / high | Controls inline emphasis, links, code spans per paragraph. Drives inline tree count. |
| Code-block share | 0 %, 20 %, 60 % | Exercises highlighter and code-block parsing. |
| Table share | 0 %, 10 %, 40 % | Exercises `TableHandler`. |
| Footnote count | 0, 25, 200 | Exercises `Document::extract` footnote harvesting. |
| Nesting depth | 1, 4, 8 | Blockquote / list nesting. Stress on tree-sitter block tree. |

We don't run the full Cartesian product. We pick a fixed set of **profiles** — each profile is one named tuple — so output is comparable across runs:

- `tiny`: 1 KB, low density, 0 % code, 0 % tables, 0 footnotes, depth 1.
- `mid_prose`: 16 KB, medium density, 0 % code, 0 % tables, 0 footnotes, depth 1. (≈ the 04-28 baseline doc.)
- `mid_mixed`: 16 KB, medium density, 20 % code, 10 % tables, 25 footnotes, depth 4.
- `big_prose`: 100 KB, medium density, 0 % code, 0 % tables, 0 footnotes, depth 1.
- `big_code_heavy`: 100 KB, low density, 60 % code, 0 % tables, 0 footnotes, depth 1.
- `big_table_heavy`: 100 KB, low density, 0 % code, 40 % tables, 0 footnotes, depth 1.
- `big_footnote_heavy`: 100 KB, medium density, 10 % code, 0 % tables, 200 footnotes, depth 1.
- `huge`: 500 KB, medium density, 20 % code, 10 % tables, 50 footnotes, depth 4.
- `pathological`: 2 MB, high density, 30 % code, 20 % tables, 200 footnotes, depth 8.

Profile names are stable identifiers; the JSON output keys by profile so trending across commits is straightforward.

### Real-doc fixtures (sanity check)

Three to five real docs committed under `libs/markoff-bench/fixtures/`:

- `foundation-design.md` (the 04-28 baseline doc — copy of the spec).
- A representative in-repo plan (e.g. `2026-04-28-typing-perf.md`).
- Optionally: a public-domain long doc (e.g. a CommonMark spec dump) to give a "user-shaped" data point that's bigger than anything we author.

These exist so that "does the synthetic profile actually predict real-doc cost?" is answerable.

## Scenarios

Run against each corpus item:

1. **`cold_parse`** — `resetContent`, no prior tree. Single shot, no warmup loop (one number).
2. **`type_end`** — append 200 characters one keystroke at a time at end-of-doc. Drop first 20 as warmup; report distribution over the remaining 180.
3. **`type_start`** — same, but at offset 0.
4. **`type_middle`** — same, at uniformly distributed random offsets within the doc body (seeded RNG for determinism).
5. **`block_boundary`** — insert a blank line between two paragraphs (50 iterations at different boundaries).
6. **`paste_4kb`** — single 4 KB insertion at end-of-doc. 20 iterations, undo-and-redo isn't simulated; each iter starts from a fresh document state.
7. **`replace_1kb`** — select 1 KB, paste 1 KB. 20 iterations.

The "fresh document state" requirement for paste/replace means we copy the prepared `MarkoffDocument` per-iteration; that's an iteration-setup cost we exclude from the timed window.

Tier 2 (render) runs scenarios 1, 2, 6 only — full matrix is overkill at the render tier and offscreen-QPA isn't realistic enough to justify it.

## Metrics

Per timed window:

- **Wall time** (`steady_clock`), broken into phases for Tier 1 (direct) only:
  - `extract` (`Document::extract`)
  - `diff` (`IncrementalParseSession` prefix/suffix scan to `ByteEdit`)
  - `parse_block` (block tree `ts_tree_edit` + `ts_parser_parse`)
  - `parse_inline` (inline tree reuse + fresh parses)
  - `queries` (`buildDocumentQueries`)
  - `snapshot` (`Document::fromComponents`)
  - `total` (sum + any unattributed remainder)
- **Reuse counts**: `TreeSitterParser::inlineTreeReuseCount()` and a block-tree reuse count we'll need to add (cheap; mirrors the inline counter).
- **Allocations**: bytes + count, captured via a global `operator new`/`delete` shim linked only into the bench binaries. Captures Qt/KF6 allocations made from the bench thread; documented as such in the report.
- **Distribution**: p50 / p95 / p99 / max for scenarios with ≥20 iterations; single value for `cold_parse`.

Tier 1b adds:
- `pool_queue` — time from `applyLocalEdit` return to worker pickup.
- `signal_hop` — time from worker emit to main-thread receipt (queued connection).

Tier 2 adds:
- `keystroke_to_render` — from `keyClick` synchronous return to the next `afterRendering` signal.
- `frames_per_keystroke` (typically 0 or 1; >1 means a render storm).
- Glyph cache miss count if reachable via private Qt instrumentation; otherwise omitted with a note.

## Harness architecture

```
libs/markoff-bench/
├── CMakeLists.txt            # builds STATIC library "markoff-bench"
├── include/markoff-bench/    # internal header — not installed
│   ├── CorpusGen.h
│   ├── FixtureLoader.h
│   ├── Scenario.h            # ScenarioRunner driving the per-iter loop
│   ├── PhaseTimer.h
│   ├── AllocCounter.h
│   ├── PercentileReducer.h
│   └── JsonReporter.h
├── src/                      # implementations
└── fixtures/                 # committed real docs

apps/bench/
├── markoff-bench-parse.cpp   # CLI frontend, full Tier-1 matrix → JSON
└── markoff-bench-render.cpp  # CLI frontend, Tier-2 → JSON

libs/markoff-core/tests/bench/
└── tst_bench_smoke.cpp       # CTest-registered, label "bench"
                              # runs ONE profile (mid_prose) × ONE scenario (type_end, 50 iters)
                              # to keep the harness honest in CI; emits JSON to a tmp path
```

`markoff-bench` links `PRIVATE` against `markoff-foundation` and is allowed `PRIVATE` access to its `src/` headers (`IncrementalParseSession.h` etc.) via a `target_include_directories(markoff-bench PRIVATE libs/markoff-core/src)` line in `markoff-bench/CMakeLists.txt`. This is contained within the bench library — no production code learns about bench internals.

The standalone CLIs accept:

```
--profile <name>            # repeatable; default = all profiles
--scenario <name>           # repeatable; default = all scenarios
--iters <N>                 # default = scenario-defined
--warmup <N>                # default = scenario-defined
--seed <N>                  # default = 0xBEEF (deterministic mid-offset RNG)
--out <path.json>           # default = stdout
--format json|human         # default = human; CI uses json
```

The CTest smoke target hard-codes a small slice (one profile, one scenario, ~50 iters) so it stays under ~10 seconds and can run on every `ctest -L bench` invocation. The full matrix runs via the standalone CLI — outside the default test loop.

## Output format (JSON sketch)

```json
{
  "schema_version": 1,
  "git_sha": "…",
  "build_type": "RelWithDebInfo",
  "host": {"cpu": "…", "qt": "6.8.x"},
  "corpus_profile": "mid_prose",
  "scenario": "type_end",
  "iterations": 200,
  "warmup": 20,
  "metrics": {
    "total_ns":       {"p50": …, "p95": …, "p99": …, "max": …},
    "phase_extract":  {"p50": …, "p95": …, "p99": …, "max": …},
    "phase_diff":     {"…"},
    "phase_parse_block":  {"…"},
    "phase_parse_inline": {"…"},
    "phase_queries":      {"…"},
    "phase_snapshot":     {"…"},
    "inline_reuse":   {"p50": …, "p95": …, "max": …, "min": …},
    "block_reuse":    {"p50": …, "p95": …, "max": …, "min": …},
    "alloc_bytes":    {"p50": …, "p95": …, "max": …},
    "alloc_count":    {"p50": …, "p95": …, "max": …}
  }
}
```

One JSON object per `(profile, scenario)` pair. The CLI emits a JSON array of these.

## Pass/fail policy

Informational. No commit is blocked by a number. The expectation is that perf-claiming commits cite before/after numbers from this harness in the commit body, and that the user (or a future reviewer agent) eyeballs the JSON diff.

A future addition could be a CI step that diffs JSON against a stored baseline and posts a comment, but that's deliberately out of scope here — get the measurement honest first.

## Caveats / known limitations

- **Offscreen QPA is not a real GPU.** Tier 2 numbers are useful for relative comparisons across commits, not for absolute "would the user feel this." Document this on the render-tier output.
- **`mallinfo2` / global `operator new` shim does not see `malloc` calls inside Qt that go through `mi_*` or `tcmalloc` if those are enabled.** For our build, default glibc malloc is fine, but we should write the build assumption into the harness output.
- **Tier 1 (direct) bypasses ParsePool** — phase timing is for the parser, not the pool. Tier 1b covers pool cost. Don't conflate them in the report.
- **Fresh document state per paste/replace iteration is expensive to set up** but the timed window excludes it. The harness should print the ratio of timed-window ns to total wall ns so a future reader can sanity-check the methodology.
- **Tree-sitter inline-tree reuse counts are post-incremental-parse instantaneous values** — `inlineTreeReuseCount()` reflects the most recent `parseIncremental` call only. Sampling per-iteration is correct; sampling between iterations is not.

## Out of scope

- Trend storage / dashboards. Emit JSON; downstream tooling is a separate task.
- Differential profiling (perf record + flamegraph) integration. The 04-28 brief covers that workflow; we're not collapsing it into this harness.
- Multi-machine reproducibility. Numbers are comparable on the same host across commits, not across hosts.
- ParsePool stress (e.g. burst-edit storms). The pool is covered at Tier 1b for the steady-state typing case; deeper pool-only benchmarks are a follow-up if pool cost ever shows up as dominant.

## Future work (acknowledged, not built)

- A `compare-bench-json` CLI that diffs two JSON outputs and flags p95 regressions over a configurable threshold.
- Hooking the bench output into a per-commit artifact in CI.
- Real-GPU render numbers (xcb against a known display) as a separate, manually-invoked target.

## Open questions for the user

1. Real-doc fixtures — are you OK with copying `foundation-design.md` and one in-repo plan into `libs/markoff-bench/fixtures/`? Alternative is symlinking, but symlinks make the bench non-portable across hosts that don't have the docs at the same paths.
2. The CTest smoke target — happy with one profile / one scenario / ~50 iters as the bench-on-every-CI footprint, or do you want a different smoke slice?
3. Allocation counter — happy with the global `operator new`/`delete` shim approach (linked only into bench binaries), or do you prefer a less-invasive method like `mallinfo2()` deltas around the timed window? The shim is more accurate but globally instruments; `mallinfo2` is approximate but zero-impact.
4. Tiny production-code change — the metrics section assumes a new `TreeSitterParser::blockTreeReuseCount()` accessor mirroring the existing `inlineTreeReuseCount()`. The counter increments live in `parseIncremental` already (the block tree reports `ts_tree_get_changed_ranges` size), so it's a one-liner getter + one private member. OK, or would you rather the bench infer reuse from comparing pre/post block-tree node counts and skip the production touch?
