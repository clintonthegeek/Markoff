# Markoff positions for the collabtext joint-design pass

**Date:** 2026-05-08
**Re:** collabtext follow-up: *"Two design calls still open that the joint-design pass with Markoff should resolve before we start writing public headers."*
**Predecessor:** `~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md`
**Companion:** `docs/specs/2026-05-07-d5-collab-activation-design.md` (Markoff D5 spec, §0.1 in particular).
**Audience:** collabtext maintainers; Markoff implementer of D5; whoever runs the joint-design pass.

This document records Markoff's incoming positions on the two design calls collabtext flagged. It is not the *outcome* — that's the joint-design pass's job. The format mirrors the structure used in 2026-05-04 D-evolution exchanges: requirement, preference with reasoning, things explicitly out of scope.

---

## 1. Public type surface for `Operation` / `IdListOperation`

**Question collabtext is asking:** which fields of `Operation` and `IdListOperation` are *contract* (consumers can rely on field layout / names / types), versus *evolution-reserved* (fields may be renamed, retyped, removed, or added across `schema_version` bumps; consumers must round-trip via encode/decode rather than touching fields directly)?

### 1.1 Markoff's actual requirement

Markoff treats `Operation` and `IdListOperation` as **opaque round-trippable values** in 95% of the boundary path. The flow is:

```
MarkoffDocument's local op → encode_operation(op) → bytes → MarkoffOp::payload → wire
wire → MarkoffOp::payload → decode_operation(bytes) → op → applyRemoteOp(op)
```

Markoff never inspects the inside of a Buffer or IdList op on the receive side. The CRDT engine handles it. So the dominant requirement is:

> **`encode_operation` / `decode_operation` (and the IdList variants) must round-trip across `schema_version` bumps.** A peer running collabtext version N+1 can still emit ops that a peer at version N understands — additively — or, at minimum, a peer at version N can detect "this op is from a future schema, skip it" without crashing.

Equivalently: **field-level access from Markoff is not in our requirement set.** We don't unpack fragments, we don't read anchors directly, we don't construct `Operation`s by hand.

### 1.2 What we DO need field-level access to (small list)

There is one place Markoff peeks inside an op: **building `MarkoffBundleMeta::producerLamport`** (D5 spec §2.3). At local-op-emission time, we need the Lamport of each op the transaction registered, so we can record the maximum Lamport in the bundle metadata for ordering replay/audit logs across replicas.

Concretely, we need:

- **`Operation::lamport()` (or equivalent accessor)** — returns the `Lamport` value of the op. Stable.
- **`IdListOperation::lamport()`** — same, for IdList ops.
- **`Lamport::counter()` and `Lamport::replica_id()`** — to extract `quint64` and `quint16` for our bundle metadata. Stable.

That's it for the contract surface. Everything else — fragments, anchors, internal segment data, op variant tags, anything specific to the within-CRDT representation — stays **evolution-reserved**.

### 1.3 Markoff's position

**Contract surface (Markoff requires stable):**

| Surface | Why |
|---|---|
| `Operation::lamport() -> Lamport` | Build `MarkoffBundleMeta::producerLamport` at emission time. |
| `IdListOperation::lamport() -> Lamport` | Same. |
| `Lamport::counter() -> uint64_t` | Embed in bundle meta. |
| `Lamport::replica_id() -> uint16_t` | Embed in bundle meta. |
| `encode_operation(Operation) -> string` | Markoff serialiser wraps this. Round-trip is the contract. |
| `decode_operation(string) -> optional<Operation>` | Inverse. Returns `nullopt` on schema mismatch — we want a typed signal, not a thrown exception. |
| `encode_idlist_operation(IdListOperation) -> string` | Same. |
| `decode_idlist_operation(string) -> optional<IdListOperation>` | Same. |

**Evolution-reserved (Markoff explicitly does not depend on):**

