# Live Render Restoration — Status Board

**This is the live status of the live-render restoration arc. Update after every commit, every dogfood pass, every spec amendment, every plan written.**

**Last updated:** 2026-05-04 (R5.5 marker-paragraph: 19 implementation commits + Bug 1 fix + Bug 2 fix landed; dogfood gate has surfaced an unresolved Bug 3 — see handoff doc.)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Branch tip when this entry was written:** see recent-changes log

---

## TL;DR — what to do *right now*

> **R5.5 marker-paragraph implementation is structurally complete (the v2-holes design retired; marker design landed; tests green) but Task 18 dogfood gate has surfaced an unresolved cursor-delivery bug (Bug 3) that has resisted three different fix attempts**. Bugs 1 and 2 from dogfood are fixed; Bug 3 persists. The R5.5 phase is `dogfood-blocked` until Bug 3 is understood and addressed. See `docs/handoff/2026-05-04-bug-3-handoff.md`.
>
> Plan tasks 1–17 of `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md` are landed. The harness suite (16 tests across save / load / undo / stacked-Enter / focus-out / stress / EOB-then-type) is green; v2-holes code (`LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, the discriminated `BlockId` variant) is deleted; clipboard scrubber strips ZWSP; backspace at qtPos 0 after a marker block scrubs the marker; `UndoCoalescer` is back to a single CRDT-undo regime. Three additional fixes landed on top of Tasks 1–17: `114b807` (Bug 1 list-gate, fixed), `7718c54` + `3c86b76` + `dd64de5` (Bug 2 + Bug 3 attempts; Bug 3 unresolved).
>
> **Next session:** read `docs/handoff/2026-05-04-bug-3-handoff.md` to pick up Bug 3 investigation.
>
> **Read first** (in this order):
> 1. `docs/specs/2026-05-03-marker-paragraph-design.md` — the active design (replaces the archived v2-holes spec).
> 2. `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md` — the active plan (tasks 1–17 landed; task 18 dogfood gate pending).
> 3. `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md` — the architectural review that retired v2 holes.
> 4. `docs/handoff/2026-05-03-section-3-1-spike-findings.md` — the spike that validated the marker approach against the parser.
> 5. `docs/archive/2026-05-03-v2-holes-design.md` and `docs/archive/2026-05-03-live-render-r5-5-holes.md` — retired v2-holes design and plan, kept for historical reference.

---

## Phase board

Status legend: `pending` (not started) · `in-progress` (commits landing) · `dogfood` (implementation done; user is testing) · `complete` (acceptance criteria met).

| Phase | Plan | Status | Commits | Notes |
|---|---|---|---|---|
| **R1A** | [r1a-parse-edit-sequence](plans/2026-05-02-live-render-r1a-parse-edit-sequence.md) | `complete` | `466121e`, `818485b` | Foundation surface: `parseUpdated` 4th arg. |
| **R1B** | [r1b-inline-span-bake](plans/2026-05-02-live-render-r1b-inline-span-bake.md) | `complete` | `65cafdf`, `d3e6384` | Parser surface: `TopLevelBlock::inlineSpans`. |
| **R1C** | [r1c-library-scaffold](plans/2026-05-02-live-render-r1c-library-scaffold.md) | `complete` | `48ba7d6` | New library shell: `libs/markoff-live-render`. |
| **R2** | [r2-read-only-render](plans/2026-05-02-live-render-r2-read-only-render.md) | `complete` | `5a0dae7` | L0 Coordinates + L1 read-only view + L2 diff model. 113/113 fast-tier. |
| **R3** | [r3-cursor-selection](plans/2026-05-02-live-render-r3-cursor-selection.md) | `complete` | `3484c11`, `2225061`, `e837710`, `1f26ec8`, `18abd96` | LiveCursorState + BlockHitTester + LiveSelectionView + scrollbar. Dogfood-surfaced fixes: selection-paint, scroll-then-click hit-test, HR source-faithful copy. 114/114. |
| **R4** | [r4-paragraph-editing](plans/2026-05-02-live-render-r4-paragraph-editing.md) | `complete` | `1b75d37`, `ce0494f`, `3181fe8`, `dec12e7`, `7ae29e1`, `033d7e7`, `fa8e80d`, `99c616f`, `5e12f10`, `563c1f3`, `77a84eb`, `28e1c8f`, `a24c766`, `26dc802`, `7252498`, `324de05`, `201cbd3`, `5662f95`, `7d49718`, `c7dca41`, `37b97bf` | Paragraph + heading + code-block writable via LiveEditBinding; freshness gate; three cycle guards; previousText cache; cachedByteOffset refresh; click-focus routing; Enter swallowed (R5). 6/6 fast-tier; 8 paragraph_edit slots. |
| **R5** | [r5-structural-keys](plans/2026-05-02-live-render-r5-structural-keys.md) | `in-progress (paused — Tasks 12–18 pending; superseded in part by R5.5's hole dispatch)` | `7c6f7f6`, `6da5698`, `dff19de`, `9d9d157`, `abc1005`, `4901383`, `05363f2`, `9a6c9f8`, `1ab0da9`, `cc64af9`, `21be140`, `b8fb639` | Tasks 1–11 landed. EOB-Enter / start-Enter now handled by R5.5 (holes); R5 dogfood criterion amended (spec §11 R5). Tasks 12–17 (delegate wiring + integration) — most are now subsumed by R5.5 work; remaining items can close independently. |
| **R5.5** | [r5-5-marker-paragraph](plans/2026-05-03-live-render-r5-5-marker-paragraph.md) (active) — supersedes archived [r5-5-holes](archive/2026-05-03-live-render-r5-5-holes.md) | `dogfood-blocked` | `a895817..5473e81` (marker plan tasks 1–17) | Marker-paragraph design (`docs/specs/2026-05-03-marker-paragraph-design.md`) replaces v2 holes. Tasks 1–17 landed: `Marker.h` constant, `MarkerScrubber` service (predicate + scrubOnFocusOut / scrubBeforeSave / scrubAfterLoad), atomic-bundled-edit primitive in `LiveEditBinding`, EOB / start-of-paragraph Enter switched to marker insertion (with corrected start-of-paragraph byte order — Gap A), stacked-Enter no-op, focus-out / save / load wiring (with two-step contract for load — Gap B), backspace-after-marker scrub, clipboard ZWSP stripping, single-regime `UndoCoalescer`, `BlockId` reverted to `Markoff::BlockAnchor`, all v2-hole code deleted (`LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, three test executables). 16 harness tests green. Dogfood (Task 18) surfaced 3 bugs: Bug 1 (`114b807`) and Bug 2 (`7718c54`) fixed; Bug 3 (`3c86b76`, `dd64de5`) persists across two additional implementation attempts. See handoff doc. |
| **R6** | *not yet written* | `pending` | — | Other text blocks + speculation refresh. |
| **R7** | *not yet written* | `pending` | — | Lists + blockquotes (II.a, II.b). |
| **R8** | *not yet written* | `pending` | — | Math block + BlockInternalEdit (I.a, L8). |
| **R9** | *not yet written* | `pending` | — | Per-block context menu (III.c). |
| **R10** | *not yet written* | `pending` | — | Hardening + perf-budget enforcement + retire old live mode. |

**Phase acceptance gating.** A phase is `complete` only when:
1. All sub-plan tests pass (per the plan's "Acceptance criterion" section).
2. The fast-tier suite (`ctest -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml"`) is green.
3. The user has run the phase's dogfood script (per spec §10.3) and signed off — recorded in the **Dogfood log** below with their words.

R1 has no formal dogfood script (it's foundation + scaffold; nothing user-visible yet). R1 acceptance is just "the three sub-plans' tests are all green."

---

## Recent-changes log

Append-only chronological record. Each entry: date, commit short SHA, one-sentence summary. **Never edit prior entries** — corrections are new entries that supersede.

| Date | Commit | Summary |
|---|---|---|
| 2026-05-04 | (this commit) | docs(spec): R5.5 marker-paragraph C-restoration amendments + restoration-status update — A2 supersedes A1; §14 amendment table applied; Gap A (start-of-paragraph payload byte order) and Gap B (`scrubAfterLoad` two-step timing contract) captured in marker-paragraph design §4.2 / §6.4 / §17 open question 8 |
| 2026-05-04 | `5473e81` | r5.5(marker): converge LiveEditBinding cache after marker-bundled edit (R5.5-marker Task 16 step 7 follow-up) |
| 2026-05-04 | `dee7b77` | r5.5(marker): test undo returns to marker then pre-Enter state (R5.5-marker Task 16 step 7) |
| 2026-05-04 | `9018f84` | r5.5(marker): test stacked-Enter on marker block is no-op (R5.5-marker Task 16 step 6) |
| 2026-05-04 | `2e4e1a7` | r5.5(marker): test load-time scrubber removes markers (R5.5-marker Task 16 step 5) |
| 2026-05-04 | `d609671` | r5.5(marker): test save path flushes pending markers (R5.5-marker Task 16 step 4) |
| 2026-05-04 | `45a669a` | r5.5(marker): test focus-out without typing scrubs marker (R5.5-marker Task 16 step 3) |
| 2026-05-04 | `52d859e` | r5.5(marker): test stress-typing race verification (R5.5-marker Task 16 step 2) |
| 2026-05-04 | `e88cd78` | r5.5(marker): test EOB-Enter then type atomic scrub (R5.5-marker Task 16 step 1) |
| 2026-05-04 | `0de76cc` | r5.5(marker): retire three v2 hole-specific test executables (R5.5-marker Task 14) |
| 2026-05-04 | `bb20558` | r5.5(marker): retire LiveHoleLayer / LiveProxyBlockModel / BlockHole (R5.5-marker Task 13) |
| 2026-05-04 | `df9878d` | r5.5(marker): revert BlockId to Markoff::BlockAnchor (R5.5-marker Task 12) |
| 2026-05-04 | `bcd5ca5` | r5.5(marker): UndoCoalescer single regime — drop hole branches (R5.5-marker Task 11) |
| 2026-05-04 | `29032eb` | r5.5(marker): clipboard scrubber strips ZWSP from copy output (R5.5-marker Task 10) |
| 2026-05-04 | `df1cd43` | r5.5(marker): backspace at qtPos 0 after marker block scrubs the marker (R5.5-marker Task 9) |
| 2026-05-04 | `1c38539` | r5.5(marker): wire MarkerScrubber to focus-out / save / load events (R5.5-marker Task 8) |
| 2026-05-04 | `89f0945` | r5.5(marker): no-op rule excludes Shift-Enter (soft-break still fires) (R5.5-marker Task 7 follow-up) |
| 2026-05-04 | `20d6c66` | r5.5(marker): stacked-Enter on a marker-only block is a no-op (R5.5-marker Task 7) |
| 2026-05-04 | `bc4dee5` | r5.5(marker): EOB-Enter and start-of-block-Enter insert marker paragraph (R5.5-marker Task 6) |
| 2026-05-04 | `27a836b` | r5.5(marker): atomic-bundled-edit primitive in LiveEditBinding (R5.5-marker Task 5) |
| 2026-05-04 | `0df4888` | r5.5(marker): contract test for cursor delivery into a marker paragraph (R5.5-marker Task 4) |
| 2026-05-04 | `37fec5d` | r5.5(marker): MarkerScrubber edit-emission methods (R5.5-marker Task 3) |
| 2026-05-04 | `f00e658` | r5.5(marker): MarkerScrubber predicate + skeleton (R5.5-marker Task 2) |
| 2026-05-04 | `a895817` | r5.5(marker): land marker constants header + parser-acceptance contract tests (R5.5-marker Task 1) |
| 2026-05-04 | `1e3d2ef` | docs: archive v2-holes design + plan; surface marker-paragraph design and plan as the live R5.5 references |
| 2026-05-04 | (earlier) | docs(handoff): R5.5 dogfood architectural review — six bugs surfaced in dogfood, cycle-guard pattern recurrence, R5.5 paused pending architectural decisions |
| 2026-05-04 | (pending) | feat(live-render): R5.5 dogfood-iteration fixes — proxy targeted insert, requestTextCaretAtNewRow, holeReified cursor delivery, anchorRenumbered for BlockAnchor instability, BlockWalker trailing-`\n` trim. All correct in isolation; full architectural rationale in 2026-05-04 review doc. |
| 2026-05-03 | (earlier) | feat(live-render): R5.5 implementation complete — test-app title bumped; entering dogfood gate |
| 2026-05-03 | `4ddd146` | test(live-render): selection across hole includes bufferText in copy (R5.5 Task 18) |
| 2026-05-03 | `5e92a8f` | test(live-render): stress-typing into hole — load-bearing R5.5 gate (R5.5 Task 17). Surfaced and fixed four real bugs: kind-role miss on hole rows, DelegateChooser-only-rebuilds-on-reset, missing Q_INVOKABLE on proxy helpers, QTest::keyClick Shift-drop in headless |
| 2026-05-03 | `75acb45` | feat(live-render): save-flush via LiveListModelBinding::flushPendingHoles (R5.5 Task 16) |
| 2026-05-03 | `1127b97` | feat(live-render): per-hole undo + UndoCoalescer hole-aware routing (R5.5 Task 15) |
| 2026-05-03 | `127c03d` | feat(live-render): hole bufferText mirror + IME guard + isHole plumbing + Enter dispatch (R5.5 Task 14) |
| 2026-05-03 | `e297a21` | feat(live-render): hole-row abandon paths — Esc / Backspace-empty / Delete-empty (R5.5 Task 13) |
| 2026-05-03 | `ea44780` | feat(live-render): hole-row Enter — commit / split / stacked-empty (R5.5 Task 12) |
| 2026-05-03 | `71e4baa` | feat(live-render): paragraph EOB-Enter + start-Enter create holes (R5.5 Task 11) |
| 2026-05-03 | `76a4632` | feat(live-render): wire LiveHoleLayer + LiveProxyBlockModel into binding (R5.5 Task 10) |
| 2026-05-03 | `3363c89` | feat(live-render): proxy hole-row insertion + anchor mapping (R5.5 Task 9) |
| 2026-05-03 | `162f15f` | feat(live-render): LiveProxyBlockModel passthrough skeleton (R5.5 Task 8) |
| 2026-05-03 | `b08432e` | feat(live-render): commitBlockHole reifies hole into source (R5.5 Task 7) |
| 2026-05-03 | `d15b1e7` | test(live-render): silence -Wunused-variable in idle-empty-buffer test (R5.5 Task 6 follow-up) |
| 2026-05-03 | `3fd6822` | feat(live-render): per-hole 250 ms idle timer (R5.5 Task 6) |
| 2026-05-03 | `b8fb0c7` | feat(live-render): LiveHoleLayer lifecycle skeleton (R5.5 Task 5) |
| 2026-05-03 | `06ec6b2` | test(live-render): initialise eb.text before setRawTextDocument (R4 test-setup fix; audit at `docs/handoff/2026-05-03-r4-paragraph-edit-tests-audit.md`) |
| 2026-05-03 | `ae4506e` | feat(live-render): BlockHole + HoleBlockId; BlockId is now a variant (R5.5 Task 4) |
| 2026-05-03 | `078d461` | test(live-render): retire synthetic broken stub + gate test (R5.5 Task 3) |
| 2026-05-03 | `b4e7072` | test(live-render): harness API hygiene (R5.5 Task 2 follow-up) |
| 2026-05-03 | `c641c39` | test(live-render): LiveRealisticInputHarness + gate test (R5.5 Task 2) |
| 2026-05-03 | `3f672eb` | docs(plan): R5.5 — paragraph holes — 19 tasks, TDD with realistic-input harness gate; subagent-driven-development recommended |
| 2026-05-03 | `8d8ea00` | docs(spec): R5/R5.5 amendment — v2 holes (premise 6 amended; §3.1 BlockId becomes variant; §4.4 cycle-guard rows restored with v2 annotations; §5.4 + §6.1 L6 + §7.2 reflect LiveHoleLayer/LiveProxyBlockModel; §11 adds R5.5; §15 resolves BlockId open question) |
| 2026-05-03 | `af902b1` | docs: R5 v2-holes post-mortem + design — verifies v1 mitigations under C; adopts audit L9 phantom-rows + concatenating proxy as structural correction; specifies LiveHoleLayer + LiveProxyBlockModel + LiveRealisticInputHarness APIs |
| 2026-05-03 | `8dac44b` | docs(handoff): R5 empty-paragraph gap analysis + redesign brief; status-doc reflects R5 pause |
| 2026-05-02 | `b8fb639` | feat(live-render): populate consumedStructuralKeys on built-in descriptors (R5 Task 11; also corrected 3 test assertions about parser behaviour — surfaced the empty-paragraph gap) |
| 2026-05-02 | `21be140` | feat(live-render): code-block consumes only Backspace/Delete edges (R5 Task 10) |
| 2026-05-02 | `cc64af9` | feat(live-render): heading structural keys mirror paragraph (R5 Task 9) |
| 2026-05-02 | `1ab0da9` | feat(live-render): paragraph Shift-Enter — soft break (R5 Task 8) |
| 2026-05-02 | `9a6c9f8` | feat(live-render): paragraph Delete at row-end merges with next (R5 Task 7) |
| 2026-05-02 | `05363f2` | feat(live-render): paragraph Backspace at row-start merges with previous (R5 Task 6) |
| 2026-05-02 | `4901383` | feat(live-render): paragraph Enter — mid-block split and start-of-block (R5 Task 5) |
| 2026-05-02 | `abc1005` | feat(live-render): LiveStructuralKeyHandler skeleton + paragraph end-of-block Enter (R5 Task 4) |
| 2026-05-02 | `9d9d157` | docs(live-render): note recordStructural/recordOther intentional duplication (R5 Task 3 follow-up) |
| 2026-05-02 | `dff19de` | feat(live-render): UndoCoalescer policy (R5 Task 3) |
| 2026-05-02 | `6da5698` | docs(live-render): clarify LiveCursorState pending-row semantics (R5 Task 2 follow-up) |
| 2026-05-02 | `7c6f7f6` | feat(live-render): pending TextCaret request via rowsInserted (R5 Task 2) |
| 2026-05-02 | `86cae4d` | docs(plan): R5 implementation plan — structural keys + IME completion + undo coalescing |
| 2026-05-02 | `37b97bf` | fix(live-render): swallow Enter in R4 to prevent split-without-focus-transition |
| 2026-05-02 | `c7dca41` | wip(live-render): trace logging for content-duplication dogfood (logging reverted by `37b97bf`) |
| 2026-05-02 | `7d49718` | fix(live-render): route delegate text through LiveEditBinding to stop content duplication |
| 2026-05-02 | `5662f95` | fix(live-render): route focus into TextEdit on click so typing works |
| 2026-05-02 | `adb359e` | feat(live-render): R4 implementation complete — entering dogfood gate (test app title bump + status update) |
| 2026-05-02 | `201cbd3` | feat(live-render): code-block delegate is editable (body-interior only) |
| 2026-05-02 | `324de05` | feat(live-render): heading delegate is editable |
| 2026-05-02 | `7252498` | feat(live-render): paragraph delegate is editable |
| 2026-05-02 | `26dc802` | feat(live-render): defensive applyingSessionSelection guard |
| 2026-05-02 | `a24c766` | feat(live-render): refresh TextCaret cachedByteOffset on parse arrival |
| 2026-05-02 | `28e1c8f` | test(live-render): stale parse never clobbers model text |
| 2026-05-02 | `77a84eb` | feat(live-render): IME composition deferral in LiveEditBinding |
| 2026-05-02 | `563c1f3` | test(live-render): applyingModelUpdate is true during dataChanged |
| 2026-05-02 | `5e12f10` | fix(live-render): cache previousText for CRDT-coherent edit translation |
| 2026-05-02 | `99c616f` | fix(live-render): gate LiveEditBinding::setRawTextDocument as test-only |
| 2026-05-02 | `fa8e80d` | feat(live-render): LiveEditBinding wires contentsChange to applyLocalEdit |
| 2026-05-02 | `033d7e7` | docs(live-render): note QPointer's complete-type requirement on LiveEditBinding |
| 2026-05-02 | `7ae29e1` | feat(live-render): scaffold LiveEditBinding (no edit logic yet) |
| 2026-05-02 | `dec12e7` | fix(live-render): qScopeGuard around applyingModelUpdate flag |
| 2026-05-02 | `3181fe8` | feat(live-render): pass parseInputEditSeq + applyingModelUpdate guard |
| 2026-05-02 | `ce0494f` | docs(live-render): clarify freshness-rule comment + fix typo |
| 2026-05-02 | `1b75d37` | feat(live-render): freshness gate on LiveBlockModel::applyOps |
| 2026-05-02 | `49e9b5d` | docs(plan): R4 implementation plan — paragraph editing through sequence-tagged binding |
| 2026-05-02 | `18abd96` | fix(live-render): HR delegate copies its source markdown bytes |
| 2026-05-02 | `1f26ec8` | fix(live-render): hit-test walks outward to nearest realized delegate |
| 2026-05-02 | `e837710` | fix(live-render): expose selectionView at delegate root so children resolve it |
| 2026-05-02 | `2225061` | fix(live-render): persistentSelection: true on text delegates (bandage; superseded by `e837710`) |
| 2026-05-02 | `3484c11` | feat(live-render): R3 — cursor, selection, keyboard nav, scrollbar |
| 2026-05-02 | `96445e3` | docs(plan): R3 implementation plan — cursor, selection, keyboard nav |
| 2026-05-02 | `5a0dae7` | feat(live-render): R2 complete — read-only render with diff model |
| 2026-05-02 | `ba0db23` | docs(plan): R2 implementation plan — read-only render with diff model |
| 2026-05-02 | `5bc9db2` | docs(status): fix R1C commit SHA in restoration-status.md |
| 2026-05-02 | `48ba7d6` | feat(live-render): scaffold libs/markoff-live-render (R1C complete) |
| 2026-05-02 | `bd10576` | docs: code-only architectural audit of live render |
| 2026-05-02 | `f6b7427` | docs(spec): live render restoration design (C-architecture, 9 layers, 10 phases) |
| 2026-05-02 | `136d600` | docs(spec): d-evolution proposal for collabtext review |
| 2026-05-02 | `a9f6d0d` | docs(plan): R1 implementation plans (R1A foundation, R1B parser, R1C scaffold) |
| 2026-05-02 | `466121e` | test(foundation): parseUpdated carries parseInputEditSequence (failing — TDD cycle) |
| 2026-05-02 | `818485b` | feat(foundation): parseUpdated carries parseInputEditSequence (R1A complete) |
| 2026-05-02 | `65cafdf` | test(parser): TopLevelBlock::inlineSpans bake (failing — TDD cycle) |
| 2026-05-02 | `d3e6384` | feat(parser): bake per-block inline spans into TopLevelBlock (R1B complete) |

---

## Dogfood log

The user does manual dogfood testing between phases. Their feedback lives here verbatim. **Treat this section as the highest-priority signal of restoration health** — test passes don't substitute for dogfood feedback.

### 2026-05-04 — R5.5 (marker-paragraph) — pass 3

User script: dogfood the live render in `markoff-live-render-app` on `docs/phase-c-status.md`, repeating the §13 marker-paragraph dogfood script after the Bug 3 v2 (byte-keyed cursor delivery) fix landed in `dd64de5`.

User result: Bug 3 reproduced again. Cursor lands one row past the user's content (on the originally-following paragraph) when pressing Enter at qtPos 0 of a regular paragraph mid-document. The byte-keyed delivery did not address it. User stopped iterating.

Action: docs-only commit (this commit) — captures honest state of the bug, retracts the spec's "byte-keyed delivery is robust" claim, transitions R5.5 to `dogfood-blocked`, and lands `docs/handoff/2026-05-04-bug-3-handoff.md` for the next agent. No further code attempts in this session.

### 2026-05-04 — R5.5 (marker-paragraph) — pass 2

User script: dogfood the live render in `markoff-live-render-app` on `docs/phase-c-status.md`, repeating the §13 marker-paragraph dogfood script after the Bug 3 v1 (anchor-keyed cursor delivery) fix landed in `3c86b76`.

User result: Bug 3 reproduced. Cursor consistently lands on the originally-following paragraph instead of the user's shifted content. Reproduced 3 times in a row. Log captured at `/tmp/dogfood2.txt` (529 lines).

Action: Bug 3 v2 attempt landed (`dd64de5`) — switched cursor delivery from anchor-keyed to byte-keyed `requestTextCaretAtByte`. See pass-3 entry above for outcome.

### 2026-05-04 — R5.5 (marker-paragraph) — pass 1

User script: dogfood the live render in `markoff-live-render-app` per the §13 marker-paragraph dogfood script (≥200 words across ≥10 paragraphs; verify no scramble, clean save, marker-free reload).

User result: two bugs surfaced.

- Bug 1: pressing Enter inside a list item mangled the list (the marker-insertion path fired on a list-item block, which is not a top-level paragraph).
- Bug 2: pressing Enter at the start of a paragraph left the cursor in the marker paragraph above the user's content, not in the user's content.

Action: Bug 1 fixed in `114b807` (list-gate predicate `rowIsListOrQuoteContent` short-circuits the marker dispatch for non-paragraph block kinds). Bug 2 fix attempted in `7718c54` (row-keyed `requestTextCaretAtNewRow(blockIndex+1)` with relaxed `rowsInserted` bound check). Bug 2 single-paragraph test passed; multi-paragraph dogfood reproduced what is now tracked as Bug 3 — see pass-2 entry above.

### 2026-05-02 — R4 (paragraph editing through sequence-tagged binding)

User script: dogfood the live render in `markoff-live-render-app` on a long markdown document (`docs/phase-c-status.md`). Click into a paragraph and type a sentence; verify the cursor appears and characters land at the cursor position without scrambling or duplication.

User result (verbatim):

1. First attempt — typing produced nothing: *"The document is not editable. There is no caret wherever I click, typing characters does nothing."*
2. After focus-routing fix (`5662f95`) — exponential content duplication: *"the document is growing, repeating elements over and over. i see the same headline from near the top repeated a dozen times, then the first few paragraphs over and over. it's many, many times longer than it should be. very laggy. the view jumps as the number of paragraphs or blocks grow."*
3. After delegate-text-binding fix (`7d49718`) — pressing Enter mid-paragraph still produced two-row duplication: *"I made a new paragraph and typed 'This is an intereting [sic] test of the thing.' Here is what came out: [screenshot showing the typed text appearing in two adjacent paragraphs]."*
4. After Enter-swallow fix (`37b97bf`) — within-paragraph editing works: *"Gotcha. Okay, i'm sticking to just typing in paragraphs. doing that near the top of the document is a smooth experience but i get pretty close to pegging the CPU core at about 100wpm. i scroll near the bottom of the doc and do the same thing and I get very close to pegging the CPU. worse, the interfaces freezes and chugs with about 500ms-1s latency."*
5. Sign-off: *"okay that's fine. are we all set for a new agent to pick up r4?"* — referring to the agreement to flip R4 to `complete` (correctness verified) and defer perf to R10.

Action: four fixes landed during the dogfood pass; R4 closes with two scope notes.

- `5662f95` — focus routing on click. Root cause: the LiveView MouseArea has `preventStealing: true` and consumes mouse presses before TextEdit's native click-to-focus runs, so the TextEdit never received keyboard focus. Fix: each text delegate exposes `focusEditAt(qtPos)` which calls `forceActiveFocus()` and sets `cursorPosition`; the LiveView MouseArea calls it after `selectionView.begin`.
- `7d49718` — delegate text driven through `LiveEditBinding.text` instead of TextEdit's direct `text: model.text` binding. Root cause: the QML `text: model.text` binding fires whenever delegates are created, recycled, or `dataChanged` fires for the row. Each fire ran `setPlainText` on the underlying `QTextDocument` and emitted `contentsChange` OUTSIDE `applyingModelUpdate`'s applyOps window, so `LiveEditBinding` interpreted the binding-driven write as a user edit and pumped the entire block back into the CRDT. Each loop appended duplicate content, the duplicate triggered another parse, more delegates appeared, more echoes — exponential growth. Fix: introduce `Q_PROPERTY(QString text)` on `LiveEditBinding`; the delegate binds `editBinding.text: model.text`, which routes to `pushTextToDocument` under a new `applyingTextUpdate` guard. The resulting `contentsChange` echo is recognized as non-user and skipped.
- `37b97bf` — Enter swallowed at delegate level via `Keys.priority: Keys.BeforeItem`. Root cause: pressing Enter mid-paragraph caused the parser to split at `\n\n`, but R4 has no focus-transition machinery (R5 territory). Focus stayed on the original delegate; subsequent typing's byte coordinates drifted past the now-truncated row 7's boundary into row 8's CRDT range; both displays grew (row 7 from local Qt insertion, row 8 from parse-driven push). Mitigation: text delegates consume Return/Enter at `Keys.priority` BeforeItem so the user can't trigger the split during R4. Real Enter handling lands in R5 with `LiveStructuralKeyHandler`.
- `c7dca41` — diagnostic logging added during the duplication investigation. Logging was reverted in `37b97bf` once the mechanism was understood; the WIP commit is left in history as an audit trail per the append-only policy.

**Scope notes carried into R5:**
- R4 swallows Enter as a workaround. Removing the swallow + handling structural Enter properly is R5 work item #1: detect Enter, apply `\n\n` to the CRDT, and after the parse-arrival split, transfer focus + cursor to the new row's delegate at qtPos 0. Deterministic via `LiveCursorState`'s pending-on-`rowsInserted` mechanism per spec §5.3.
- Backspace at row-start, Delete at row-end, Shift-Enter (soft break), Tab/Shift-Tab on lists, IME composition completeness, and undo coalescing also belong to R5.

**Scope notes carried into R10:**
- Within-paragraph typing chugs and pegs a CPU core in long documents (worse near the bottom — 500ms–1s latency). Likely contributors per the architecture: per-keystroke whole-CRDT-body parse re-runs (spec §9.2 says "no whole-document toUtf8() round-trip per keystroke"; not yet enforced), `BlockWalker::walk` linear in document size on every parse arrival, `applyOps` copies `BlockRecord` (text + inlineSpans QList) per Equal op even when no fields changed, `pushTextToDocument` calls `toPlainText()` on equality check. None of these are bugs — they're the architectural cost of "always re-parse the whole document, then diff" without R10's optimizations. R10 is "Hardening + perf-budget enforcement" with CI benchmarks against the 16/50/4/8 ms targets.

### 2026-05-02 — R3 (cursor + selection + keyboard nav)

User script: dogfood the live render in `markoff-live-render-app` on a long markdown document (~150 blocks). Drag-select within a paragraph; drag-select across paragraph boundaries; scroll a long way; drag-select again in a different region; copy a multi-block selection (including a horizontal rule in the range) and paste into a text editor.

User result (verbatim):

- "two problems from the work we just wrapped up: selection is invisible and horizontal rules are not copied. i can copy across blocks and paste them into a text editor just fine (except horizontal rules)."
- "selection *sometimes* doesn't work after performing a selection in a previous block and then scrolling down to a new section. … it *does* seem to matter what parts of the document are in view."
- (after fixes, on retest of the scroll bug): "unable to reproduce the bug. call this a success and let's move on"

Action: three fixes landed; R3 closes.

- `e837710` — selection-paint visibility. Root cause: `Connections` block nested inside each text delegate's TextEdit had `target: ListView.view.binding.selectionView`, but `ListView.view` only resolves on the delegate root item. Target evaluated to `null`, `selectionChanged` was never connected, the highlight never painted (mouse handling and Ctrl-C still worked because those paths read `binding` directly off the LiveView root). Fix: each text delegate exposes a `selectionView` property on its root `Item`; the inner Connections binds to `root.selectionView`. Same pattern as the prior `markoff-view-qml` ParagraphDelegate. The earlier `2225061` (`persistentSelection: true`) was a misdiagnosis bandage; harmless and left in place.
- `1f26ec8` — scroll-then-click selection failure. Root cause: after fast scrolling, the trailing delegates weren't yet realized, so `ListView.contentHeight` underreported total height; clicks past the last realized delegate fell into `hit()`'s `cy >= contentHeight` branch, whose fallback returned `{lastIdx, qtPos:-1}` when `itemAt(probeX, contentHeight-1)` was null. `begin()` then snapped selection to position 0 of an off-screen last block; every subsequent move/release reconfirmed it; the user saw an invisible collapsed selection. Diagnosed via `DBG-PRESS / DBG-LSV.begin / DBG-APPLY` instrumentation that the user reproduced and shared. Fix: drop the special-case branch and use a single outward walk from `cy` in both directions, snapping to the nearest realized delegate's edge. Walk radius widened from 64 px to viewport height to cross trailing empty space and tall recycled-row gaps.
- `18abd96` — HR copy. Root cause: R3 plan stubbed `HorizontalRuleDelegate.blockText` as `""`, diverging from the spec (§6 `serializeForCopy`: "canonical Markdown bytes for this block's source range") and from the other "non-text" delegate (Image), which already exposes `model.text`. Fix: HR exposes `model.text` so a paragraph + HR + paragraph selection round-trips the rule's source. Per-phase plan stub, not a spec divergence — no amendment.

Format for entries:

```
### YYYY-MM-DD — [phase being tested]

User script: [what was tested, per spec §10.3]
User result: [their words, copy-pasted]
Action: [what we did about it — opened a bug, amended the spec, deferred, etc.]
```

---

## Spec-amendment log

The restoration spec (`docs/specs/2026-05-02-live-render-restoration-design.md`) and D-proposal (`docs/specs/2026-05-02-d-evolution-proposal.md`) are *premise* documents — agents do not unilaterally rewrite them. When implementation surfaces something the spec didn't anticipate or got wrong, the agent **proposes** an amendment here, then waits for user approval before editing the spec. Approved amendments edit the spec inline; the amendment record stays here for the audit trail.

Format:

```
### A1 — YYYY-MM-DD — [short title]

Proposed by: [agent context / human]
Affects: [§ of spec]
Reason: [what surfaced; why the spec needs adjusting]
Proposed change: [concrete edit in spec terms]
User decision: [approved / declined / revised, with date and concrete next action]
Spec edit commit: [SHA when applied]
```

### A1 — 2026-05-03 — Re-introduce holes for end-of-paragraph Enter (v2-holes design)

Proposed by: agent (during R5 Task 11 spec-compliance review; design refined through 2026-05-03 post-mortem + design session)
Affects: spec premise 6, §3.1 (BlockId type), §4.4, §5.4, §6.1 L6, §7.2, §11 R5 + new R5.5, §15.
Reason: R5 Tasks 1–11 landed correctly per the plan, but the test corrections in commit `b8fb639` revealed that tree-sitter's CommonMark grammar emits zero block nodes for blank-only regions. The spec's §7.2 data-flow assumption — that `applyLocalEdit("\n\n")` at end-of-paragraph produces an `Insert(B_new)` op the cursor can resolve into — is structurally incorrect for end-of-paragraph and start-of-paragraph cases. Premise 6 ("Notion-style Enter; holes deleted") conflated v0 holes' five concrete failure modes (`docs/specs/2026-05-01-live-projection-layer.md` §3.6) with the broader cycle-guards / six-sources-of-truth architecture the audit retired. The v1 IME-preedit-pattern hole design was specced but never implemented; the post-mortem (`docs/handoff/2026-05-03-r5-holes-postmortem.md`) verified each v1 mitigation under C and adopted the audit's L9 prescription (phantom-rows + concatenating proxy) as the structural correction relative to v1.
Concrete amendments (per design doc §13):
1. **Premise 6** — replace "EOB-Enter hole feature is deleted" with paragraph-EOB and start-of-paragraph Enter creating a `LiveHoleLayer` hole that reifies on idle / focus-out / save / explicit Enter; v0 implementation permanently retired; v2 is structurally distinct (IME-preedit pattern + concatenating proxy).
2. **§3.1 BlockId type** — change from `using BlockId = Markoff::BlockAnchor` to a discriminated union `std::variant<Markoff::BlockAnchor, HoleBlockId>` to disambiguate hole identity from CRDT anchors.
3. **§4.4 cycle-guards-retired table** — restore the `commitBlockHole rowsInserted listener leak` and `Detach/reattach hole around applyOps` rows; annotate with v2-pattern note ("applyOps runs against the parser-pure inner model below the proxy; no detach/reattach; cursor delivery via deterministic `LiveProxyBlockModel::rowsInserted`").
4. **§5.4 structural keys** — note that paragraph EOB-Enter and start-of-paragraph-Enter create a hole via `LiveHoleLayer::createBlockHole`, not `applyLocalEdit("\n\n")`. Mid-block Enter unchanged.
5. **§6.1 L6** — add `LiveHoleLayer` and `LiveProxyBlockModel` as L6 components alongside `LiveSpeculationLayer`. Architecture diagram updated.
6. **§7.2 structural-edit data flow** — replace EOB-Enter flow with hole-create → local-typing → commit-on-trigger flow; mid-block split flow unchanged.
7. **§11 R5** — caveat dogfood criterion: end-of-paragraph and start-of-paragraph Enter cases require R5.5; R5 ships with these as documented limitations.
8. **§11 (new R5.5)** — paragraph holes phase between R5 and R6; plan at `docs/plans/2026-05-03-live-render-r5-5-holes.md` (later archived at `docs/archive/2026-05-03-live-render-r5-5-holes.md`; superseded by A2 / marker-paragraph plan).
9. **§15 open questions** — resolve §3.1 BlockId question; add v2-holes scope note (paragraph-only initially; full hole inventory deferred to R7+).
User decision: **PRE-APPROVED.** User authorised autonomous execution of spec amendments + R5.5 plan + implementation start (2026-05-03 conversation transcript).
Spec edit commit: `8d8ea00` (`docs(spec): R5/R5.5 amendment — v2 holes`).

### A2 — 2026-05-04 — Replace v2 holes with marker-paragraph (supersedes A1)

Proposed by: agent (during 2026-05-04 R5.5 dogfood architectural review; design refined through `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md` §3.1 spike + `docs/handoff/2026-05-03-section-3-1-spike-findings.md`).
Affects: spec premise 6, §3.1 (BlockId type), §4.4 cycle-guards table, §5.4 structural keys, §6.1 L6, §7.2 structural-edit data flow, §11 R5 + R5.5 phase scope, §15 open questions. (Same surface area as A1; the cumulative effect is "A1 reverted, replaced with marker-paragraph design.")
Reason: R5.5 v2-holes implementation passed all unit tests but dogfood surfaced six architectural-level bugs (Bug A–F per the architectural-review handoff) where each fix exposed the next bug in the layer beneath. The cycle-guard pattern that A1's v2 design was supposed to eliminate had recurred under new names (proxy↔model anchor renumbering, holeReified vs rowsInserted ordering, BlockWalker trailing-`\n` trim, etc.). The §3.1 spike validated approach (c) — marker character — against the parser: a U+200B in source produces a real paragraph block; the existing parser-driven row pipeline delivers it; cursor lands via the existing `requestTextCaretAtNewRow`. The marker design eliminates `LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, the `HoleBlockId` discriminator, the per-hole undo regime, the IME hole guard, the idle-commit timer, ~690 LOC of production code and ~870 LOC of tests. The atomic-bundled-edit primitive in `LiveEditBinding` makes the "type into the marker paragraph" case race-free as a single CRDT op.
Concrete amendments (per design doc §14):
1. **Premise 6** — replace v2 holes language with marker-design language: paragraph EOB-Enter inserts `"\n\n​"`, start-of-paragraph-Enter inserts `"​\n\n"` (note byte-order difference), `MarkerScrubber` handles focus-out / pre-save / post-load leakage paths.
2. **§3.1 BlockId type** — revert to `using BlockId = Markoff::BlockAnchor`. No discriminated union; every block is parser-real.
3. **§4.4 cycle-guards-retired table** — replace the two "v2 holes return" rows with marker-design pointer (atomic-bundled-edit eliminates the hole authority; one named predicate `isMarkerOnlyParagraph` shared across three deterministic event points).
4. **§5.4 structural keys** — paragraph EOB-Enter and start-of-paragraph-Enter follow the §4 source-edit contract from the marker design; cursor delivery via standard `requestTextCaretAtNewRow`. No hole-row dispatch.
5. **§6.1 L6** — drop `LiveHoleLayer` and `LiveProxyBlockModel`; add `MarkerScrubber` as a stateless service (not a layer).
6. **§7.2 structural-edit data flow** — replace hole-create → bufferText → commit flow with source-edit → parse-back → row arrival → cursor lands; marker scrubbed atomically by first keystroke or by `MarkerScrubber` on leakage paths.
7. **§11 R5** — caveat updated to "EOB-Enter / start-Enter delivered by R5.5 marker-paragraph"; no R5.5-specific limitation language remains.
8. **§11 R5.5** — phase scope re-described in marker-design terms; plan reference updated to `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md`.
9. **§15 open questions** — A1's "v2 holes scope" entry resolved as "marker-paragraph scope, paragraph-only initially"; A1's "BlockId discriminated union" answer reverted (BlockId is again `BlockAnchor`).

Two spec gaps surfaced during plan execution and captured in the marker-paragraph design:
- **Gap A (Task 6 finding) — §4.2 atStart payload byte order.** The spec said start-of-paragraph Enter inserts the same `"\n\n​"` payload as EOB. Wrong. The start-of-paragraph payload must be `"​\n\n"` so the marker becomes the *leading* paragraph and the existing block becomes the second paragraph. Implementation in `LiveStructuralKeyHandler.cpp` got this right; the spec text is corrected in §4.2 (and a marginal note records the correction).
- **Gap B (Task 16 finding) — §6.4 `scrubAfterLoad` timing.** `MarkoffDocument::documentReloaded` fires synchronously inside `resetContent` *before* the parse worker populates the model. The auto-scrub via that connection is therefore a no-op at load time. The integration contract is now two-step: (1) the wiring stays as documentation of intent; (2) the host (or a future helper) must call `binding.markerScrubber()->scrubAfterLoad()` after the model populates from the first parse-back. Documented in §6.4 + §13 test row + §17 open question 8 (foundation-level fix candidate).

User decision: **PRE-APPROVED.** User authorised autonomous execution of marker-paragraph design + plan + implementation (2026-05-04 conversation; the dogfood gate at Task 18 is the user's explicit checkpoint).
Spec edit commit: (this commit) — `docs(spec): R5.5 marker-paragraph C-restoration amendments + restoration-status update`.

**The bar for amendment:** the spec is wrong about something material, OR a premise turned out to be unworkable, OR a phase boundary needs reshaping based on what the prior phase taught us. The bar is **not** "I have a different opinion" — agents implement the design as specified unless something genuinely contradicts.

---

## Plan-generation log

When a phase's predecessor lands successfully, the agent invokes `superpowers:writing-plans` to derive the next phase's plan from the spec. Each generated plan is recorded here.

Format:

```
### YYYY-MM-DD — RX — [plan filename]

Generated from: spec §11 RX
Generated by: [agent context — fresh session, prior session, etc.]
Predecessor acceptance: [SHA / date of the prior phase reaching `complete`]
Self-review notes: [anything surprising the writing-plans pass surfaced]
```

### 2026-05-04 — R5.5 (marker-paragraph) — 2026-05-03-live-render-r5-5-marker-paragraph.md

Generated from: spec §11 R5.5 (re-derived per amendment A2 / 2026-05-04); design doc `docs/specs/2026-05-03-marker-paragraph-design.md`.
Generated by: fresh agent context (autonomous-execution authorisation 2026-05-04).
Predecessor acceptance: v2-holes design + plan archived at commit `1e3d2ef`; the 2026-05-04 architectural review (`docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`) and the §3.1 spike (`docs/handoff/2026-05-03-section-3-1-spike-findings.md`) closed the architectural questions A1 left open.
Self-review notes: 18 tasks; 17 landed (commits `a895817..5473e81`), Task 18 dogfood gate pending. Surfaced two execution-time spec gaps (Gap A — start-of-paragraph payload byte order; Gap B — `scrubAfterLoad` post-parse timing) which are amendment-A2 marginalia in the marker-paragraph design (§4.2 / §6.4 / §17). Task 16's harness suite landed as 7 sub-steps (EOB-then-type, stress-typing, focus-out scrub, save flush, load-time scrub, stacked-Enter no-op, undo round-trip) plus a follow-up convergence fix for `LiveEditBinding.cachedText` after the marker-bundled `setPlainText` (commit `5473e81`). One follow-up tracked separately in the task list (cursor-position preservation after marker bundle setPlainText).

### 2026-05-03 — R5.5 — 2026-05-03-live-render-r5-5-holes.md (archived)

Generated from: spec §11 R5.5 (added by amendment A1 / commit `8d8ea00`); design doc `docs/specs/2026-05-03-v2-holes-design.md` (since archived at `docs/archive/2026-05-03-v2-holes-design.md`).
Generated by: fresh agent context (autonomous-execution authorisation 2026-05-03).
Predecessor acceptance: R5 Tasks 1–11 landed; R5 Tasks 12–18 may close independently of R5.5 (Tasks 12–17 are integration plumbing that holds for the holes-augmented design; Task 18 dogfood gate happens after R5.5 closes the EOB-Enter limitation).
Self-review notes: 19 tasks. Resolves design-doc §16 open questions inline in the plan preamble: per-hole QTimer (vs single cycling timer); per-hole undo coalescing on 1 s idle threshold (matching legacy `UndoCoalescer`); cursor preserves qtPos on external bufferText updates (paste case); reifyAnchor via `MarkoffDocument::anchorAtByte`; full mapping rebuild on parse-back (cheap; holes sparse); synthetic broken stub format; harness gap-time tuning starts at 30 ms with documented procedure if it doesn't reproduce v0 race. Task 2 lands the harness AND a synthetic v0-mimic stub; gate test asserts harness sees scramble; stub deleted in Task 3 before any v2 hole code lands. Task 4 lands the BlockId variant change cascade (small refactor across LiveCursorState / LiveSelectionView / LiveStructuralKeyHandler call sites). Task 17 is the load-bearing stress-typing test (the v0 equivalent that wasn't written). Tasks 11/12/13 stage the structural-key handler in three slices: source-row Enter (creates hole), hole-row Enter (commit / split), hole-row abandon (Esc / Backspace-empty / Delete-empty). The plan includes flag-for-verification points where the implementer should confirm exact API names against the current branch tip (e.g. `Markoff::TextAnchor::resolvedByteOffset`); these are intentional, not placeholders.

### 2026-05-02 — R5 — 2026-05-02-live-render-r5-structural-keys.md

Generated from: spec §11 R5
Generated by: fresh agent context (post-R4 dogfood sign-off; user invoked "let's start r5")
Predecessor acceptance: R4 — `37b97bf` + dogfood log entry 2026-05-02
Self-review notes: 18 tasks. Resolves spec open questions §15.1 (handler-registration mechanism: per-(kind, key) `std::function` table held inside `LiveStructuralKeyHandler`; `BlockKindDescriptor::consumedStructuralKeys` is the gating set) and §15.4 (undo idle threshold pinned at 1000 ms, matching legacy `markoff-view-qml::LiveEditBinding`). §15.8 (test-app `--live` flag rename) deferred — R5 doesn't touch `markoff-view-qml`. Heading + paragraph share structural handlers because `BlockWalker::walk` produces source-faithful blockText (the `#` prefix is part of the bytes), making qtPos arithmetic uniform across the two kinds. Code-block consumes only the edge-merge keys (Backspace-at-start, Delete-at-end) and lets Enter pass through to TextEdit's native `\n` insertion — the expected behaviour inside fenced code. Cross-task dependency surfaced: Tasks 4–10's tests fail until Task 11 wires `consumedStructuralKeys`; the plan documents this and recommends running cumulative ctest after Task 11. Code-block end-of-body byte arithmetic remains approximate (the closing-fence bytes aren't in `blockText`); ships as-is for R5 with the limitation documented. R5's `LiveCursorState::requestTextCaretAtRow` mechanism uses `cachedByteOffset` to stash qtPos for ASCII-correct routing — multi-byte cursor placement remains a known limitation, same as legacy.

---

## How to update this document

You are an agent landing in this worktree. After every commit, add an entry to the **Recent-changes log**. After every dogfood pass, add an entry to the **Dogfood log**. After every plan generation, add an entry to the **Plan-generation log**. After every spec amendment, add an entry to the **Spec-amendment log**. After every phase status transition, update the **Phase board**.

The TL;DR at the top — the "what to do right now" pointer — is the single most important field. Update it whenever the right next action changes. A fresh agent should be able to read just the TL;DR and know what to do.

**Commit changes to this file alongside the code commit they describe** — never as a standalone "status update" commit. Status updates without code changes drift; status updates bundled with their code commit stay honest.

**If you are about to start a non-trivial action and the status doc disagrees with reality** — for example, the `Phase board` says R1A is `pending` but you can see the commits are landed — fix the board first. The doc is the contract between sessions; reality must match.

---

## Reference index

The five documents that define this restoration arc, in dependency order:

1. **Audit** — `docs/2026-05-02-live-view-architectural-audit.md` *(diagnostic; what was wrong)*
2. **Restoration spec (C)** — `docs/specs/2026-05-02-live-render-restoration-design.md` *(architecture; ten phases)*
3. **D-evolution proposal** — `docs/specs/2026-05-02-d-evolution-proposal.md` *(long-term; not active work)*
4. **Session brief** — `docs/handoff/2026-05-02-restoration-session-brief.md` *(working protocol for fresh agents)*
5. **R1 plans** — `docs/plans/2026-05-02-live-render-r1{a,b,c}-*.md` *(actionable; one of these is the fresh agent's starting point)*

Plus the worktree-level `CLAUDE.md` which auto-loads and points at this status doc.
