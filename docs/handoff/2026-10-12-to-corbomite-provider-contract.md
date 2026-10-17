# Markoff response: proposed document storage provider

2026-10-12. In response to Corbomite's
[`collabtext-host-response`](../../../Corbomite/docs/handoff/2026-10-12-to-markoff-collabtext-host-response.md).

We accept the proposed UUID/history namespace, device-private authority location,
document-service ownership and designated first-import owner. These are agreed
design directions; neither side has production acceptance yet. Markoff retains
the editing transaction and recovery state machine, including scoped gaps and
cross-stream undo. Corbomite's Vault and editor leaves will not orchestrate Core.

**Coordination update from the user:** Corbomite development remains paused
until Markoff's final readiness signal (A16/M12). This document is a provider
draft, not that signal. The [execution roadmap](../plans/2026-10-12-collabtext-adoption-execution.md)
records all Markoff prerequisites; host implementation begins after the final
transition handoff.

The concrete draft Qt/C++ interface is
[`DocumentStorageProvider.h`](../../libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h),
with the call/recovery contract in
[`document-storage-provider.md`](../specs/2026-10-12-document-storage-provider.md).
It is compiled in Markoff's installed-package adapter target, but is not yet
wired into MarkoffDocument. Please review it as a proposed provider interface,
not as a stable published API.

Key decisions in this draft:

- An opened storage session retains the exclusive document lease and storage
  resources for its lifetime. Markoff enables writing only after recovery.
- Calls are synchronous and serialized on the document thread, with no provider
  callbacks or event-loop pumping. Durable action outcomes must have an explicit
  result/status surface; change signals do not acknowledge persistence.
- Accepted revision is an opaque stable prefix token. Prepared transactions
  bind exact shared bytes, format, reservation intervals and intended private
  undo state to their original ID and expected revision.
- Append and inspection return committed, absent, uncertain or rejected with
  diagnostics. Rejection is distinct from proof of absence. Only proven absence
  permits Markoff to construct gaps; uncertainty requires original-transaction
  reconciliation.
- Private undo writes are recoverably tied to their committed action/revision.
  Preparations remain available after shared commit until that update is finished.
- Provider recovery includes durable first-import source/intent and reservations
  created before an action preparation could be persisted.

The host atomically checks Markoff-supplied operation identity claims against
transaction ID and exact record. Markoff validates those claims against Core
bytes. This lets storage reject conflicting identity reuse while treating the
encoded operation payloads as opaque.

The alpha.5 adapter now has one allocation path: `ScopedTextStream` and
`ReplicatedMapStream` require an open `DocumentStorageSession` and pass the full
canonical stream name to its reservation journal. The earlier direct
`ReplicaState` constructors and temporary MPSK/1 scope-to-token registry have
been removed. A Corbomite provider should key reservations by the complete
`(document UUID, epoch, canonical stream)` scope and journal every exact interval
before returning it; it should not reproduce Core's internal writer-token rules.

Markoff has implemented the reference provider, provider-backed stream adapters,
action/recovery coordinator, versioned gap records, accepted-prefix replay and
cross-stream private-history codec. Production `MarkoffDocument`/view integration,
remote receipt integration and accepted external-replacement actions remain open.
Markoff will complete and verify the document path before signaling Corbomite to
implement its local service/journal and adapt the application.

The migration list will include the fallible document construction path, composite
BlockId/hash/position-token surfaces, explicit durable edit outcomes, revision-aware
export acknowledgment and replacement-action API. Existing production numeric
constructors and wholesale reset calls do not gain durable semantics from this draft.
