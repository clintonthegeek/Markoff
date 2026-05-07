> **Status: superseded.** Foundation Phase 11+ tasks all shipped. Do not execute.

# Foundation execution Phase 11+ — fresh-context SESSION BRIEF

**Read this first.** This document is a self-contained briefing for a fresh-context Claude session picking up the Markoff-foundation implementation. Tasks 1–42 are landed; you are continuing from Task 43.

**Status:** Phases 1–10 complete (Tasks 1–42 + two pre-Phase-8 polish refactors + one Phase-10 cross-task bug fix). Ready to resume from Task 43 (Phase 11 — Code blocks).
**Branch:** `exploration/new-foundation` (you are likely already on it if reading this in the worktree).
**Worktree path:** `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`
**Master is untouched and must remain so.** This is exploratory work; the existing Markoff family on master continues to ship for CorbomiteApp.

---

## TL;DR

Three short steps:

1. **Verify orientation:** confirm worktree at `.worktrees/foundation-exploration`, branch `exploration/new-foundation`, latest commit (the one immediately before this brief commit). 17 foundation test executables green. The Part 2 plan exists at `docs/plans/2026-04-28-foundation-library-part2.md`.
2. **Invoke `superpowers:subagent-driven-development`** to orchestrate execution. Same execution mode as the prior two sessions.
3. **Resume from Task 43.** Walk Tasks 43–55 by dispatching one implementer subagent per task, then a combined spec-+-quality reviewer (lighter eyeball-the-diff for trivial test-only or one-line additions). Heavier `superpowers:code-reviewer` pass for tasks 43, 45, 51 (multi-file integration / design judgement).

If anything below this line surprises you or contradicts what you'd otherwise do, follow the brief.

---

## 1. Background — what's done

### Commits ahead of master on this branch

You can run `git log --oneline master..HEAD | wc -l` to get the count. As of this brief, ~37 commits.

The most recent block — Phases 7 / 8 / 9 / 10 — landed in two prior sessions:

```
cb30517 feat(foundation): ReplaceController::replaceAll batched              ← Task 42 (Phase 10 close)
aaca478 feat(foundation): ReplaceController::replaceCurrent                   ← Task 41
13d3a3e fix(foundation): SearchEngine::findNext lands on first match ...      ← Cross-task fix (see §6 lesson 14)
d070b8d feat(foundation): SearchEngine::findNext / findPrevious / clearMatches ← Task 40
ec16727 feat(foundation): SearchEngine::findAll with FindFlags                ← Task 39
5e0689d feat(foundation): CommandFacade Q_OBJECT for QML                      ← Task 38 (Phase 9)
6f07144 feat(foundation): Cmd::applyToAllPrimaryAndSecondaries ...            ← Task 37 (Phase 8 close)
667f94b feat(foundation): Cmd insert family (table/link/image/HR)             ← Task 36
5354cfc feat(foundation): Cmd::toggleCheckbox + blockQuote                    ← Task 35
8884ef3 feat(foundation): Cmd::setHeading with line-prefix replacement        ← Task 34
44c94b4 test(foundation): Cmd italic/strikethrough/inlineCode coverage        ← Task 33
e18cc49 feat(foundation): Cmd::toggleBold + shared inline-format toggler      ← Task 32
33c971d refactor(foundation): Cmd wrappers + helpers take MarkoffDocument by ref ← Pre-Phase-8 polish
36950e9 refactor(foundation): unify impl-side namespace as Markoff::<Feature>::Detail ← Pre-Phase-8 polish
bdee406 feat(foundation): Cmd::undo/redo wrappers + Cmd::Detail helpers        ← Task 31
b414683 feat(foundation): DefaultLinkService classify/resolve                  ← Task 30
5d98dd1 feat(foundation): LinkKind + LinkActivation + abstract LinkService     ← Task 29
12ab282 feat(foundation): Theme defaults (light/dark) + JSON roundtrip         ← Task 27 (Phase 6 close; Task 28 deferred — see §4)
... (Tasks 18–26 above; see prior brief for that block)
```

### Test status as of `cb30517`

17 foundation test executables green (full set — the regex below is the canonical filter):

```
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$' --output-on-failure
```

