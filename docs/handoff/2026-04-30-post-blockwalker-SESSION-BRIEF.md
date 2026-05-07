# Markoff render-tier optimization — post-BlockWalker SESSION BRIEF

Branch: `exploration/new-foundation`
Worktree: `/home/clinton/dev/Markoff/.worktrees/foundation-exploration/`
Last commit: `f3bbdf6 perf(view-qml): dispatch BlockWalker off main thread`

## Required reading, in order

1. `docs/specs/2026-04-30-optimization-baseline-and-plan.md` — running plan.
   Read the "Stage 2 candidate #1 outcome" and "Stage 2 candidate #2"
   sections added in `f3bbdf6`. They record what landed, what was retired
   without code change, and which assumptions in the prior brief were
   wrong.

2. `docs/specs/2026-04-30-blockwalker-threading-decision.md` — load-bearing
   architectural decision (option 1A view-side QThreadPool dispatch). Read
   the "When to promote to 1C" and "When to reject 1C even when triggered"
   sections before considering ANY change to BlockWalker, the binding's
   walk dispatch, or the foundation's parse-result shape.

3. `docs/handoff/2026-04-30-render-optimization-SESSION-BRIEF.md` — the
   prior brief. Treat its quantitative claims with skepticism: candidate
   #1 (the "5.15 ms vs 0.36 ms live/source gap") was a profile mix-up
   and was retired. Candidate #2's "scenegraph rebuild for visible
   delegates" framing did NOT fit the data; the real cost was BlockWalker
   on the main thread, now fixed.

4. `libs/markoff-bench/README.md` § Render-tier phase taxonomy — refresher
   on the six phases.

5. `docs/TODO.md` — top entry is a one-shot revert task (small-replicaId
   workaround in the bench now that collabtext fixed the SBO bug).
   Independent of perf work; do it as a separate commit if you take it on.

## Your immediate task

Re-baseline the render bench at `f3bbdf6` and decide the next priority
from fresh data. Do NOT pick a candidate before re-baselining — the
prior brief's priority ordering is stale.

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark"   # fast-loop, must be green
./build-dev/bin/markoff-bench-render --mode live --git-sha f3bbdf6 \
    --out docs/bench-baselines/2026-04-30-render-f3bbdf6-postblockwalker.json
```

Diff against `docs/bench-baselines/2026-04-30-render-cc63eed-1A-blockwalker-async.json`
(the just-landed baseline) AND `docs/bench-baselines/2026-04-30-render-f7acaab-phased.json`
(the original phased baseline). Look at: `phase_apply_edit`,
`phase_render_frame`, and `total_ns` p50 deltas across the matrix. Note
any phase whose share of `total_ns` shifted by >10 % — that's where to
look next.

## Candidates to consider (in no particular order — let data pick)

### A. Tier-2 instrumentation upgrade

The bench's `phase_model_update` tap underreports on heavy profiles
because parse-pool coalescing makes `onParseUpdatedAt` fire only once
per scenario while the bench attributes phantom 1–2 µs samples to all
180 iters. Now that BlockWalker runs off-thread, the same artefact
will hide off-thread walk cost from view.

Split: `tWalkDispatchedNs` (main-thread post-`onParseUpdatedAt`
synchronous return) and `tWalkAppliedNs` (after the post-walk queued
lambda's applyOps completes). New phase `phase_walk_work` = dispatched
→ applied. Sums must still equal `total_ns` by construction.

### B. paste_4kb `phase_apply_edit` ~100 ms

Collabtext-owned (`Buffer::apply_local_edit` for a 4 KB single
insertion). Verify it's still ~100 ms post-`f3bbdf6`; if so, write
an addendum to `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`
noting the floor. DO NOT implement Markoff-side chunking — that would
re-introduce multi-edit batching the CRDT was designed to avoid.

### C. `phase_render_frame` revisited with new data

Pre-`f3bbdf6` it was confounded by main-thread BlockWalker bleed. With
BlockWalker off-thread, `render_frame` should now mean closer to what
its name says. If it still scales linearly with doc size, the brief's
"ListView delegate / scenegraph" hypothesis becomes plausible again —
but only if the data supports it after re-baselining. `perf record`
against `markoff-view-qml-app --live <100 KB doc>` typing at end is
the disambiguating method (look for `prepareAlphaBatches`,
`nodeChanged`, `uploadMergedElement`, QtGui text-layout symbols).

### D. Incremental BlockWalker

Cache prior records keyed by line range; re-walk only the changed line
range on each parse. Reduces per-call cost from O(doc_size) to
O(edit_size). Currently off-thread so user-invisible until off-thread
walks pile up. Check the new baseline first — if the off-thread walk
pool isn't a bottleneck, defer.

### E. Backlog: phase_pool_queue, phase_signal_hop

Sub-100 µs in every prior baseline. Don't touch unless the new baseline
surfaces them.

## Hard constraints

- **One commit per candidate.** Before/after JSON diff in the commit
  body. Same pattern as Stage 1.1 / 1.2 / 1A.
- **If a candidate's bench numbers don't move, REVERT and write up why.**
  Don't ship a perf claim that doesn't show in numbers — `f3bbdf6`'s
  `reuseItems` trial set the precedent.
- **No production-side perf instrumentation.** The opt-in `RenderPhaseTaps`
  pattern in `libs/markoff-core/include/markoff-foundation/RenderPhases.h`
  is the model: pointer-default-null, zero overhead in production.
- **Don't undo the bench's small-replicaId workaround as part of perf
  work.** `docs/TODO.md` flags it as a separate one-shot. Doing both at
  once introduces two simultaneous variables.
- **BlockWalker stays on its own thread (option 1A).** Don't promote to
  1C unless the triggers in
  `docs/specs/2026-04-30-blockwalker-threading-decision.md` fire AND
  the reject criteria don't apply. Document any change to that decision
  in the same spec.
- **Build with `cmake --build build-dev -j 8`.** Never bare `-j`
  (saturates the user's CPU per `~/.claude/CLAUDE.md`).

## Stop condition

After landing one candidate (or recording a "no-op, here's why"
outcome), re-run the matrix and STOP. The data may shift enough that
the next candidate's priority changes — re-prioritize against fresh
data, not against this brief.

## What "done" looks like for this session

Either:

- **(a)** one commit on top of `f3bbdf6` with a perf claim backed by
  JSON diff, OR
- **(b)** a written outcome ("verified that X is no longer the hot
  spot, here's the new bench data, here's the next candidate's
  hypothesis") appended to
  `docs/specs/2026-04-30-optimization-baseline-and-plan.md`.

Both are acceptable.
