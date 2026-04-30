# Optimization plan — first use of the parse/render bench harness

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Status:** Stages 0 / 1.1 / 1.2 complete (commits `6040dd2`, `96bb42c`, `5568c93`). Post-Stage-1.2 perf-record captured; parse-pipeline is no longer the bottleneck on the long-doc reproducer. Stage 1.3 demoted. Stage 2 candidates re-prioritised against the post-Stage-1.2 perf — see "Post-Stage-1.2 perf-record outcome" below.

**Bench corpus baselines:**
- Pre-phase: `docs/bench-baselines/2026-04-30-parse-9bf4fad.json` (commit `9bf4fad`, parse-tier bucketed wholly into `parse_block`).
- Stage 0 (phased): `docs/bench-baselines/2026-04-30-parse-9bf4fad-stage0-phased.json` (commit `9bf4fad+stage0`).
- Stage 1.1 (incremental queries): `docs/bench-baselines/2026-04-30-parse-5f6ef77-stage1-1-queries.json` (commit `5f6ef77+stage1.1`).
- Stage 1.2 (inline matcher hash-map): `docs/bench-baselines/2026-04-30-parse-96bb42c-stage1-2-matcher.json` (commit `96bb42c+stage1.2`).
- Render: `docs/bench-baselines/2026-04-30-render-*.json` (commit `9bf4fad`).
- Post-Stage-1.2 perf-record (live UI, 30 s typing into 72 KB doc): `docs/bench-baselines/2026-04-30-perf-typing-stage1-2-after.data`.

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

- **Task 1.1: Incremental `buildDocumentQueries` — DONE (commit `96bb42c`).**
  Implementation: `TreeSitterParser` now stores `m_lastChangedRanges`
  (the union of `ts_tree_get_changed_ranges` and edits-derived ranges
  in the new frame — both are needed because tree-sitter's block-tree
  changed-ranges report nothing for inline-only edits). New
  `buildDocumentQueries(prior, edits)` overload carries over each prior
  entry whose full byte range doesn't overlap any changed range (with
  shifted offset), and walks only the subtrees of the new tree that do
  overlap. `HeadingInfo`/`LinkInfo`/`TagInfo` gained a `sourceLength`
  field so the carry-over check can use the full entry range, not just
  the start byte (TDD caught the heading-text-changed bug this protects
  against).

  **Phase delta:** `phase_queries` p50 dropped 14-18× across the matrix:
  - `mid_prose`: 103 → 10 µs
  - `big_prose`: 1 082 → 74 µs
  - `huge`: 7 832 → 547 µs (−6 % total wall time)
  - `pathological`: 45 804 → 2 500 µs (−11 % total wall time)

