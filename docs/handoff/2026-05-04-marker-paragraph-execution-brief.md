# Marker-paragraph execution — fresh-context brief

**Date:** 2026-05-04
**Branch:** `exploration/new-foundation`
**HEAD at handover:** `1e3d2ef docs(archive): add redirect notices to archived v2 hole docs`
**Worktree:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration/` — work here, not in the master worktree.

---

## Your job

Execute the marker-paragraph implementation plan at
`docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md`
using **`superpowers:subagent-driven-development`** — a fresh subagent
per task, with two-stage review (spec compliance → code quality) after
each. The plan has 18 tasks across 4 phases. Total work is "land code
that retires `LiveHoleLayer` + `LiveProxyBlockModel` and replaces them
with a marker-character source-edit + a `MarkerScrubber` service."

Invoke the skill at the start of your first turn:
```
Skill superpowers:subagent-driven-development
```

The skill's prompt templates live at:
- `~/.claude/plugins/cache/claude-plugins-official/superpowers/5.0.7/skills/subagent-driven-development/implementer-prompt.md`
- `…/spec-reviewer-prompt.md`
- `…/code-quality-reviewer-prompt.md`

---

## Read in this order before starting

1. The plan: `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md`
2. The spec the plan implements: `docs/specs/2026-05-03-marker-paragraph-design.md`
3. The spike that justifies the spec: `docs/handoff/2026-05-03-section-3-1-spike-findings.md`
4. The architectural review that ordered the §3.1 decision: `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`

You don't need to read the C-restoration spec end-to-end up front — refer to it (`docs/specs/2026-05-02-live-render-restoration-design.md`) when a task touches a section it pins. You also do not need to read the archived v2 docs (`docs/archive/2026-05-03-{v2-holes-design,live-render-r5-5-holes}.md`) — they are reference history, retired by the marker design. The plan tells you which files to touch and what to write.

---

## Working environment

- Build dir: `build-dev` (already configured at handover but stale wrt the marker work; reconfigure with `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` before building).
- **Cap parallelism at `-j 8` always.** Project policy — never bare `-j` or higher in `cmake --build` / `ctest` (it saturates the user's CPU).
- Fast test inner loop: `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`.
- Slow tail (skip during inner loop): `tst_benchmark` (~7 min) and `tst_realistic` (~90 s).
- Tests: 78/78 pass at the tip of `exploration/new-foundation` going into the marker work. Don't merge any task that drops that number.

---

## Critical constraints

1. **Active branch is `exploration/new-foundation`**, not master. Never check out master. Never push without explicit user approval.
2. **The dogfood-iteration snapshot** at `e94553d wip: R5.5 dogfood iteration snapshot (retired by marker design)` is the labeled archive of the work the architectural review halted. Tasks 6, 13, 14 will `git rm` files that contain those changes — that is intended; the snapshot commit preserves them in history.
3. **No subagent should run `git checkout <sha>`** in a worktree. Detaches HEAD silently and the next commit lands on a dangling ref. (User memory.)
4. **Do not amend or rewrite history.** Every task's commit is its own commit, per the plan.
5. **Don't ask the user what the plan / spec / spike already answers.** Be autonomous within documented scope.
6. **Sequential implementation subagents only** — never dispatch two implementation subagents in parallel; they collide on the same files.
7. **Two pre-existing untracked items in the worktree** (`Testing/`, `libs/jkqtmathtext/`) are NOT yours to commit. Leave them.

---

## Plan structure (so you can plan model-tier choices)

Brief skill guidance: use the cheapest model that can handle each role. The plan's tasks roughly tier as:

| Task(s) | Tier | Why |
|---------|------|-----|
| 1 (Marker constants + parser test) | cheap | Single header + 2 contract tests; 1 file. |
| 2 (Scrubber predicate) | cheap | One static method + 7 unit tests. |
| 3 (Scrubber edits) | standard | Multi-block byte arithmetic; reverse iteration is non-obvious. |
| 4 (Cursor contract test) | cheap | Test-only addition. |
| 5 (Atomic-bundled-edit primitive) | standard | Touches `LiveEditBinding` integration + 1 new test; subtle re: `setText` ordering. |
| 6 (EOB-Enter switch + ctor signature change) | standard | Cross-cutting: ctor change, callers, hole-test temporary disablement. |
| 7 (Stacked-Enter no-op) | cheap | One early-return + 1 test. |
| 8 (Wire scrubber to events) | standard | Multi-file integration. |
| 9 (Backspace marker-aware) | standard | Existing-handler insertion. |
| 10 (Clipboard scrubber) | cheap | 1-line strip. |
| 11 (UndoCoalescer simplification) | standard | Ctor signature change + caller updates. |
| 12 (Revert BlockId) | standard | Ripples through many consumers. |
| 13 (Delete v2 files) | standard | Bulk deletion + QML wiring updates. |
| 14 (Delete v2 tests) | cheap | Mechanical. |
| 15 (Final cleanup) | cheap | Audit + small fixes. |
| 16 (Harness end-to-end tests) | most capable | 7 sub-steps; race verification is load-bearing; design judgment on harness gap times. |
| 17 (Spec amendments) | standard | Doc edits per the §14 amendment table. |
| 18 (Dogfood gate) | manual | Runs the test app interactively; not a subagent task. |

---

## Per-task workflow

For tasks 1–17:

1. Extract the task's full text from the plan into your dispatch prompt — don't make the subagent read the plan file.
2. Provide the scene-setting context: working directory, the 1–2 prior tasks the new task depends on, the relevant API surface (any of the existing classes/methods named in the task).
3. Dispatch implementer subagent (`Agent` tool, `general-purpose` subagent_type, model per the tier table above).
4. If the implementer reports `NEEDS_CONTEXT` or `BLOCKED`, address per the skill's guidance — provide context, escalate model, or break into smaller pieces.
5. On `DONE` / `DONE_WITH_CONCERNS`: dispatch spec-compliance reviewer subagent (`general-purpose`). The reviewer reads code, not just the report.
6. Loop on spec gaps until ✅.
7. Dispatch code-quality reviewer subagent (`Agent` tool, `qt-code-reviewer` subagent_type — better-suited than `superpowers:code-reviewer` for this Qt6 codebase).
8. Loop on quality issues until ✅.
9. Mark the plan task complete (in your TodoWrite / TaskUpdate).
10. Move to the next task.

For task 18 (dogfood gate), do NOT dispatch a subagent — it requires running `markoff-live-render-app` interactively. Surface to the user with build instructions and gate criteria.

After all tasks: dispatch a final code-reviewer subagent for the entire `exploration/new-foundation`-relative diff, then invoke `superpowers:finishing-a-development-branch`.

---

## State of the worktree at handover

```
$ git log -5 --oneline
1e3d2ef docs(archive): add redirect notices to archived v2 hole docs
44c5297 docs: marker-paragraph design + plan (replaces v2 holes)
e94553d wip: R5.5 dogfood iteration snapshot (retired by marker design)
f20fecc fix(build): list delegate components in qml/delegates/qmldir
bfbc097 build: zero out CMake configure-time warnings

