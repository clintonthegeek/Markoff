# BlockWalker threading decision (1A vs 1C)

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Status:** Decision recorded. Implementation: 1A. Promotion path to 1C documented.

## Context

`LiveListModelBinding::onParseUpdatedAt` runs `BlockWalker::walk(parsed->sourceText())` synchronously on the main thread every time a parse completes. Direct timing (instrumented run, reverted before commit) measured the walker at:

| profile      | walk per call |
|--------------|--------------:|
| mid_prose    |        85 µs |
| big_prose    |       614 µs |
| huge         |     3 487 µs |
| pathological |    16 242 µs |

Scales O(doc_size). The walker does `source.split('\n')` and runs per-line regexes over the entire document on every parse.

The bench's `phase_model_update` p50 hides this on heavy profiles because parse-pool coalescing means `onParseUpdatedAt` fires only once per scenario while the bench attributes phantom small samples to all 180 iters. Direct in-slot timing is the reliable signal.

This is the dominant remaining main-thread cost on the live keystroke→frame path for documents ≥ 100 KB. (`phase_render_frame` on heavy profiles is mostly parse_work bleed + frame-cadence wait, not real scenegraph cost — see `2026-04-30-optimization-baseline-and-plan.md` for the full data.)

## Options considered

Three sub-architectures all share the goal of getting the walker off the main thread. They differ in *where* the walker runs and *who owns it*.

### 1A — View-side QThreadPool dispatch (in-binding)

`LiveListModelBinding` captures `sourceText()` + `atVersion` on `parseUpdatedAt`, submits `BlockWalker::walk` to `QThreadPool::globalInstance()`, posts the result back via `QMetaObject::invokeMethod(Qt::QueuedConnection)`. An in-flight version cookie drops stale results. LCS-diff + applyOps stay on the main thread (sub-100 µs even on pathological).

- **Lives in:** `markoff-view-qml` only. Foundation untouched.
- **N views, N walks per parse.**
- **+1 frame of model-update latency** (handoff cost). On heavy profiles `parse_work` is already 60–240 ms, so the extra frame is invisible.

### 1B — Foundation parse-result post-processor hook

Foundation gains a `ParsePool::registerPostProcessor(...)` API. Each view registers a worker-side projection function; results ride on the parsed Document via a generic projection bag.

- **Lives in:** new foundation API + per-view registration.
- **Awkward type erasure.** Every consumer downcasts to its concrete projection type.
- **Reverts poorly** once shipped.

### 1C — Foundation owns a canonical block stream

`Markoff::Document` exposes `QList<RawBlock> blocks()` derived from tree-sitter's top-level node sequence on the worker thread. View walkers degrade to per-block O(1) enrichers running on the main thread.

- **Lives in:** new foundation field + worker-side population + view-side thin enrichers.
- **One walk shared by N views.**
- **Forces a common block shape.** Different views must agree on the `RawBlock` schema or fall back to bypassing it.
- **Reuses tree-sitter's existing block boundaries** — no regex line-split work duplicated.

## Decision: 1A now

We're shipping 1A. The smallest, most reversible change that fixes the observed main-thread cost.

### Why 1A wins today

1. **Only one block-walking view exists on this branch.** `LiveView` is the sole consumer. Reading view, outline view, etc. either don't exist yet or are served by the foundation's existing `DocumentQueryResult` (headings/links/tags). 1C's "shared decomposition" optimises a redundancy that isn't paid.
2. **The 1A → 1C migration is additive, not a rewrite.** If we later land a foundation-side `RawBlock` list, `BlockWalker` stops doing regex line-splitting and instead iterates the foundation's `RawBlock`s, decorating each. The "dispatch walk to thread pool, post back, version-cookie" plumbing in `LiveListModelBinding` survives unchanged.
3. **1A imposes zero shape on future views.** A reading view with a wholly different decomposition (HTML-rendered blocks, inline-math expanded, callouts split out) doesn't have to fit a pre-baked foundation projection.
4. **Reverts cleanly.** Single-file change; no foundation API to deprecate if we want to roll back.