- **Task 1.2: Inline-tree matcher hash-map + cached old ranges — DONE
  (commit `5568c93`).** The original hypothesis ("matcher over-invalidates
  on paste/replace") turned out to be wrong. Reading `inline_reuse_count`
  on the phased JSON showed reuse was already perfect on big-region
  profiles (`big_code_heavy` 92/92, `huge` 734/734, `pathological`
  2193/2193 every iteration). The cost was in the matcher *machinery*,
  not in cache misses. Two fixes landed:
  1. Replaced the O(N²) nested-loop matcher with a packed
     `(start_byte << 32 | end_byte)` hash map. O(N) lookups; old ranges
     are pairwise disjoint and shifts are monotonic, so keys are unique.
  2. Cached `m_inlineRanges` on the parser parallel to `m_inlineTrees`,
     populated by `parse()` and `parseIncremental()`. Eliminates one of
     the two `collectInlineRanges` tree walks per call (the old-tree
     walk became a vector copy).

  **Phase delta:** `phase_parse_inline` p50 dropped 36-50 % across every
  big-region profile and every scenario. Net total wall-time gains
  surfaced where the savings were big enough:
  - `pathological / type_end`: −21.8 ms (−8 %)
  - `pathological / paste_4kb`: −17.7 ms (−7 %)
  - `pathological / replace_1kb`: −19.6 ms (−7 %)
  - `pathological / block_boundary`: −17.3 ms (−7 %)
  - `huge / replace_1kb`: −3.7 ms (−6 %)
  - `huge / paste_4kb`: −2.9 ms (−4 %)

- **Task 1.3 (demoted, deferred): Skip `extract` when the edit window misses
  frontmatter and footnote bytes.** Phase ceiling is ~2 % across the
  matrix. Implement only if a re-profile after 1.1 + 1.2 still shows
  extract growing as a relative share, or if a future profile gets added
  with much heavier frontmatter.

#### Dropped from the plan

- **Task 1.1 (old): cache `priorBodyUtf8`.** Diff phase is 0.3-1.0 %. Not worth the cache invariant.
- **Task 2.1 (old): shared_ptr queries through `Document`.** Snapshot phase is ~0 %. The move-only path through `fromComponents` is already free.

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

## Cumulative outcome through Stage 1.2

`pathological / type_end` p50 went from **292 466 µs** (Stage 0
baseline) to **238 523 µs** (Stage 1.2). That's −18 % per keystroke
on a 2 MB doc. `huge / type_end` went from 71 727 µs to 64 996 µs
(−9 %). On smaller profiles the savings are real per-phase but get
buried under run-to-run variance in `phase_parse_block` (which is
tree-sitter internal and not affected by these changes).

`phase_parse_block` is now the only meaningful cost left on the parse
tier — 79-89 % of total across the corpus, all internal to tree-sitter.
That's Stage 2 territory and requires a different approach (parser
configuration, larger edit-window hints, or upstream changes); not
going to fall to a 100-line patch.

## Post-Stage-1.2 perf-record outcome

A 30-second perf-record session against `markoff-view-qml-app` typing
into the 72 KB long-doc reproducer (`docs/specs/2026-04-28-foundation-design.md`)
captures the new profile shape post-Stage-1.2. Methodology matches
04-28: `perf record -F 99 -g`, ~100 wpm steady typing, same reproducer.

**Per-thread CPU split:**

| Thread | 04-28 baseline | Stage 1.2 after |
|---|---|---|
| Main (`markoff-view-qm`) | 67.3 % | 97.2 % |
| Parse worker (`QThread`) | 32.5 % | **2.6 %** |

**The parse pipeline is no longer the bottleneck.** The parse-worker
share dropped 12×. Tree-sitter symbols are absent from the top 30; the
only entry left is `ts_node_child` at 0.98 %.

**Main-thread cost categorisation** (post-Stage-1.2):

| Bucket | Share | Notes |
|---|---|---|
| libc / allocator | **26.31 %** | The 14.81 % at `libc:0x16d107` is the malloc inner loop. Reached primarily through Qt Quick scenegraph + glyph cache; some unknown share from per-keystroke QString work. |
| CRDT (`Global::join`) | **20.87 %** | Pure compute; sub-tree dominated by inlined `max<unsigned int>`. **No allocation** — version-vector merge per `applyLocalEdit`. |
| Qt Quick scenegraph | 15.39 % | `prepareAlphaBatches`, `nodeChanged`, `unmap`, `uploadMergedElement`, `markDirty`. |
| QtGui text layout | 7.69 % | `QGlyphRun`, `QTextBlock`, `QTextLine`. |
| GPU driver | 2.30 % | `nvidia-eglcore` command submission. |
| Tree-sitter | 1.21 % | Down from 32.5 % at 04-28. |
| Markoff app code | 0.70 % | Our own code. |

The long-doc profile now resembles 04-28's *small-doc* profile shape
(allocator + CRDT dominant). Parsing has been compressed enough that
its share collapsed; the constant-per-keystroke costs (CRDT, allocator,
render) are now the visible cost.

**KSyntaxHighlighting symbols are not in the top 30.** Either the
highlighter isn't the cause of the scenegraph cost (contradicts 04-28
hypothesis), or its work is inlined into the scenegraph call paths and
doesn't surface as its own line. Render-tier instrumentation will
disambiguate.

## Stage 2 — Re-prioritised against the perf-record data

- **Task 2.A (out of mandate, handed off): CRDT `Global::join`
  optimisation.** 20.87 % of total CPU on a single function — the
  largest single eliminable cost. Out of this repo's scope: the
  `collabtext` library has its own bench harness and dedicated
  development agents. See `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`
  for the data we're handing across.

