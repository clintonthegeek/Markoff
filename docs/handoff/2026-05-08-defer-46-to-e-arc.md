# Defer §4.6 (public-API freeze + Corbomite migration); E-arc begins now

**Date:** 2026-05-08
**Branch:** `exploration/new-foundation`
**Status:** authoritative — overrides pivot-doc §4.6/§4.7 ordering and the
E-arc-framing §4 prerequisite list.

---

## What changed

`docs/handoff/2026-05-07-pivot-to-d5-first.md` §4 ordered the post-D5 work as:

> §4.5 audit → §4.6 public-API freeze + Corbomite migration → §4.7 E-arc.

§4.5 landed 2026-05-08. **§4.6 is deferred until Corbomite is ready to
consume the freeze.** E-arc begins now in §4.6's slot. E1 (inline-format
highlighter) is the next active phase.

§4.6 is **deferred**, not **cancelled**. It runs after E-arc ships, or
earlier if Corbomite signals readiness mid-arc.

---

## Why

§4.6 has two halves:

1. Lock down `Markoff::MarkdownView`, `Markoff::MarkoffDocument`,
   `Markoff::CursorPos`, etc. as a stable consumer surface.
2. Write the Corbomite migration guide.

Half (2) is unproductive while Corbomite isn't ready to act on it — a guide
written today goes stale before it ships, and the iteration loop with the
real consumer is what keeps a migration guide honest. Half (1) is
unproductive without (2) because freezing in the dark risks freezing
primitives that E-arc work would re-shape, exactly the thrash the pivot doc
was written to prevent.

E-arc, by contrast:

- Touches widget-internal types (`InlineFormatHighlighter`, projection-layer
  types, delegate registries) that the framing-doc §1.2 already declared
  out-of-scope for the §4.6 freeze.
- Will *inform* what the right shape of the public API is: building features
  against the current surface is the cheapest way to discover which
  primitives need reshaping before freeze.
- Was always going to ship before any new-widget arcs.

The clean ordering inverts: build E-arc against the current surface, let
E1–E5 expose the seams, then freeze a public API that has actually been
exercised under load.

This is consistent with operating principle 3 of the pivot doc —
*speculative architectural change requires a measured premise*. §4.6 today
is speculative (no ready consumer); §4.6 post-E-arc is informed.

---

## Pivot-doc §4.7 sequencing argument, reconsidered

The pivot doc §4.7 said: *"E-arc work does not start until §4.6 ships —
building features against an unfrozen public surface throws work away."*

The framing doc §4 echoed: *"if §4.6's freeze re-shapes a primitive E1 was
about to use, E1 wastes work."*

Both arguments assumed §4.6 was about to land. With Corbomite not ready,
§4.6 is not about to land — it's pending an external readiness signal that
may be weeks out. Holding E-arc on §4.6 in that state is the work-throwing
risk, not the protection against it. The inversion: build E-arc, surface
the seams, freeze the post-E-arc surface.

---

## What stays the same

- Pivot-doc §4.5 (already landed) stands.
- D-arc invariants and operating principles stand.
- "One arc at a time" still applies — **E-arc is the active arc.** No work
  on §4.6, no work on speculative new widgets, no opportunistic touches.
- The framing-doc §1.2 statement that E-arc internals are out-of-scope for
  the §4.6 freeze stands. E-arc adds nothing that retroactively belongs in
  the freeze.
- Framing-doc §5 invariants (subtractability notes, single-user default,
  no `Corbomite`-named types in public API, append-only `master`) still
  bind every E-phase.

---

## What this changes in tree

- `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.6 carries a banner
  noting the deferral; §4.7 carries a banner noting E-arc begins now.
- `docs/specs/2026-05-08-e-arc-framing.md` carries a §0.1 amendment
  noting the §4.6 prerequisite is removed.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` §4.1 prerequisite list updated.
- `docs/d-arc/d-arc-status.md` TL;DR + active phase + recent-changes log
  updated.
- `docs/e-arc/e-arc-status.md` created as the live status board.
- The worktree `CLAUDE.md` banner updated to point at this doc as part of
  the fresh-agent reading list.

---

## Resumption criteria for §4.6

§4.6 resumes when **all** of the following hold:

1. E-arc has shipped at least through E2 (inline-highlighter + delimiter
   visibility), so the public-API surface has been exercised by a
   non-trivial feature consumer.
2. Corbomite signals readiness to act on the migration guide — concretely,
   the Corbomite side has scheduled the migration into a planning cycle and
   has a target version pin in mind.
3. The user explicitly authorises §4.6 to begin. The pivot-doc §4.6 banner
   is updated at that point to remove the deferred status.

Mid-E-arc resumption is allowed if (2) lands early — in that case the active
E-phase finishes to a green-tree checkpoint, then §4.6 runs, then E-arc
resumes from the next phase.

---

## See also

- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — D-arc-era pivot doc.
- `docs/specs/2026-05-08-e-arc-framing.md` — E-arc constitutional framing.
- `docs/e-arc/2026-05-08-e-arc-roadmap.md` — E-arc orientation.
- `docs/e-arc/e-arc-status.md` — E-arc live status board.
