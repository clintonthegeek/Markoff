# Archived: C-Restoration arc paper trail

**Retired:** 2026-05-07 (final retirement; arc closed 2026-05-04).
**Retired by:** `docs/handoff/2026-05-07-pivot-to-d5-first.md` §2.2.

The C-restoration arc (R1–R5.5) ran 2026-05-02 → 2026-05-04, on the
`exploration/new-foundation` branch, as the planned restoration of the
live-render path on top of the parser-as-authoritative model. R5.5
dogfood surfaced an architectural-review § 3.1 question that resolved
into the marker-paragraph design — which itself was superseded one day
later by the D-evolution pivot (`docs/specs/2026-05-02-d-evolution-proposal.md`)
that replaced parser-as-authority with per-block CRDT
(D1/D2). All forward C-restoration work was cancelled at that pivot.

The pieces of the C-restoration design that survived the pivot
(L0–L3 layering, discriminated cursor model, layered library
structure, inline span pre-bake, the per-layer test contract) carried
forward into D2 and live in the current codebase. The pieces that
depended on parser-as-authority (the §4 source-of-truth protocol,
the §11 R5+ phase scope, the marker-paragraph mechanism) did not.

These documents are kept as historical context. They are **not
authoritative**, are **not to be executed**, and are **not to be cited
in new specs or plans except as historical context**. The D-arc
(`docs/d-arc/`) is the live record for everything they intended to
deliver.

## Contents (this directory)

### Plans (R1–R5.5)
- `2026-05-02-live-render-r1a-parse-edit-sequence.md`
- `2026-05-02-live-render-r1b-inline-span-bake.md`
- `2026-05-02-live-render-r1c-library-scaffold.md`
- `2026-05-02-live-render-r2-read-only-render.md`
- `2026-05-02-live-render-r3-cursor-selection.md`
- `2026-05-02-live-render-r4-paragraph-editing.md`
- `2026-05-02-live-render-r5-structural-keys.md`
- `2026-05-03-live-render-r5-5-marker-paragraph.md`

### Specs
- `2026-05-02-live-render-restoration-design.md` — the C-restoration
  architecture spec.
- `2026-05-03-marker-paragraph-design.md` — the marker-paragraph
  design that replaced v2 holes.

### SESSION-BRIEFs and bug/handoff docs
- `2026-05-02-restoration-session-brief.md`
- `2026-05-03-r4-paragraph-edit-tests-audit.md`
- `2026-05-03-r5-empty-paragraph-gap.md`
- `2026-05-03-r5-holes-postmortem.md`
- `2026-05-03-section-3-1-spike-findings.md`
- `2026-05-04-bug-3-handoff.md`
- `2026-05-04-marker-paragraph-execution-brief.md`
- `2026-05-04-r5.5-dogfood-architectural-review.md`
- `2026-05-04-c-restoration-bookend-d-pivot.md` — the bookend doc
  that closed the arc and authorised the D-evolution pivot.

### Status board
- `restoration-status.md` — the live status board the arc used while
  active. Frozen at the 2026-05-04 closing state.

### Already-archived (moved here for consolidation)
- `2026-05-03-live-render-r5-5-holes.md` — the original R5.5 plan
  (paragraph-holes approach), paused mid-implementation when § 3.1
  chose marker characters over holes; previously archived in-place.
- `2026-05-03-v2-holes-design.md` — the v2 holes design that the
  marker-paragraph design replaced; previously archived in-place.

## See also

- `docs/specs/2026-05-02-d-evolution-proposal.md` — the D-evolution
  proposal that closed this arc. Stays in `docs/specs/` because it
  remains authoritative as the D-arc kickoff document.
- `docs/d-arc/` — the live D-arc record.
- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — the unified-direction
  pivot doc that authorised this archive consolidation.
