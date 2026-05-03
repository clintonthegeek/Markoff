# Live Render Restoration — Status Board

**This is the live status of the live-render restoration arc. Update after every commit, every dogfood pass, every spec amendment, every plan written.**

**Last updated:** 2026-05-03 (R5 Tasks 1–11 landed; PAUSED on architectural finding — see TL;DR)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Branch tip when this entry was written:** see recent-changes log

---

## TL;DR — what to do *right now*

> **R5 PAUSED on architectural finding (empty-paragraph gap).** R5 Tasks 1–11 are landed (commits `7c6f7f6..b8fb639`); 7/7 live-render fast-tier executables green; 21/21 structural-test slots green. **Tasks 12–18 are paused.** The R5 dogfood criterion (spec §10.3) — *"caret lands in the new empty paragraph each time"* — is structurally unachievable with the current `\n\n`-insert design because tree-sitter's CommonMark grammar emits zero block nodes for blank-only regions. Inserting `\n\n` at end of `"hello"` produces `model->rowCount() == 1`, not 2. The planned `requestTextCaretAtRow(blockIndex+1, 0)` cursor delivery has no row to resolve into. Mid-block split works; end-of-paragraph and start-of-paragraph Enter do not. The R5 spec premise 6 ("Notion-style Enter; holes deleted") retired the only mechanism that could deliver the dogfood UX. **Holes need to come back, in their v1 IME-preedit-pattern form, scoped narrowly.** The full failure analysis + redesign brief is at `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md`.
>
> **Recommended next (fresh context window):** Read `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` in full. Conduct the post-mortem of the v0 holes implementation per that document's §7.2. Adapt the v1 IME-preedit-pattern design (specced at `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5 but never implemented) to the C-architecture restoration. Propose spec amendments per `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6 — surface to the user with concrete edit drafts, await explicit approval, then re-derive the R5 (or R5.5) plan. Until that work happens, do not run the R5 dogfood gate (Task 18).
>
> **Read first** (in this order):
> 1. `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` — call-for-design + post-mortem brief (THIS is the entry point).
> 2. `docs/handoff/2026-05-02-restoration-session-brief.md` — orientation / working-protocol doc; particularly §3.6 spec-amendment protocol.
> 3. `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5 (v1 hole design) and §3.6 (v0 five failure modes) — load-bearing prior art.
> 4. `docs/specs/2026-05-02-live-render-restoration-design.md` premise 6, §4.4, §5.3, §7.2, §11 R5/R6 — the spec to be amended.
> 5. `docs/plans/2026-05-02-live-render-r5-structural-keys.md` — the R5 plan (Tasks 1–11 executed; 12–18 paused).

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
| **R5** | [r5-structural-keys](plans/2026-05-02-live-render-r5-structural-keys.md) | `in-progress (paused)` | `7c6f7f6`, `6da5698`, `dff19de`, `9d9d157`, `abc1005`, `4901383`, `05363f2`, `9a6c9f8`, `1ab0da9`, `cc64af9`, `21be140`, `b8fb639` | Tasks 1–11 landed (12 commits). PAUSED at the empty-paragraph gap — see TL;DR + `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md`. Tasks 12–18 are paused pending spec amendment to re-introduce holes. |
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
| 2026-05-03 | (this commit) | docs(handoff): R5 empty-paragraph gap analysis + redesign brief; status-doc reflects R5 pause |
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

### A1 — 2026-05-03 — Re-introduce holes for end-of-paragraph Enter (proposed; awaiting design)

Proposed by: agent (during R5 Task 11 spec-compliance review)
Affects: spec premise 6, §4.4, §5.4, §6.1 L6, §7.2, §11 R5/R6 (and possibly §15 open questions).
Reason: R5 Tasks 1–11 landed correctly per the plan, but the test corrections in commit `b8fb639` revealed that tree-sitter's CommonMark grammar emits zero block nodes for blank-only regions. The spec's §7.2 data-flow assumption — that `applyLocalEdit("\n\n")` at end-of-paragraph produces an `Insert(B_new)` op the cursor can resolve into — is structurally incorrect for end-of-paragraph and start-of-paragraph cases. The R5 dogfood criterion (§10.3 R5) cannot be satisfied without holes. Premise 6 ("Notion-style Enter; holes deleted") conflated v0 holes' five concrete failure modes (`docs/specs/2026-05-01-live-projection-layer.md` §3.6) with the broader cycle-guards / six-sources-of-truth architecture the audit retired; the v1 IME-preedit-pattern hole design (specced at the same document §3.1–§3.5 but never implemented) is structurally compatible with the C-architecture's freshness rule and pending-cursor mechanism, and the v0 failure modes are addressed by v1 design choices.
Proposed change: scope-narrowed re-introduction of the `BlockHole` primitive (paragraph-only initially) into a new layer position in §6.1 (either widen `LiveSpeculationLayer` or add `LiveHoleLayer`); rewrite §7.2 structural-edit data flow to use hole-create → local-typing → commit-on-trigger semantics; amend premise 6 to retire only the v0 implementation, not the concept; add hole tasks to R5 (or split R5.5).
User decision: **AWAITING DESIGN.** Fresh-context post-mortem and v1-redesign-adaptation session is the next step (driven by `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md`). The amendment proposal is the OUTPUT of that session, not its INPUT — this entry is a placeholder to track the open question. Concrete amendment drafts will be added in a follow-up entry once the receiving session has done the design work.
Spec edit commit: pending.

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