- Field layout of `Operation` / `IdListOperation` — order of members, padding, presence/absence of internal fields.
- Internal types: `Fragment`, `Anchor`, `Locator`, segment metadata. Markoff never names these in code.
- Op-variant discriminators (insert/delete/retain/etc.) — Markoff's `applyRemoteOp` doesn't switch on these; collabtext does.
- Any `Global` / clock / vector-clock state internal to the engine.

**Constructibility:** Markoff does NOT need to construct `Operation` or `IdListOperation` from individual fields. We only obtain them via `decode_*` or via `setOnLocalOp` callbacks. If your refactor keeps these constructors private or only-accessible-through-the-engine-internals, that's fine with us.

### 1.4 What we'd accept

- A header that exposes the four encode/decode functions, the `Lamport` accessors, and forward declarations of `Operation` / `IdListOperation` + their `lamport()` methods — and nothing else. Field layout fully reserved.
- Or: a header that exposes the full struct definitions but with a written contract ("only `lamport()` is stable; other fields may evolve") attached. Higher risk of consumer drift; cleaner from collabtext's internal perspective.

We prefer the first form (smaller contract surface = fewer things to evolve). But if the second form is significantly easier on your side — e.g., because `Operation` has to be passed by value through public APIs and forward-declaration is awkward — we'll take it. The written contract carries the weight either way.

### 1.5 What we want to avoid

- Field-level inspection by consumers turning into a de facto contract over time. The 2026-05-04 scope-line item 6 ("no precedent for further primitives") is the analogue here: we want to set the precedent now that field-level access from consumers is *not* the path. Round-trip via encode/decode is the path.
- A version-skew silent failure mode where a future-schema op decodes as a partial value that misbehaves. `decode_operation` returning `optional` (or equivalent typed-failure signal) lets the consumer detect and skip.

---

## 2. Ack-frontier file format — extend presence vs sibling `acks.json`

**Question collabtext is asking:** does the per-peer ack-frontier publication (response item 5) live as additional fields inside the existing presence file, or as a sibling file (proposed name `acks.json`)?

### 2.1 What ack-frontier needs to do, from Markoff's view

D5 spec §3.4–§3.5 describes the watermark/ack lifecycle. The consumer's job (the one Markoff hands to the testapp / future Corbomite plugin) is:

1. Maintain a per-peer `last_observed_lamport` for each enrolled peer.
2. When `MarkoffDocument::wantsAcksAtWatermark(W)` fires, check whether `min(per-peer-acks) >= W`. If yes, call `notifyAcksAtWatermark(W)`. If no, wait for more peer updates and re-check.
3. On each transport sync cycle, re-publish *our* high-watermark so peers can advance theirs.

For the consumer to do (1), the transport (collabtext's `OpStream` / `StreamSync`) must surface per-peer ack-frontier observations. For the consumer to do (3), the transport must publish the local replica's frontier outward.

### 2.2 The trade-off

**Option (a): extend presence.**

Pros:
- Fewer files; one watch path; one read.
- Presence already publishes per-replica metadata; this is one more field.
- Atomicity comes for free with whatever atomicity presence currently has.

Cons:
- Mixes ephemeral data (presence: "who's here right now," disappears on disconnect) with durable data (ack-frontier: "what each replica has persisted, monotonic, persists forever").
- If presence is intentionally short-TTL or expiring, ack-frontier inherits unwanted ephemerality unless special-cased.
- Conceptually the two have different lifetimes and different consumers (presence drives UI; ack-frontier drives GC).

**Option (b): sibling `acks.json`.**

Pros:
- Clean separation of concerns. Presence stays presence; ack-frontier stays ack-frontier.
- Lifetimes are different and now visibly different on disk.
- Per-peer GC of ack entries (delete the entries of long-departed peers, but only after policy-defined timeout) is easier to reason about as a separate operation on a separate file.
- Easier for a future tool to reason about "what's been persisted up to where" without parsing presence.

Cons:
- One more file per replica. Slightly more I/O on sync cycles.
- Two atomic-write paths to maintain.

### 2.3 Markoff's preference

**Sibling `acks.json` is our preference.** The conceptual cleanliness is worth the small I/O overhead, and it makes ack-frontier independent of any future evolution of presence (e.g., ephemeral-only presence, expiring presence entries, multi-cursor presence broadcast, presence richer than current).

That said, **we defer to collabtext's engineering judgment.** If extending presence is materially simpler on your side (e.g., because presence already has a generic key-value extension slot), and you can handle the lifetime mismatch internally (e.g., ack-frontier entries persist even when presence-status disappears), we're equally happy with that.

### 2.4 Markoff's actual requirements (independent of file format)

Whichever format wins, the consumer-side semantics we depend on:

1. **Per-peer monotonic ack-frontier values.** Once peer P has been observed at frontier W, P's frontier never goes backward in our view, even across disconnect/reconnect cycles. (If the underlying file format is observed in a non-monotonic order, the consumer takes the max — but we'd prefer the format itself be monotonic.)
2. **Notification of advance.** A callback fires when any peer's ack-frontier advances. Polling is acceptable but worse.
3. **Aggregation visibility.** The consumer can read `min(per-peer-acks)` cheaply (constant or near-constant time, not a linear scan over a large peer set).
4. **No silent staleness.** If a peer's ack-frontier hasn't been observed in a long time, the consumer can detect that (e.g., by reading a "last-observed-at" timestamp). This drives the silent-peer eviction policy in D5 spec §3.5.

