# Collabtext × Markoff D5 — negotiation opener

**Date:** 2026-05-07
**Status:** opening of dialogue.
**From:** Markoff design (clinton@concernednetizen.com).
**To:** collabtext maintainers.
**Companion:** `docs/specs/2026-05-07-d5-collab-activation-design.md` (Markoff's D5 spec, written under the assumption your current shape works as-is).
**Predecessor:** `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` (your D-evolution response that produced the six-item scope line).

---

## 0. Posture

This document is the **opening of a negotiation**, not a request-for-implementation. We've designed Markoff's D5 collab boundary against the collabtext shape that exists today. That design works. We're not asking you to refactor in order for D5 to ship.

What we *are* asking: take a look at a refactor we believe would meaningfully improve the consumer story for any user of the Markoff + collabtext pair (including, possibly, a Markoff-as-collabtext-demo arrangement we'd be open to). Tell us yes, no, different-shape, or "later." We'll incorporate whatever you decide.

The 2026-05-04 six-item scope line stays binding. None of the items here are re-litigation. This is a follow-on conversation, not a re-opening.

---

## 1. Context

### 1.1 What's happened on the Markoff side since D-evolution

D2 / D3 / D4 all shipped on `exploration/new-foundation`. D2 implemented the per-block CRDT foundation against your `IdList` + `Buffer` primitives; D3 adapted the view layer; D4 retired the legacy parser pipeline. 103/103 tests pass at branch tip.

In parallel, the Markoff team attempted a "v1.0" plan that would freeze a public consumer API and ship a Corbomite migration guide. That plan was retired before any phase began (`docs/handoff/2026-05-07-pivot-to-d5-first.md`) on the basis that **D5 needed to ship before any API freeze made sense**. The branch is now on D5-first posture.

### 1.2 The new framing: three-layer widget model

The retired v1.0 plan implicitly framed Markoff as the primary, with collabtext as a substrate and Corbomite (the eventual host) as a downstream consumer. The new framing is:

- **Markoff is a widget.** It plugs into a host.
- **Collabtext is a widget.** It plugs into a host.
- **The host (Corbomite, eventually; possibly others before then) owns the orchestration**: transport choice, identity policy, multi-user logistics, side channels, the wiring between the two widgets.

At runtime, **Markoff and collabtext do not talk directly**. All op routing flows through the host. The static dependency stays — Markoff `#include`s your headers and links your CRDT code — but no runtime call from one widget to the other exists.

This frame is what motivates this opener. Several pieces of the 2026-05-04 scope line presupposed Markoff sitting *on top of* collabtext at runtime, which the new frame rejects.

---

## 2. What we're asking, and why

The ask in one sentence:

> **Expose collabtext's CRDT primitives + serialisation behind a transport-agnostic boundary, with `StreamSync` and `SyncManager` as a reference implementation rather than the only available surface.**

The current shape bundles three concerns into the public surface: CRDT primitives (`Buffer`, `IdList`, `Anchor`, `Lamport`), op serialisation, and a file-based transport (`StreamSync` reading/writing per-replica segment files in a shared folder). For a host that wants to wire collabtext under a different transport — network, in-memory mock for tests, hybrid transports, future technology choices — the host has to either route around `StreamSync` or extract the per-stream codec from inside it.

What we'd want from the refactor:

- A **transport-agnostic op-stream surface** (call it `OpStream` or whatever you prefer) that exposes: per-stream `push(blob)` / `onInbound(callback)` / per-peer ack-frontier reporting / catch-up replay.
- `StreamSync` keeps existing, possibly renamed (`FileBackedOpStream`?), as **one reference implementation** of that surface.
- The CRDT primitives' op-serialisation path is **separable from the transport choice**. A consumer can serialise a `Buffer` op into bytes without going through `StreamSync`.
- `SyncManager` and `Identity::*` either stay collabtext-internal (used by `StreamSync`'s reference implementation) or become **opt-in** layers that hosts can use or ignore.

Why we'd want this:

1. **The host owns the transport.** Under the three-layer model, the host (Corbomite or other) chooses *how* peers communicate. Tying CRDT use to a single transport ties the host's hands.

2. **Test ergonomics.** Two-replica convergence tests are tier-0 confidence verification for any CRDT-backed application. With a clean op-stream surface, the test is "spin up two `MarkoffDocument`s, route their op signals to each other in-process, run divergent edits, assert convergence." Today the alternative is filesystem fixtures or invasive mocking.

3. **The Markoff-as-collabtext-demo possibility.** You currently ship `CollabPlainTextEdit` as your demo consumer. Markoff's reference test harness (`apps/markoff-collab-testapp/`, planned in the D5 implementation) is a candidate richer demo — multi-block CRDT, structural ops, sibling maps, presence — exercising more of your library's surface. For us to ship that comfortably, we'd want the test harness's transport to be in-memory (for determinism), not file-based.

4. **Future-proofing.** Other hosts will want collabtext under transports we haven't imagined. The current shape works for "Syncthing-backed shared folder" and not much else without consumer-side acrobatics.

---

## 3. What we'd accept as alternatives

We've thought about three ways the refactor could land. Listed in our preference order; all three are acceptable. We're confident there are shapes we haven't thought of that would also work; treat this list as a starting point, not a ranked menu.

### Alternative A (preferred) — Extract `OpStream` as a separate type

A new type, name negotiable, that defines the transport-agnostic surface. `StreamSync` becomes a class implementing that surface. Consumers can implement their own. The CRDT primitives are agnostic of which `OpStream` implementation they're connected to.

- Pros: cleanest separation; future transports plug in without touching collabtext core; reference implementation stays.
- Cons: most refactor work; some API surface change.

### Alternative B — Make `StreamSync` virtualisable

Add virtual methods to `StreamSync`'s public surface so consumers can subclass it for non-file transports. Keep `StreamSync` as both the surface and a concrete file-based implementation; alternative implementations subclass.

- Pros: smaller refactor; existing `StreamSync` consumers untouched.
- Cons: subclassing is heavier than composition; the file-based assumptions baked into the base class might leak; likely less clean over time.

### Alternative C — Expose the per-stream codec separately, leave `StreamSync` alone

Don't refactor `StreamSync`. Instead, expose collabtext's per-CRDT op (de)serialisation as a separate public API that consumers can use without going through `StreamSync`. Consumers wanting alternative transports take responsibility for plumbing the bytes themselves; collabtext just gives them serialisation primitives.

- Pros: smallest refactor; `StreamSync` semantics untouched.
- Cons: each consumer reinvents the transport wrapper; convergence-on-the-wire is harder to standardise.

We can adapt to any of these. We'd prefer A; B is fine; C is fine. A "do nothing, current shape stays" outcome is also workable from our side — Markoff's D5 spec was written to be shape-independent — but we believe the upside of even alternative C is worth the conversation.

---

## 4. What stays the same

The 2026-05-04 six-item scope line still holds, every item:

1. **No `moveAfter` in v1.** Markoff D5 expresses moves as remove + insert; we've accepted intent loss. Confirmed.
2. **No per-element values, ever.** Markoff's sibling maps (kind, attrs, link refs, footnote defs, frontmatter) are application-side LWW, not collabtext primitives. Confirmed.
3. **No cross-CRDT undo log primitive.** Markoff's `UndoLog` is application-side. Confirmed.
4. **No cross-CRDT GC coordination primitive.** Markoff's `WatermarkCoordinator` is application-side. D5 wires the per-peer ack predicate consumer-side. Confirmed.
5. **No `CollabDocument` generalisation.** *Slightly affected by this opener.* The current scope-line text is "Markoff composes `DocumentStructure` directly on top of public `IdList` + `Buffer` + the existing `StreamSync` (which is already multi-stream and won't need changes — register one stream for structure plus one per block, payloads are already opaque to the transport)." Under the new three-layer frame, **Markoff doesn't compose against `StreamSync` at runtime**; the host does. The substance of item 5 (no `CollabDocument`) is unchanged; the implication that Markoff calls `StreamSync` directly is what we're asking to revisit.
6. **No precedent for further primitives.** Confirmed. Markoff is not asking for `Map`, `Tree`, or `Counter` primitives.

The asks above are not additions to the "we'll do this" list. They're a request to reshape the boundary of what already exists.

---

## 5. What we offer

### 5.1 A real (non-trivial) test consumer

Markoff's D5 implementation includes a reference test harness at `apps/markoff-collab-testapp/`. It's planned to be:

- Two-window in-process testapp wiring two `MarkoffDocument`s through an in-memory `ITransport` mock.
- Manual dogfood: type in window A, observe in window B, undo, observe undo.
- Convergence tests routed through the mock (proves the boundary works through a router, not just direct method calls).

If you'd find this useful as a demo consumer for collabtext alongside or in place of `CollabPlainTextEdit`, we'd be happy to coordinate. Markoff's testapp exercises significantly more of collabtext's surface (multi-block CRDT, sibling maps, presence) than a single `Buffer`-backed plain text edit can.

### 5.2 Joint-design review on the new boundary

We'd welcome a joint-design pass on the refactored boundary, mirroring the 2026-05-04 D-evolution exchange. We have opinions about what would help us; you have opinions about what would fit collabtext's broader trajectory. Resolving those in dialogue produces a better outcome than either side designing in isolation.

### 5.3 Implementation feedback

As Markoff D5 implementation proceeds, we'll surface anything that's awkward against the current shape, friction we hit, places where we wished a boundary existed. Those reports inform whatever refactor you do (or don't) ship.

---

## 6. Timeline / posture

Markoff D5 implementation begins as soon as this spec lands writing-plans output (probably within days of this opener). We do not gate on your response.

If you respond with "yes, refactor coming, here's the timeline":
- We adapt the consumer wiring sketch in the D5 spec to the new collabtext shape.
- The reference test harness uses the new clean surface.
- We coordinate timing on Markoff-as-demo-consumer if that's of interest.

If you respond with "different shape, here's what we'd do":
- We discuss; we adapt; we go.

If you respond with "no" or "later":
- Markoff D5 ships against the current shape.
- The integration sketch documents the consumer's job under the current shape, which is workable but heavier than alternative A would produce.
- We re-open this conversation in 6+ months if it still seems worth it.

Any of these is fine for us. We'd value the response, not the answer.

---

## 7. What we'd ask of you

A response. Not urgent — Markoff D5 implementation has its own pace and isn't waiting. But before we lock the spec into "this is how we treat collabtext forever," knowing your direction would let us write the integration story once instead of twice.

If a response involves "we want to know more about X before answering," ask. We can elaborate on any part of this.

---

## 8. References

- Markoff D5 design spec (companion): `docs/specs/2026-05-07-d5-collab-activation-design.md`
- Markoff D5-first pivot doc: `docs/handoff/2026-05-07-pivot-to-d5-first.md`
- Markoff D-arc roadmap: `docs/d-arc/2026-05-04-d-arc-roadmap.md`
- Cross-arc constraint record: `docs/d-arc/collabtext-scope-line.md`
- Collabtext D-evolution response (2026-05-04): `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md`
- Markoff D2 foundation spec (the per-block CRDT shape): `docs/specs/2026-05-04-d2-foundation-reshape-design.md`
- Markoff developmental-history record (informs operating principles): `docs/handoff/2026-05-07-live-binding-developmental-history.md`

---

*Opening of dialogue. Whatever you reply, we'll incorporate.*
