# D-arc — Status Board

**This is the live status of the D-evolution work arc. Update after every commit, every spec amendment, every plan written, every dogfood pass.**

**Last updated:** 2026-05-08 (collabtext D5 deliverables landed and accepted; D5 Phase 0 prerequisite met).
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** **D5** (collab activation) — `spec-approved` 2026-05-08. Implementation plan via writing-plans next; see `docs/specs/2026-05-07-d5-collab-activation-design.md`.

---

## TL;DR — what to do *right now*

> **Branch is on D5-first posture. v1.0 retired. D5 is the next
> substantive design step.** No work happens outside the ordered list
> in pivot-doc §4.
>
> **Read first** (in this order, for a fresh agent context):
> 1. `docs/handoff/2026-05-07-pivot-to-d5-first.md` — **the pivot doc; authoritative for the branch.**
> 2. `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance.
> 3. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation.
> 4. `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items.
> 5. `docs/specs/2026-05-07-d5-collab-activation-design.md` — **D5 substantive design (spec-approved 2026-05-08; next work-unit: writing-plans).** Companion: `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md`. The retired stub is at `docs/archive/2026-05-04-d5-collab-activation-STUB.md`.
> 6. `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` — D4 spec (complete; background).
> 7. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — D3 spec (background; complete).
> 8. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` — D3 corrective spec (background; complete).

---

## Phase board

| Phase | Status | Notes |
|---|---|---|
| **D0** | `complete` | Joint design — proposal + response delivered and accepted. |
| **D1** | `complete` | `CollabText::Crdt::IdList` shipped 2026-05-04. Available for D2 consumption. |
| **D2** | `complete` | All 15 phases implemented and dogfooded. 141/141 tests pass (3 pre-existing QML selection failures unrelated to D2 cleared during dogfood). User signed off 2026-05-05. |
| **D3** | `complete` | Original plan + corrective spec both shipped. Per-item ListItem blocks, caller-driven renumbering, marker attrs, delegate rendering, 146/146 tests pass. |
| **D4** | `complete` | Spec: `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md`. Plan: `docs/plans/2026-05-07-d4-parser-scope-reduction.md`. Final commit: `22ea352`. 103/103 tests pass. |
| **D5** | `spec-approved` | Spec: `docs/specs/2026-05-07-d5-collab-activation-design.md` (approved 2026-05-08). Companion negotiation opener: `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md`. Retired stub at `docs/archive/2026-05-04-d5-collab-activation-STUB.md`. Implementation plan next via writing-plans. |

**Phase status legend.** `pending` (not yet started) · `stubbed` (inputs + scope captured, no substantive design) · `spec-in-brainstorm` · `spec-approved` (spec written and user-approved) · `plan-approved` (writing-plans output landed) · `in-progress` (commits landing) · `dogfood` (implementation done; user is testing) · `complete` (acceptance criteria met).

**D2 acceptance criterion.** All core tests pass against the new internals; round-trip corpus tests pass; markoff-live L4 / L5 migration completes (marker-paragraph machinery deleted); user signs off on a dogfood pass per the plan's dogfood script. **MET 2026-05-05.**

**D2 dogfood status.** Complete. Bugs surfaced during dogfood pass and fixed: empty-view (loadFromMarkdown fix), merge newline contamination (trailing-\\n stripping in backspaceMerge/deleteMerge), merge cursor race (anchor-keyed deferred resolution), heading/code-block key dispatch (delegate forwarding stubs replaced), SOB Enter cursor (goes to new empty block, not shifted content). User signed off 2026-05-05.

---

## Recent-changes log

Append-only chronological record. Each entry: date, commit short SHA (when committed), one-sentence summary. Never edit prior entries — corrections are new entries that supersede.

| Date | Commit | Summary |
|---|---|---|
| 2026-05-08 | (this commit) | E-arc framed and documented as the post-D-arc trajectory. Live-render view named as the maximalist Markoff prototype; every future view is a structural subset. New constitutional spec `docs/specs/2026-05-08-e-arc-framing.md`; new orientation doc `docs/e-arc/2026-05-08-e-arc-roadmap.md`. Phases E1 (inline-format highlighter) → E2 (cursor-aware delimiter visibility) → E3 (Obsidian affordances) → E4 (tables/frontmatter/footnotes) → E5 (math/mermaid Live parity) → E6 (distillation: extract view-construction recipe). Pivot-doc §4.5 narrowed: `inlineSpansFor` removed from the audit-target list — it is load-bearing infrastructure for E1, not dead code. Erratum landed on `docs/handoff/2026-05-07-live-binding-developmental-history.md` §A.7 to retract the original "Class B dead code" framing. D-arc roadmap §6 added pointing at E-arc. Worktree CLAUDE.md updated. **D-arc work continues; E-arc is named-but-deferred until D-arc's §4.5/§4.6 bookend ships.** |
| 2026-05-08 | (earlier) | Collabtext OpStream extraction landed (their commits `08875fd` → `ef44e41`). Public surface verified: `<collabtext/OpStream.h>`, `<collabtext/Operations.h>`, `<collabtext/IdListOperations.h>`, `<collabtext/Serialization.h>`, `<collabtext/StreamSync.h>`, `CrdtEngine` `setOnLocalOp`/`applyRemoteOp`, IdList `set_on_local_op`/`apply_remote_op`, sibling `acks.json` ack-frontier with all four hard requirements satisfied. One nit raised: `Lamport::replica_id` exposed as field rather than accessor (asymmetric with `counter()`; documentation/three-line fix, not blocking). Markoff acceptance recorded at `docs/handoff/2026-05-08-collabtext-d5-acceptance.md`. **D5 Phase 0 prerequisite is satisfied.** Phase 1 of `docs/plans/2026-05-08-d5-collab-activation.md` (boundary types, includes migration to `<collabtext/...>`) can begin. |
| 2026-05-08 | (earlier) | Collabtext maintainers responded YES to Alternative A (`~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md`). Public op API + public `Serialization.h` land in ~2 weeks; full `OpStream` + `StreamSync` adoption + ack-frontier in ~8–10 weeks. D5 spec §0.1 records the negotiation outcome. D5 implementation plan landed at `docs/plans/2026-05-08-d5-collab-activation.md` (written under writing-plans skill); Phase 0 prerequisite added — Markoff D5 implementation pauses until collabtext items 1–2 land. Joint-design positions on the two open design calls (`Operation`/`IdListOperation` contract surface; ack-frontier file format) recorded at `docs/handoff/2026-05-08-collabtext-joint-design-positions.md`. |
| 2026-05-08 | `8a9d802` | D5 spec-approved. Substantive design at `docs/specs/2026-05-07-d5-collab-activation-design.md` (written 2026-05-07, approved 2026-05-08). Companion negotiation opener at `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md`. Stub archived. Pivot-doc §4.3 closed; §4.4 (writing-plans for implementation plan) next. D5 → `spec-approved`. |
| 2026-05-07 | `1f44926` | pivot-doc §4.2 closed: view-qml deletion was already landed in `f646c90`. CLAUDE.md and pivot doc updated to mark §4.2 closed. |
| 2026-05-07 | `6b0dc03` | docs: point CLAUDE.md, d-arc-status, and citations to pivot doc (final §4.1 commit). |
| 2026-05-07 | `6b77a25` | docs: archive C-restoration paper trail under `docs/archive/c-restoration-arc/` (pivot-doc §2.2). |
| 2026-05-07 | `390cc58` | docs: archive v1.0 plan series under `docs/archive/v1.0-plan-pre-d5/` (pivot-doc §2.1). |
| 2026-05-07 | `c054897` | docs: pivot to D5-first; v1.0 plan retired as authoritative. Pivot doc + developmental-history doc landed. |
| 2026-05-07 | `22ea352` | D4 complete. All 14 phases shipped. 103/103 tests pass. D4 → complete; D5 (collab activation) is the next design target. |
| 2026-05-07 | `9b0aabd` | D4 Phase 11 — parser-library deletions: `ByteEdit`, `parseIncremental`, `buildDocumentQueries(prior,edits)`, pruned-walk helpers, observability counters, `fromComponents` all deleted. `tst_incremental_parse.cpp` removed. |
| 2026-05-07 | `5db6c98` | D4 Phase 10 — delete foundation-internal deprecated infra: `ParsePool`, `ParsePoolWorker`, `IncrementalParseSession`, `RenderPhases`. |
| 2026-05-07 | `3478132` | D4 Phase 9 — delete `parseUpdated` / `parseSequence` / `MarkoffEdit` / `applyLocalEdit` from `MarkoffDocument`. |
| 2026-05-07 | `d87bb1f` | D4 Phase 8 — excise `parsePool` from `MarkoffDocument` runtime. |
| 2026-05-07 | `499bdc6` | D4 Phase 7 — delete deprecated foundation parse/anchor tests. |
| 2026-05-07 | `275dae7` | D4 Phase 6 — retire view-qml live mode (source mode stays); delete live-mode tests. |
| 2026-05-07 | `868d651` | D4 Phase 5 — retire `markoff-bench` library + `apps/bench`. |
| 2026-05-07 | `7dfd040` | D4 Phase 4 — delete dead legacy `Cmd::*` family + `CommandFacade` + `ReplaceController`. |
| 2026-05-07 | `1c9cf98` | D4 Phases 1–3 — `applyFlatEdit` primitive + source-widget D2 migration + test rewrite. |
| 2026-05-07 | (plan landing commit) | D4 substantive spec + implementation plan landed. Spec covers parser-library deletions, foundation-side parsePool/parseUpdated/MarkoffEdit/applyLocalEdit retirement, source-widget D2 migration via new `applyFlatEdit` primitive, markoff-bench retirement, view-qml live-mode retirement, and dead-code deletion of legacy `Cmd::*` family + `CommandFacade` + `ReplaceController` (zero external consumers). D4 status → `plan-approved`. Stub superseded. |
| 2026-05-07 | `be4e079` | D4 spec landed (substantive). |
| 2026-05-06 | `5da92dc` | D3-correction complete — 17-task plan implemented. Parser emits one `TopLevelBlock::Kind::ListItem` per `list_item` node; foundation materializes per-block CRDT with marker attrs; `Cmd::renumberRunStartingAt` caller-driven in transaction; `ListItemDelegate.qml` renders marker from model roles; `BlockRecord::operator==` extended to include attrs; kind-transition inference guarded to Paragraph-only. 146/146 tests pass. D3 → complete; D4 design next. |
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
| 2026-05-04 | `0fa0111` | C-restoration bookend committed; spike/marker-hole worktree retired. (See `docs/archive/c-restoration-arc/2026-05-04-c-restoration-bookend-d-pivot.md`.) |
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