### What we accept by picking 1A

- **Duplicate walks if a future split-view UI shows two block-walking views simultaneously.** Worst-case cost is one extra ~16 ms walk per parse on pathological docs. Acceptable until a real split-view UI exists.
- **+1 frame of latency** between parse-completion and model update. Quantitatively negligible vs parse work itself.
- **Bench instrumentation needs an extra tap** ("walk dispatch → walk done") so we don't lose visibility into walker cost. Cheap to add.

## When to promote to 1C

Promote *only* when **at least one** of these triggers fires:

1. **A second block-walking view exists in the tree.** The reading view (Phase-2 follow-up) is the obvious candidate. An outline view that needs more than `DocumentQueryResult.headings` would also count. A view that consumes `DocumentQueryResult` only does *not* count — it doesn't walk blocks.
2. **A split-view UI ships** that displays two block-walking views simultaneously, paying duplicate walks per parse.
3. **The reading view's decomposition shape converges with the live view's** — i.e. they want the same block boundaries with different per-block enrichments. (If they diverge — e.g. reading expands inline math to its own block while live keeps it inline — 1C buys nothing.)

If trigger (1) fires but (3) doesn't, **stay on 1A**. The two views having divergent shapes means a foundation-side common decomposition would be the wrong abstraction; each view's walk stays in its own library.

## When to reject 1C even when triggered

Reject 1C and stay on 1A if any of:

1. **Views want fundamentally different block sets.** Reading view splits inline math into its own block; live view keeps it inline → no shared boundary set. 1C becomes a forced average that serves neither well.
2. **The foundation's `RawBlock` shape would need to grow per new view.** If every new view forces a new field on `RawBlock`, the foundation is paying view-shape rent. View-side walks isolate that.
3. **Walk cost becomes incremental.** If `BlockWalker` becomes incremental (re-walks only the changed line range — see "Future optimisation" below), per-walk cost drops to O(edit_size). At that point the duplicate-walk cost in 1A is sub-millisecond and 1C's "share the walk" win is negligible.
4. **The bench shows main-thread apply time stays sub-millisecond on every profile after 1A.** If 1A delivered the win and the main-thread budget is healthy, 1C is a layering cost without a perf justification.

## Reference: where this gets re-evaluated

- When a new view is added to `markoff-view-qml` or a new view library is created (e.g. `markoff-view-reading`): re-read this doc *before* writing the view's parse-update handler. Default to 1A pattern; consider 1C only if the triggers above fire.
- When `BlockWalker` is touched: if making it incremental, note it weakens the case for ever promoting to 1C.
- When the bench matrix is re-baselined: if main-thread apply time creeps back above ~1 ms p50 on any profile, that's a signal to either fix the regression or revisit 1C.

## Future optimisation orthogonal to this decision

**Incremental BlockWalker.** Cache prior records keyed by line range; on each parse, walk only the changed line range and reuse the rest. Reduces per-call cost from O(doc_size) to O(edit_size). Doesn't affect the 1A vs 1C choice — works under either. Costs more code. Defer until 1A's main-thread budget is no longer enough.

**Tree-sitter block boundary reuse.** Even within 1A, `BlockWalker::walk` could iterate the parser's top-level nodes instead of doing `source.split('\n')` + regex. Reduces per-call cost without adding caching state. Requires exposing top-level node ranges from `markoff-parser`. A natural ~50% improvement to the off-thread walk; doesn't change the architectural decision either.

## Cross-references

- `docs/specs/2026-04-30-optimization-baseline-and-plan.md` — the running optimization plan; this decision lands in the Stage 2 work for candidate #2.
- `docs/specs/2026-04-29-live-render-design.md` — the live-view architectural invariants (see §4); 1A respects all of them.
- `libs/markoff-view-qml/src/LiveListModelBinding.cpp` — implementation site.
- `libs/markoff-view-qml/src/BlockWalker.cpp` — walker (untouched by 1A).
