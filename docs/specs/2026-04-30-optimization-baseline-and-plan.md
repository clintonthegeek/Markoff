# Optimization plan — first use of the parse/render bench harness

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Status:** Stage 0 complete (commit `6040dd2`). Stage 1 candidates re-prioritised against phased data — see "Phased baseline outcome" below.
**Bench corpus baselines:**
- Pre-phase: `docs/bench-baselines/2026-04-30-parse-9bf4fad.json` (commit `9bf4fad`, parse-tier bucketed wholly into `parse_block`).
- Phased: `docs/bench-baselines/2026-04-30-parse-9bf4fad-stage0-phased.json` (commit `9bf4fad+stage0`, post-Stage-0).
- Render: `docs/bench-baselines/2026-04-30-render-*.json` (commit `9bf4fad`).

All on RelWithDebInfo, x86_64 / Linux 6.12 / Qt 6.11.0.

## Why this doc

The parse/render benchmark harness landed on this branch. This is the
first end-to-end use of it. The user asked: *what does the harness
actually let us decide?* This doc answers that by (a) reading a baseline
off the harness, (b) cross-referencing with the source we have today,
and (c) ranking optimization candidates by what the harness can prove.

The 04-28 typing-perf plan stopped after Task 1 — coalescing got rid of
the visible buffer-drain freeze, and the user explicitly chose to stop
optimising further until we had measurement infrastructure. We now do.

## What the bench actually answers today

**It answers:** for a fixed corpus and scenario, what is the per-iter
cost (p50/p95/p99) of the parse pipeline at two depths — direct (parser
+ session, no thread hop) and pool (`MarkoffDocument::applyLocalEdit` →
`ParseUpdated`, including CRDT + queue + signal hop). And: how many
inline trees got reused, how many bytes/allocations the iter cost,
and (Tier 2) how long until the next `frameSwapped`.

**It does *not* answer (yet):** which *phase* of the parse the cost
sits in. Per `libs/markoff-bench/README.md` caveat 1, every Tier-1
result currently buckets the entire per-iter parse cost into
`phase_parse_block`. The other phase fields (`phase_extract`,
`phase_diff`, `phase_parse_inline`, `phase_queries`, `phase_snapshot`)
are wired into the JSON schema but emit zero. Confirmed on the baseline
JSON — every `type_end` row reports `phase_parse_block` ≈ `total_ns`
and every other phase = 0.

This is the single highest-leverage gap to close first. **Without
phase splits, every optimization below is a guess at where the cost
lives.** With them, every optimization either wins on the right phase
or is reverted.

## Baseline at a glance

`type_end` p50 (microseconds), direct vs pool tier:

| profile | size | direct p50 | pool p50 | Δ (pool overhead) |
|---|---|---|---|---|
| `tiny` | ~1 KB | 144 | 195 | +50 (35 %) |
| `mid_prose` | 16 KB | 2 256 | 2 320 | +64 (3 %) |
| `mid_mixed` | 16 KB | 1 913 | 1 976 | +63 (3 %) |
| `big_prose` | 100 KB | 15 591 | 15 759 | +167 (1 %) |
| `big_code_heavy` | 100 KB | 8 537 | 8 777 | +240 (3 %) |
| `big_table_heavy` | 100 KB | 12 726 | 12 786 | +60 (0.5 %) |
| `big_footnote_heavy` | 100 KB | 15 712 | 15 473 | within noise |
| `huge` | 500 KB | 72 715 | 72 765 | within noise |
| `pathological` | 2 MB | 293 022 | 295 217 | within noise |

`paste_4kb` p50 (typical: 4 KB single insertion), direct tier:

| profile | direct p50 (µs) |
|---|---|
| `tiny` | 2 012 |
| `mid_prose` | 4 210 |
| `big_prose` | 17 871 |
| `huge` | 74 573 |

Render-tier `type_end` p50 (keystroke → frameSwapped, offscreen QPA):

