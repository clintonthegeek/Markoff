# D-arc roadmap — orientation for fresh contexts

**Date:** 2026-05-07
**Branch:** `exploration/new-foundation`
**Purpose:** This is the canonical orientation doc for the D-evolution work arc. An empty agent context picking up any phase of D should read this first to understand where each piece lives, what depends on what, what's been decided vs deferred, and what the binding cross-arc constraints are.

---

## 1. Phase summary

D = "per-block CRDT + structural CRDT" — the long-term Markoff foundation architecture. Replaces the C-restoration arc that bookended on 2026-05-04 (see `docs/archive/c-restoration-arc/2026-05-04-c-restoration-bookend-d-pivot.md`). The arc is divided into six phases, named D0 through D5.

| Phase | Status | Owner | Deliverable | Read |
|---|---|---|---|---|
| **D0** | ✅ done | joint (Markoff + collabtext maintainers) | Architectural alignment: per-block CRDT direction + collabtext supporting primitive | `docs/specs/2026-05-02-d-evolution-proposal.md` (Markoff side) and `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` (collabtext side) |
| **D1** | ✅ done | collabtext maintainers | `CollabText::Crdt::IdList` primitive shipped (opaque uint64 elements, sharing the existing Anchor / Operation / Undo / GC machinery) | collabtext header `~/dev/collabtext/include/collabtext/Crdt/IdList.h` |
| **D2** | ✅ done | Markoff | Foundation reshape — `Markoff::MarkoffDocument` rebuilt on `IdList` + per-block `Buffer`s + sibling causal-LWW maps | `docs/specs/2026-05-04-d2-foundation-reshape-design.md`; status `docs/d-arc/d-arc-status.md` |
| **D3** | ✅ done | Markoff | View-layer adaptation — cursor delivery redesign; inline span consumption; kind-transition detection; L6/L7/L8 full delegates; per-block undo UI. ListItem path corrected per the post-dogfood corrective spec. | Original: `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`. Corrective: `docs/specs/2026-05-06-per-item-listitem-blocks-design.md`. |
| **D4** | ✅ done | Markoff | Parser scope reduction — retired `ParsePool` / `IncrementalParseSession` / `parseUpdated` / `MarkoffEdit` / `applyLocalEdit`; source-widget migrated to D2 via `applyFlatEdit`; markoff-bench and view-qml live mode retired; dead legacy `Cmd::*` + `CommandFacade` + `ReplaceController` deleted | Spec: `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md`. Plan: `docs/plans/2026-05-07-d4-parser-scope-reduction.md`. |
| **D5** | 🟢 plan-approved | Markoff (boundary) + consumer host (transport/identity/multi-user) + collabtext (delivered 2026-05-08) | Collab activation — boundary API on `MarkoffDocument`, sibling-map sync, watermark/ack mechanics, presence, reference test harness. Transport and identity are consumer-owned (three-layer widget model). Collabtext OpStream extraction landed 2026-05-08; Phase 0 prerequisite met; Phase 1 ready. | Spec: `docs/specs/2026-05-07-d5-collab-activation-design.md` (§0.1.1 acceptance addendum 2026-05-08). Plan: `docs/plans/2026-05-08-d5-collab-activation.md`. Acceptance: `docs/handoff/2026-05-08-collabtext-d5-acceptance.md`. Negotiation opener: `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md`. Retired stub: `docs/archive/2026-05-04-d5-collab-activation-STUB.md`. |

Status legend: ✅ done · 🟢 active (`spec-approved` / `plan-approved` / `in-progress`) · 🟡 dogfood (implementation done; awaiting user sign-off) · ⏸ stubbed (inputs and intended scope captured; substantive design deferred).

---

## 2. Where to start (by purpose)

| If you want to … | Read |
|---|---|
| Understand why we're doing D at all | `docs/archive/c-restoration-arc/2026-05-04-c-restoration-bookend-d-pivot.md` (the cut from C-restoration) |
| Understand the architectural premise | `docs/specs/2026-05-02-d-evolution-proposal.md` §3 (data model) |
| Understand collabtext's commitments and limits | `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` AND `docs/d-arc/collabtext-scope-line.md` (the six "won't do" items quoted verbatim) |
| Implement D2 | `docs/specs/2026-05-04-d2-foundation-reshape-design.md` (binding spec) — implementation plan to follow once writing-plans runs |
| Pick up D3 | `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` (original D3 spec; non-ListItem sections still authoritative) AND `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` (corrective spec for ListItem; active subject) |
| Review D4 (complete) | `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` (spec) and `docs/plans/2026-05-07-d4-parser-scope-reduction.md` (plan) |
| Pick up D5 | `docs/specs/2026-05-07-d5-collab-activation-design.md` (substantive spec, approved 2026-05-08) AND `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md` (collabtext maintainer-facing follow-up) |
| Track D2 progress in real time | `docs/d-arc/d-arc-status.md` |
| Audit a design decision in D2 | The corresponding "Why this and not the alternatives" subsection in `docs/specs/2026-05-04-d2-foundation-reshape-design.md` (every major section ends with one) |

