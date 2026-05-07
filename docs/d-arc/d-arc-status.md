# D-arc — Status Board

**This is the live status of the D-evolution work arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-06 (D3 dogfooded; corrective ListItem spec landed.)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **D3-correction** — D3's original 27-task plan implemented
2026-05-05. Dogfood 2026-05-06 surfaced that ListItem blocks were
implemented as whole-multi-line-list-blocks, contradicting D3 §1 premise 6
("each list item is a separate block"). Corrective spec landed at
`docs/specs/2026-05-06-per-item-listitem-blocks-design.md`. Implementation
plan pending. Heading / CodeBlock / HR / Image / Math / Blockquote / per-
block undo / structural cursor signals from D3 stand as-shipped.

---

## TL;DR — what to do *right now*

> **D3-correction implementation pending.** Read the corrective spec and
> derive an implementation plan (writing-plans).
>
> **Read first** (in this order, for a fresh agent context):
> 1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — orientation
> 2. `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items
> 3. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — original D3 spec (premise 6 is the load-bearing line)
> 4. **`docs/specs/2026-05-06-per-item-listitem-blocks-design.md`** — the corrective spec, active subject
>
> **Do not** continue patching `LiveStructuralKeyHandler.cpp`'s multi-line
> ListItem path. Commits `cc62280`, `799eb94`, `21b2ce3` are the dead
> end the corrective spec replaces.

---

## Phase board

| Phase | Status | Notes |
|---|---|---|
| **D0** | `complete` | Joint design — proposal + response delivered and accepted. |
| **D1** | `complete` | `CollabText::Crdt::IdList` shipped 2026-05-04. Available for D2 consumption. |
| **D2** | `complete` | All 15 phases implemented and dogfooded. 141/141 tests pass (3 pre-existing QML selection failures unrelated to D2 cleared during dogfood). User signed off 2026-05-05. |
| **D3** | `partial-dogfood` | Original 27-task plan implemented and dogfooded. Heading/CodeBlock/HR/Image/Math/Blockquote/per-block-undo land as-shipped. **ListItem path requires correction** — corrective spec at `docs/specs/2026-05-06-per-item-listitem-blocks-design.md`; implementation plan pending. D3 closes when the corrective spec ships. |
| **D4** | `stubbed` | `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`. Substantive design post-D2/D3. |
| **D5** | `stubbed` | `docs/specs/2026-05-04-d5-collab-activation-STUB.md`. Substantive design post-D4. |

**Phase status legend.** `pending` (not yet started) · `stubbed` (inputs + scope captured, no substantive design) · `spec-in-brainstorm` · `spec-approved` (spec written and user-approved) · `plan-approved` (writing-plans output landed) · `in-progress` (commits landing) · `dogfood` (implementation done; user is testing) · `complete` (acceptance criteria met).

**D2 acceptance criterion.** All foundation tests pass against the new internals; round-trip corpus tests pass; markoff-live-render L4 / L5 migration completes (marker-paragraph machinery deleted); user signs off on a dogfood pass per the plan's dogfood script. **MET 2026-05-05.**

**D2 dogfood status.** Complete. Bugs surfaced during dogfood pass and fixed: empty-view (loadFromMarkdown fix), merge newline contamination (trailing-\\n stripping in backspaceMerge/deleteMerge), merge cursor race (anchor-keyed deferred resolution), heading/code-block key dispatch (delegate forwarding stubs replaced), SOB Enter cursor (goes to new empty block, not shifted content). User signed off 2026-05-05.

---

## Recent-changes log

Append-only chronological record. Each entry: date, commit short SHA (when committed), one-sentence summary. Never edit prior entries — corrections are new entries that supersede.

| Date | Commit | Summary |
|---|---|---|
| 2026-05-06 | `37661b5` (+ amendments) | D3 dogfood found ListItem implementation compromised D3 §1 premise 6 (lists were stored as one whole-list block, not one block per item). Corrective spec `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` landed; resolves marker storage (attribute, `MarkerStyle`+`MarkerNumber`), loose-list (per-item `LooseRun` attr), renumber location (caller-driven via `Cmd::renumberRunStartingAt`), task-list scope (in). Implementation plan pending. D3 status → partial-dogfood. |
| 2026-05-06 | `cc62280`, `21b2ce3`, `799eb94` | Band-aid fixes for ListItem dogfood bugs (in-block insert, ordered-marker increment, multi-trailing-`\n` strip). These over-fitted the wrong shape; the corrective spec replaces them. Do not extend this lineage. |
| 2026-05-05 | (D3 plan + impl session) | D3 implementation plan written (27 tasks). Implementation completed; pre-corrective dogfood reported clean for non-list block kinds. D3 status was → plan-approved → in-progress → partial-dogfood. |
| 2026-05-05 | `edcd104` | D2 dogfood complete — user signed off. Six bugs fixed during pass (empty view, merge newline, merge cursor, heading/code-block dispatch, SOB Enter cursor). D2 → complete; D3 brainstorm next. |
| 2026-05-04 | `6b45b4a` | D2 Phase 14 complete — deprecated `ParsePool`, `parseUpdated`, `parseSequence`, `MarkoffEdit` with D4-deletion markers. D2 implementation done; status → dogfood. |
| 2026-05-04 | `a862ce0` | D2 Phase 13 — CRDT primitive convergence tests: structural convergence, buffer content convergence, mixed ops, remove-vs-edit race, local undo. |
| 2026-05-04 | `dd00234` | D2 Phase 12 — `SearchEngine::findByBlock`, `ReplaceController::replaceInBlock`, `CompletionContext`/`CompletionDetector` off Crdt::Anchor. |
| 2026-05-04 | `473fb1f` | D2 Phase 11 — live-render migrated to D2: `LiveEditBinding` uses D2 buffer edits, `LiveStructuralKeyHandler` uses `Cmd::*`, `LiveListModelBinding` uses `d2DocumentChanged`. Marker/UndoCoalescer machinery deleted. |
| 2026-05-04 | (multiple) | D2 Phases 5–10 — sibling maps, `Cmd::*` commands, `loadFromMarkdown`, `serializeForSave` + GC, `InlineParseCache`, `WatermarkCoordinator`. |
| 2026-05-04 | (multiple) | D2 Phases 1–4 — foundation primitives (`BlockId`, `BlockEdit`, `StructuralOp`, `CausalLwwMap`), `UndoLog`, `TextAnchor` + `BlockAnchor` reshape, `MarkoffDocument` new D2 internals. |
| 2026-05-04 | (this commit) | D2 implementation plan landed (`docs/plans/2026-05-04-d2-foundation-reshape.md`) — ~85 tasks across 16 phases (Phase 0 setup through Phase 15 dogfood). |
| 2026-05-04 | `afdbc1c` | D2 spec landed (`docs/specs/2026-05-04-d2-foundation-reshape-design.md`); D-arc roadmap + status board + scope-line doc + D3/D4/D5 stub specs published; worktree `CLAUDE.md` banner updated to point at the roadmap. |
| 2026-05-04 | `0fa0111` | C-restoration bookend committed; spike/marker-hole worktree retired. (See `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`.) |
| 2026-05-04 | (external) | collabtext maintainers shipped `IdList` v1 (Option β); D1 complete. |
| 2026-05-04 | (external) | collabtext maintainer response to D-evolution proposal: yes to β with six explicit "won't do" lines. (`~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md`.) |
| 2026-05-02 | (in-tree) | D-evolution proposal authored (`docs/specs/2026-05-02-d-evolution-proposal.md`). |

---

## Open architectural questions across the arc

These are deferred decisions noted at design time that the relevant phase will resolve. They are not blockers.

| # | Question | Phase to resolve |
|---|---|---|
| Q1 | Inline parse cache eviction policy (never-evict / LRU / time-based) | D2 plan-time, informed by real workload |
| Q2 | Plugin block-kind registration timing and registry interface | D3 |
| Q3 | `tst_roundtrip` corpus license review (Obsidian excerpts) | D2 plan-time |
| Q4 | `BlockAttrsMap` `AttrValue` variant scope | D3 (once block kinds are reviewed) |
| Q5 | `Cmd::pasteMarkdown` parser invocation threading (synchronous vs background) | D2 plan-time |

---

## Spec amendment log

When the D2 spec is amended (after spec approval but before retiring the arc), record the amendment here with date, section affected, and reason.

*(No amendments yet.)*