| profile | render p50 (µs) | render p95 (µs) |
|---|---|---|
| `tiny` | 8 338 | 15 238 |
| `mid_prose` | 16 645 | 23 522 |
| `mid_mixed` | 24 898 | 33 579 |
| `big_prose` | 31 530 | 42 429 |
| `big_code_heavy` | 39 098 | 46 109 |
| `big_footnote_heavy` | 24 077 | 26 109 |

`paste_4kb` render p50 is 110-215 ms across profiles — that's the worst
user-visible cost in the corpus, and it's also where the harness
currently has the least insight (no phase split, render bench has only
a single keystroke→frame bucket).

## Reading the baseline

1. **Direct ≈ pool everywhere except `tiny`.** Pool tier adds CRDT
   `Global::join`, queue, and signal hop. On any doc ≥ 16 KB the parse
   cost dominates and pool overhead is invisible. On `tiny` it's 35 %
   of total — consistent with the 04-28 small-doc perf-record showing
   `Global::join` at 22 % and allocator at 44 %. **The CRDT cost is a
   constant per keystroke.** This lines up with the deferred
   "lightweight non-CRDT codepath" idea — when there's no remote
   replica, paying ~50 µs/keystroke for CRDT is unnecessary, but it's
   noise-level on real-sized docs.

2. **Per-keystroke cost scales with doc size, not edit size.** A
   single-character append on `big_prose` (100 KB) takes ~10× the time
   of the same edit on `mid_prose` (16 KB). The ratio matches the
   size ratio. That is the cost the user-reported freezes were about,
   and it persists despite incremental tree-sitter — meaning the
   incremental win on the *block tree* is not enough on its own.

3. **`big_code_heavy` is faster than `big_prose` despite the same size.**
   8.5 ms vs 15.6 ms for `type_end`. Code blocks are opaque to the
   inline parser; less inline-tree work per keystroke. Confirms that
   inline parsing is a real cost on prose-heavy docs.

4. **`big_footnote_heavy` allocates ~10× more bytes per keystroke than
   `big_prose`** (67 KB vs 28 KB) and ~10× the alloc count (259 vs 27).
   This is `Document::extract` doing two `QRegularExpression::globalMatch`
   passes over the body on every keystroke (footnote definitions and
   refs). On 100 KB with 200 footnotes that's a measurable cost. The
   bench ammo for the optimization we'd try is right there.

5. **Inline reuse counts are healthy.** `big_prose` reuses ~190 inline
   trees per `type_end`; the unreused ones are the paragraphs near the
   edit. The reuse mechanism is doing its job.

6. **`type_start` is dramatically faster than `type_end`** at every
   size (e.g. `big_prose`: 1.2 ms vs 15.6 ms). The reason is almost
   certainly that `type_start` benchmark-side resets between iterations
   and the prefix/suffix diff converges very fast, but it's worth
   verifying — if it's actually a parser reuse asymmetry, that's
   exploitable for `type_middle` too.

## What's hidden behind `phase_parse_block: 100 %`

`IncrementalParseSession::applyEdit` (`libs/markoff-foundation/src/IncrementalParseSession.cpp:56`)
does, every keystroke:

```
extract(newRaw)                       // QRegExp footnoteDef + QRegExp footnoteRef
m_extracted.body.toUtf8()             // QString → QByteArray, full body
newExtracted.body.toUtf8()            // QString → QByteArray, full body
diffBodyBytes(prior, new)             // O(n) prefix/suffix scan
parser.parseIncremental({edit}, utf8) // ts_tree_edit + ts_parser_parse + inline reuse
parser.buildDocumentQueries()         // full block-tree walk + full inline-tree walks
                                      //   per `libs/markoff-parser/src/TreeSitterParser.cpp:902`
m_extracted = std::move(newExtracted);
```

`ParsePoolWorker::onParse` then calls `m_session.snapshot()` which
calls `Document::fromComponents` — the snapshot itself is shallow
(moves of `QString` / `QHash` / `QList`), but `m_queries` is copied by
value out of the session into the new Document. `DocumentQueryResult`
holds the full headings/links/tags lists, so this is one full structure
copy per keystroke.

