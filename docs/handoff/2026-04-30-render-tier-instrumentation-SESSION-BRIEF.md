# Render-tier instrumentation — fresh-context SESSION BRIEF

**Read this first.** Self-contained briefing for a fresh-context Claude session picking up render-tier (Tier 2) bench instrumentation on the `exploration/new-foundation` branch.

**Status:** Stages 0 / 1.1 / 1.2 of the optimization plan are committed. Parse-pipeline is no longer the bottleneck (see baseline below). Render-tier needs phase splits before further work can be motivated.
**Branch:** `exploration/new-foundation`.
**Worktree path:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`.
**Master is untouched and stays so.** Exploratory branch.

---

## TL;DR

Do for the render bench what Stage 0 did for the parse bench: split the single `phase_render_frame` bucket into four named phases that attribute cost to layers of the UI pipeline.

Then re-run, write up the resulting profile shape, and stop. Optimisations are out of scope for this work-unit — the goal is to make the bench answer "which phase" so a follow-up work-unit can act.

---

## 1. Why this is the next move

A 30-second `perf record` against `markoff-view-qml-app` on a 72 KB doc, post-Stage-1.2 (commit `5568c93`):

| Bucket | Share of total CPU |
|---|---|
| libc / allocator (unresolved) | 26.31 % |
| **CRDT `Global::join`** | 20.87 % (handed off to `collabtext` agents) |
| **Qt Quick scenegraph** | 15.39 % |
| **QtGui text layout** | 7.69 % |
| GPU driver | 2.30 % |
| Tree-sitter | 1.21 % |
| Markoff app code | 0.70 % |

Render-side cost (Qt Quick + QtGui-text + GPU) is about 25 % of total CPU. The 04-28 perf baseline's hypothesis was "KSyntaxHighlighting rehighlight scope drives scenegraph rebuilds." The post-Stage-1.2 perf data **does not show KSyntaxHighlighting symbols in the top 30** — meaning either the hypothesis is wrong, or the highlighter cost is inlined into scenegraph call paths and not surfacing as its own line.

The render bench (`apps/bench/markoff-bench-render.cpp`) currently times the entire keystroke→`frameSwapped` window as one bucket. Splitting that bucket is the prerequisite to confirming or refuting the highlighter hypothesis and to localising the 23 % render+text cost.

This is exactly the situation Stage 0 was in for the parse bench: a single `phase_parse_block` bucket that had to be split before any Stage 1 candidate could be ranked by data instead of guess. Same pattern here.

---

## 2. Required reading (in order)

1. **`docs/specs/2026-04-30-optimization-baseline-and-plan.md`** — the optimization plan. Read the "Post-Stage-1.2 perf-record outcome" and "Stage 2" sections; you're executing what's described as Task 2.B.
2. **`docs/specs/2026-04-29-parse-render-bench-design.md`** — the bench harness design spec. The Tier-2 metrics it described (`pool_queue`, `signal_hop`, `model_update`, `frame`) are the targets you'll be wiring.
3. **`libs/markoff-bench/README.md`** — usage and caveats of the bench harness. Note caveat 3: offscreen QPA ≠ real GPU; numbers are useful for relative comparisons across commits, not absolute UX claims. Honest about that in any commit-body claims.
4. **`apps/bench/markoff-bench-render.cpp`** — the Tier-2 frontend you'll be instrumenting. Already imports the `MarkoffEditor` QML module and drives `applyLocalEdit` directly to bypass key-event delivery.
5. **`libs/markoff-bench/include/markoff-bench/PhaseTimer.h`** — the existing `Phase` enum already has `PoolQueue`, `SignalHop`, `RenderFrame` slots reserved. You'll add `ModelUpdate` to that enum (or fold the model-update share into one of the existing slots; see §4).
6. **`libs/markoff-foundation/src/IncrementalParseSession.h` + `ParsePhases.h`** — the foundation's `ParsePhaseTable` is the reference pattern for opt-in, allocation-free instrumentation. Tier 2 will follow the same shape but at the QML/view boundary.
7. **`libs/markoff-foundation/src/ParsePool.cpp` / `ParsePoolWorker.cpp`** — where pool-queue and signal-hop happen on the foundation side. Already has timestamp infrastructure (Tier 1b uses `QSignalSpy::wait` to capture combined queue+hop time).
8. **`libs/markoff-bench/src/ScenarioRunner.cpp`** — the Tier 1 / Tier 1b reference. The `runPoolParse` function lumps `PoolQueue + SignalHop` into `Phase::SignalHop`. You can either un-lump (split it into two real phases at Tier 1b too) or leave Tier 1b alone and only un-lump at Tier 2. The latter is fewer changes; pick that unless you have a reason.

---

## 3. Goal

For every render-tier `(profile, scenario)` row in the bench JSON, report:

- `phase_pool_queue` — `MarkoffDocument::applyLocalEdit` return → `ParsePool` worker entry.
- `phase_signal_hop` — worker `parsed` signal emit → main-thread `parseUpdated` slot entry.
- `phase_model_update` — `parseUpdated` slot entry → first `LiveBlockModel` `dataChanged` / `modelReset` settled (i.e. model finishes propagating the new parse to the QML view).
- `phase_render_frame` — model settled → next `QQuickWindow::frameSwapped`.

The four should sum to within ±5 % of `total_ns`. The remaining ±5 % is acceptable noise (steady_clock capture overhead, signal-emit / slot-call gaps between phases).

**Exit criterion:** for `mid_prose` and `big_prose` `type_end` and `paste_4kb`, every phase reports a non-zero p50 and the four phases sum to within ±5 % of `total_ns`. Same shape as Stage 0's exit criterion.

---

## 4. Implementation sketch (not prescriptive)

### 4.1 `phase_pool_queue` and `phase_signal_hop`

These already exist conceptually in `ParsePoolWorker`. Two timestamps to capture:

- T_workerEntry: the moment the queued lambda body starts on the worker thread (in `ParsePoolWorker::onParse` or wherever the QMetaObject::invokeMethod target lands).
- T_signalEmit: the moment `Worker::parsed.emit(...)` fires on the worker thread.
- T_signalReceipt: the moment the main-thread connected slot starts.

The bench already records T_scheduleStart (right before `applyLocalEdit`) and T_parseUpdatedReceived (after `QSignalSpy::wait`). To split:

- `pool_queue` = T_workerEntry − T_scheduleStart. Worker-thread side capture; expose via a member or a per-iter slot so the bench can read it.
- `signal_hop` = T_parseUpdatedReceived − T_signalEmit. Capture T_signalEmit as a `QDateTime`-like value carried with the signal payload (or as a bench-only side channel).

Watch out: clock-skew between worker thread and main thread doesn't apply (`std::chrono::steady_clock` is process-global on Linux). But QThread context-switch latency is real and is what `signal_hop` measures.

### 4.2 `phase_model_update`

The boundary here is fuzzier. The model update is whatever happens between `MarkoffDocument::parseUpdated` reaching `markoff-view-qml`'s slot and the QML view being ready to render the new state.

Concretely: `LiveBlockModel::onParseUpdated` (or whatever it's called) runs `beginResetModel` / `endResetModel` (or per-row `dataChanged`). The model-update phase ends when the QML side has consumed the model change and a render is scheduled.

Pragmatic measurement: T_modelUpdateDone = the moment after the model's last signal emission inside the slot. This is approximate — Qt may schedule render asynchronously after model signals — but it gets within tens of microseconds.

If the model update is hard to time precisely, an acceptable fallback is to report `phase_model_update + phase_render_frame` together as a single `phase_view_render` bucket and note in the JSON that the split wasn't achievable. Document the deviation honestly; don't fake a split.

### 4.3 Reporting through to the JSON

The bench `Phase` enum already has slots for these. Either reuse them (reassign meaning at Tier 2) or add a new `Phase::ModelUpdate` slot. Adding a slot is cheaper than retrofitting meaning.

Add a `direct_render_phases` analog to `runPoolParse`'s currently-lumped phase report. The data path is short: each iter populates a per-iter phase array; the iterator reduces to percentiles and the JSON reporter emits.

### 4.4 What not to instrument

Do NOT add timing scopes inside `LiveBlockModel` itself for production builds. The same opt-in pattern as `ParsePhaseTable` applies: a pointer-based tap that's null for production callers, set only by the bench. Production binaries pay zero (one `steady_clock::now()` per scope at most, only when set).

---

## 5. Tests

Stage-0 pattern: a smoke test that asserts every phase reports a non-zero p50 on at least one profile/scenario combination. Add to `libs/markoff-bench/tests/tst_bench_smoke.cpp` (or a new test if Tier 2 deserves its own).

Render-tier offscreen QPA tests are slower than parse-tier — gate the smoke under the `bench` ctest label and keep iteration counts low.

---

## 6. Deliverable

1. A single commit (or two — one for foundation/QML side instrumentation, one for the bench frontend) wiring the phase splits.
2. A new JSON file under `docs/bench-baselines/2026-04-30-render-<sha>-phased.json` containing the post-instrumentation render matrix.
3. An update to `docs/specs/2026-04-30-optimization-baseline-and-plan.md` recording the new render-tier breakdown (which phase dominates per profile, sum-of-phases vs total_ns) and re-prioritising Stage 2.B and 2.C against the data.
4. Stop. Do not act on the data within this work-unit; the optimisation candidates that fall out are a separate conversation with the user.

---

## 7. Tone for the optimisation plan update

Match the style of the existing post-Stage-1.2 entry in `docs/specs/2026-04-30-optimization-baseline-and-plan.md`. Tables of phase percentages per profile/scenario; one paragraph per surprising finding. Don't speculate about fixes you didn't measure.

---

## 8. Hard warnings

- **No production-side perf instrumentation.** Same pattern as Stage 0's `ParsePhaseTable`: opt-in pointer set only by the bench. Confirm every guard is no-op when the table is null.
- **Honest about offscreen QPA.** The offscreen platform isn't a real GPU. Any UX claim ("typing feels faster") needs a real-display perf-record run, not bench numbers. Note this in the commit body and in the plan-doc update.
- **Do not start Stage 2.A (CRDT).** It's been handed off to the `collabtext` agents — see `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`. If you find a CRDT-related opportunity while instrumenting, write it up in that handoff doc as an addendum; don't act.
- **Do not start Stage 2.C (KSyntaxHighlighter).** Whether to act on it is a downstream conversation gated on what the render-tier instrumentation reveals. If after instrumentation `phase_model_update` is dominated by highlighter calls, the answer becomes "yes, attack KF6"; if it's dominated by scenegraph node creation, the answer becomes "no, look at QML delegate cost." Don't presume.

---

## 9. Verification before "done"

Run from the worktree root:

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -j 8 -E "tst_realistic|tst_benchmark" --output-on-failure
./build-dev/bin/markoff-bench-render --profile mid_prose --scenario type_end \
    --out /tmp/render-phased-smoke.json
# Inspect /tmp/render-phased-smoke.json: every render-side phase has p50 > 0,
# phases sum to within ±5 % of total_ns.
```

Then the full matrix per §6.

If your smoke shows phases that don't sum, **stop and diagnose** before running the full matrix (which takes ~10 minutes). The phase splits are wrong if the sum is off by more than a few percent.
