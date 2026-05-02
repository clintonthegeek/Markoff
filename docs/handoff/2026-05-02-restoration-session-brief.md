# Restoration Session Brief — Live Render

**For a fresh agent context picking up the live-render restoration arc.**

**Date authored:** 2026-05-02
**Worktree:** `.worktrees/foundation-exploration/` (you should already be here — if not, `cd` there before doing anything else).
**Branch:** `exploration/new-foundation`. `master` is unrelated to this work and is not the target.

If you are reading this for the first time, finish this document before opening any code. It is ~250 lines; the time investment pays back across every subsequent task.

---

## 1. What this restoration is

The live render widget (the WYSIWYG-ish Markdown editor surface) is being rebuilt. The diagnostic, design, and implementation plan for the *first phase* of that rebuild are already complete. Subsequent phases get planned as their predecessor lands.

The work is collaborative: the user does dogfood testing between phases; you (or whichever agent context picks up next) do implementation and planning. The user has authority over architecture and design changes; you have authority over implementation choices within the spec.

The restoration is expected to span 18–30 weeks of agent-time across 10 phases (R1–R10). Don't try to compress it. Phases land one at a time, with dogfood gates between them.

---

## 2. Read order

1. **`docs/restoration-status.md`** — read in full. The TL;DR tells you what to do next. The phase board tells you where everything stands. The dogfood log tells you what the user has surfaced as broken.
2. **`docs/specs/2026-05-02-live-render-restoration-design.md`** — the architecture spec. Read §0–§5 carefully; skim §6–§10; bookmark §11 (phasing) for reference. This is the *contract* — it does not change without an entry in the spec-amendment log of the status doc.
3. **The active plan** (whichever the status doc's TL;DR points at). The plan tells you exactly which files to edit and what code to write.
4. **The audit (`docs/2026-05-02-live-view-architectural-audit.md`)** — read once for context on *why* the spec is shaped the way it is. The "six sources of truth → one principle" framing is the conceptual key.

You do **not** need to read the D-evolution proposal (`docs/specs/2026-05-02-d-evolution-proposal.md`) for current work. It's a long-term direction document for the `collabtext` maintainers. Skip unless someone asks about D.

You do **not** need to read prior session briefs in `docs/handoff/` (the older ones predate the restoration arc and reflect a different design direction). They exist for archaeology, not orientation.

---

## 3. Working protocol

### 3.1 Per-task discipline

Each plan task follows this cycle:

1. **Read the task in full** before editing. Skip nothing — including the test code, including the commit message templates.
2. **Write the failing test first**, where the plan calls for it. Run it; confirm it fails for the reason the plan says it should.
3. **Implement**. Show your work via real diffs, not summaries.
4. **Run the test**. Confirm it passes.
5. **Run the regression suite** if the plan calls for it (typically `ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8`).
6. **Commit per the plan's commit message template** — these are concrete, not boilerplate. Use them verbatim or close-to-verbatim. Always include the `Co-Authored-By` trailer matching the project style.
7. **Update `docs/restoration-status.md`**: append to the recent-changes log; if a phase status transitions (`pending` → `in-progress` → `complete`), update the phase board. **Bundle this update into the same commit as the code change** so status never drifts.

### 3.2 Build cadence

The build system uses CMake presets. The build directory is `build-dev/`. Cap parallelism at `-j 8` (the user's machine saturates at higher levels).

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON   # one-time
cmake --build build-dev -j 8                                   # incremental
ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8
```

`tst_realistic` and `tst_benchmark` are slow (~90s and ~7min respectively); they run in CI but not in the inner loop. Exclude them per the project CLAUDE.md guidance. `tst_view_qml_live_view_qml` has a known pre-existing baseline of 9 pass / 3 fail / 2 skip — that is documented as out-of-scope-for-restoration in the repair plan and should not block your work.

### 3.3 Frequency of commits

Commit at every plan-prescribed boundary. The plans are written to make commit boundaries natural (failing-test commit; implementation commit; per-sub-plan commits). Do not bundle multiple sub-plans' work into one commit; do not split a single sub-plan's implementation across multiple commits unless the plan explicitly says to.

If you find yourself wanting to deviate from the plan's commit structure, that's a signal — read §3.5 below.

### 3.4 Dogfood gates

Phases R4–R10 have dogfood scripts in spec §10.3 that the user runs manually (we don't try to automate UI testing; `QTest::keyClick` doesn't reproduce the async parse round-trip's timing reliably enough). The user runs these on `markoff-live-render-app` after a phase's implementation is done.

- The phase enters `dogfood` status after all the phase's plans are merged.
- The user runs the script and posts feedback in the dogfood log of `restoration-status.md`.
- If the feedback says "passes," the phase moves to `complete`, and you (or the next session) write the next phase's plan.
- If the feedback surfaces a regression, **stop and diagnose**. Don't paper over it; don't move on to the next phase. The dogfood signal is the highest-priority signal in this arc.

R1 has no dogfood script (it's foundation + scaffold; nothing user-visible). R1 acceptance is purely "tests green."

### 3.5 When the plan and reality disagree

Plans are written ahead of execution; reality occasionally surprises. When you encounter:

- **A small discrepancy** (a file path that's slightly different, a CMake macro named differently, a test helper you couldn't find at the exact line cited) — adapt and proceed. Note the discrepancy in your commit message so the trail stays honest.
- **A larger discrepancy** (the foundation API the plan calls expects different arguments than the plan describes; an earlier task's output doesn't match what a later task assumes) — stop. Diagnose the root cause. Update the plan inline if the change is clarifying-not-changing. If the change is *changing*, escalate per §3.6.
- **A spec-level contradiction** (the spec asserts X; implementation reveals X is unworkable or wrong) — never silently rewrite the spec. Open a spec-amendment entry in the status doc per the template; surface it to the user; wait for approval before editing.

The order of trust is: **dogfood > tests > spec > plan**. When two disagree, the higher-trust signal wins. The plan is the lowest-trust because it's written ahead of contact with reality.

### 3.6 Spec-amendment protocol

The restoration spec is *the* design contract. Edits to it are not casual. Process:

1. Identify the contradiction concretely. Cite spec section + plan task + observed reality.
2. Propose the edit in the **Spec-amendment log** of `docs/restoration-status.md`, following the template there.
3. **Stop work on the affected task** (it's now blocked on the amendment).
4. Surface the amendment to the user explicitly: a clear message saying "the spec asserts X; implementation reveals Y; I propose amending §N to read Z; OK to proceed?"
5. Wait for user response. Do not amend the spec without their explicit approval.
6. Once approved, edit the spec inline, commit the spec change in its own commit (subject: `docs(spec): R<phase> amendment — <short title>`), record the commit SHA in the amendment log entry, and resume.

Amendments to the D-proposal follow the same protocol. Amendments to a *plan* are lower-stakes — fix inline and note in the commit message — unless the plan-amendment cascades into a spec-level change (in which case it's a spec amendment, not a plan amendment).

### 3.7 Plan generation for the next phase

When a phase reaches `complete`:

1. Confirm the dogfood log shows user sign-off (or, for R1, that all sub-plan tests are green).
2. Confirm the phase board is updated.
3. Generate the next phase's plan(s):
   - Invoke `superpowers:writing-plans` skill.
   - Source the next phase's scope from spec §11 R<N+1>.
   - Decide whether the phase is one plan or multiple sub-plans (R1 was three because it had three independent sub-projects; R2+ are usually one plan unless the phase has natural sub-divisions).
   - Follow the skill's task-granularity discipline: bite-sized tasks, full code blocks, exact paths, TDD cycles, frequent commits, no placeholders.
   - Save to `docs/plans/2026-05-02-live-render-r<N>-<topic>.md` (consistent date prefix; consistent prefix `live-render-r<N>-`).
4. Self-review per the writing-plans skill's checklist (placeholder scan, type consistency, spec coverage).
5. Commit the new plan.
6. Update the **Plan-generation log** of `restoration-status.md` and update the phase board's plan link.
7. Update the TL;DR pointer in `restoration-status.md` to direct the next session at the new plan.

The next session starts the new phase's first task. You do not need to be the agent who executes — once you've generated the plan and updated status, you can hand off.

---

## 4. What the user does, what you do

| Activity | Owner |
|---|---|
| Architecture decisions (premise changes; phase reshaping; spec-level edits) | User |
| Design decisions within the spec (implementation choices in the plan) | You (agent) |
| Plan task execution | You |
| Test writing per plan | You |
| Status doc updates | You (after every commit) |
| Manual dogfood testing | User |
| Spec amendments | Proposed by you; approved by user; applied by you |
| New plan generation | You (per §3.7) |
| Decisions about whether to roll back / pause / pivot | User |

---

## 5. Anti-patterns — do not do these

- **Do not try to do the entire restoration in one session.** The phasing is deliberate. Land one phase, dogfood, move on.
- **Do not skip the failing-test step** of TDD cycles. Plans are written assuming the discipline; skipping it lets bugs slip through that the plan was specifically structured to catch.
- **Do not silently rewrite the spec.** Even if you're confident the change is right. The spec-amendment protocol exists because past sessions have done exactly this and produced inconsistent designs that took weeks to untangle.
- **Do not bundle status-doc updates into separate "status update" commits.** Bundle them into the code commit they describe. Standalone status commits accumulate drift.
- **Do not skip dogfood feedback.** "All tests pass" is necessary but not sufficient for phase acceptance.
- **Do not paper over dogfood failures.** A failure means the architecture or implementation has a hole; dig until you find it.
- **Do not introduce abstractions the plan didn't ask for.** This restoration is recovering from over-abstraction in the prior code; the plan errs deliberately on the side of concrete.
- **Do not start R<N+1> work before R<N> is dogfood-accepted.** Phase boundaries are gates, not suggestions.
- **Do not rebuild the entire test suite when working on one library.** Use `cmake --build build-dev --target <specific-target> -j 8` for inner-loop work.
- **Do not commit binaries, build artifacts, or temporary files.** Standard hygiene; `git status` should be clean of unrelated noise before committing.
- **Do not delete the `markoff-view-qml` library** — it stays in service of source mode and the legacy live mode (regression reference) until the very end of R10. Removal happens in one specific R10 task, not earlier.

---

## 6. Restoration-arc invariants

These are properties the spec demands that every commit must preserve. Re-read these whenever you're about to make a structural change.

1. **The premises (spec §1) are binding** until the user says otherwise. The seven brainstorming-pinned decisions — block-based mental model, the I.a / II.a / II.b / III.c feature scope, Shape-1 cursor, sequence-tagged hybrid (C), side-by-side library (β), Notion-style Enter (N), 16/50ms perf budget — do not get walked back without a spec amendment.
2. **The C protocol's freshness rule (spec §4.1) is the architecture.** Whatever specific implementation you land for it must implement that one rule literally — "trust output that was current when it was emitted." Cycle-guard archaeology must not return.
3. **No `Qt.callLater` retry loops in QML.** Focus delivery is via `LiveCursorState::cursorChanged()` watching `LiveBlockModel::rowsInserted` deterministically.
4. **No `setFocusProxy` chains, no manual `QApplication::sendEvent` re-dispatch.** This was the legacy `markoff-live` SEGV class; it stays dead.
5. **No `TextEdit.MarkdownText`.** Every text-bearing delegate uses `PlainText`; inline rendering is via the highlighter.
6. **No CRDT-typed symbols in the live-render library's public headers.** `Markoff::TextAnchor` and `Markoff::BlockAnchor` are the boundary types.
7. **The 16ms keystroke budget is CI-enforced.** Anything that puts a synchronous parse, full-document allocation, or per-delegate parser instantiation on the keystroke hot path is a regression.
8. **Foundation and view layers are unidirectional.** View depends on foundation; foundation never depends on view.

These are the boundaries inside which all implementation choices are yours. Outside them, escalate.

---

## 7. Tooling notes

- **Skills.** The `superpowers:*` skills are available; the orientation set most relevant here is `subagent-driven-development` (per-task fresh-agent dispatch with review gates), `executing-plans` (in-session task execution), `writing-plans` (for plan generation when phases land), `verification-before-completion` (for confirming work is actually done before claiming so).
- **Memory.** `~/.claude/projects/-home-clinton-dev-Markoff/memory/` is the persistent memory store; cross-reference `MEMORY.md` if you need user preferences. Do not write project status into memory — `restoration-status.md` is the source of truth for that.
- **Editor preferences (from prior memory entries):** complex-first-simplify-later for designs; subagent git inspection must not run `git checkout <sha>`; default working directory is the worktree (this directory); cap build parallelism at `-j 8`.

---

## 8. Failure modes and recovery

If the worktree is in an unexpected state (uncommitted changes you didn't make; HEAD detached; missing files): **stop and ask the user**. Don't try to recover by guessing.

If a test fails for an unclear reason: read the failure output, read the test, read the production code under test, write down the hypothesis, verify before fixing. If you can't form a hypothesis, escalate.

If `git pull` brings in changes from the user (they may push fixes between your sessions): rebase your work on their changes; never force-push; never destructively rewrite their work.

If you find yourself doing the same exploration multiple times across sessions: that is a signal that *this* doc is missing something. Add the missing context here, commit it, and the next session benefits.

---

## 9. Closing

The user has framed this restoration as the most important feature of the Markoff library. The work is long but not vague — every phase has concrete acceptance criteria, every plan has concrete tasks, every commit has a concrete diff. Trust the process. The discipline is what makes the timeline tractable.

Welcome to the arc. When in doubt, the status doc is your map and the spec is your contract.