| Executable | Coverage |
|---|---|
| `tst_markoff_edit` | MarkoffEdit JSON |
| `tst_anchor_json` | Anchor JSON |
| `tst_selection` | Selection value type + JSON |
| `tst_fold_ref` | FoldRef + anchor binding |
| `tst_foundation_markoff_document` | core CRDT doc API |
| `tst_foundation_session` | Session selections / scroll / folds / lifecycle |
| `tst_foundation_parse_pool` | parse pool integration |
| `tst_foundation_theme` | Theme value type + defaults + JSON |
| `tst_foundation_link_service` | LinkService classify/resolve/activate |
| `tst_foundation_cmd_edit` | Cmd::undo/redo wrappers |
| `tst_foundation_cmd_inline_format` | bold + italic + strikethrough + inline code |
| `tst_foundation_cmd_block` | setHeading + toggleCheckbox + blockQuote |
| `tst_foundation_cmd_insert` | table / link / image / HR |
| `tst_foundation_cmd_multi` | multi-cursor `applyToAllPrimaryAndSecondaries` |
| `tst_foundation_command_facade` | Q_OBJECT facade bridges to Cmd::* |
| `tst_foundation_search_engine` | findAll / findNext / findPrevious / clearMatches |
| `tst_foundation_replace_controller` | replaceCurrent + replaceAll |

The pre-existing `markoff-live` failures (`tst_markoff_undo_grouping`, `tst_markoff_table_operations`) sit in code this branch has never touched — pre-existing master-side failures, not regressions. Continue ignoring.

---

## 2. Required reading (in order)

1. **Part 2 plan:** `docs/plans/2026-04-28-foundation-library-part2.md` — Tasks 18–55. **Resume from Task 43 (line ~3940).**
2. **Spec** (source of truth): `docs/specs/2026-04-28-foundation-design.md`. Sections relevant to remaining tasks:
   - §7.5 Theme code-token color mapping (Task 28 — deferred body lit by Task 43)
   - §7.9 Code-block services (Tasks 43–47)
   - §7.10 Completion (Tasks 48–52)
   - §7.11 MarkoffServices (Task 53)
   - §11.3 Property tests (Task 54)
   - §12 Acceptance (Task 55)
3. **Audit (background):** `docs/2026-04-28-codebase-audit.md`.
4. **collabtext API surface:** `~/dev/collabtext/libs/collabtext/src/crdt/Buffer.h`, `Anchor.h`, `Operations.h`. Already used heavily.
5. **Prior briefs** (if you want context on what came before): `docs/handoff/2026-04-28-foundation-execution-SESSION-BRIEF.md` (Tasks 1–17), `docs/handoff/2026-04-28-phase-4-onward-SESSION-BRIEF.md` (Tasks 18–42 brief).

You do **not** need to read existing `libs/markoff-{core,live,source,reading}/` sources unless a specific task references them.

---

## 3. Conventions established this session — *must* follow

These were settled across Phases 7–10 and codified in two refactor commits (`36950e9`, `33c971d`). The plan literal in places still uses the older patterns; you must translate.

### 3.1 `Cmd::*` API takes references, not pointers

Every free function under `Markoff::Cmd::*` (in `include/markoff-foundation/Cmd/*.h` and `Cmd.h`) takes `MarkoffDocument &` (or `const MarkoffDocument &`), not `*`. The wrappers do **not** null-check the document — the type system enforces non-null. Same for `Markoff::Cmd::Detail::*` impl-side helpers.

**Translation when reading the plan literal:** wherever you see `Cmd::X(MarkoffDocument *, ...)` change to `MarkoffDocument &`. Wherever the literal calls `Cmd::X(&doc, ...)` from a test, change to `Cmd::X(doc, ...)`. Drop any `if (!doc) return {};` guards. Use `doc.method()` (dot) not `doc->method()` (arrow).

The Phase 8 plan literal predates this convention. The Phase 11 plan literal probably does too — apply the same translation by default. Surface any place where the translation is awkward (e.g. signature would clash with another overload) before forcing it.

### 3.2 Stateful Q_OBJECT services keep pointer-based signatures

`SearchEngine`, `ReplaceController`, and the Phase-9 `CommandFacade` keep `MarkoffDocument *` and `Session *` parameters. Their `if (!doc || !sess) return ...;` guards stay. The rationale: these are stateful services that intentionally tolerate null inputs — a UI binding may have a nullable "active document" property. Returning 0 / false / nullopt on null is a designed-in graceful-degradation, not defensive programming.