- **Task 2.B: Render-tier instrumentation — DONE (commit landing this update).**
  Render bench now reports six per-iteration phases (apply_edit,
  pool_queue, parse_work, signal_hop, model_update, render_frame) that
  by construction sum to `total_ns` per iteration. Bench runs in **live
  mode** by default so the four UI-side phases are sequential on the
  critical path. Baseline JSON: `docs/bench-baselines/2026-04-30-render-f7acaab-phased.json`.

  Phase split, p50 (microseconds), `type_end` scenario:

  | profile | total | apply | pool | parse | hop | model | render |
  |---|---:|---:|---:|---:|---:|---:|---:|
  | `tiny`               |    6 010 |    170 |   8 |     190 |   8 |    32 |    5 570 |
  | `mid_prose`          |    7 750 |    350 |  15 |   2 440 |  16 |   140 |    4 820 |
  | `mid_mixed`          |    9 120 |    430 |  11 |   1 960 |  14 |   190 |    6 440 |
  | `big_prose`          |   17 280 |  1 160 |  54 |  15 310 |   0 |     1 |   16 070 |
  | `big_code_heavy`     |   25 630 |  1 320 |  28 |   8 770 |   0 |     1 |   24 280 |
  | `big_table_heavy`    |   28 390 |  1 260 |  29 |  12 200 |   0 |     1 |   27 080 |
  | `big_footnote_heavy` |   19 160 |  1 210 |  34 |  15 070 |   0 |     1 |   17 930 |
  | `huge`               |   99 000 |  5 350 |  32 |  63 120 |   0 |     2 |   93 510 |
  | `pathological`       |  549 750 | 24 680 |  36 | 239 290 |   0 |     2 |  523 620 |

  `paste_4kb` p50 (microseconds), same matrix:

  | profile | total | apply | pool | parse | hop | model | render |
  |---|---:|---:|---:|---:|---:|---:|---:|
  | `tiny`               |  106 640 |  96 880 |  27 |   2 190 |   0 |    1 |    9 750 |
  | `mid_prose`          |  109 010 |  98 060 |  32 |   4 880 |   0 |    1 |   10 990 |
  | `big_prose`          |  116 970 |  96 810 |  28 |  18 290 |   0 |    1 |   20 150 |
  | `pathological`       |  667 170 | 123 900 |  37 | 246 680 |   0 |    2 |  541 770 |

  **Sum of per-iter phases equals `total_ns` exactly by construction.**
  Sum of phase p50s diverges from p50 of total on heavy profiles (e.g.
  `big_prose / type_end`: sum of p50s is 32.5 ms vs 17.3 ms total) because
  `parse_work` and `render_frame` are inversely correlated per-iter under
  parse-pool coalescing — when parse coalesces into pending, the next iter
  sees a small parse_work but a large render_frame attributed to the
  earlier coalesced work, and vice versa. This is an artifact of percentile
  decomposition, not a measurement bug; the per-iter invariant
  (apply + pool + parse + hop + model + render = total) holds exactly. See
  `libs/markoff-bench/README.md § Render-tier phase taxonomy`.

  **Findings (informing 2.C and follow-up work):**

  1. **`phase_render_frame` is the dominant UI-side cost on every doc
     ≥ 16 KB**, ranging from 4.8 ms on `mid_prose` to 524 ms on
     `pathological`. Scales roughly linearly with doc size, as expected
     for a non-virtualizing ListView with per-paragraph delegates. This is
     not highlighter cost (highlighter is in `apply_edit`); it's
     scenegraph rebuild + QtGui text layout for visible delegates.

  2. **`phase_apply_edit` is non-trivial in live mode (1-5 ms on big
     profiles)** and is dominated by `paste_4kb` (~100 ms on every
     profile). Live-mode apply_edit is ~14× source-mode apply_edit on
     `mid_prose / type_end` (5.1 ms vs 0.36 ms), implying that a live-
     mode-specific synchronous path (likely SourceTextDocumentBinding's
     reverse-update of the invisible source TextArea, since
     `MarkoffDocument::contentsChanged` is a DirectConnection signal and
     SourceTextDocumentBinding is the only foundation-side subscriber)
     is paying a cost it doesn't need to in live mode. **Follow-up
     candidate.** Easy win if confirmed: short-circuit
     SourceTextDocumentBinding when its TextArea is `enabled: false`.

  3. **`phase_model_update` is essentially zero on every profile** (1-200
     µs p50). Either LiveListModelBinding's BlockWalker + AstBlockDiff +
     applyOps work is genuinely fast, OR the work happens inside QML's
     async polish phase that blends into `phase_render_frame`. Worth a
     focused profile to disambiguate before betting on Stage 2.C
     (KSyntaxHighlighter): if model_update is genuinely cheap, the
     scenegraph cost is delegate-driven (QML pipeline / QtGui text), not
     highlighter-driven. The 04-28 highlighter hypothesis loses support.

  4. **`phase_pool_queue` and `phase_signal_hop` are noise** (sub-100 µs
     across every profile). The Qt queued-connection plumbing is not a
     cost contributor at any size we measured.

  5. **`paste_4kb`'s ~100 ms `phase_apply_edit`** is striking and
     consistent across every profile (96-124 ms). This is one CRDT
     `apply_local_edit` for a single 4 KB insertion; the cost is
     dominated by `Buffer::apply_local_edit` itself, not by anything
     downstream. Belongs to Stage 2.A (CRDT) territory and to the
     `collabtext` agents — it's nearly invariant of doc size, so it's a
     constant per-paste cost in the buffer write path.

