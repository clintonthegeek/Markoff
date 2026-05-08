# Markoff acceptance of collabtext's D5 deliverables

**Date:** 2026-05-08
**Re:** collabtext's OpStream extraction is complete; verifying suitability for Markoff D5
**Predecessors:**
- `~/dev/collabtext/docs/handoff/2026-05-08-d5-joint-design-outcomes.md` (collabtext's resolution of the two design calls)
- `~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md` (the response we're acting on)
- `docs/handoff/2026-05-08-collabtext-joint-design-positions.md` (Markoff's incoming positions)
**Companion:** `docs/specs/2026-05-07-d5-collab-activation-design.md` §0.1 (negotiation outcome)
**Drives:** `docs/plans/2026-05-08-d5-collab-activation.md` Phase 0 → Phase 1 transition

This document records Markoff's acceptance of collabtext's OpStream extraction as a basis for Phase 1 of D5 implementation, raises one substantive feedback item, and notes lighter observations. Brief by design — mirrors the format of the collabtext-side outcomes doc.

---

## 1. What collabtext delivered

Verified against the public include tree at `~/dev/collabtext/libs/collabtext/include/collabtext/`:

| Surface | Header | Status |
|---|---|---|
| `OpStream` interface (4 methods) | `OpStream.h` | ✅ public |
| Operation types + narrow contract | `Operations.h` | ✅ public |
| IdListOperation types + narrow contract | `IdListOperations.h` | ✅ public |
| Encode/decode functions | `Serialization.h` | ✅ public |
| File-backed reference impl | `StreamSync.h` | ✅ public, implements `OpStream` |
| Per-block Buffer-side callback path | `CrdtEngine.h` | ✅ public `setOnLocalOp` / `applyRemoteOp` |
| IdList structural ops callback path | `<crdt/IdList.h>` (internal) | ✅ `set_on_local_op` / `apply_remote_op` added |
| Ack-frontier file format | `replicas/{id}/acks.json` | ✅ documented in `CRDT_TRANSPORT_SPEC.md` |
| Transport-level test coverage | 4 suites in `tests/` | ✅ two-replica convergence, IdList convergence, ack-frontier under partition, catch-up replay |

All sequencing constraints from the response doc are met: IdList β was accepted on 2026-05-08 (`3ead04b`), and the OpStream extraction landed against that base.

## 2. Match against Markoff's positions

### 2.1 Public type surface — Form 2 accepted

We requested Form 1 (pImpl + accessors). collabtext chose Form 2 (full structs public, narrow contract documented) on the rationale that Form 1 would require routing all internal `Operation` construction through a pImpl carrier — 1–2 weeks for incremental protection over a contract Markoff already commits to. Our positions doc explicitly listed Form 2 as an acceptable fallback if Form 1 was materially harder; the reasoning given clears that bar.

The contract block at the top of `Operations.h` and `IdListOperations.h` matches the format we expected: stable surface itemised, evolution-reserved itemised, `decode_*` returning `std::optional` for warn-and-skip, public construction APIs absent. **Accept.**

### 2.2 Lamport surface — one substantive nit

We requested both `counter()` and `replica_id()` as **accessor methods**.

Delivered:
- `Lamport::counter() const -> uint64_t` ✅ (accessor as requested).
- `Lamport::replica_id` — exposed as a **public field** (uint16_t), documented as "directly accessible" in the stable contract.

**Nit:** the contract block declares "field layout … is evolution-reserved" and then carves out one field as part of the stable surface. That works mechanically — Markoff reads `lamport.replica_id` to fill `MarkoffBundleMeta::producerLamport.replicaId` — but it's an inconsistency between the rule and the carve-out.

**Markoff's preference:** wrap it in an accessor (`uint16_t Lamport::replica_id() const`) for symmetry with `counter()` and to preserve the "no consumer field access" precedent. Three lines; no API break for any consumer using a free reader.

**Acceptable alternative:** keep the field, but rewrite the contract block to call out `Lamport`'s field layout as an *explicit* carve-out from the otherwise-reserved layout rule. Documentation-only fix.

This is **not a blocker** — Markoff Phase 1 can proceed with either form. Flagging it for the next opportunistic touch on the public headers.

### 2.3 Op-identity accessors — free functions instead of methods

We asked for `Operation::lamport()` and `IdListOperation::lamport()` member methods.

Delivered as free functions: `op_lamport(const Operation&)` and `idlist_op_lamport(const IdListOperation&)`. The reason — `Operation` is a `std::variant`, which can't carry member methods without a wrapper class — is sound. Same shape, different syntax. Markoff calls a free function instead. **Accept; spec language updated to match.**

### 2.4 Ack-frontier file format — sibling `acks.json` accepted

Exactly the format we preferred:

```json
{
  "schema_version": 1,
  "acks": {
    "<peer_replica_id>": {
      "max_lamport_observed": <uint64>,
      "last_observed_at": "<ISO 8601 UTC>"
    }
  }
}
```

All four hard requirements from our positions doc §2.4 are explicitly satisfied:

1. ✅ Per-peer monotonic (`max(existing, fresh)` enforced on write path).
2. ✅ Notification of advance (`set_on_ack_update(callback<uint64_t>)` fires on cached fence advance).
3. ✅ Aggregation visibility (`lowest_peer_acked_lamport()` returns from cached scalar).
4. ✅ No silent staleness (`last_observed_at` ISO 8601 UTC per peer; eviction is consumer-side per Markoff D5 §3.5).

Atomic write via temp-file-rename, monotonicity enforcement on write, persistence of ack values across peer disconnect — all present. **Accept as-is.**

### 2.5 Other decisions — aligned

| Item | Markoff position | Delivered |
|---|---|---|
| `OpStream` method names | No strong preference | `push` / `set_on_inbound` / `lowest_peer_acked_lamport` / `set_on_ack_update` |
| `Operation` move semantics | Either works | Move-only target; copy where cheap |
| `applyRemoteOp` error path | Typed; prefer status return | Returns `bool` (false = dependency unmet, caller may retry/buffer); never throws on in-domain ops |

All accepted.

## 3. Things that exceeded what we asked for

Worth naming so they don't get lost:

- **`decode_*` takes `std::string_view`** rather than `const std::string&`. Slightly cheaper at the boundary; no Markoff change required.
- **`encode_global` / `decode_global`** exposed as a bonus. We don't need them; transitively useful.
- **`OpStream.h` includes a 10-line "implement your own transport" skeleton** in the file-level doc-comment. Useful when direct-channel transports come up.
- **Transport-level test coverage already in collabtext's tree:** two-replica Buffer convergence, IdList convergence, mixed Buffer+IdList, ack-frontier under partition, catch-up replay, idempotent re-delivery. Markoff's plan can shrink — we don't have to recreate any of these. Markoff-side tests stay focused on the bundle-meta layer, MarkoffOp serialization, and tier-0/tier-1 above the boundary.

## 4. Light observations (not blocking)

These don't require action; recording for the file.

### 4.1 No public `IdList.h` header

`IdList` retains its API in `src/crdt/IdList.h`. Markoff already includes it via `<crdt/IdList.h>`, so this changes nothing for us. The plan documented this choice (File Structure table marks it "Modified" not "New public").

If a third consumer ever wants per-block CRDT, they'd reach into the internal-style include the same way Markoff does. Not blocking now; worth revisiting if/when a second consumer surfaces. Noted; no action requested.

### 4.2 Buffer kept its return-style API

`CrdtEngine::setOnLocalOp` / `applyRemoteOp` was added (single-buffer wrapper). `Buffer` itself was *not* given an analogous callback API — it retains `apply_local_edit` returning the `Operation` directly, and `apply_ops` accepting a vector.

For Markoff's per-block usage pattern this is *more* direct than callbacks: at the call site for a local block edit, we have the `Operation` immediately and route it into the bundle without a callback hop. The plan's prose ("pImpl plumbing for `Buffer`") suggested Buffer would gain the callback API too; the actual delivery routes that through `CrdtEngine` only. End shape works for us. Noted.

### 4.3 Encoded ops are JSON strings

`encode_operation` returns `std::string` (single-line UTF-8 JSON, no trailing newline). Markoff's `MarkoffOp::payload` is `QByteArray`. Trivial conversion at the boundary. Documenting for the implementer: payloads transiting `MarkoffOp` are UTF-8 JSON bytes, not opaque binary.

## 5. Net acceptance

**Phase 0 prerequisite of `docs/plans/2026-05-08-d5-collab-activation.md` is satisfied.** Phase 1 can begin:

- Migrate `<crdt/Operations.h>` → `<collabtext/Operations.h>` and `<crdt/Serialization.h>` → `<collabtext/Serialization.h>` at the Markoff/collabtext include boundary in `markoff-core` headers and sources that name op types or encoders.
- Build `MarkoffOp`, `MarkoffBundleMeta`, `MarkoffSerializer` against the public surface.
- Per-block Buffer + IdList paths keep their `<crdt/Buffer.h>` / `<crdt/IdList.h>` includes — those types stay internal by collabtext's documented choice.
- `CollabText::Crdt::Anchor` keeps its current include path; no migration there.

The single feedback item we'd ask collabtext to consider — `Lamport::replica_id` accessor vs field — is a documentation/three-line change, not a blocker. Phase 1 implementation proceeds either way.

## 6. Reply form

This doc captures Markoff's acceptance for the file. The maintainer-facing reply lives wherever collabtext's communication channel is — pointer to this doc plus the one substantive feedback item is enough. The acceptance is implicit in starting Phase 1 against the public surface.