Of these:
- `extract`: O(body size) + regex cost. **Re-runs on every keystroke even
  when the edit didn't touch frontmatter or footnote refs.**
- two `toUtf8` conversions: O(body size) each. The prior one is
  recomputable from cached state — we already had `priorBodyUtf8` last
  iter and threw it away.
- `buildDocumentQueries`: O(full tree). **Walks the entire block tree
  and every inline tree even when nothing changed in 95 % of them.**
  Tree-sitter's `ts_tree_get_changed_ranges` already tells us where the
  edit's effects landed; we're not using it for the query layer.
- `fromComponents` queries copy: O(query result size). One copy of
  every heading/link/tag every keystroke, regardless of edit.

The bench can prove or refute the cost of each of these — once it has
phase splits.

## Proposed optimization plan

### Stage 0 — Make the bench answer "which phase" — DONE (commit `6040dd2`)

Wired six per-phase RAII guards into `IncrementalParseSession::applyEdit`,
`reset`, and `snapshot`. Added `lastParseBlockNs()` / `lastParseInlineNs()`
to `TreeSitterParser` so the session can attribute parser-internal cost
to ParseBlock vs ParseInline buckets without splitting the parser's
public API. Bench `ScenarioRunner` now allocates a foundation
`ParsePhaseTable` per iter, reads it back, and copies into the bench
`PhaseTable` (foundation indices 0..5 align with bench `Phase::Extract..Snapshot`
by construction). `snapshot()` moved inside the timed window.

**Exit criterion met:** every phase reports a non-zero p50 across the
full matrix, and the six phases sum to 99-100 % of `total_ns` (the ~0.5 %
gap is the bench's outer steady_clock capture and inter-phase non-
instrumented work — well within tolerance).

#### Phased baseline outcome

`direct_parse / type_end` p50 — phase % of total:

| profile | total µs | extr | diff | blok | inln | quer | snap | sum% |
|---|---|---|---|---|---|---|---|---|
| `tiny` | 151 | 1.6 | 0.5 | 92.3 | 3.0 | 0.7 | 0.1 | 98.3 |
| `mid_prose` | 2 410 | 0.8 | 0.3 | **92.9** | 1.0 | 4.3 | 0.0 | 99.4 |
| `mid_mixed` | 1 905 | 1.7 | 0.4 | **80.7** | 8.0 | 8.6 | 0.0 | 99.5 |
| `big_prose` | 15 670 | 0.8 | 0.4 | **90.9** | 0.9 | 6.9 | 0.0 | 99.8 |
| `big_code_heavy` | 8 904 | 1.4 | 0.7 | **75.2** | 14.1 | 8.4 | 0.0 | 99.9 |
| `big_table_heavy` | 12 732 | 0.9 | 0.5 | **79.7** | 11.7 | 7.0 | 0.0 | 99.8 |
| `big_footnote_heavy` | 15 289 | 1.4 | 0.4 | **87.8** | 1.9 | 8.3 | 0.0 | 99.8 |
| `huge` | 71 727 | 1.0 | 0.5 | **77.8** | 9.6 | 10.9 | 0.0 | 99.8 |
| `pathological` | 292 466 | 1.1 | 0.5 | **67.5** | 14.9 | 15.7 | 0.0 | 99.7 |

`paste_4kb` flips the inline cost up: `mid_prose` 14.6 % inline, `big_code_heavy` 17.8 %, `replace_1kb` on `mid_prose` 17.9 %. Big paste/replace edits land inside the inline-tree reuse window and force more fresh inline parses.

#### What this changes about the plan

The pre-phase plan ranked candidates by *guess*. With actual per-phase
data, the ordering shifts:

