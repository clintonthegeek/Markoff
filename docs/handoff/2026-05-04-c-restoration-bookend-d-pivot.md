# C-Restoration bookend — pivot to D

**Date:** 2026-05-04
**Branch:** `exploration/new-foundation`
**Status:** C-restoration arc closed at R5.5 dogfood-blocked. All forward C work is cancelled in favour of the D-evolution architecture. D2 design work is the new active project.

---

## 1. The decision

The user has elected to halt the C-restoration and cut directly to **D** (per-block CRDT). The triggering observation: the entire reason the `exploration/new-foundation` branch exists was to nail down the right architecture, and the R5.5 marker-paragraph work is — by its own design doc's admission — a workaround layered on top of a workaround. Each successive R5.5 fix attempt has exposed the next bug in the layer beneath it (the v0 cycle-guard pattern recurring under different names; see `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`). Continuing on this path defeats the branch's purpose.

The decision is informed by the collabtext maintainers' response to the D-evolution proposal:

- **Proposal:** [`docs/specs/2026-05-02-d-evolution-proposal.md`](../specs/2026-05-02-d-evolution-proposal.md)
- **Response (external):** `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md`

collabtext approved Option β: a single new `CollabText::Crdt::IdList` primitive (opaque `uint64` elements, sharing the existing anchor/op/undo/GC machinery), 4–8 weeks of focused work, forward-compatible wire-schema bump. The maintainers also drew six explicit "won't do" lines that the D2 design must record.

---

## 2. State at cut

### 2.1 Code state