**Translation rule:** if the plan literal types are pointers and the prose says something like "If no document loaded, return X" or "the controller may be unbound", *keep pointers*. If the prose uses the language of preconditions or invariants, *use references*.

For Phase 11–13 specifically:
- **Code-block services** (Phase 11) are likely closer to "value-computing functions" than "stateful UI services" — judge per-task. The `Cmd::*` ref convention probably applies if they're free functions.
- **Completion providers** (Phase 12) are closer to UI services — pointers probably stay.
- **MarkoffServices bundle** (Task 53) — likely takes pointers since it's the QML-facing aggregate.

### 3.3 `CommandFacade`-style internal dereferencing

Where a Q_OBJECT service stores `MarkoffDocument *m_doc` (pointer member) and needs to call ref-typed `Cmd::*` API, dereference at the call site: `Cmd::toggleBold(*m_doc, ...)`. The null-check guards `if (m_doc && m_sess)` are in the wrapper bodies above the call. Don't try to encode the non-null invariant in the type system at the member level — pointers are correct for QML/Qt property bindings.

### 3.4 Impl-side namespace: `Markoff::<Feature>::Detail`

Library-internal helpers live in per-feature `Detail` namespaces:
- `Markoff::Cmd::Detail::*` for Cmd helpers (currently just `selectionByteRange`)
- `Markoff::Parse::Detail::ParsePool` (post `36950e9` rename)

Do **not** reintroduce a library-wide `Markoff::Core::*` namespace for impl-side code (that one was renamed). The CMake target alias `Markoff::Core` (Qt-style) is unrelated and stays.

For Phase 11 code-block services, expect a new `Markoff::Code::Detail::*` (or similar feature name) for impl-side helpers.

### 3.5 `<crdt/Anchor.h>` does NOT come transitively via `<crdt/Operations.h>`

We discovered this in Task 34. Always include `<crdt/Anchor.h>` explicitly when a public header references `CollabText::Crdt::Anchor`. The Phase 11–13 plan literals may or may not include it — check and add as needed.

### 3.6 Q_PROPERTY pointer types need full include in the header

Qt6 MOC's `QMetaType` requires complete type definitions for `Q_PROPERTY` of pointer-to-QObject types. Forward declarations are insufficient. We discovered this in Task 38 — `CommandFacade.h` had to include `<markoff-foundation/MarkoffDocument.h>` and `<markoff-foundation/Session.h>` directly rather than forward-declaring. Future Q_OBJECTs with `Q_PROPERTY(SomeQObject *...)` need the same.

### 3.7 CMake `add_library` grouping

`libs/markoff-core/CMakeLists.txt` has labeled comment groups:
- Top-level public headers (MarkoffDocument.h, Theme.h, LinkService.h, CommandFacade.h, SearchEngine.h, ReplaceController.h, etc.) — alongside each other.
- `# --- Cmd/ (public) ---` — under-Cmd public headers (Edit.h, InlineFormat.h, Block.h, Insert.h) plus the aggregate `Cmd.h`.
- Top-level impl sources (mirroring public layout).
- `# --- Cmd/ (impl) ---` — Cmd impls + impl-side helpers.

**For Phase 11+:** new feature subsystems (e.g. code-block) may want their own `# --- Code/ ---` group if they accumulate >1 file. Match the established pattern.

### 3.8 The plan literal is wrong about pointer/reference choice — translate every time