$ git status -s
?? Testing/                # CTest output dir; ignore
?? libs/jkqtmathtext       # sibling library (per CLAUDE.md); ignore
```

Live-render library is at `libs/markoff-live-render/` — that's the lib the plan modifies. The neighbouring `libs/markoff-foundation/` and `libs/markoff-parser/` should not need source edits (one parser test addition in Task 1).

Existing test count: `ctest --test-dir build-dev -N | wc -l` should be 78 at the start of work. Track this — it should stay at 78 ± delta-from-marker-tests at every commit.

---

## A short note on the dogfood snapshot

The `e94553d` commit captures a 506-line uncommitted iteration the user (or earlier sessions) had been doing against the v2 hole code: `[dogfood]` log instrumentation, Bug-A through Bug-E cycle-guard fixes per the architectural review's enumeration, and two test edits. That work was paused per the architectural review's "stop iterating" recommendation. The plan deletes most of those files — that's correct and intended. The snapshot exists so the deletions are reviewable in history, not so the work needs to be preserved going forward.

---

## What good looks like at the end

- 18 task commits (or near to it) — small, focused, reviewable.
- Final `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`: green, with the marker tests added and the v2 hole tests retired.
- No reference to `LiveHoleLayer` / `LiveProxyBlockModel` / `BlockHole` / `HoleBlockId` / `holeId` / `bufferText` (in the hole sense) anywhere in `libs/markoff-live-render/` or its QML.
- C-restoration spec amendments (Task 17) landed.
- Dogfood gate (Task 18) passed and noted in `docs/restoration-status.md`.
- A summary message to the user: tasks completed, LOC delta, any caveats, ready to consider `superpowers:finishing-a-development-branch`.

---

## If you get stuck

- Spec ambiguous? Re-read the relevant section of `docs/specs/2026-05-03-marker-paragraph-design.md` (open questions are in §17). Still ambiguous after that — surface to user.
- Test won't pass? Don't paper over with a workaround — that's the cycle-guard pattern the review doc warns against. Investigate root cause; surface if needed.
- Subagent loops on the same fix? Try a more capable model. If still stuck, break the task into smaller pieces and dispatch them individually.
- Plan is wrong? Surface to user. The plan was written from the spec; if the plan and spec disagree, the spec wins; the plan needs amending.

---

## Launch sentence (paste into the new session)

> Read `.worktrees/foundation-exploration/docs/handoff/2026-05-04-marker-paragraph-execution-brief.md` and proceed.
