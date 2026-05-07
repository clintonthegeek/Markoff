> **Status: superseded.** Foundation Phase 4+ tasks all shipped. Do not execute.

# Foundation execution Phase 4+ — fresh-context SESSION BRIEF

**Read this first.** This document is a self-contained briefing for a fresh-context Claude session picking up the Markoff-foundation implementation. Tasks 1–18 are landed; you are continuing from Task 19.

**Status:** Part 1 (Tasks 1–17) complete; Part 2 plan committed; Task 18 (first task of Phase 4 — Sessions) complete; ready to resume from Task 19.
**Branch:** `exploration/new-foundation` (you are likely already on it if reading this in the worktree).
**Worktree path:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`
**Master is untouched and must remain so.** This is exploratory work; the existing Markoff family on master continues to ship for CorbomiteApp.

---

## TL;DR

Three short steps:

1. **Verify orientation:** confirm worktree at `.worktrees/foundation-exploration`, branch `exploration/new-foundation`, latest commit `28d020d` (Task 18). The Part 2 plan exists at `docs/plans/2026-04-28-foundation-library-part2.md`.
2. **Invoke `superpowers:subagent-driven-development`** to orchestrate execution. Same execution mode as the prior session.
3. **Resume from Task 19.** Walk Tasks 19–55 by dispatching one implementer subagent per task, then a combined spec-+-quality reviewer subagent (the prior session compressed the two formal review steps into one combined-review prompt for tasks where the diff is small and literal-from-spec — that's the established cadence for this plan and worked well).

If anything below this line surprises you or contradicts what you'd otherwise do, follow the brief.

---

## 1. Background — what's done

### Commits on this branch (20 total ahead of master)

```
28d020d feat(foundation): SessionParams + Session skeleton; restore createSession default arg   ← Task 18
5973fdb plan: foundation library Part 2 (Tasks 18-55 expanded)
8a121bf test(foundation): anchor_at + resolveAnchor through edits                                ← Task 17
1fba959 feat(foundation): MarkoffDocument::resetContent with Origin semantics                    ← Task 16
4b27363 test(foundation): coalesceLastUndo groups consecutive edits                              ← Task 15
a50cc3d feat(foundation): MarkoffDocument::undo / redo / undoDepth / coalesceLastUndo            ← Task 14
53bebb7 feat(foundation): MarkoffDocument::applyRemoteOps with edits_since translation           ← Task 13
8ce0198 test(foundation): batch applyLocalEdit with non-overlapping ranges                       ← Task 12
bb9795d test(foundation): assert contentsChanged signal shape                                    ← Task 11
cb47273 feat(foundation): MarkoffDocument::applyLocalEdit (insert/replace/delete)                ← Task 10
87639a9 feat(foundation): MarkoffDocument scaffold with replicaId + version + reads              ← Task 9
dbc2472 feat(foundation): add FoldRef value type with anchor binding                             ← Task 8
8497212 feat(foundation): add Selection value type with kinds + JSON roundtrip                   ← Task 7
b7225b0 feat(foundation): add Anchor JSON serialization helpers                                  ← Task 6
cd521cf feat(foundation): add MarkoffEdit value type with JSON roundtrip                         ← Task 5
4a1e2e7 feat(foundation): add Origin enum                                                        ← Task 4
d11ff0e feat(foundation): scaffold markoff-foundation library skeleton                           ← Task 2
90f659d feat(foundation): wire collabtext sibling lib + scaffold foundation entry                ← Task 1
2312d01 spec: fresh-context SESSION-BRIEF for foundation execution                               ← prior brief
2834d22 plan: foundation library implementation (Tasks 1-17 expanded; 18-55 summarized)          ← Part 1 plan
```

(Task 3 was a no-op; `compile_commands.json` is gitignored, so the plan's commit instruction was correctly skipped.)

### Test status as of `28d020d`

Six foundation test executables green:

| Executable | Cases (incl. init/cleanup) |
|---|---|
| `tst_markoff_edit` | 8 / 8 |
| `tst_anchor_json` | 6 / 6 |
| `tst_selection` | 8 / 8 |
| `tst_fold_ref` | 7 / 7 |
| `tst_foundation_markoff_document` | 22 / 22 |
| `tst_foundation_session` | 4 / 4 (2 cases × 2 init/cleanup) |

Full-tree ctest also runs the existing markoff-* family (~127 tests). At the Task 17 pause point two pre-existing `markoff-live` failures (`tst_markoff_undo_grouping`, `tst_markoff_table_operations`) were observed; they sit in code this branch never touched, so they are pre-existing master-side failures rather than regressions.

---

## 2. Required reading (in order)

1. **Part 2 plan:** `docs/plans/2026-04-28-foundation-library-part2.md` — Tasks 18–55 expanded into TDD steps. **This is your primary source for what each task requires.** Task 18 is already complete (commit `28d020d`); resume from Task 19 (line ~390).

2. **Part 1 plan** (for context): `docs/plans/2026-04-28-foundation-library.md` — Tasks 1–17.

3. **Spec** (source of truth for design): `docs/specs/2026-04-28-foundation-design.md`. When a task says "implement X per the spec," this is X's source of truth. Sections relevant to remaining tasks:
   - §7.2 Session (Tasks 19–22 + Task 23 lifecycle)
   - §6.1 ParsePool salvage (Task 24)
   - §7.5 Theme (Tasks 25–28; note: Task 28 depends on Task 43)
   - §7.6 LinkService (Tasks 29–30)
   - §7.7 Commands (Tasks 31–37 + Task 38 facade)
   - §7.8 Search (Tasks 39–42)
   - §7.9 Code-block services (Tasks 43–47)
   - §7.10 Completion (Tasks 48–52)
   - §7.11 MarkoffServices (Task 53)
   - §11.3 Property tests (Task 54)
   - §12 Acceptance (Task 55)

4. **Audit (background):** `docs/2026-04-28-codebase-audit.md` — only consult if you need to understand "why is this design avoiding pattern X?"

5. **collabtext API surface:** `~/dev/collabtext/libs/collabtext/src/crdt/Buffer.h` and `~/dev/collabtext/libs/collabtext/src/crdt/Anchor.h`. Already used heavily in Tasks 9–17. Task 24 (ParsePool) and Task 39+ (Search) will use anchors and possibly `Buffer::edits_since`.

You do not need to read the existing `libs/markoff-{core,live,source,reading}/` sources unless a specific task references them (notably Task 24 salvages `ParsePool` from `markoff-core`).

---

## 3. Recommended execution mode (continuing prior session's choice)

**Use `superpowers:subagent-driven-development`.** The same orchestration mode that ran Tasks 1–18.

**Per-task cadence (established in the prior session):**

1. Mark task `in_progress` via TaskUpdate.
2. Dispatch an implementer subagent (`general-purpose` agent type) with:
   - The full Task block from the Part 2 plan (paste it verbatim).
   - Working directory pointer.
   - State-on-entry summary (latest commit, library/test list, anything the prior task changed).
   - Any deviations to anticipate (see lessons below).
   - TDD discipline (don't skip the failing-build step).
   - Self-review + Report format.
3. After the implementer returns DONE, dispatch a **combined spec-+-quality review** in a single subagent (the prior session compressed two reviews into one for small/mechanical tasks — this is fine; tighter reviews like full code-reviewer with `superpowers:code-reviewer` are still warranted for large or design-flexible commits, e.g., Tasks 24, 31, 38, 51).
4. If review surfaces real issues, dispatch a fix subagent; re-review until clean.
5. Mark task `completed`. Move on.

For purely test-only or one-line additions (e.g., Tasks 12, 15, 17 in Part 1), the prior session shortcut the review by having the controller eyeball the diff with `git show`. That's an OK accelerator when the diff is small and faithful to the plan literal — use judgement.

**Model selection:** the prior session used Sonnet (default) throughout. That worked. Use heavier models for Tasks 24, 31, 38, 45, 51 (multi-file integration / design judgement); cheaper models for value-type additions or test-only tasks.

---

## 4. Phase strategy

### Resume now: Tasks 19–55

The Part 2 plan is fully expanded. Walk through phases in order:

- **Phase 4 — Sessions** (Tasks **19**–23): Selection getters/setters, scroll/folds, copy + JSON, MarkoffDocument lifecycle.
- **Phase 5 — ParsePool integration** (Task 24): salvage + wire.
- **Phase 6 — Theme** (Tasks 25–28): note Task 28 depends on Task 43 (CodeTokenKind) — defer Task 28's body until after Phase 11 (or implement an empty stub that the Task 43 commit lights up).
- **Phase 7 — LinkService** (Tasks 29–30).
- **Phase 8 — Commands** (Tasks 31–37): `Markoff::Cmd::*` family. Task 31 sets the helper / undo-redo wrapper pattern; Tasks 32–37 follow it.
- **Phase 9 — CommandFacade** (Task 38).
- **Phase 10 — Search + Replace** (Tasks 39–42).
- **Phase 11 — Code blocks** (Tasks 43–47): unblocks Task 28's body.
- **Phase 12 — Completion** (Tasks 48–52).
- **Phase 13 — Acceptance** (Tasks 53–55): bundle struct, property tests, full-suite acceptance baseline.

### Pause points worth proposing back to the user

The user's pattern is to stop, update docs, and clear context at natural boundaries. Suggested pause points:

- After **Phase 4** (Task 23 — full Session lifecycle integrated with MarkoffDocument).
- After **Phase 6** (Task 28 stub — Theme done save the post-Phase-11 body).
- After **Phase 8** (Task 37 — full command family landed).
- After **Phase 10** (Task 42 — search + replace working end-to-end).
- After **Phase 11** (Task 47 — code-block services; here Task 28's body should be revisited and lit up).
- After **Phase 13** (acceptance — the actual end of the plan).

You don't need to ask permission to keep going through small phases; do propose a pause when you cross a major boundary.

---

## 5. Hard warnings (inherited from prior brief)

### Do NOT modify master
The existing `libs/markoff-{core,live,source,reading,parser}/` directories on master are off-limits. Branch is purely additive.

### Do NOT skip the test-first cycle
Each task's first step is "write the failing test." The cmake build *will* fail with a missing-header / missing-symbol error before the implementation lands; that's the TDD signal.

### Do NOT use Qt's `QUndoStack`
The foundation uses `Buffer::undo()` (replica-aware CRDT undo). Decision D5 in the spec.

### Do NOT add an `InMemoryCanonicalBuffer` or abstract `CanonicalBuffer` interface
Heavy CRDT mode. Direct `Buffer` use is the design. Decision D3.

### Do NOT skip commits between tasks
One commit per task. Bisect-friendliness > diff noise.

### Do NOT auto-resolve cmake configure failures
If `cmake -S . -B build-dev` fails after a task, stop and investigate. Don't `rm -rf build-dev && cmake ...` reflexively.

### Do NOT touch the spec, audit, Part 1 plan, Part 2 plan, or this brief in implementation commits
These are reference. If you discover a real spec/plan issue during implementation, raise it with the user first; don't silently amend.

---

## 6. Lessons learned from Tasks 1–18 (apply when dispatching 19+)

The prior session encountered these — bake them into implementer briefings:

1. **Test target naming.** `tst_markoff_document` collides with `libs/markoff-core/tests`. Use a `tst_foundation_*` prefix for ALL foundation test executables. The Part 2 plan already adopts this convention.

2. **`SessionParams` forward-declared default arg compile-time hazard** — already resolved in Task 18 (the `= {}` default was restored once `SessionParams` became a complete type).

3. **`MarkoffDocument *` upcast to `QObject *`** — Task 18 needed `Session.cpp` to `#include <markoff/core/MarkoffDocument.h>` and use a normal upcast (`QObject(doc)`), NOT `reinterpret_cast`. The Part 2 plan literal had the unsafe cast; the implementer was steered around it. Watch for similar forward-decl-vs-upcast hazards in any task that creates a Q_OBJECT child of `MarkoffDocument` (e.g., other Phase 8/10/11 services that take a `MarkoffDocument *` parent).

