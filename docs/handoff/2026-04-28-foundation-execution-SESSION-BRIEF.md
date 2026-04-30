> **Status: superseded.** All 55 foundation tasks landed; foundation library is feature-complete. Kept for arc context only; do not execute.

# Foundation execution — fresh-context SESSION BRIEF

**Read this first.** This document is a self-contained briefing for a fresh-context Claude session picking up the Markoff-foundation implementation. It encodes the prior session's decisions, recommendations, and hard warnings so you don't repeat the brainstorming work or violate constraints.

**Status:** Plan committed; ready to execute.
**Branch:** `exploration/new-foundation` (you are likely already on it if reading this in the worktree).
**Worktree path:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`
**Master is untouched and must remain so.** This is exploratory work; the existing Markoff family on master continues to ship for CorbomiteApp.

---

## TL;DR

You are executing the foundation library plan. Three short steps:

1. **Verify orientation:** confirm you're in the worktree at `.worktrees/foundation-exploration`, on branch `exploration/new-foundation`, and that the spec, plan, and audit docs exist (paths in §2 below).
2. **Invoke the `superpowers:subagent-driven-development` skill** to orchestrate execution. This is the prior session's recommended (and user-accepted) execution mode for this plan.
3. **Execute Tasks 1–17 only.** Then **stop and report back to the user** so they can decide whether to expand the summarized Tasks 18–55 or expand them as you go. **Do not improvise expansion of Tasks 18–55 without user approval.**

If anything below this line surprises you or contradicts what you'd otherwise do, follow the brief. The decisions here are user-approved.

---

## 1. Background — what's been done

### Three commits exist on this branch (in order):

```
2834d22  plan: foundation library implementation (Tasks 1-17 expanded; 18-55 summarized)
75c6c1d  spec: foundation design — Markoff family hard-reset exploration
54c8b3b  docs: full codebase audit (markoff family)         ← also on master
a4e6033  spec: fresh-context SESSION-BRIEF for Editor key-dispatch fix  ← inherited from master
```

### Decision history (one-line per decision)

- The audit identified `markoff-live` as bandage-saturated and the `MarkdownView` base contract as leaky. Hard reset chosen over in-place fix.
- Foundation = **C+D view-concept**: foundation owns commands + selection types; views render + handle input; subscribers attach freely. No view base class.
- **Heavy CRDT mode** from day one — `CollabText::Crdt::Buffer` is the storage; `Anchor` is the cursor primitive; no `InMemoryCanonicalBuffer` layer. This is the deliberate user choice ("the affordances are useful for us as well").
- Sessions live on `MarkoffDocument`; multiple views can attach; hot-swap and CRDT presence both compose naturally.
- POC view (`markoff-view-qml`) is a **separate plan** — not in this plan's scope. Acceptance for "viability proven" requires both this plan AND the POC plan to land.
- Plan structure: Tasks 1–17 fully TDD-expanded (scaffolding through `MarkoffDocument` core); Tasks 18–55 summarized (Sessions, Theme, LinkService, Commands, Search, code-block services, Completion, property tests, acceptance).

---

## 2. Required reading (in order)

Read these in this order; do not skim:

1. **The plan**: `docs/plans/2026-04-28-foundation-library.md`
   The implementation document. Tasks 1–17 are fully expanded; Tasks 18–55 are summarized. Understand the structure of an expanded task (test → fail → impl → pass → commit) before dispatching anything.

2. **The spec**: `docs/specs/2026-04-28-foundation-design.md`
   The design rationale and complete API surface. When a task says "implement X per the spec," this is X's source of truth.

3. **The audit (input that motivated the spec)**: `docs/2026-04-28-codebase-audit.md`
   Background on why we're doing this hard reset. Useful when you want to understand "why is this design avoiding pattern X?" — the audit will explain the failure mode of pattern X in the existing family.

4. **collabtext API surface**: `~/dev/collabtext/libs/collabtext/src/crdt/Buffer.h` and `~/dev/collabtext/libs/collabtext/src/crdt/Anchor.h` — the foundation depends directly on these types. Read them once before Task 9 (the first task that uses Buffer).

5. **collabtext Qt UI integration (reference for POC pattern, not used in this plan)**: `~/dev/collabtext/libs/collabtext/src/ui/CollabPlainTextEdit.h`. Models the UTF-8↔UTF-16 conversion pattern you'll need in the POC plan (later).

You do **not** need to read the existing `libs/markoff-{core,live,source,reading}/` sources unless a specific task references them.

---

## 3. Recommended execution mode (user-approved)

**Use `superpowers:subagent-driven-development`.** Reasons:

- Plan has 17 expanded tasks; inline execution would burn context across all of them and review fidelity drops.
- Subagent-driven gives a clean per-task review cycle: code review of the change, then plan-fidelity review against the task's TDD steps.
- Fresh subagent context per task means the subagent reads only what it needs (the relevant task) and isn't distracted by earlier tasks' artifacts.

When you invoke the skill, follow its instructions exactly. Don't substitute "I'll do it inline because it's small" — the user explicitly chose subagent-driven for this plan.

---

## 4. Phase strategy

### Execute now: Tasks 1–17 (Phases 1–3)

These are the fully-expanded tasks. They land:
- Phase 1 (Tasks 1–3): library scaffolding, collabtext sibling-lib wiring, build verification.
- Phase 2 (Tasks 4–8): `Origin` enum, `MarkoffEdit` value type, `Anchor` JSON helpers, `Selection` + `FoldRef` value types.
- Phase 3 (Tasks 9–17): `MarkoffDocument` core — construction, `applyLocalEdit`, `applyRemoteOps`, undo/redo, coalescing, `resetContent` + `Origin` semantics, anchor passthrough.

After Task 17, the foundation library compiles and links, with ~30 unit-test cases passing across `tst_markoff_edit`, `tst_anchor_json`, `tst_selection`, `tst_fold_ref`, and `tst_markoff_document`.

### Pause point: between Tasks 17 and 18

After Task 17 commits cleanly and tests pass, **stop and message the user**. Report:
- What landed (one line per phase).
- Test status (`ctest --output-on-failure` summary, all passing).
- Any task-during-implementation surprises that should inform the expansion of Tasks 18–55.
- Ask: "Tasks 1–17 complete. Should I expand Tasks 18–55 as a follow-on plan, or expand and execute one phase at a time? (Phase 4 = Sessions, Phase 5 = ParsePool, Phase 6 = Theme, ...)"

The prior session's recommendation was: **expand 18–55 after Tasks 1–17 land**, so the expansion is informed by what real implementation surfaces. The user accepted this recommendation. Do not pre-emptively expand 18–55 before user confirmation.

### Execute next (after user approves): Tasks 18–55 expansion + execution

This is a separate phase requiring its own work. The summarized tasks need TDD-detailed expansion before subagent dispatch can proceed cleanly. You'll either:
- Expand all 38 summary tasks at once into a Part 2 of the plan, then resume subagent-driven execution; or
- Expand one phase at a time (e.g., Phase 4 Sessions first, execute Phase 4, then Phase 5, etc.) — slower but each expansion is informed by the previous phase's lessons.

User picks; you don't choose unilaterally.

---

## 5. Hard warnings

### Do NOT modify master
The existing `libs/markoff-{core,live,source,reading,parser}/` directories on master are off-limits. This branch is purely additive. If you find yourself wanting to "just fix something quick" in markoff-core, stop — that work belongs on a different branch and is out of scope.

### Do NOT skip the test-first cycle
Each task's first step is "write the failing test." Don't shortcut this even if the test seems trivial. The test failing first proves the test is actually testing what you think it tests; reversing the order means a tautology can land green.

### Do NOT improvise Tasks 18–55
The summary descriptions in the plan are intentionally not full TDD specs. If a subagent needs to implement Task 25 (Theme slot enum), that subagent needs the FULL TDD steps for Task 25 in the plan first. Tasks 18–55 must be expanded by the orchestrating agent (you, with user approval) before any subagent dispatches against them.

### Do NOT touch the audit doc, spec, or this brief in implementation commits
These three docs are reference material. If you discover a real spec issue during implementation, raise it with the user first; don't silently amend the spec.

### Do NOT use Qt's `QUndoStack`
The foundation uses `Buffer::undo()` (replica-aware CRDT undo). Qt's `QUndoStack` would conflict with replica semantics. This is decision D5 in the spec. Any subagent that proposes adding `QUndoStack` to a task is mistaken; reject and re-dispatch.

### Do NOT add an `InMemoryCanonicalBuffer` or abstract `CanonicalBuffer` interface
The user's choice was Heavy CRDT mode. Direct `CollabText::Crdt::Buffer` use is the design. Adding an abstraction layer would be re-introducing exactly what we deliberately removed. This is decision D3 in the spec.

### Do NOT skip commits between tasks
Each task ends with an explicit commit. Don't batch multiple tasks into one commit even when they're "small" — bisect-friendliness is worth the noise.

### Do NOT auto-resolve cmake configure failures
If `cmake -S . -B build-dev` fails after a task, stop and investigate. Don't `rm -rf build-dev && cmake -S . -B build-dev` reflexively — the failure is information about the previous task's correctness.

---

## 6. Acceptance criteria for "Tasks 1–17 complete"

Sub-acceptance for the pause point at Task 17:

1. ✅ Branch `exploration/new-foundation` has commits for each of Tasks 1–17 (one commit per task except where the plan groups, e.g., Task 11 adds tests to a previous task's class).
2. ✅ `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` configures clean.
3. ✅ `cmake --build build-dev --target markoff_foundation -j` builds clean (no warnings as errors are not enforced; document any deprecations).
4. ✅ `ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|markoff_document)$' --output-on-failure` reports all tests pass.
5. ✅ The existing tests on this branch (existing markoff-core/live/source/reading tests) still build and pass — your additions did not regress anything.