1. **`phase_diff` is 0.3-1.0 % everywhere.** Stage 1.1 (cache `priorBodyUtf8`) saves at most 0.5 %. **Drop.**
2. **`phase_snapshot` is ~0 %.** Stage 2.1 (shared_ptr queries through `Document`) saves nothing measurable. **Drop.**
3. **`phase_extract` is 0.8-2.0 %.** Even on `big_footnote_heavy` it's only 1.4 %. The hypothesis "footnote-heavy docs make extract dominate" is false — the regex sweeps are O(N) in body bytes, and N is fixed at 100 KB. The footnote multiplier is small. Stage 1.2 (skip `extract` when the diff window misses frontmatter/footnote bytes) is still doable but the ceiling is ~2 %. **Demote** to "if a future re-profile motivates it."
4. **`phase_queries` is 4-16 %, growing with doc size.** On `huge` 7.8 ms (10.9 %), on `pathological` 46 ms (15.7 %), on `big_prose` 1.1 ms (6.9 %). This is the next-largest *eliminable* cost and grows with doc size — the right place to invest. **Promote to Stage 1.1** (was 1.3).
5. **`phase_parse_inline` swings from 1 % (typing on prose) to 18 % (paste on code-heavy).** When paste/replace forces inline reparses, the cost is real. Investigate WHY reuse drops on these scenarios — possibly the reuse mechanism only matches *exact* byte ranges, so any block whose inline range shifted by the edit delta gets unfairly invalidated. **Promote to Stage 1.2** (new — was deprioritised before; data motivates).
6. **`phase_parse_block` is 67-93 % everywhere — the dominant cost.** This is tree-sitter's incremental block reparse. Hard to attack directly without changing the parser. Two avenues are worth investigating but neither is small: (a) Are we reusing the `TSTree *` correctly? Verify `ts_tree_get_changed_ranges` reports small windows for typing edits (the bench shows `block_changed_bytes` p50 = 0 on typing — so reuse should be perfect, yet 90+ % of cost is here). (b) The `parse_block` bucket includes the `ts_tree_edit` loop and ts-parser overhead. **Stage 2 candidate.**

### Stage 1 — Re-prioritised post-Stage-0

Each is a one-commit `perf:` change with a before/after JSON diff cited
in the body. Do them in order, re-run the bench between each, and stop
early if the phase that motivated the change isn't actually moving.

- **Task 1.1: Incremental `buildDocumentQueries`.** When `parseIncremental`
  runs, `ts_tree_get_changed_ranges(prevTree, newTree)` already gives us
  exact changed byte ranges. The query layer should keep the prior
  `DocumentQueryResult` and only re-walk subtrees overlapping those
  ranges. Implementation skeleton: a "range-filtered" walk that takes
  prior queries + changed ranges and produces a new result by replacing
  only the entries whose source offset lies in a changed range. **Bench
  target:** drive `phase_queries` on `big_prose` / `huge` / `pathological`
  to <2 % of total (current 6.9 / 10.9 / 15.7 %). If it doesn't, the
  walk isn't actually `O(tree size)` and a second look at `collectHeadings*`
  / `collectInlineQueries` is needed.

- **Task 1.2: Investigate the inline-reuse drop on paste/replace.** Read
  `inline_reuse_count` p50 / p95 across `paste_4kb` and `replace_1kb` rows
  in the phased JSON. For each profile: how many inline trees did we have
  pre-paste, how many got reused, what percentage? If the answer is <50 %
  on prose docs, the reuse mechanism is over-invalidating. The hypothesis
  to test: the matcher in `parseIncremental` requires *exact* shifted-old
  range equality with new ranges, but `ts_tree_get_changed_ranges` may
  report ranges adjacent to the edit as "changed" even when the inline
  content is byte-identical post-shift. If true, expand the matcher to
  accept content-equivalent ranges. **Bench target:** `phase_parse_inline`
  on `paste_4kb / mid_prose` from 14.6 % to <5 %.

- **Task 1.3 (demoted, deferred): Skip `extract` when the edit window misses
  frontmatter and footnote bytes.** Phase ceiling is ~2 % across the
  matrix. Implement only if a re-profile after 1.1 + 1.2 still shows
  extract growing as a relative share, or if a future profile gets added
  with much heavier frontmatter.

#### Dropped from the plan