If the design call settles on extending presence, those four requirements still need to be met within whatever presence's structure provides.

### 2.5 What we want to avoid

- Ack-frontier values disappearing when a peer disconnects (would block GC indefinitely on a single-disconnect event). Ack-frontier should be "what was last observed," not "what is currently online."
- A format that requires the consumer to parse every peer's full history to know the current frontier. The latest entry should be self-describing.

---

## 3. Other things while we're talking

These aren't on collabtext's two-design-calls list but are worth raising in the same pass since they touch the same surface:

### 3.1 Naming negotiation on `OpStream` methods

The response noted method names are negotiable. Markoff's `ITransport` shape uses:

- `push(stream, blob)` — fine
- `set_on_inbound(callback)` — fine
- `lowest_peer_acked_lamport()` — descriptive but verbose; collabtext's `lowest_peer_acked` would also work
- `set_on_ack_update(callback)` — fine; minor: "ack" is ambiguous in collab generally, but in context (a method on `OpStream`) it's clear

We don't have strong preferences. If collabtext finds shorter / more idiomatic forms that fit your existing API style, defer to your side. The shape matters; the spelling doesn't.

### 3.2 `Operation` move semantics

We pass `Operation` values around in the boundary path. Move-only is fine; copyable is fine; either matches what we'd write. If you make `Operation` move-only, the consumer code (and our `MarkoffSerializer`) handles that idiomatically.

### 3.3 Error handling on `applyRemoteOp`

In the worst case, a malformed `Operation` is passed in. What's the contract — does `applyRemoteOp` throw, return a status, or trust input? Markoff's preference: a typed error path (status return or `optional`/`expected`-style signal) so we can warn-and-skip rather than crash. But if the existing `Buffer::apply_remote_op` is "trust input, undefined on malformed," we'll match by validating before passing in (we already do `decode_operation` first; `optional`-returning decode covers most cases).

---

## 4. Format we expect from the joint-design output

Mirrors the IdList header pass from 2026-05-04. After the back-and-forth resolves:

- **Resolution doc** at `docs/handoff/2026-05-XX-collabtext-joint-design-outcomes.md` (Markoff side) and a corresponding doc on collabtext's side. Brief; one decision per call, with the reasoning if it differs from this doc.
- **Header drafts** for `OpStream.h`, `Serialization.h`, and the ack-frontier format (whichever file). Markoff reviews; one round of comments; collabtext sets.
- **Plan-doc updates** on the Markoff side: §0.1 of the D5 spec, the Phase 0 / Phase 1 / Phase 4 / Phase 8 task notes in `docs/plans/2026-05-08-d5-collab-activation.md`. Substantive only — we don't update for spelling.
