# D-arc roadmap — orientation for fresh contexts

**Date:** 2026-05-07
**Branch:** `exploration/new-foundation`
**Purpose:** This is the canonical orientation doc for the D-evolution work arc. An empty agent context picking up any phase of D should read this first to understand where each piece lives, what depends on what, what's been decided vs deferred, and what the binding cross-arc constraints are.

---

## 1. Phase summary

D = "per-block CRDT + structural CRDT" — the long-term Markoff foundation architecture. Replaces the C-restoration arc that bookended on 2026-05-04 (see `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md`). The arc is divided into six phases, named D0 through D5.

| Phase | Status | Owner | Deliverable | Read |
|---|---|---|---|---|
| **D0** | ✅ done | joint (Markoff + collabtext maintainers) | Architectural alignment: per-block CRDT direction + collabtext supporting primitive | `docs/specs/2026-05-02-d-evolution-proposal.md` (Markoff side) and `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` (collabtext side) |
| **D1** | ✅ done | collabtext maintainers | `CollabText::Crdt::IdList` primitive shipped (opaque uint64 elements, sharing the existing Anchor / Operation / Undo / GC machinery) | collabtext header `~/dev/collabtext/include/collabtext/Crdt/IdList.h` |
| **D2** | ✅ done | Markoff | Foundation reshape — `Markoff::MarkoffDocument` rebuilt on `IdList` + per-block `Buffer`s + sibling causal-LWW maps | `docs/specs/2026-05-04-d2-foundation-reshape-design.md`; status `docs/d-arc/d-arc-status.md` |
| **D3** | ✅ done | Markoff | View-layer adaptation — cursor delivery redesign; inline span consumption; kind-transition detection; L6/L7/L8 full delegates; per-block undo UI. ListItem path corrected per the post-dogfood corrective spec. | Original: `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`. Corrective: `docs/specs/2026-05-06-per-item-listitem-blocks-design.md`. |
| **D4** | ✅ done | Markoff | Parser scope reduction — retired `ParsePool` / `IncrementalParseSession` / `parseUpdated` / `MarkoffEdit` / `applyLocalEdit`; source-widget migrated to D2 via `applyFlatEdit`; markoff-bench and view-qml live mode retired; dead legacy `Cmd::*` + `CommandFacade` + `ReplaceController` deleted | Spec: `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md`. Plan: `docs/plans/2026-05-07-d4-parser-scope-reduction.md`. |
| **D5** | ⏸ stubbed | Markoff + collabtext | Collab activation — wire format, transport, presence, conflict UI | `docs/specs/2026-05-04-d5-collab-activation-STUB.md` |

Status legend: ✅ done · 🟢 active · 🟡 dogfood (implementation done; awaiting user sign-off) · ⏸ stubbed (inputs and intended scope captured; substantive design deferred).

---

## 2. Where to start (by purpose)

| If you want to … | Read |
|---|---|
| Understand why we're doing D at all | `docs/handoff/2026-05-04-c-restoration-bookend-d-pivot.md` (the cut from C-restoration) |
| Understand the architectural premise | `docs/specs/2026-05-02-d-evolution-proposal.md` §3 (data model) |
| Understand collabtext's commitments and limits | `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` AND `docs/d-arc/collabtext-scope-line.md` (the six "won't do" items quoted verbatim) |
| Implement D2 | `docs/specs/2026-05-04-d2-foundation-reshape-design.md` (binding spec) — implementation plan to follow once writing-plans runs |
| Pick up D3 | `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` (original D3 spec; non-ListItem sections still authoritative) AND `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` (corrective spec for ListItem; active subject) |
| Review D4 (complete) | `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` (spec) and `docs/plans/2026-05-07-d4-parser-scope-reduction.md` (plan) |
| Pick up D5 | `docs/specs/2026-05-04-d5-collab-activation-STUB.md` — substantive design needed before execution |
| Track D2 progress in real time | `docs/d-arc/d-arc-status.md` |
| Audit a design decision in D2 | The corresponding "Why this and not the alternatives" subsection in `docs/specs/2026-05-04-d2-foundation-reshape-design.md` (every major section ends with one) |

