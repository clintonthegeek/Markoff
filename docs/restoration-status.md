# Live Render Restoration — Status Board

**This is the live status of the live-render restoration arc. Update after every commit, every dogfood pass, every spec amendment, every plan written.**

**Last updated:** 2026-05-02 (R4 in dogfood gate — implementation complete)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Branch tip when this entry was written:** see recent-changes log

---

## TL;DR — what to do *right now*

> **R4 in dogfood gate.** Implementation complete: paragraph, heading, and code-block delegates are editable via per-delegate `LiveEditBinding` translating `QTextDocument::contentsChange` into a single `MarkoffEdit` and applying it to the CRDT. The C-architecture freshness rule is wired (`LiveBlockModel::applyOps` gates Equal-op text-role updates on per-row `lastEditEditSequence` against the parse's `parseInputEditSeq`). Three surviving cycle guards in place: `applyingModelUpdate` (set across applyOps via qScopeGuard), IME `composing` deferral with single-edit flush on commit, and a defensive `applyingSessionSelection` scaffold for future read-back. `LiveEditBinding` caches `m_previousText` (CRDT-coherent before-state) so consecutive keystrokes between parse arrivals don't scramble. `TextCaret::cachedByteOffset` re-resolves after every parse arrival. 6/6 live-render fast-tier executables green; 8/8 paragraph_edit slots cover the spec's regression bug-classes.
>
> **Recommended next:** Run the R4 dogfood script (spec §10.3): "Type a 200-word paragraph at 100+ wpm into a 5-page document; cursor never jumps; characters never scramble." Use `./build-dev/bin/markoff-live-render-app <a-long-markdown>`. If clean, flip R4 to `complete` and start writing R5 (structural keys + IME + undo coalescing). If anything misbehaves, paste the verbatim observation and we diagnose before R5.
>
> **Read first** (in this order):
> 1. `docs/handoff/2026-05-02-restoration-session-brief.md` — the orientation / working-protocol doc.
> 2. `docs/plans/2026-05-02-live-render-r4-paragraph-editing.md` — the R4 implementation plan, for context on what just landed.
> 3. `docs/specs/2026-05-02-live-render-restoration-design.md` §11 R5 — the next phase's spec section.

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
| **R4** | [r4-paragraph-editing](plans/2026-05-02-live-render-r4-paragraph-editing.md) | `dogfood` | `1b75d37`, `ce0494f`, `3181fe8`, `dec12e7`, `7ae29e1`, `033d7e7`, `fa8e80d`, `99c616f`, `5e12f10`, `563c1f3`, `77a84eb`, `28e1c8f`, `a24c766`, `26dc802`, `7252498`, `324de05`, `201cbd3` | Paragraph + heading + code-block writable via LiveEditBinding; freshness gate; three cycle guards; previousText cache; cachedByteOffset refresh. 6/6 fast-tier; 8/8 paragraph_edit slots. |
| **R5** | *not yet written* | `pending` | — | Structural keys + IME + undo coalescing. |
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

*No amendments yet.*

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

*No phase-N+1 plans generated yet — R1 plans were generated as part of the spec-authoring session.*

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