- **Task 2.C (deferred): KSyntaxHighlighter rehighlight scope.** Render-
  tier instrumentation now lets us decide. `phase_model_update` p50 is
  1-200 µs across every profile, while `phase_render_frame` carries
  3.6-523 ms. If the scenegraph cost were highlighter-driven, we'd
  expect it to surface in `phase_apply_edit` (KSyntaxHighlighter is a
  DirectConnection subscriber to QTextDocument::contentsChange) or
  `phase_model_update` (if the highlighter ran during the parsed-doc
  consumption path). Neither is the case — `apply_edit` is small except
  on `paste_4kb`, where the cost is in CRDT, not the highlighter.
  **Recommendation: drop Task 2.C's "highlighter rehighlight scope"
  framing.** Re-prioritize as "QML delegate / Live-render scenegraph
  rebuild" — the cost is in scenegraph node creation, not in the
  highlighter. Pursue only if Live mode is the actual target UX (current
  user-facing app defaults to source mode per `markoff-view-qml-app`
  arg parsing).

- **Task 2.C (deferred): KSyntaxHighlighter rehighlight scope.** Cannot
  be confirmed or refuted by the perf-record (highlighter symbols don't
  surface in the top 30). Render-tier instrumentation (Task 2.B) is
  the prerequisite; until we localise the scenegraph cost to a phase,
  guessing at the highlighter is premature.

- **Task 2.D (deferred): Allocator reduction.** 26 % of CPU but no
  single hot site — diffuse target. The allocator share will likely
  drop as a side-effect of either CRDT (Task 2.A) or render (Task 2.B)
  wins, since both reach the allocator from many paths. Standalone
  attack only if the diffuse share remains after 2.A and 2.B.

Stage 1.3 (skip `extract` when the edit window misses
frontmatter/footnote bytes) remains demoted: `phase_extract` is still
0.8-2.0 % of total and the conditional adds complexity for little win.

## Stage 2 candidate #1 outcome — phantom (not pursued)

The 2026-04-30 render-optimization SESSION-BRIEF (`docs/handoff/2026-04-30-render-optimization-SESSION-BRIEF.md`)
flagged candidate #1 as: *"live-mode `phase_apply_edit` is 14× source-mode
(~5.15 ms vs 0.36 ms gap on mid_prose / type_end)"*. Verification against
the cited baseline JSON found this premise didn't survive contact with
the data:

| profile          | apply_edit p50 LIVE (baseline) | apply_edit p50 SOURCE (fresh run) |
|------------------|-------------------------------:|----------------------------------:|
| mid_prose        | 347.5 µs                       | 371.3 µs                          |
| big_prose        | 1 159 µs                       | 1 205 µs                          |
| big_code_heavy   | 1 316 µs                       | 1 376 µs                          |
| huge             | 5 353 µs                       | 5 376 µs                          |