---

## 3. Cross-arc binding constraints

Two constraints bind every phase of D:

### 3.1 The collabtext scope line (six "won't do" items)

The collabtext maintainers committed to Option β (the `IdList` primitive) under explicit scope conditions. These are recorded verbatim in `docs/d-arc/collabtext-scope-line.md`. **No D-arc spec may take a design decision that depends on collabtext shipping any of the six refused items.** If a design pressure arises that pushes against one of these lines, the answer is "build it Markoff-side or use an external dependency" — never "ask collabtext to extend the line."

### 3.2 Carried-forward C-restoration decisions

The C-restoration's lower-layer architectural decisions (L0–L3, the discriminated cursor model, the layered library structure, the inline span pre-bake, the per-layer test contract) carry forward to D2 as authoritative inputs. Specifically:

- **Discriminated cursor model** (Shape 1; `TextCaret | BlockSelected | BlockInternalEdit`) per `docs/specs/2026-05-02-live-render-restoration-design.md` §3 — D2 reuses this shape unchanged with `BlockId` rebound to an `IdList` element id.
- **Layered library structure** (L0 coords → L8 interactive blocks) — D3 picks this up; D2 doesn't touch it.
- **Inline span pre-bake** (R1B's `TopLevelBlock::inlineSpans`) — repurposed as the per-block `InlineParseCache` in D2.
- **Per-layer test contract** ("each layer Lₙ tests against L_{k<n} as real, L_{k>n} absent") — survives in D3.

The C-restoration's *upper-layer* decisions (the source-of-truth protocol, sequence-tagged staleness, marker-paragraph machinery, `LiveEditBinding` cycle guards) all retire — they were workarounds for the parser-vs-CRDT race that D removes structurally.

---

## 4. Document organization

```
docs/
├─ d-arc/
│  ├─ 2026-05-04-d-arc-roadmap.md            ← this file (orientation)
│  ├─ d-arc-status.md                         ← live status board for active phase
│  └─ collabtext-scope-line.md                ← the six "won't do" items, verbatim
├─ specs/
│  ├─ 2026-05-02-d-evolution-proposal.md     ← D0 Markoff side (the proposal)
│  ├─ 2026-05-04-d2-foundation-reshape-design.md  ← D2 (active)
│  ├─ 2026-05-04-d3-view-layer-adaptation-STUB.md
│  ├─ 2026-05-04-d4-parser-scope-reduction-STUB.md
│  ├─ 2026-05-04-d5-collab-activation-STUB.md
│  └─ 2026-05-02-live-render-restoration-design.md  ← C spec (lower layers carry forward)
├─ handoff/
│  └─ 2026-05-04-c-restoration-bookend-d-pivot.md  ← the cut from C to D
├─ plans/
│  ├─ 2026-05-04-d2-foundation-reshape.md          ← D2 plan (complete)
│  └─ 2026-05-05-d3-view-layer-adaptation.md       ← D3 plan (pending writing-plans)
└─ ...
```

---

## 5. Update protocol

When a phase advances:

- The active phase's status badge above changes from ⏸ stubbed to 🟢 active (or 🟢 to ✅ done).
- A new line in `docs/d-arc/d-arc-status.md` records the change with a date and one-sentence summary.
- If a stub spec graduates to a substantive spec, update the "Read" column above to point at the substantive spec.
- The worktree `CLAUDE.md` banner gets a one-line update if the active phase changes.

When the D-arc completes (D5 done): this roadmap stays in tree as historical context, the status board is closed (analog of how `docs/restoration-status.md` was closed at the C-restoration bookend), and a new active arc may begin or the foundation-exploration branch may merge to master.