The full plan acceptance criteria are in the plan's Phase 13 (Task 55) and the spec's §12. Those are the *plan* acceptance, not Task 17's pause.

---

## 7. First action checklist

When this brief is read, before invoking any skill:

1. `pwd` — confirm you're in `/home/clinton/dev/Markoff/.worktrees/foundation-exploration` (or `cd` there).
2. `git rev-parse --show-toplevel && git branch --show-current` — confirm worktree + branch.
3. `git log --oneline -5` — confirm commits present (audit, spec, plan).
4. `ls libs/collabtext` — confirm symlink target. If missing, plan Task 1 will create it.
5. `ls libs/markoff-foundation 2>/dev/null` — likely empty/nonexistent (Task 2 creates it).
6. Read the plan's Phase 1 (Tasks 1–3) to understand the scaffolding before dispatching.
7. Invoke `superpowers:subagent-driven-development`.

---

## 8. If you get stuck

Common failure modes and what to do:

| Symptom | What it means | What to do |
|---|---|---|
| `cmake configure: cannot find collabtext` | symlink not created (Task 1) or libs/collabtext is a directory not a symlink | follow Task 1 step 1 |
| `KF6SyntaxHighlighting not found` | system missing KF6 dev package | tell the user; this is a host environment issue, not a plan bug |
| Test fails for "wrong reason" (compiles + runs but assertion is unrelated to feature) | test code may be wrong; review the task's test code carefully against the spec | fix the test, not the implementation, unless the test matches the spec exactly |
| Subagent returns implementation that adds methods/types not in the spec | scope creep — likely the subagent inferred a need that doesn't exist | reject the work, re-dispatch with explicit scope |
| `applyLocalEdit` returns garbage Operation | the stub default in Task 9 (returns empty `EditOperation{}`) was never replaced with Task 10's real impl | check Task 10 was committed |
| Anchor JSON roundtrip fails for `Bias::Right` | the toJson/fromJson implementations may have a string-comparison bug | review Task 6 step 5 |

