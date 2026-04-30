# CollabText CRDT — `Global::join` perf handoff

**From:** Markoff `exploration/new-foundation` development.
**Date:** 2026-04-30.
**Audience:** `collabtext` development agents.
**Ask:** This is informational — please look at `CollabText::Crdt::Global::join` if it fits your roadmap. We are not prescribing a fix; you have your own bench harness and your own optimization rounds.

## TL;DR

After three rounds of parse-pipeline optimization in Markoff, **`CollabText::Crdt::Global::join` is now the single largest CPU cost on the main thread of `markoff-view-qml-app`** during a long-doc typing session — 20.87 % of total CPU cycles. The cost is dominated by an inlined `max<unsigned int>` sub-tree, suggesting a linear scan of the version vector. The function does not allocate; the cost is pure compute.

If you have a way to make `Global::join` faster — or a single-replica fast path — Markoff would benefit measurably.

## Context

Markoff calls `MarkoffDocument::applyLocalEdit` once per keystroke. That call routes through `collabtext`'s CRDT layer: insert/delete the edit into the local replica, then merge the local version vector with itself (or with a peer's, when sync is active). The merge is `Global::join`.

In Markoff's `exploration/new-foundation` branch through 2026-04-30, only single-replica editing is exercised — there are no remote peers. `Global::join` is still called per keystroke; we believe it's mostly a self-merge.

## What Markoff has done on its side

Three commits dropped per-keystroke parse-pipeline cost on a 72 KB markdown reproducer:

1. `6040dd2 feat(bench)` — split parse-tier timing into per-phase buckets so we could see where time was actually going.
2. `96bb42c perf(parser)` — incremental `buildDocumentQueries`: walk only subtrees that overlap a tree-sitter changed range, carry the rest forward shifted. `phase_queries` dropped 14-18× across the bench corpus.
3. `5568c93 perf(parser)` — replaced the O(N²) inline-tree matcher with a hash-map lookup, and cached old inline ranges across `parseIncremental` calls so we don't re-walk the old block tree. `phase_parse_inline` dropped 36-50 % on big-region profiles.

Cumulative effect on a 2 MB pathological document, per keystroke wall time: −18 %.

These are all parse-side changes. Nothing CRDT-related changed.

## The before/after that motivates this handoff

`perf record -F 99 -g` against `markoff-view-qml-app` while a human types ~100 wpm into a 72 KB markdown document for 30 seconds. Same reproducer, same conditions, same machine in both runs.

| | 2026-04-28 (pre-optimization) | 2026-04-30 (post-Stage-1.2) |
|---|---|---|
| Main-thread CPU share | 67.3 % | 97.2 % |
| Parse-worker thread CPU share | 32.5 % | **2.6 %** |
| `Global::join` % of total CPU | 5.5 % | **20.87 %** |
| `ts_lex` / `ts_parser_parse` % | ~6 % combined | absent from top 30 |
| Render scenegraph + glyph + GPU % | ~14 % | ~25 % |
| libc allocator unresolved % | ~7 % | ~26 % |

The main-thread share grew because the parse-worker share collapsed; that's expected. But the absolute cycles spent in `Global::join` did not go down — they grew. The user types into a more responsive UI, so they make more keystrokes per second; each keystroke still pays the full CRDT join.

## Call-graph evidence

```
20.87%  CollabText::Crdt::Global::join(CollabText::Crdt::Global const&)
        │
        ├── 12.58 %  max<unsigned int> (inlined)
        │            join (inlined)
        │            └── 10.56 %  ...
        │
        └── 8.25 %   join (inlined)
                     └── 7.11 %  ...
```

Read literally: roughly 60 % of `Global::join`'s self-cost lives in a sub-tree whose hottest leaf is an inlined `std::max<unsigned int>`. That's the shape of a linear scan over an integer-valued vector taking element-wise maxes — i.e. a version-vector merge implemented as a flat loop.

## Hypothesis (entirely speculative, take or leave)

If the version vector grows monotonically — one entry per replica observed, ever — then `Global::join` for a long-running single-replica session may be merging two vectors of length M where M > 1 even though only one replica is ever active locally. Whether M is small or growing is something the CollabText agents will know better than we do.

A single-replica fast path that skips the join entirely (or short-circuits when both sides have the same singleton replica id) would be one option. Replacing the linear scan with a different data structure (sorted small-vector with `std::merge`-like logic; or a flat hash with per-replica ids) is another. We have no opinion on which is right.

## How to measure on your side

CollabText has its own bench harness. The relevant signal is wall time of `Global::join` against vectors of varying sizes — single-replica steady state, multi-replica converging state. Markoff's bench can't measure this directly; we can only see the aggregate.

If you do land a change, Markoff would re-run:

1. The full parse-tier matrix (`./build-dev/bin/markoff-bench-parse --out …`). Tier-1b - Tier-1 is the ParsePool overhead bucket which includes one `applyLocalEdit` per iter; if `Global::join` gets faster the Tier-1b numbers should drop, especially on small profiles where pool overhead is a large share (currently 35 % of total on `tiny`).
2. A new `perf record -F 99 -g` run with the new `collabtext` build, same reproducer. The expected change: `Global::join` % drops; the freed cycles surface as either more keystrokes per second or measurable wall-time reduction.

Markoff's `exploration/new-foundation` branch tag at the time of this handoff is in commit `5568c93`. The perf data file used here lives at `docs/bench-baselines/2026-04-30-perf-typing-stage1-2-after.data` in that branch — readable with any standard `perf report` invocation.

## Out of scope for this handoff

- The 26 % allocator share. We don't know yet whether it's reached primarily through CRDT, scenegraph, or per-keystroke QString work. If `Global::join` gets cheaper, we can re-profile and the allocator picture should clarify.
- Markoff render-tier work. We're starting Tier-2 (offscreen QPA) instrumentation locally; that's independent of `collabtext`.
- API or behavioural changes. Markoff does not need a behaviour change — same `Global::join` semantics, just faster.

Thanks for taking a look.
