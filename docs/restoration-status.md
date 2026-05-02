# Live Render Restoration — Status Board

**This is the live status of the live-render restoration arc. Update after every commit, every dogfood pass, every spec amendment, every plan written.**

**Last updated:** 2026-05-02 (R3 complete — dogfood pass landed, three follow-up fixes)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Branch tip when this entry was written:** see recent-changes log

---

## TL;DR — what to do *right now*

> **R1–R3 complete** (114/114 fast-tier tests pass). R3 delivers LiveCursorState (Shape-1 cursor validation), BlockHitTester (mouse→block hit via QMetaObject), LiveSelectionView (cross-block selection + Session sync + Ctrl-C copy), scrollbar, and mouse/keyboard wiring in LiveView.qml. The next session should write and execute **R4** — paragraph editing through sequence-tagged LiveEditBinding.
>
> **Recommended next:** Write the R4 plan from spec §11 R4, then execute it.
>
> **Read first** (in this order):
> 1. `docs/handoff/2026-05-02-restoration-session-brief.md` — the orientation / working-protocol doc (~250 lines; one-time read per agent context).
> 2. `docs/specs/2026-05-02-live-render-restoration-design.md` §11 R4 — the next phase's spec section.

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
| **R4** | *not yet written* | `pending` | — | Paragraph editing through sequence-tagged binding. |
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