This is the biggest ongoing translation cost. The plan was generated before the ref convention was settled, so every Phase-8 / Phase-10 task needed translation, and every Phase 11–13 task likely will too. Build the translation into your implementer-subagent prompts as a **CRITICAL: deviations from the plan literal** block (see how the prior session's Task-32 / Task-34 / Task-39 prompts did it). Don't ask the implementer to figure it out from memory.

---

## 4. Deferred work — don't forget

### Task 28 — Theme code-token color mapping

Deferred during Phase 6 because it depends on `CodeTokenKind` (Task 43). When you reach Task 43 (the first task of Phase 11) and `CodeTokenKind` is in place, **circle back to Task 28** and implement its body. The plan literal for Task 28 (line ~1633 in the Part 2 plan) is the source. There's no Task-28 commit on the branch yet — Phase 6 closed without it.

The mechanically-clean way to handle this: implement Task 43 first (which introduces `CodeTokenKind`), then immediately implement Task 28's body as a follow-on commit before continuing to Task 44. The brief's pause-point list mentions this: "After Phase 11 (Task 47 — code-block services; here Task 28's body should be revisited and lit up)."

### `tst_foundation_cmd_multi` test gap

Noted by Task 37 implementer: the multi-cursor sort invariant isn't pinned by a test where a secondary selection precedes the primary in byte order. Worth adding a slot when convenient. Not blocking.

### `QEventLoop: Cannot be used without QCoreApplication` warning

Emitted during `replaceCurrent` and `replaceAll` runs (parse-pool dispatch trying to use queued connections). Tests still pass; this is a known artifact of `QTEST_APPLESS_MAIN` against code that uses `QMetaObject::invokeMethod` queued. Cosmetic only.

---

## 5. Phase strategy

### Resume now: Tasks 43–55

The Part 2 plan is fully expanded. Walk through phases in order:

- **Phase 11 — Code blocks** (Tasks 43–47):
  - Task 43 introduces `CodeTokenKind` + `CodeSpan` value types. **Heavier review** — design judgement.
  - Task 28 (Theme code-token colors) — fold in immediately after Task 43.
  - Tasks 44–47: code-block services. Task 45 (Kf6SyntaxHighlightService) is plan-flagged ambiguous (per-line state) — heavier review.
- **Phase 12 — Completion** (Tasks 48–52):
  - Task 48–50 are the completion-provider machinery; Task 51 is design-flexible (heavier review). Task 52 is the curated emoji table.
- **Phase 13 — Acceptance** (Tasks 53–55):
  - Task 53 — `MarkoffServices` bundle struct.
  - Task 54 — property tests.
  - Task 55 — full-suite acceptance baseline (the actual end of the plan).

### Pause points worth proposing

- After **Phase 11** (Task 47 — code-block services done; Task 28 body lit). Major boundary; offer to write a fresh SESSION-BRIEF and clear context.
- After **Phase 12** (Task 52 — completion done).
- After **Phase 13** (Task 55 — acceptance baseline; this is THE end of the plan).

You don't need to ask permission to keep going through small phases; do propose a pause when you cross a major boundary.

---

## 6. Lessons learned (cumulative across all prior sessions)

Lessons from the original brief (1–13) carry forward. New since:

14. **Cross-task bug surfaces in next-task TDD.** Task 41's test exposed a Task 40 `findNext` bug where `>` against a default-constructed Anchor (resolves to byte 0) silently skipped matches at byte 0. Fix landed as a separate `fix:` commit before the Task 41 commit. **Pattern:** when a literal test reveals a bug in earlier code, isolate the fix as its own commit (matches the brief's bisect-friendliness rule). Don't fold the fix into the new task's commit.

15. **Implementer subagents may misreport test counts.** Several reports said "8/8 passed" or "12/12 passed" when the true count via the canonical regex was higher. Always specify the **exact ctest filter** the implementer should use (`^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$`) and demand the literal output. Verify yourself if a count looks off.

16. **Q_PROPERTY pointer types require full type definitions.** See §3.6.

17. **Helpers.cpp can host both `Detail::` and bare `Markoff::Cmd::` namespace blocks.** Task 37 added `Markoff::Cmd::applyToAllPrimaryAndSecondaries` (public-API helper) into `src/Cmd/Helpers.cpp` as a sibling to the existing `Markoff::Cmd::Detail::selectionByteRange` (impl-side helper). Two namespace blocks in the same .cpp file is fine — keep them separate for clarity.

18. **Impl-side helpers are TU-local until they aren't.** `Cmd::Detail::selectionByteRange` is in `src/Cmd/Helpers.{h,cpp}` because two TUs need it. TU-local helpers (`toggleDelim`, `lineStart`, `existingHashes`, `insertOne`, `matchesInOrder`) live in anonymous namespaces inside their owning .cpp. Don't promote to `Detail::` until a second TU needs them.

19. **The plan literal will keep using pointers and `*doc`-style call sites for the rest of the work.** Build the translation rule (refs for Cmd, pointers for stateful services) into every implementer prompt. The prior session's Task 32 / 34 / 38 / 39 prompts have good templates to crib from.

20. **`MarkoffDocument::applyLocalEdit({})` is a safe no-op.** Returns a default-constructed `Operation` variant. Wrappers can call `d.applyLocalEdit(editsForX(d, s))` even when `editsForX` returns `{}`.

