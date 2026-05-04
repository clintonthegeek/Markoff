# D-arc — Status Board

**This is the live status of the D-evolution work arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-04 (D2 plan landed; ready for execution.)
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **D2** (foundation reshape) — spec approved + plan written; ready for execution.

---

## TL;DR — what to do *right now*

> **D2 spec and plan are both approved.** Spec at `docs/specs/2026-05-04-d2-foundation-reshape-design.md`; plan at `docs/plans/2026-05-04-d2-foundation-reshape.md` (~85 tasks across 16 phases; estimated 6–10 weeks). D1 (collabtext IdList) is shipped, so execution can begin immediately. Recommended execution mode: subagent-driven (per `superpowers:subagent-driven-development`), with the user reviewing between tasks.
>
> **Read first** (in this order, for a fresh agent context):
> 1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — orientation
> 2. `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items
> 3. `docs/specs/2026-05-04-d2-foundation-reshape-design.md` — the binding D2 spec
> 4. `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md` — the cut from C-restoration

---

## Phase board

| Phase | Status | Notes |
|---|---|---|
| **D0** | `complete` | Joint design — proposal + response delivered and accepted. |
| **D1** | `complete` | `CollabText::Crdt::IdList` shipped 2026-05-04. Available for D2 consumption. |
| **D2** | `plan-approved` | Spec at `docs/specs/2026-05-04-d2-foundation-reshape-design.md`; plan at `docs/plans/2026-05-04-d2-foundation-reshape.md`. Ready for execution. |
| **D3** | `stubbed` | `docs/specs/2026-05-04-d3-view-layer-adaptation-STUB.md`. Substantive design post-D2. |
| **D4** | `stubbed` | `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`. Substantive design post-D2/D3. |
| **D5** | `stubbed` | `docs/specs/2026-05-04-d5-collab-activation-STUB.md`. Substantive design post-D4. |

**Phase status legend.** `pending` (not yet started) · `stubbed` (inputs + scope captured, no substantive design) · `spec-in-brainstorm` · `spec-approved` (spec written and user-approved) · `plan-approved` (writing-plans output landed) · `in-progress` (commits landing) · `dogfood` (implementation done; user is testing) · `complete` (acceptance criteria met).

**D2 acceptance criterion.** All foundation tests pass against the new internals; round-trip corpus tests pass; markoff-live-render L4 / L5 migration completes (marker-paragraph machinery deleted); user signs off on a dogfood pass per the plan's dogfood script.

---

## Recent-changes log

Append-only chronological record. Each entry: date, commit short SHA (when committed), one-sentence summary. Never edit prior entries — corrections are new entries that supersede.

| Date | Commit | Summary |
|---|---|---|
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