If a problem isn't on this table, message the user before improvising.

---

## 9. Out of scope for this execution

You are NOT to do these things, even if they seem helpful:

- Implement the QML POC view. That's the next plan, after this one lands.
- Migrate any code from `libs/markoff-{core,live,source,reading}/` to the new foundation. Salvage means "look at, not depend on" — the spec specifies which files are transplanted-with-refinement vs new vs dropped, and the plan implements the spec's choice.
- Fix any bugs you spot in the existing markoff-* libs on this branch. Open an issue or report to the user; don't touch them.
- Add features not in the spec. The spec is the contract. If you think something's missing, ask the user first.
- Tag a release. Tagging happens after acceptance, by the user.
- Optimize collabtext's `Buffer`. It's a sibling library; treat it as immutable from this branch's perspective.

---

## 10. Reporting back to the user

After Tasks 1–17 complete (or if you hit a hard block):

- One concise paragraph: what landed, test status, any surprises.
- Pose the expand-strategy question: "Tasks 1–17 complete with N tests passing. Phase 4 onward (Tasks 18–55) is summarized. Expand all 38 summarized tasks at once, or expand and execute one phase at a time?"
- Include the test summary (e.g., `tst_markoff_edit: 6 passed; tst_anchor_json: 4 passed; ...`).
- Note any deviations from the plan (e.g., "Task 12 needed an extra step to handle X; updated the plan").

Don't over-summarize. The user reads the commits via `git log`; your message is just the framing.

---

*End of session brief.*