- `libs/markoff-live-render` exists with L0 → L4 implemented, L5 partially implemented (R5 paused at Tasks 1–11; Tasks 12–17 effectively replaced by R5.5).
- R5.5 marker-paragraph implementation (commits `a895817..5473e81` plus the four post-Task-17 fixes) is in the tree. Tests green; dogfood blocked on Bug 3.
- The marker-paragraph machinery (`MarkerScrubber`, ZWSP scrubbing, atomic-bundled-edit primitive in `LiveEditBinding`, the no-op stacked-Enter rule, the marker-aware initial-qtPos rule in cursor delivery) exists and is functional within the limits Bug 3 reveals.
- v2-holes code (`LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, the discriminated `BlockId` variant) was already deleted by R5.5 Tasks 11–14.
- Old `markoff-view-qml` is intact (the C-restoration's "side-by-side" premise — old library still ships, new library never reached dogfood-stability and now will not).

### 2.2 What ships, what doesn't

This branch never ships to master in its current shape. D2 will replace `libs/markoff-live-render`'s L4+ layers; the lower layers carry forward (see §4). Master continues to ship Phase C of the original Markoff family for CorbomiteApp until D2 is ready to supersede it.

### 2.3 The Bug 3 handoff is cancelled

`docs/handoff/2026-05-04-bug-3-handoff.md` is closed without resolution. The bug lives entirely inside the parser-vs-CRDT race window that D removes. There is no point investigating further.

---

## 3. What is cancelled

The following are **cancelled, not paused**. Do not pick them up.

| Cancelled item | Where | Why |
|---|---|---|
| R5.5 Bug 3 investigation | `docs/handoff/2026-05-04-bug-3-handoff.md` | Race vanishes in D. |
| R5.5 marker-paragraph dogfood gate (Task 18) | `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md` Task 18 | Mechanism retires under D. |
| R5 Tasks 12–17 | `docs/plans/2026-05-02-live-render-r5-structural-keys.md` | Subsumed by D2's L4–L5 redesign. |
| R6 (other text blocks + speculation refresh) | not yet written | D2 replans L4–L8 from the per-block-CRDT premise. |
| R7 (lists + blockquotes) | not yet written | Same. |
| R8 (math block + BlockInternalEdit) | not yet written | Same. |
| R9 (per-block context menu) | not yet written | Same. |
| R10 (hardening + perf-budget enforcement + retire old live mode) | not yet written | Same. |
| Marker-paragraph design as **active** | `docs/specs/2026-05-03-marker-paragraph-design.md` | Workaround for a problem D removes. |
| Sequence-tagged staleness reconciliation as the canonical primitive | C spec §4 | Removed by D — canonical state and structural state are the same state. |

Documents move from `docs/specs/` and `docs/plans/` into `docs/archive/` as part of this bookend. The C-restoration spec stays in `docs/specs/` but is annotated "superseded in part by D2" — its lower-layer architectural decisions remain authoritative as inputs to D2.

---

## 4. What carries forward to D2

The C-restoration spec was written against a "collab-ready" premise (C spec premise 9: "every cursor/edit/undo decision is tested against concurrent remote edits"). That premise made the lower layers transport-independent, so most of L0–L3 transfers cleanly:

| Carry-forward | From | Notes for D2 |
|---|---|---|
| L0 coordinate primitives | `libs/markoff-live-render` | byte/qtPos/block-local conversions; D removes the document-level byte conversion needs but block-local primitives stay |
| L1 read-only render | same | `ListView` + delegates, no events |
| L2 diff-driven model | same | Diff target changes from parser-derived `BlockAnchor`s to structural-CRDT element ids; the layer's *shape* is sound |
| L3 cursor model (Shape 1) | C spec §3 | `BlockId` becomes the structural-CRDT element id — *more* stable than the current `BlockAnchor`; the discriminated union holds |
| Inline span pre-bake | R1B (`TopLevelBlock::inlineSpans`) | Parser keeps its load-time and inline-content roles |
| Layered library structure | C spec §2 | The L0–L8 layering and dependency-direction discipline remain the right organizing principle |
| Test contract | C spec §2 | "Each layer Lₙ has tests against L_{k<n} as real, L_{k>n} absent" stays |
| Notion-style Enter premise | C spec premise 6 | The *semantics* survive; the *mechanism* (marker paragraph) does not — D's structural CRDT inserts a new block as a first-class structural op |
| Discriminated cursor for non-text blocks | C spec §3 | Math, mermaid, image-controls all need this; D doesn't change that |

What does **not** carry: L4+ as currently implemented. L4 in D becomes "translate keystroke into per-block CRDT op" — radically simpler than `LiveEditBinding`'s sequence-tagged freshness gate. The cycle guards, the focus-delivery retry loops, the marker-paragraph machinery, the `MarkerScrubber` event wiring all retire.

---

## 5. What needs writing — D2 spec scope

The collabtext response is one half of D. The Markoff-side D2 spec covers the other half. At minimum it must:

1. **`Markoff::DocumentStructure`** — owns one `IdList` (block ids) plus N `Buffer`s (one per text block) plus the kind-tag sibling map plus per-document watermark GC coordination. Public API surface; ownership/lifetime; reload semantics.
2. **Block kind tag storage and merge rules.** collabtext won't carry per-element values, so this is application-side. Decide LWW (causal? wall-clock?) vs. another scheme. Specify behaviour under concurrent kind-changes.
3. **Save/load round-trip.** Load: tree-sitter still carves into top-level blocks at load time; allocate per-block CRDTs. Save: walk `IdList` in order, ask each `Buffer` for content, format with kind serializer, concatenate. Specify the (de)serializer registry.
4. **Cross-CRDT undo log.** Application-side per maintainer answer to Q2. Specify the data structure; specify the "what was the most recent op?" query.
5. **Per-document watermark GC.** Coordinate `compact(watermark)` across all CRDTs in the document. Trigger conditions; safety against in-flight ops; collab safety.
6. **Replan of L4–L8 of `markoff-live-render`** on the per-block-CRDT premise. The structural-edit handler becomes the simplest layer in the system; per-block edits never traverse the document; per-block undo is natural.
7. **Record the collabtext scoping line.** The maintainers' six "won't do" items recorded in writing in D2 §X so future contributors don't pull on them. Quote verbatim from the response doc.
8. **Schedule the early API-shape exchange** with collabtext maintainers on the `IdList` header. Per their response: ~1 week of back-and-forth on the header file. Trigger this once D2 §1 has enough detail to consume it.

---

## 6. Worktree disposition

Three worktrees exist beyond `master`:

| Worktree | Disposition |
|---|---|
| `.worktrees/foundation-exploration` (`exploration/new-foundation`) | **Active.** D2 design and implementation continue here. |
| `.worktrees/spike-marker-hole` (`spike/marker-hole`) | **Retire.** No commits unique to it; pure diagnostic spike that informed marker-paragraph design. Two probe `.cpp` files (`marker_probe.cpp`, `marker_flow.cpp`) can move into `docs/spikes/` if a record is desired; otherwise delete. |
| `.worktrees/tri-view-phase-a` (`feature/tri-view-phase-a`) | **Retire.** Stale pre-pivot Phase-A worktree; no unique commits. |

Worktree retirement is **not** done as part of this bookend commit — it's a separate cleanup pass to be authorized explicitly.

---

## 7. Pointers

- D-evolution proposal (Markoff side): `docs/specs/2026-05-02-d-evolution-proposal.md`
- collabtext response: `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md`
- C-restoration spec (lower layers carry forward): `docs/specs/2026-05-02-live-render-restoration-design.md`
- C-restoration audit (the diagnostic that drove C, and now drives D): `docs/2026-05-02-live-view-architectural-audit.md`
- R5.5 dogfood architectural review (the precipitating event for the cut): `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`
- Restoration status board (now closed): `docs/restoration-status.md`