---

## 3. Cross-arc binding constraints

Two constraints bind every phase of D:

### 3.1 The collabtext scope line (six "won't do" items)

The collabtext maintainers committed to Option β (the `IdList` primitive) under explicit scope conditions. These are recorded verbatim in `docs/d-arc/collabtext-scope-line.md`. **No D-arc spec may take a design decision that depends on collabtext shipping any of the six refused items.** If a design pressure arises that pushes against one of these lines, the answer is "build it Markoff-side or use an external dependency" — never "ask collabtext to extend the line."

### 3.2 Carried-forward C-restoration decisions

The C-restoration's lower-layer architectural decisions (L0–L3, the discriminated cursor model, the layered library structure, the inline span pre-bake, the per-layer test contract) carry forward to D2 as authoritative inputs. Specifically:

- **Discriminated cursor model** (Shape 1; `TextCaret | BlockSelected | BlockInternalEdit`) per `docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` §3 — D2 reuses this shape unchanged with `BlockId` rebound to an `IdList` element id.
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
│  ├─ 2026-05-02-d-evolution-proposal.md           ← D0 Markoff side (the proposal)
│  ├─ 2026-05-04-d2-foundation-reshape-design.md   ← D2 (complete)
│  ├─ 2026-05-04-d3-view-layer-adaptation-STUB.md  ← D3 stub (superseded by substantive spec below)
│  ├─ 2026-05-04-d4-parser-scope-reduction-STUB.md ← D4 stub (superseded by substantive spec below)
│  ├─ 2026-05-05-d3-view-layer-adaptation-design.md ← D3 substantive (complete)
│  ├─ 2026-05-06-per-item-listitem-blocks-design.md ← D3 corrective (complete)
│  ├─ 2026-05-07-d4-parser-scope-reduction-design.md ← D4 substantive (complete)
│  └─ 2026-05-07-d5-collab-activation-design.md    ← D5 substantive (spec-approved 2026-05-08)
├─ handoff/
│  ├─ 2026-05-07-pivot-to-d5-first.md              ← D5-first posture (authoritative)
│  ├─ 2026-05-07-live-binding-developmental-history.md ← pipeline provenance
│  └─ 2026-05-07-collabtext-d5-negotiation-opener.md ← D5 maintainer-facing follow-up
├─ plans/
│  ├─ 2026-05-04-d2-foundation-reshape.md          ← D2 plan (complete)
│  ├─ 2026-05-05-d3-view-layer-adaptation.md       ← D3 plan (complete)
│  └─ 2026-05-07-d4-parser-scope-reduction.md      ← D4 plan (complete)
├─ archive/
│  ├─ c-restoration-arc/                           ← retired arc papers
│  ├─ v1.0-plan-pre-d5/                            ← retired v1.0 plan series
│  └─ 2026-05-04-d5-collab-activation-STUB.md      ← D5 stub (retired 2026-05-08)
└─ ...
```

---

## 5. Update protocol

When a phase advances:

- The active phase's status badge above changes from ⏸ stubbed to 🟢 active (or 🟢 to ✅ done).
- A new line in `docs/d-arc/d-arc-status.md` records the change with a date and one-sentence summary.
- If a stub spec graduates to a substantive spec, update the "Read" column above to point at the substantive spec.
- The worktree `CLAUDE.md` banner gets a one-line update if the active phase changes.

When the D-arc completes (D5 done + §4.5/§4.6 bookend per the pivot doc): this roadmap stays in tree as historical context, the status board is closed (analog of how `docs/archive/c-restoration-arc/restoration-status.md` was closed at the C-restoration bookend), and the **E-arc** begins.

---

## 6. What's next (E-arc)

After D-arc closes, the next arc is **E-arc** — live-render completion as the maximalist Markoff prototype. The QML live-render view is treated as the ice-breaker experimental prototype: every other view Markoff will ship is a structural subset of what live-render does once E-arc is complete.

**E-arc phases (sketch):**

- E1 — Inline-format highlighter in QML delegates
- E2 — Cursor-aware delimiter visibility (auto-hide markers unless caret in span)
- E3 — Wikilinks, embeds, tags, callouts (Obsidian-flavoured affordances)
- E4 — Tables, frontmatter, footnote rendering
- E5 — Math / Mermaid Live-mode parity with Reading mode
- E6 — Distillation: extract the foolproof view-construction recipe

**Read first:**

- `docs/specs/2026-05-08-e-arc-framing.md` — constitutional framing for E-arc
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — orientation doc, mirrors this file's structure

E-arc is named here so the post-D-arc trajectory is visible from D-arc orientation. E-arc work does not begin until D-arc's bookend (`pivot-to-d5-first.md` §4.5 + §4.6) ships.
