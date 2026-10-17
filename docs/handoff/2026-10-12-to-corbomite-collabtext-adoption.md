# Corbomite host contract for Markoff's CollabText adoption

**Audience:** Corbomite maintainers  
**From:** Markoff  
**Date:** 2026-10-12  
**Host inspected:** Corbomite `1f4b23dd`  
**Library candidate:** `collabtext-markoff-core-2026-10-11`,
`1399e9533c9cfc1006854487af4e71b8b79989fd` (alpha.4).

## Why this handoff is needed

CollabText has delivered the ordered structural identities, terminal-gap
application, and durable operation readers Markoff requested. Markoff is now
preparing its production document port. The remaining identity and persistence
decisions belong to the host and need a contract before that port can safely
create writable streams.

The inspected `NoteDocument` constructor receives `vaultRoot` and
`relativePath`, then constructs Markoff with replica ID `1`.
`setRelativePath()` changes the path on rename. Neither the path nor that
replica constant supplies the stable document namespace and durable writer
authority required by the new engine.

Read this alongside the [Shared Editing Manifesto](../EDITING-MANIFESTO.md)
and [Markoff adoption plan](../plans/2026-10-11-collabtext-vnext-adoption.md).
This is a proposed host contract; the Markoff construction/provider API is
still to be implemented and agreed.

## Request 1: shared document identity and copy policy

Please define and persist an opaque document key that survives rename, move,
close/reopen, and synchronization to another device. It must identify the
shared editing history independently of the current file path and Markdown
contents.

Recommended policy:

- Generate the document key once and retain it in shared document metadata.
  A UUID is a reasonable representation. Select a concrete storage scheme,
  such as a shared sidecar or vault metadata record, and specify how it travels
  with the document. Preserve existing Markdown source bytes.
- Rename and move preserve the key and history.
- Explicit duplicate/fork creates a new key and a new editing history.
- Synchronizing or restoring the same collaborative document preserves the
  key and its accepted history.
- An external copy that carries the same metadata needs an explicit policy:
  continuation of the same document or a new fork. Specify how ambiguity is
  surfaced and resolved; path or content equality alone cannot decide it.

Markoff will derive each block's identity from its structural insertion
identity and construct canonical stream names. Corbomite supplies the document
namespace; it does not need to allocate or narrow block identities.

## Request 2: device-private writer authority

Please provide durable `ReplicaState` storage outside the shared/synced vault,
and define its owner and lifetime. Ordinary reopen must preserve the actor and
counter reservations. Independent devices must have independent actor state.
Copying or synchronizing a document must never copy its writer authority.

Specify behavior for private-state loss, corruption, backup restoration, and
copied device state. Copied state used by an independent writer requires an
explicit fresh-actor fork. Errors must reach the integration rather than
silently replacing authority or reusing counters. Coordinate writer leases
when several views or sessions open the same document.

The proposed Markoff construction boundary requires the stable document key,
epoch, and private allocation authority explicitly. The current replica-ID
constructor is not the vNext collaboration contract.

## Request 3: durable action storage and initial import

Please identify the host component that owns the accepted operation history
and its atomic append/recovery interface. Markdown saves remain visible-text
exports; they cannot reconstruct CRDT identities, references, or undo.

The interface needs exact versioned action bytes, an idempotent transaction
identity, and outcomes distinguishing committed, definitively absent, and
uncertain. An uncertain append must be inspected/reconciled using that original
transaction. It must not trigger replacement operations or a counter-gap
decision.

For a definitively absent operation, the integration must record its full
reserved interval, rebuild the tentative view from the accepted prefix, and
durably commit a scoped terminal gap before later same-stream writes. Markoff
will define the bundle/gap formats and Core application behavior; agree which
host component stores them and provides the commit barrier. Also identify the
device-private home for local undo metadata.

Define first import as one durable shared history. Other devices replay that
history; independently seeding identical Markdown creates different item
identities. Specify how existing Markdown-only files acquire a document key
and initial history, and how an interrupted first import is recovered.

## First deliverable and acceptance

Please return a short design response naming the metadata location, copy/fork
policy, private-state owner, durable action-store owner, and proposed provider
interface. Markoff can then implement against that explicit boundary. Full
transport or a completed collaboration UI is not required for this first
deliverable.

Before enabling production writers, acceptance should demonstrate:

| Scenario | Required result |
| --- | --- |
| Rename/move and reopen | Same document key, block identities and accepted history |
| Same document on two devices | Same document namespace; distinct writer actors |
| Explicit duplicate | New document namespace with a defined initial history |
| Multiple views | Shared document ownership and coordinated writer lease |
| Interrupted first import | One recoverable initial history, without duplicate seeding |
| Failed or uncertain append and restart | Original transaction reconciled; no counter reuse; terminal gaps only after proven absence |
| Markdown save and reopen | Source preservation plus recovery of editing history through the operation store |

Markoff owns the production CRDT port, composite block routing, scoped
positions, Markdown serialization, and cross-stream action/undo semantics.
Corbomite owns document lifecycle and the persistence services those mechanisms
need. We will verify one real editing path through both layers against the
same candidate before treating adoption as complete.