21. **`SearchEngine` is stateless.** `replaceCurrent` and `replaceAll` construct temporaries (`SearchEngine().findNext(doc, sess)`, `SearchEngine().clearMatches(sess)`) inline. Reproduce as the literal does — don't refactor to "inject a SearchEngine" unless the plan asks.

---

## 7. Hard warnings (inherited from prior briefs)

- **Do NOT modify master.** Branch is purely additive.
- **Do NOT skip the test-first cycle.** Each task's first step is the failing test. The build *will* fail with a missing-header error before the impl lands; that's the TDD signal.
- **Do NOT use Qt's `QUndoStack`.** Foundation uses `Buffer::undo()` (CRDT undo). Decision D5.
- **Do NOT add an `InMemoryCanonicalBuffer` or abstract `CanonicalBuffer` interface.** Heavy CRDT mode. Decision D3.
- **Do NOT skip commits between tasks.** One commit per task.
- **Do NOT auto-resolve cmake configure failures.** Investigate first; don't `rm -rf build-dev` reflexively.
- **Do NOT touch the spec, audit, Part 1 plan, Part 2 plan, or any prior brief in implementation commits.** These are reference. Raise spec/plan issues with the user; don't silently amend.

---

## 8. Acceptance criteria for "Tasks 43–55 complete"

Final-plan acceptance is the spec's §12 + plan's Task 55 (foundation viability gate). Per-pause-point acceptance:

1. ✅ One commit per task. Cross-task bug fixes get their own `fix:` commit (lesson 14).
2. ✅ `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` configures clean.
3. ✅ `cmake --build build-dev --target markoff_core -j` builds clean.
4. ✅ `ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$' --output-on-failure` passes for all foundation test executables. After Phase 11, expect ~22 executables; after Phase 13, ~25+.
5. ✅ Existing markoff-* libs still build. Pre-existing `markoff-live` failures (undo_grouping, table_operations) OK to ignore — they predate this branch.

---

## 9. First-action checklist

Before invoking any skill:

1. `pwd` — confirm `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`.
2. `git rev-parse --show-toplevel && git branch --show-current` — confirm worktree + branch `exploration/new-foundation`.
3. `git log --oneline -3` — confirm latest commit is `cb30517` (Task 42, Phase 10 close) or the brief commit on top of it.
4. `ls libs/collabtext libs/jkqtmathtext compile_commands.json` — confirm symlinks in place.
5. `ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_.*)$' --output-on-failure | tail -3` — confirm 17/17 baseline.
6. Read Part 2 plan's Phase 11 — start at line ~3940 (Task 43) — to understand the rest of Phase 11.
7. Invoke `superpowers:subagent-driven-development`.

---

## 10. Out of scope

- POC view (`markoff-view-qml`) — separate plan, after this one lands.
- Migrating code from `libs/markoff-{core,live,source,reading}/` (Task 24's ParsePool salvage was the only expected migration; it landed in Phase 5).
- Bug-fixing in existing markoff-* libs.
- Adding features not in the spec.
- Tagging a release.
- Optimizing collabtext.
- Merging to master.

---

## 11. Reporting back to the user

After each phase boundary you propose as a pause point:

- One concise paragraph: what landed, test status, any surprises (cross-task bug fixes, deviations beyond the pre-approved set, etc.).
- Pose the next decision (continue / different pause / handoff for context-clear / etc.).
- Don't over-summarize — the user reads commits via `git log`. Lead with deviations and surprises, not a recap of every task.

---

## 12. Known plan-side ambiguities (still relevant for remaining tasks)

Carrying forward from the prior brief; the writer flagged these `<!-- AMBIGUITY -->` markers:

- **Task 45 (Kf6SyntaxHighlightService):** KF6 AbstractHighlighter requires per-line state; impl is sketched and tests assert only that spans are non-empty.
- **Task 52 (EmojiCompletionProvider):** plan ships a curated 50-entry subset rather than the spec's "~600-emoji table."

Plus invented details flagged at task-by-task level (Task 50 fenced-code detection heuristic, etc.). All marked in the plan; if any turns out to be wrong during implementation, raise to the user.

---

## 13. Per-task task list hygiene

Task tools persist across sessions — when you start, run `TaskList` to see if any tasks are stale (e.g., Tasks 5–14 from this prior session marked completed). Either keep them as completed history or delete them — your call. Then `TaskCreate` for the tasks you intend to walk in this session, in batches per phase.

---

*End of session brief.*
