# D5 — Collab activation (STUB)

**Date stub created:** 2026-05-04
**Status:** STUB — intended scope and inputs captured. **Substantive design happens after D2 / D3 / D4 land.** D5 is the largest open scope of the D arc (the proposal called it "open-ended depending on collab feature scope").

---

## What this stub is for

Orient a fresh agent context picking up D5. Not a substantive design — the substantive design is a sizable spec in its own right and likely a multi-month implementation effort.

---

## Inputs

1. **`docs/specs/2026-05-04-d2-foundation-reshape-design.md`** — the foundation. Of particular interest: §3 (BlockId stability), §4.7 (Origin enum + remote-edit handling), §7.4 (collab-evolution path for GC), §8 (signal API including remote-op surfaces).
2. **`~/dev/collabtext/include/collabtext/StreamSync.h`** (or current equivalent) — the existing transport primitive. Per the maintainer scope line item 5, Markoff composes `DocumentStructure` "directly on top of public `IdList` + `Buffer` + the existing `StreamSync` (which is already multi-stream and won't need changes — register one stream for structure plus one per block, payloads are already opaque to the transport)."
3. **`~/dev/collabtext/docs/research/2026-04-06-multi-cursor-widget-research.md`** — collabtext maintainers' existing thinking on live-cursor presence; cited in D-evolution-proposal §6.3 as relevant for D5's presence design.
4. **`docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` §3.5** — the cursor "survival under remote edits" rules that the C-restoration architected (and D2 §4.7 carries forward). D5 implements the wire format and presence; the local survival logic is already specified.
5. **`docs/d-arc/collabtext-scope-line.md`** — what collabtext won't do for D5. Specifically: no `CollabDocument` wrapper (D5 wires transport directly via `StreamSync`), no further primitives.

---

## Intended scope

D5 designs and implements:

1. **Wire format / multiplexing.** Per-CRDT op streams over `StreamSync`: one stream for `IdList` ops, one per `Buffer` (per-block) ops. Payloads opaque to the transport (collabtext handles the within-stream serialization). Stream enrollment/unenrollment as blocks are created/deleted.
2. **Presence.** Live-cursor broadcast: each replica publishes its current `Markoff::Cursor` (Shape 1 discriminated union per C-spec §3); receiving peers translate through their local CRDTs and render the remote cursor. Presence is a separate stream (or a separate channel within transport).
3. **Conflict UI.** What does the user see when a remote edit arrives during their typing? When their block is deleted underneath them? When a remote rename overwrites their kind change? Largely handled by D2's local rules (cursor falls back, orphan disposal); D5 decides whether/how to surface notifications.
4. **Replica-ack tracking.** Required by D2's GC collab-evolution (D2 §7.4) — `WatermarkCoordinator` needs to know when all known replicas have ack'd up to the snapshot watermark. Design the ack tracker, the "known replicas" set (joining/leaving), and the failure case (a replica that goes silent indefinitely).
5. **Session model.** What constitutes a collab session? File-scoped? Document-scoped (multiple files in one session)? Markoff-instance-scoped (multiple sessions across multiple files)? Substantial UX surface.
6. **Authentication / authorization.** Out of scope for collabtext (which is transport-agnostic); D5 owns the policy layer for "who can join this document?" Likely host-application-level (CorbomiteApp or whatever future host).
7. **Persistence under collab.** When a single replica saves to disk, does that save include op history from remote replicas not yet received locally? How does the watermark interact with disk persistence under multi-replica writes?

---

## Explicitly out of scope

- **Server infrastructure.** Markoff is peer-to-peer via the existing transport (Syncthing-class). D5 does not build a centralized collab server.
- **Authentication infrastructure.** D5 designs the authorization layer (who can do what); the auth itself is host-app concern.
- **Selection broadcast** (highlighting another user's selection range, not just their cursor). Possible follow-up; D5 can decide whether to include or defer.

---

## Open questions for D5 brainstorm time

This is where the largest design space sits in the D arc. Examples:

- Single-document vs multi-document sessions
- Conflict notification UX (silent / banner / inline / modal)
- Replica identity (anonymous? user-named? cryptographic?)
- Disconnected-then-reconnect semantics (rejoin transparently? show diff? resolve manually?)
- What counts as a "known replica" for GC ack tracking
- Performance under high-replica counts (10? 100? 1000?)
- Wire format versioning across replicas with different Markoff versions
- Plugin block kinds in collab (a remote replica that doesn't have the math plugin sees a math block — what does it do?)

---

## Why D5 is last

The D-evolution proposal §7.6 framed it: "With per-block CRDTs and a structural CRDT, the collab story is genuinely well-shaped. Wire format, presence, conflict semantics — all benefit. D5 is where collab actually ships, with the architecture having been collab-ready from C onwards."

D2 + D3 + D4 deliver a single-user editor on a collab-ready architecture. D5 is the activation step. Until D2 ships (foundation actually exists in per-block-CRDT form) there's nothing for D5 to wire transport against, so D5 cannot start substantive design until D2 implementation is at least at API-stable.

---

## Brainstorming checklist (when ready)

D5 is large enough that it might decompose into sub-phases (D5a wire format, D5b presence, D5c session model, etc.). The brainstorming will likely flag this immediately.

1. Read inputs above.
2. Invoke `superpowers:brainstorming`.
3. **Assess scope first** — D5 may need decomposition into sub-phases before substantive design.
4. Resolve scope question; brainstorm the first sub-phase substantively.
5. Write substantive D5 spec(s), replace this stub. Update D-arc status board + roadmap.