- **Task 1.1 (old): cache `priorBodyUtf8`.** Diff phase is 0.3-1.0 %. Not worth the cache invariant.
- **Task 2.1 (old): shared_ptr queries through `Document`.** Snapshot phase is ~0 %. The move-only path through `fromComponents` is already free.

### Stage 2 — Bigger-ticket items (gated on Stage 1 + a re-profile)

These are work-units, not one-commit fixes. Do not start any of these
until Stage 1's exit criterion is met *and* a perf-record on the long
doc (per the 04-28 methodology) shows the cost actually sits where the
bench claims.

- **Task 2.1: Reuse `DocumentQueryResult` across the snapshot boundary.**
  Today every `Document::fromComponents` copies the full `queries` value.
  The Document is immutable; the snapshot can hold a `shared_ptr` to
  the queries. The change is small but touches public-ish API. Bench
  target: `Phase::Snapshot` p50 on `big_prose`.

- **Task 2.2: Render-tier paste cost.** `paste_4kb` render p50 is
  110-215 ms — the worst single-keystroke cost in the corpus. The
  render bench currently has a single bucket (`phase_render_frame`).
  Splitting that into `pool_queue` / `signal_hop` / `parse_total` /
  `model_update` / `frame` would let us localise the cost. The render
  bench is more invasive to instrument than the parse bench — likely
  needs a worker-thread side timestamp + a main-thread receipt
  timestamp, both in the QML view module. Treat as a separate plan if
  it survives prioritisation.

- **Task 2.3 (deferred from 04-28): KSyntaxHighlighter rehighlight scope.**
  04-28 plan Task 2 stopped here. The render-tier numbers above
  suggest the highlighter is contributing to the per-keystroke cost
  on prose docs (`mid_prose` 16 ms render p50 is dominated by glyph /
  scenegraph work, per the 04-28 perf-record). The bench doesn't help
  identify this directly — perf-record does. Pair the bench output
  with a fresh perf-record run *after* Stage 1 lands and decide.

- **Task 2.4 (memory-resident, future): Lightweight non-CRDT codepath.**
  Tier 1b - Tier 1 = 35 % overhead on `tiny`. On real docs it's
  invisible. Worth it eventually, but only after the dominant per-doc-
  size costs are addressed.

## What the bench will and won't catch

It catches:
- Regression in any Tier-1 phase, per profile, per scenario, p50/p95/p99.
- Reuse counter regressions (e.g. someone breaks inline-tree reuse by
  invalidating too aggressively — `inline_reuse_count` will drop).
- Allocation regressions in foundation parse code (the shim sees
  bench-thread `operator new`).
- Pool-vs-direct delta — i.e. CRDT + queue + signal hop changes.

It does not catch:
- Highlighter rehighlight scope (out of bench surface; needs perf-record).
- Real-GPU render cost (offscreen QPA caveat).
- Cross-host perf trends (caveat 4).
- Anything that happens in a different process (e.g. font cache).
- Allocator behaviour outside the bench thread, or via mmap / jemalloc.

This bound is fine. The bench is the source of truth for the parse
pipeline; perf-record stays the source of truth for everything else.

## Recommended next move

Stage 0 done. The phased baseline above answers "which phase" across
the matrix.

The recommended Stage-1 entry point is **Task 1.1 (incremental
`buildDocumentQueries`)**. It targets the single largest *eliminable*
cost (queries grows from 4 % to 16 % with doc size; on `huge` it's 7.8 ms
per keystroke, on `pathological` 46 ms). The implementation is bounded:
read `ts_tree_get_changed_ranges`, filter the prior `DocumentQueryResult`
by source offset against the changed-range list, re-walk only the
subtrees that overlap. Existing per-call wall time gives a clean
before/after.

If 1.1 lands and 1.2 (inline-reuse-drop investigation) confirms a real
regression on paste/replace scenarios, that's the second move. After
those two, the plan is mostly out of cheap wins inside the parse tier;
further wins live in tree-sitter parameters, the render tier, or the
deferred non-CRDT codepath — all of which require a fresh
`docs/specs/...-design.md` cycle and are out of scope here.
