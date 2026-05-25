> **Status: superseded.** This brief's quantitative claims were retired by `f3bbdf6` (BlockWalker off main thread). Successor brief: `docs/handoff/2026-04-30-post-blockwalker-SESSION-BRIEF.md`. Do not execute.

# Markoff render/CRDT optimization — fresh-context SESSION BRIEF

Branch: exploration/new-foundation
Worktree: /home/clinton/dev/Markoff/.worktrees/foundation-exploration
Most recent commits to read first: 80074b7 (bench phase splits), 91719e7
(foundation taps), and the "Task 2.B" section of
docs/specs/2026-04-30-optimization-baseline-and-plan.md.

## Required reading (in order)

1. docs/specs/2026-04-30-optimization-baseline-and-plan.md — the running
   optimization plan. Read the "Task 2.B" subsection (under "Stage 2 —
   Re-prioritised against the perf-record data") for the post-instrumentation
   findings. Earlier sections explain how the parse tier was attacked and
   what the bench harness provides.
2. libs/markoff-bench/README.md, "Render-tier phase taxonomy" section —
   the six phases and what each measures.
3. docs/bench-baselines/2026-04-30-render-f7acaab-phased.json — the
   baseline you'll diff against.
4. docs/handoff/2026-04-30-collabtext-sbo-regression-repro.md — context
   for the bench's small-replicaId workaround. Don't undo it without
   confirming with the collabtext team that 0008577 is fixed.

## Workflow

- Build with `cmake --build build-dev -j 8`. Never bare `-j`.
- `ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark"` is the
  fast inner loop (~2 s, 85 tests).
- Per perf claim: cite a before/after JSON diff in the commit body, same
  pattern as the parse-tier Stage 1.1 / 1.2 commits (96bb42c, 5568c93).

## Candidates, in order of decreasing priority

### 1. Live-mode `phase_apply_edit` is 14× source-mode (~4.8 ms gap on mid_prose / type_end)

**The data:** mid_prose / type_end at p50 — apply_edit is 5.15 ms in live
mode but 0.36 ms in source mode. Same CRDT, same QTextDoc rebuild, same
KSyntaxHighlighter rehighlight in both modes (SourceTextDocumentBinding
runs unconditionally; LiveListModelBinding subscribes to parseUpdatedAt
*after* applyLocalEdit returns, so it's not in apply_edit by design).

**Hypothesis (unconfirmed):** something synchronous on the
contentsChanged DirectConnection path is doing extra work when the
source TextArea is `enabled: false` / `visible: false` — possibly
QTextDocument layout that QQuickTextDocument can short-circuit when its
host is hidden, or a redundant binding evaluation in
SourceTextDocumentBinding's Qt↔Markoff offset round-trip.

**Why it's #1:** highest leverage per line of code. Live mode is the
walking-skeleton future-default; trimming 4-5 ms off every keystroke
there is a felt UX win without touching CRDT or the scenegraph.

**Method:** instrument SourceTextDocumentBinding with PhaseTimer-style
guards inside its onMarkoffEdits / onQtContentsChange. Run the bench
in source vs live mode side by side, attribute the gap. If it's the
hidden-TextArea hypothesis, the fix is a `if (!visible) return` early-
exit gate or rewiring the binding so it doesn't run when its host
isn't rendered. Confirm by re-running the matrix.

**Out of scope:** don't refactor the binding. Add the smallest possible
opt-out gate.

### 2. `phase_render_frame` dominates above 16 KB and scales linearly with doc size

**The data:** render_frame p50 goes from 4.8 ms (mid_prose) to 524 ms
(pathological), tracking doc size linearly. On big_prose (100 KB) it's
16 ms — the biggest single UI cost outside paste. apply_edit, model_update,
pool_queue, signal_hop are all small.

**The cause (informed guess, not confirmed):** ListView in LiveView.qml
has no virtualization tuning. Every paragraph is its own delegate, each
allocating a TextEdit. For 100 KB / ~1000 paragraphs that's a lot of
scenegraph nodes per frame. Confirm with `perf record` while typing,
specifically looking at `prepareAlphaBatches` / `nodeChanged` /
`uploadMergedElement` (the same symbols flagged in the post-Stage-1.2
perf-record).

**Method:**
  a. perf-record against `markoff-view-qml-app --live <100KB doc>` typing
     at the end. Confirm the scenegraph cost is delegate-driven, not
     highlighter-driven (the 04-28 highlighter hypothesis was already
     weakened by the apply_edit data; this confirms it).
  b. Either tune ListView's `cacheBuffer` / `reuseItems` so off-screen
     delegates don't rebuild on every parse, or switch the delegate
     factory to a lighter primitive than TextEdit for the common
     paragraph case (most paragraphs don't need TextEdit-level features
     — read-only Text would do until editing-spec lands).

**Out of scope:** the per-block delegate model itself is load-bearing for
Phase 2 editing-spec; don't restructure it. Optimize within the existing
shape.

### 3. `paste_4kb` apply_edit is ~100 ms invariant of doc size — CRDT write path

**The data:** paste_4kb's apply_edit is 96-124 ms on every profile, from
tiny to pathological. parse_work and render_frame on top of that get the
total to 100-670 ms. The apply_edit floor is set by
`Buffer::apply_local_edit` for a single 4 KB insertion.

**Why it's #3 not #1:** out of Markoff's mandate. CRDT belongs to the
collabtext team and is paid each paste regardless of doc size. But it
*is* the worst single keystroke-equivalent in the corpus — worth
flagging back to the collabtext team along with the SBO regression
already documented.

**Method:** write a brief addendum to
docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md noting
the paste_4kb apply_edit floor. Don't implement. Markoff-side
mitigations like chunking a paste into 256-byte sub-edits are an
*application-level* workaround that re-introduces multi-edit batching
the CRDT was designed to avoid; only consider if collabtext can't fix.

### 4. `phase_model_update` is ≤ 200 µs — confirm it's not hiding behind QML async

**The data:** model_update p50 is 1-200 µs across every profile, but
parse_work + render_frame combined leave a ~3 ms gap on mid_prose live
mode that doesn't fit any phase (the apply_edit anomaly partially
explains it). It's possible BlockWalker + AstBlockDiff + applyOps work
is genuinely fast and the missing time is QML's async polish phase
folded into render_frame.

**Why it's #4:** if model_update is genuinely cheap, items 1 and 2 are
where the effort goes. If it's hidden behind QML's polish, the model
update path is a quiet hot spot worth a focused look.

**Method:** add `qDebug()` timing inside
LiveListModelBinding::onParseUpdatedAt around BlockWalker / AstBlockDiff
/ applyOps. One-shot run on big_prose live mode; if the slot really
takes < 200 µs even on 1000 paragraphs, drop the candidate. If it's
multi-millisecond and the bench is missing it, fix the bench (likely
move the tModelDone tap inside LiveListModelBinding rather than on the
parseReady lambda exit).

### 5. (Backlog) phase_pool_queue and phase_signal_hop

Both are sub-100 µs across every profile. Listed last because there's
no signal of cost. Don't touch unless something else surfaces them.

## Hard warnings

- **No production-side perf instrumentation.** The opt-in
  `RenderPhaseTaps` pattern is the model: pointer-based, null-default,
  zero overhead in production. If a candidate needs new instrumentation,
  follow that pattern and gate it through the bench library.
- **Don't undo the bench's small-replicaId workaround** until the
  collabtext team confirms 0008577's heap-promotion crash is fixed.
  Reproducer in docs/handoff/2026-04-30-collabtext-sbo-regression-repro.md.
- **Don't act on Stage 2.A (CRDT `Global::join`).** The collabtext
  team owns it and just landed an SBO win on it; pile-on changes will
  collide.
- **Don't restructure LiveView's per-block delegate model.** It's
  load-bearing for the editing-spec follow-up.
- **One commit per candidate.** Same pattern as Stage 1.1 / 1.2.
  Before/after JSON diff in the commit body. Don't bundle multiple
  candidates into one commit.

## Verification before "done"

```
cmake --build build-dev -j 8
ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark" \
    --output-on-failure
./build-dev/bin/markoff-bench-render --git-sha <new-sha> \
    --out docs/bench-baselines/2026-04-30-render-<new-sha>-<candidate>.json
# Diff against 2026-04-30-render-f7acaab-phased.json. Cite specific
# (profile, scenario, phase) rows in the commit body. The expected p50
# delta should be on the phase your change targeted; if it's elsewhere,
# stop and explain.
```

If a candidate's bench numbers don't move, **revert and write up why**
in the plan doc. Don't ship a perf claim that doesn't show in the
numbers — that's the rule the parse-tier work followed (see Stage 1.3
demotion as the precedent).

## Stop conditions

After landing candidate #1 (live-mode apply_edit), re-run the matrix
and stop. The render data may shift enough that #2's prioritization
changes — re-prioritize against fresh data, not against this brief.
Same pattern Stage 0 → Stage 1.1 → Stage 1.2 followed.