Live and source `apply_edit` are within 4-6 % of each other on every
profile — within run-to-run noise. The brief's "5.15 ms" figure was a
profile mix-up: it's the `huge` profile's apply_edit p50 (5.35 ms),
not mid_prose's. There is no live-vs-source apply_edit gap to chase.
Candidate dropped without code change.

## Stage 2 candidate #2 — `phase_render_frame` re-investigation → BlockWalker move (DONE)

**Brief's hypothesis:** "ListView with no virtualization tuning; every
paragraph allocates a TextEdit; for 100 KB / ~1000 paragraphs that's a
lot of scenegraph nodes per frame."

**What the data actually showed.** Setting `ListView.reuseItems: true`
produced no measurable change anywhere (mid_prose +0.2 %, big_prose
−0.8 %, huge +1.3 %, pathological +3.1 % — all noise). Reverted.

Direct timing inside `LiveListModelBinding::onParseUpdatedAt`
(instrumented run; instrumentation reverted before commit) found the
real cost is **`BlockWalker::walk`**, which scales O(doc_size) and
runs synchronously on the main thread:

| profile      | walk per call |
|--------------|--------------:|
| mid_prose    |        85 µs  |
| big_prose    |       614 µs  |
| huge         |     3 487 µs  |
| pathological |    16 242 µs  |

This is the dominant remaining main-thread cost on the live keystroke→
frame path for documents ≥ 100 KB. The bench's `phase_model_update`
hides it on heavy profiles because parse-pool coalescing means
`onParseUpdatedAt` fires only once per scenario while the bench tap
attributes phantom small samples to all 180 iters (the brief acknowledged
this dynamic for `parse_work` ↔ `render_frame` but not for
`model_update`). The `phase_render_frame` framing in the brief
("scenegraph rebuild for visible delegates") didn't fit the data —
on heavy profiles `phase_render_frame` is mostly parse_work bleed +
frame-cadence wait, not real scenegraph cost.

**Fix:** dispatch `BlockWalker::walk` to a private QThreadPool inside
`LiveListModelBinding`, post the result back via queued
`QMetaObject::invokeMethod`, drop stale results via a monotonic
generation cookie. LCS-diff + `LiveBlockModel::applyOps` stay on the
main thread (sub-100 µs).

**Architectural decision:** view-side QThreadPool dispatch (option 1A)
rather than a foundation-side canonical block stream (option 1C). The
1A → 1C migration is additive, not a rewrite; promote only when a
second block-walking view exists or a split-view UI ships paying
duplicate walks. See `docs/specs/2026-04-30-blockwalker-threading-decision.md`.

**Bench delta (this commit, vs baseline `f7acaab`):**

- `mid_prose / type_end` — `phase_model_update` p50 −94.2 % (135 → 8 µs); `total_ns` p50 −3.9 %.
- `mid_mixed / type_end` — `phase_model_update` p50 −95.5 % (185 → 8 µs); `total_ns` p50 −0.8 %.
- `mid_mixed / paste_4kb` — `total_ns` p50 −5.0 %.
- `huge / paste_4kb` — `total_ns` p50 −3.7 %.
- `pathological / type_end` — `total_ns` p50 −2.1 % (−11 ms).
- `pathological / paste_4kb` — `total_ns` p50 −3.1 % (−20 ms).

The `model_update` win is muted on heavy profiles (`big_prose`+) only
because the bench's tap was already underreporting model_update there
(phantom samples from coalescing). Direct in-slot timing (above)
confirms the equivalent off-thread move on those profiles.

Total wall-time is roughly flat across heavy profiles, as expected:
we're moving work off the main thread, not eliminating it. The
user-visible benefit (main thread freed for keystroke processing
during parse update) doesn't surface in the bench's existing taps;
that's a tier-2 instrumentation follow-up, not a perf claim for this
commit.

Baseline JSON: `docs/bench-baselines/2026-04-30-render-cc63eed-1A-blockwalker-async.json`.
