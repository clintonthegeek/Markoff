# Markoff consumer asks for CollabText vNext

**Audience:** CollabText maintainers  
**From:** Markoff  
**Candidate reviewed:** `feat/fuguemax-placement`, `ffe9875ee35a525f422eff3c1aaee2841f55d962`  
**Purpose:** identify the upstream API and compatibility decisions Markoff needs
before it can move its production document model from the legacy CRDT engine.

This is a consumer handoff and request for discussion. It is not a claim that
the asks are accepted, scheduled, or implemented. Markoff's adoption record is
in [`docs/plans/2026-10-11-collabtext-vnext-adoption.md`](../plans/2026-10-11-collabtext-vnext-adoption.md).

**Response received (2026-10-12):** CollabText's alpha.4 candidate says all
three requests are delivered: ordered IdList entry identities, scoped committed
counter-gap application, and an indefinite CTLO/0002 reader commitment. See
the [CollabText response](../../../collabtext/.worktrees/reliability-v2/docs/handoff/markoff-core-response.md)
and [candidate evidence](../../../collabtext/.worktrees/reliability-v2/docs/reports/2026-10-11-markoff-core-candidate.md).
Those upstream changes do not complete Markoff's host persistence, production
port or consumer acceptance; the requests below preserve their acceptance
criteria for Markoff's integration.

## Context

Markoff keeps one structural list for block order and one text stream per
stable block. We intend to preserve that D2 model and use CollabText's public
scoped facades. Markoff owns Markdown parsing/serialization, sibling maps,
cross-stream actions, durable host transactions and document identity.

The current `IdListEngine::ids()` returns visible `uint64_t` application
values. Its public `element_of(value)` also addresses entries by that value.
Those values may repeat and are not the list entries' CRDT identities. Markoff
needs a block identity that remains unique when two devices independently
create blocks while offline. Hashing, truncating or randomly mapping the
identity into `uint64_t` would not preserve a deterministic, recoverable
address.

The candidate already stores each list entry's insertion origin internally,
uses that identity for anchors, and exposes the same kind of identity through
`Crdt::idlist_op_identity()`. The list facade's existing position projection
also sees the visible entries in order. An additive public query appears able
to expose the needed view without changing operation bytes or exposing the
tree representation.

## Ask 1 — expose visible IdList entry identities in order

Please add an installed public query on `IdListEngine`, for example:

```cpp
struct VisibleIdListEntry {
    uint64_t application_value;
    Crdt::Identity identity;
};

[[nodiscard]] Result<std::vector<VisibleIdListEntry>> visible_entries() const;
```

The exact naming is open; the behavior is the important contract:

- Return visible entries only, in current document order.
- For each entry, return its caller-owned application value and the full
  insertion identity (`ActorId` plus counter). Equal application values must
  still yield distinct entries and identities.
- Preserve the original insertion identity through remote replay and
  delete/undo visibility changes. A deleted entry is absent while deleted and
  returns with the same identity when undo makes it visible again.
- Return a typed error if retained list state cannot provide a valid identity;
  do not silently omit an invalid visible entry.
- Keep the result a value projection. Do not expose `IdListEntry`, tree nodes,
  or other storage details.
- Keep existing `ids()` and operation codecs source/wire compatible.

Markoff will use the identity as its `BlockId`, derive the per-block text-stream
name from it, and address insertion/removal through identity-bearing anchors.
The application value can remain an opaque compatibility field; Markoff will
not treat it as identity.

### Acceptance evidence requested

Please include installed-package API coverage for:

1. Several visible entries with the same application value remain separate and
   appear in the same order as `ids()`.
2. Concurrent inserts from distinct actors converge to the same ordered
   `(application_value, identity)` sequence on both peers.
3. Removing an entry omits it from the projection; undo restores the same
   insertion identity, not a new one.
4. Encode/replay into a fresh engine returns the same visible identities and
   order.
5. The public header is present in the installed package and a consumer can
   build using only that package.

This is an additive facade over existing entry state, not a request to change
IdList ordering or wire formats.

## Ask 2 — provide an applicable durable counter-gap path

The candidate exposes `CounterGap` CTGP/0001 encode/decode, while its public
header says decoding does not publish or certify a gap. The scoped text and list
facades do not expose an operation that applies a terminal gap to causal
coverage. Markoff reserves counters durably before creating local operations;
a definitive failure to append one such operation can therefore leave a hole
that peers cannot currently resolve by applying a later operation.

Please either provide a supported Core path or document a supported host
protocol that lets a consumer:

- bind the gap to document, stream, epoch and actor;
- state the exact reserved interval and the prior causal frontier;
- durably commit the gap before any later same-stream operation is published;
- apply it idempotently on peers, advancing causal coverage without fabricating
  a document operation;
- reject malformed, overlapping or conflicting gap records; and
- recover correctly after writer/peer restart and delivery retry.

The operation log remains consumer-owned. We expect Markoff to add a versioned
gap record to its durable transaction format and advance receipts only after
the gap is committed and applicable. Core must supply semantics that make a
later operation valid after the missing interval has been observed.

### Acceptance evidence requested

Use at least two peers and a crash/reopen boundary: reserve an operation
identity, prove its bundle is definitively absent, commit and deliver the gap,
then deliver the next operation. Both peers should accept the next operation
without inventing a placeholder edit. Replaying the same gap must be harmless;
reusing its scope/interval with different bytes must be rejected. Include list
and text streams if their causal application differs.

Until this path exists, Markoff will stop local writes on a stream after a
definitively failed append. An uncertain append is different: the host must
inspect/replay the exact original transaction before deciding whether a gap is
needed.

## Ask 3 — settle CTLO/0002 durable-reader support

The candidate capability manifest promises indefinite durable readers for
CTBO/0002 and CTBO/0003, but labels CTLO/0002 checked alpha interchange without
a cross-version durable-reader obligation. Markoff plans to retain structural
operations in its durable action log and therefore needs an upgrade-safe
reader policy before it writes CTLO records long term.

Please confirm one of these paths:

1. CollabText will retain the CTLO/0002 reader and its insert/remove/undo
   semantics for durable archives, with literal compatibility fixtures; or
2. CTLO/0002 remains consumer-owned interchange, and Markoff should implement
   and maintain an independent archive decoder/application path.

If the first path is supported, please state the compatibility commitment in
the capability manifest and exercise literal insert, remove, undo and replay
fixtures across a reader upgrade. Wave's CTBO archive commitment is useful
precedent but does not by itself establish the CTLO promise.

Until this is resolved, Markoff can validate CTLO bytes in codecs but will not
enable durable structural-operation writes.

## Out of scope for these asks

Markoff is not asking CollabText to own document IDs, block parsing, Markoff's
map CRDTs, bundle transactions, network delivery, cross-stream undo, Markdown
serialization or the host's applied receipts. Those remain Markoff/host
responsibilities. We are also not requesting a general-purpose document
container or a change to the current FugueMax ordering model.

## Markoff follow-through

Once the ordered identity query and host document-key contract are available,
Markoff will convert `BlockId` and every route/key/anchor/undo/persistence seam
as listed in the adoption plan, then run the actual relocated-package consumer
acceptance against the exact candidate SHA. We will keep CTLO writes and
post-failure writes gated until the corresponding compatibility and gap paths
have evidence.