4. **Worktree had no `libs/jkqtmathtext`** — main checkout has it as an untracked symlink to `Corbomite/libs/jkqtmathtext`; the worktree didn't inherit it. The prior session created the same symlink as one-time setup. **It is now in place** — `ls -la libs/jkqtmathtext` should show `→ /home/clinton/dev/Corbomite/libs/jkqtmathtext`. If a fresh worktree is ever set up from scratch again, this step needs to be replicated.

5. **`compile_commands.json` is `.gitignore`'d.** Task 3's plan literal `git add compile_commands.json` would force-add an ignored file. The implementer correctly escalated; the symlink was created on disk but not committed. If any future task tries to add a build artifact, double-check it's not gitignored.

6. **Empty-list defensive checks on signal-emitting functions.** `applyLocalEdit` / `applyRemoteOps` / `undo` / `redo` only emit `contentsChanged` when the resulting edit list is non-empty. Mirror this pattern for any new edit-emitting APIs (notably Search's `findAll` populating selections in Phase 10).

7. **`set_max_undo_depth(0); set_max_undo_depth(saved)`** is the prescribed way to clear the undo stack. Used in Task 16 (`resetContent` with `Origin::FirstOpen`).

8. **collabtext include paths are `<crdt/...>`** — not `<collabtext/crdt/...>`. The collabtext sibling lib exposes `src/` publicly; relative paths work cleanly.

9. **`QApplication` vs `QCoreApplication`.** Foundation should stay widget-free. Tests with custom `main()` (e.g., for `qRegisterMetaType` registration) should use `QCoreApplication` not `QApplication`. The Task 11 test currently uses `QApplication` — left as-is to avoid a churn commit, but new test mains should prefer `QCoreApplication`.

10. **`QSignalSpy` + `QTEST_APPLESS_MAIN`.** Direct-connection signals work fine without an event loop. `QTEST_APPLESS_MAIN` is preferred for Session tests. The Part 2 plan adopts this.

11. **Pulling in collabtext via `add_subdirectory(libs/collabtext)`** also builds collabtext's standalone `app` target as a side effect. Tolerable but a few seconds slower configure/build. Not worth fixing at this stage.

12. **LSP/clangd cache lag** — after every commit the IDE diagnostics complain about "missing files" / "unknown types" for ~30s while clangd re-indexes. Ignore those; the build itself confirms correctness. The `-mno-direct-extern-access` flag warning is a permanent compiler/LSP-clang mismatch and also harmless.

13. **For tests using `QSignalSpy` on a payload type other than primitive types, register the metatype.** Task 11 uses `qRegisterMetaType<QList<Markoff::MarkoffEdit>>("QList<Markoff::MarkoffEdit>")` in a custom `main()`. Phase 8/10 tests that spy on edit-list signals will need the same.

---

## 7. Acceptance criteria for "Tasks 19–55 complete"

Final-plan acceptance is the spec's §12 + plan's Task 55 (foundation viability gate). Per-pause-point acceptance (suggested boundaries above):

1. ✅ One commit per task (Tasks 19–55).
2. ✅ `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` configures clean.
3. ✅ `cmake --build build-dev --target markoff_core -j` builds clean.
4. ✅ `ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$' --output-on-failure` passes for all foundation test executables.
5. ✅ The existing markoff-* libs on this branch still build (your additions did not regress them); pre-existing markoff-live failures (`tst_markoff_undo_grouping`, `tst_markoff_table_operations`) are tracked separately and OK to ignore — they predate the foundation branch.

---

## 8. First-action checklist

Before invoking any skill:

1. `pwd` — confirm you're in `/home/clinton/dev/Markoff/.worktrees/foundation-exploration` (or `cd` there).
2. `git rev-parse --show-toplevel && git branch --show-current` — confirm worktree + branch.
3. `git log --oneline -3` — confirm latest commit is `28d020d` (Task 18).
4. `ls libs/collabtext libs/jkqtmathtext compile_commands.json` — confirm symlinks in place.
5. `ctest --test-dir build-dev -R '^tst_foundation_session$' --output-on-failure` — confirm the Task 18 baseline passes (1/1 ctest target, 2 functional cases).
6. Read Part 2 plan's Phase 4 — Tasks 19–23 — to understand the rest of Phase 4.
7. Invoke `superpowers:subagent-driven-development`.

---

## 9. Out of scope for this execution

- POC view (`markoff-view-qml`) — separate plan, after this one lands.
- Migrating code from `libs/markoff-{core,live,source,reading}/` (Task 24 EXCEPT — it salvages `ParsePool` from `markoff-core`; that's the one expected migration).
- Bug-fixing in existing markoff-* libs.
- Adding features not in the spec.
- Tagging a release.
- Optimizing collabtext.

---

## 10. Reporting back to the user

After each phase boundary you propose as a pause point:

- One concise paragraph: what landed (one line per phase), test status, any surprises.
- Pose the next decision (continue / different pause / handoff for context-clear / etc.).
- Don't over-summarize — the user reads the commits via `git log`.

---

## 11. Known plan-side ambiguities (from Part 2 plan generation)

The Part 2 plan's writer flagged these `<!-- AMBIGUITY -->` markers and made reasonable choices the user may want to revisit:

- **Task 24 (ParsePool):** debounce / worker thread internals are inherited from existing `markoff-core` and treated as black-box.
- **Task 45 (Kf6SyntaxHighlightService):** KF6 AbstractHighlighter requires per-line state; impl is sketched and tests assert only that spans are non-empty.
- **Task 52 (EmojiCompletionProvider):** plan ships a curated 50-entry subset rather than the spec's "~600-emoji table."

Plus invented details (Task 35 checkbox cycle behavior, Task 36 GFM table format, Task 39–40 case-insensitivity default + UTF-8/UTF-16 conversion, Task 50 fenced-code detection heuristic, Task 18 Session construction parent wiring). All marked in the plan; the user accepted the writer's choices implicitly by approving option (a). If any of these turn out to be wrong during implementation, raise to the user.

---

## 12. Per-task task list

The TaskCreate tasks for 18–23 already exist (Task 18 marked completed). The next session should TaskCreate for Tasks 24–55 lazily as it walks the phases (or in batches per phase) — don't pre-create all 32 ahead since the user may steer between phases.

---

*End of session brief.*
