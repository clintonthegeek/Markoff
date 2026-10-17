# Markoff document storage provider — draft v1 contract

Prepared 2026-10-12 from the
[Corbomite host response](../../../Corbomite/docs/handoff/2026-10-12-to-markoff-collabtext-host-response.md).
Status: concrete API proposal, compiled in the isolated adapter. The
restartable local reference provider is implemented for development and
conformance tests; production session orchestration and Corbomite storage remain
unimplemented.

User coordination update: Corbomite development remains paused until the final
A16/M12 readiness signal in the [execution roadmap](../plans/2026-10-12-collabtext-adoption-execution.md).
Markoff validates this boundary with reference providers before signaling the
host transition. Later Corbomite acceptance verifies its own storage implementation.

The C++ boundary is
[DocumentStorageProvider.h](../../libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h).
It uses Qt value types and public CollabText allocation/error types. Providers
store opaque versioned Markoff records; they do not parse Core operation bytes.
These signatures are proposed for host review and carry no stable ABI promise.

## Accepted ownership and lifecycle

Corbomite owns the shared UUID/path manifest and immutable document histories
under `.corbomite/collaboration/v1/`, the private writer state under
`AppLocalDataLocation/collaboration/v1/`, and the document-service lease. Its
search SQLite index remains a disposable projection. Corbomite coordinates
sessions by document UUID across Vault instances and processes.

Markoff owns canonical stream names, operation/history formats, action groups,
replay, tentative engine rebuild, terminal-gap decisions and the writable
barrier. Editor leaves use the owning NoteDocument's session. They acquire no
storage or allocation authority themselves.

`DocumentStorageProvider::open(scope, mode)` returns a session that retains the
exclusive document-service lease until destruction. The returned session owns
or retains every persistence resource its methods use; it must not depend on a
provider factory remaining alive. Opening storage does not enable editing.
Markoff must read recovery state, reconcile transactions/reservations, replay
the accepted prefix and restore accepted owner history before enabling writes.

The scope has a nonempty UTF-8 document key bounded to 256 bytes and nonzero
epoch. The host's chosen UUID representation fits that contract. Missing history
for a registered UUID, corrupt authority, an unsupported epoch or a conflicting
lease fails opening. A host-authorized durable first-import intent may open an
empty prefix; its exact source bytes and transaction ID appear in recovery state.

`StorageMode::ephemeral` must be selected explicitly for previews, tests and
standalone rendering. It promises no crash durability. A production registered
document requests durable mode; a provider cannot silently substitute ephemeral
storage when durable storage fails.

Markoff supplies `makeEphemeralDocumentStorageProvider()` for this mode. It
keeps its actor, counters and transaction state in memory for the provider
object's lifetime, enforces one active session per document, and rejects
`StorageMode::durable`. Destroying the provider discards all of this state.
This implementation is a development/reference path; it does not satisfy the
restartable A02 provider requirement and is never a durable fallback.

`makeReferenceDocumentStorageProvider(root)` is a separate development provider
for durable-mode contract tests. It stores each document/epoch in a hashed
`.mref` file beneath `root/documents`, with a process lease beneath `root/leases`
and a stable writer identity beneath `root/identity`. MREF/5 stores the full
scope for collision detection, the writer actor/counters/reservations, exact prepared
transaction metadata, the immutable accepted prefix, private-history completion
state, export watermark, and exact opaque quarantine records already acknowledged
by the caller. It also records whether an accepted MAB is a local prepared action
or a remote acceptance without local reservations/history. MREF/4 remains
readable without a receipt ledger; new writes use MREF/5, which also stores
durable transport receipts for local accepted MABs. Earlier experimental MREF
versions are rejected.
Quarantine writes are idempotent by exact bytes and appear in the
next recovery snapshot. Updates use `QSaveFile` and synchronize the file and
parent directory on Unix platforms; any failure after the atomic rename is
reported as `commit_uncertain`. Unsupported platforms fail provider creation
before state is written. This is a local reference format, not Corbomite's
journal or a production compatibility promise. The consumer test
verifies provider-object close/reopen at preparation and accepted-prefix recovery
boundaries, then starts a child that reserves an unattached interval and exits
with `_Exit`; a fresh process recovers its exact actor and interval. It also
verifies refusal of corrupted MREF bytes and starts a competing process while a
provider holds the shared identity authority; the competing provider receives
`writer_conflict`. Deleting private identity state while document state remains
is refused because the stored actor no longer matches. A forced pre-commit
reservation write failure is reported as definite absence and leaves the prior
counter state intact after reopen. These checks establish process-restart,
contention, corruption refusal, authority-loss refusal, a pre-commit failure
boundary and injected post-rename/file-sync/directory-sync uncertainty with exact
reopen reconciliation. They do not establish power-loss or storage-device failure
behavior, nor the full document action/recovery coordinator.

An empty durable document does not imply authorization to create its first
history. The host records that decision through
`DocumentStorageSession::beginInitialImport(intent)` before reserving operation
identities. The intent binds a nonempty transaction ID (at most 256 bytes) to
the exact source Markdown (at most 16 MiB). An exact retry is idempotent; a
different source or an intent after counter allocation/reservation/history
exists is refused. After an attempt is proven absent and every reservation is
covered by accepted terminal gaps, the host may bind a fresh transaction ID to
the same source bytes. Recovery returns the pending intent until its matching
initial MAB commits and its prepared owner history is durably stored.

## Calls, notifications and revisions

The initial interface is synchronous and confined to the owning document's
thread. This applies to reads and accessors as well as mutations: the lifecycle
owner exposes borrowed references to its session, recovery snapshot and
projection, and those references are neither synchronized nor transferable.
Lifecycle mutations invoked off-thread or reentrantly are refused with typed
errors. Calls invoke no consumer callbacks and pump no event loop. A provider
can implement local journal writes internally; network calls per edit are
outside this boundary.

`AcceptedRevision` is a nonempty opaque equality token, including for an empty
prefix. It identifies an exact accepted history prefix, independently of Core
causal vectors, delivery sequences and UI edit counters. Identical prefixes
retain their token across reopen. A transaction inspection returns that
transaction's committed prefix, which may precede the current head. Do not order
or increment these opaque tokens in Markoff.

`ProviderBackedDocumentState::publishAction()` is the current lifecycle API for
publishing one staged MAB. It is available only after exact initial-import
authorization or when the accepted document is ready, runs on the opening
thread, refuses reentry, and refreshes the accepted projection after publication.
It returns committed, committed-history-pending, absent, uncertain or rejected
with an optional diagnostic. Tentative change signals, if exposed, are
identified as tentative; they are never a commit acknowledgement. No new
affected-stream edit or undo proceeds during uncertain recovery. Existing void
edit APIs still need the production document migration to expose these durable
outcomes before they can be used in a durable session; their current behavior
has not been silently changed.

### Synchronous editor outcome contract

The production document keeps the existing synchronous interaction model. A
local edit first mutates a tentative projection and stages the exact shared
action plus intended private history. Its API returns a typed outcome; it must
not report success merely because the in-memory CRDT accepted the edit. The
view may render a tentative edit immediately, but tentative notifications are
separate from committed-content notifications and include the transaction ID
so an embedding editor can present pending state.

| Durable result | Document projection | Editor notifications | Write barrier |
| --- | --- | --- | --- |
| committed | Keep the staged projection as accepted. | Emit normal committed document/block notifications and a committed outcome. | Remains open unless another recovery item exists. |
| committed, private history pending | Keep the shared projection; mark owner history incomplete. | Emit shared-content committed notifications and a history-pending outcome; do not claim undo is available for the action. | Closed to local edits and undo until exact private history is completed. |
| absent or rejected with proven no publication | Rebuild from the accepted prefix; discard the tentative projection. | Emit a rollback outcome and the content refresh needed to repaint restored bytes. | Closed while reservations or other recovery work remain; reopen only after the lifecycle owner reports ready. |
| uncertain | Keep the tentative projection visible and mark it pending; do not infer commit or roll it back. | Emit recovery-required state and an uncertain outcome, not committed-content notifications. | Closed to all local edits and undo until exact transaction inspection and accepted-prefix replay resolve it. |

Signals emitted before durability are explicitly tentative. Existing
`documentChanged`, `blocksChanged`, structural notifications, dirty-state
notifications, and action-produced callbacks represent accepted state only;
they fire after the matching durable action commits and the accepted
projection refreshes. A failed or uncertain action never advances the
saved/export watermark. If action publication commits but private-history
completion is pending, shared content is accepted while undo availability and
further writes remain blocked. This contract preserves responsive typing
without allowing a failed durable write to look like accepted document state.

## Recovery snapshot and allocation

`readRecoveryState()` returns one consistent snapshot under the session lease:
the complete ordered accepted records and current revision, outstanding
prepared transactions, unassociated reservations, accepted private history if
present, any pending first-import intent, and the last acknowledged Markdown
export revision if one exists. Each accepted record also carries the provider's
opaque prior and committed prefix tokens. They are metadata from the host's
transaction index, not fields in the Markoff record; replay uses them to check
that a terminal gap names the exact prefix immediately before that gap without
ordering or incrementing opaque tokens. Committed preparations whose private
history update is incomplete remain recoverable. Reservation metadata must not
disappear before an accepted action or terminal gap accounts for the interval.

`reserve(stream, count)` durably reserves the entire scoped interval and journals
its exact actor, first counter and count before returning it. Actors remain
stable for the session and ordinary reopen; counters never wrap or reuse.
Reservations associated with prepared transactions appear in those preparations;
unassociated reservations appear separately in recovery state. Markoff matches
exact intervals, rather than inferring gaps from high-water marks.

If reservation durability is uncertain, return a typed `commit_uncertain` error
and require reconciliation. A successful reservation followed by an unpersisted
preparation must remain discoverable after restart. Coordinating Core counter
storage with the private reservation journal is a host implementation obligation;
two unrelated successful file writes do not establish atomic recovery.

`observeCounter()` raises allocation above validated accepted input. This derived
observation can be reconstructed by replay. `nextCounter()` exposes the next
checked value and reserves nothing.

`ScopedTextStream` and `ReplicatedMapStream` accept only an open
`DocumentStorageSession`; there is no public `ReplicaState` constructor or
private token registry. The provider receives the full canonical stream name
and scopes its reservation journal by the opened `(document, epoch)`. Every
successful allocation is therefore represented by the exact reservation that
action preparation must later commit or cover with a gap. The reference provider
persists reservations, observed counters, acknowledged quarantine envelopes
(up to 26 MiB each), and transport receipt acknowledgments in MREF/5. Do not add a second
allocation path or a Markoff-owned scope-to-token
format to work around Core's internal writer-key constraints.

Remote actions use `acceptRemoteAction()` rather than local `prepare()`/`append()`.
Markoff first validates the complete incoming MAB, its action ID and every
operation identity claim. It then supplies the exact record bytes and unique
`(stream, actor, counter)` claims. The provider atomically appends that record
and claims those identities without requiring reservations from the receiving
writer or replacing its private undo history. It rejects stale prefixes,
identity reuse, changed retries and transaction-ID conflicts. An exact retry
after an uncertain result returns the accepted revision if the prior attempt
committed; otherwise it may commit the same exact transaction. The provider
does not decode MAB bytes. `MarkoffDocument::acceptRemoteAction()` validates
the MAB and identity claims, uses the provider acceptance path, then replays and
refreshes the accepted projection before returning a committed result. Hosts
must advance transport receipts only after that result. Bounded remote-router
deferred/wait handling and durable quarantine integration at the document boundary
are implemented. Markoff's durable outbox exposes locally accepted MABs until a
host-verified receipt is recorded; production network delivery and receipt
authentication remain host responsibilities.

`StorageRecoveryState::pendingOutboundActions` is derived from local accepted
MAB/3 records and the provider's durable receipt ledger. Remote accepted actions
are never returned as outbound work. After authenticating a peer receipt, the
host calls `acknowledgeTransportReceipt(actionId, acceptedRevision)`; an exact
retry is idempotent and a changed revision for the same action conflicts. The
provider does not authenticate peers or interpret the opaque peer revision.
This makes a crash before send or before receipt persistence recoverable by
resending the exact accepted MAB; receiver duplicate handling returns a fresh
receipt without repeating the action.

Rejected and deferred remote operations use MQR/2. The record binds document, epoch, stream,
primitive, operation actor/counter, optional target block, exact payload,
delivery sequence, action ID, bundle index/count, and the typed rejection
reason. It can also carry the exact enclosing MAB/3 bytes so a document-level
quarantine preserves action metadata and source snapshots for retry. MQR/2
records are bounded to 26 MiB and the decoder remains compatible with MQR/1.
A router encodes the record and calls
`DocumentStorageSession::storeQuarantinedOperation()` before removing the
envelope from its bounded hold. Provider failure is returned to the caller and
leaves the envelope charged in the router. Exact-byte retries are idempotent.
If persistence returns `commit_uncertain`, the provider session is poisoned;
close and reopen it before reading recovery or attempting the exact retry.
Wait records use `history_unavailable` or `unknown_stream` as their typed reason.
They remain in the same append-only recovery journal after application; document
reopen filters exact MABs already present in the accepted prefix and restores
the remaining waits into the bounded owner-thread queue. A later permanent
rejection supersedes the wait record for recovery while preserving both records
as audit evidence. Primitive-router records without an enclosing MAB continue
to be restored through their matching bounded router APIs.

## Prepare, publish, inspect

Markoff builds exact versioned bytes, operation identity claims, reservation
metadata, and the complete intended owner-history record after the action.
`prepare()` durably binds these values and the expected accepted revision to
one transaction ID before append. An identical retry succeeds; changed bytes
or metadata for that ID fail with a typed conflict. Stale expected revision
refuses preparation and requires reconciliation.

The host atomically indexes Markoff-supplied `(stream, actor, operation counter)`
claims against the transaction ID and exact record. Markoff checks those claims
against decoded operations; the host compares/indexes them without decoding Core
wire formats. Another transaction cannot reuse a claimed identity. Terminal gap
records use the Markoff-owned `MTG/1` envelope; unscoped CTGP bytes cannot
substitute for that envelope. `MTG/1` binds document, stream, primitive, epoch,
the accepted-prefix revision token against which absence was established, and
the complete Core CTGP/0001 actor interval and causal prior. Readers compare both
the full scope and accepted-prior token before exposing CTGP bytes to an engine.
The caller still must establish absence and commit the exact MTG bytes before
calling the adapter apply path; decoding does not authorize a gap.

The Markoff-owned LWW maps have no Core primitive and use `MMG/1` for terminal
counter coverage. It binds document, map name, epoch, actor, the full nonoverflowing
counter interval and accepted-prefix token. A host records and replays this
metadata on the map stream, rejects any operation identity covered by a committed
interval, and advances its durable counter observation before permitting later
writes. MMG evidence is not an MMP operation and does not synthesize a map value.
The current codec defines the record; projection/replay enforcement remains
integration work.

`append(id)` publishes only a prepared transaction. It binds its whole shared
record and format to the ID, commits the entire action atomically, and never
publishes a partial operation list. `inspect(expectedTransaction)` checks the
original ID and all bound values after failure/restart.

| Disposition | Meaning and Markoff action |
| --- | --- |
| committed | The exact record is recoverably committed. Return its accepted revision; apply/retain the accepted projection and finish private history persistence. |
| absent | The record is definitively absent, including pending publication paths. Rebuild the tentative view and account for unused reservations through a committed scoped gap. |
| uncertain | Durability cannot be decided. Block dependent writes and inspect/recover the same transaction. |
| rejected | The request is malformed, conflicting or otherwise refused. Return a diagnostic; rejection alone proves no unused reservation interval. |

A bare I/O error must never stand in for publication certainty. Only `committed`
has a committed revision. Rejected results require a diagnostic. Exact retries
must not create another accepted record.

If append persistence reports uncertainty after atomic replacement may have occurred,
the provider session is poisoned: it must refuse inspection and further writes
until closed and reopened from durable state. This prevents an in-memory rollback
from being mistaken for proof that the accepted record is absent.

`ActionPublicationCoordinator` validates each prepared MAB against its
transaction ID, ordered operation identity claims, and MOH owner-history bytes
before prepare. It attempts append and inspects the exact same transaction after
uncertainty. A committed result stores those exact MOH bytes against the returned
revision. The reference-provider fault trace commits the MAB and then reports
uncertainty; after reopen, `recover()` finds the original transaction, stores
its private history and verifies that there is one accepted record. Unrelated
reservations block new publication, while reservations listed on the action are
bound by prepare. An absent action remains blocked until its complete reserved
intervals receive committed MTG/MMG coverage. `recoverAndRebuild()` requires a
callback to confirm replay of the complete accepted prefix before
`publishGap()` can proceed; concrete production engine construction and replay
remain integration work. An unresolved uncertain append also keeps writes
blocked.

A second fault trace makes the shared MAB commit succeed and fails private-history
persistence before its atomic rename. The coordinator reports
`committedHistoryPending`, retains the barrier and exact preparation, and after
reopen finishes the exact prepared MOH bytes without adding another shared record.
The absent-action recovery trace also injects uncertainty after terminal MMG
rename; after reopen the coordinator recovers the original gap ID and covers the
exact reservation once. A separate first-import trace then binds a new ID to the
same source, publishes the replacement initial MAB and its exact owner history,
and confirms the import intent is cleared without reusing the abandoned counter.

## Private history, first import and export

`storeLocalHistory()` stores versioned owner-local metadata tied to the exact
committed transaction and revision. The intended bytes are already retained in
the preparation, so a crash after shared commit can finish this update without
inventing a local undo stack from remote replay. Verify the transaction/revision
association before accepting the private record. Recovery finishes outstanding
updates in accepted order; an older retry cannot overwrite newer owner history.
The owner-private `MAI/3` codec records ordered action ancestry, per-stream
engine-group to operation identity associations, the action that records each
retired group, and the prior value of map edits needed for selective undo.
Undo/redo events name their parent action. Replacement events also retain the
exact pre-replacement source snapshot so document-level undo can restore the
matching Markdown export after reopen. `MOH/1` composes MAI with per-stream MAH snapshots into one intended private-history value and
binds it to the transaction ID and expected pre-action revision. The provider's
`PrivateHistoryRecord` separately binds those exact bytes to the resulting
committed revision, which cannot be predicted during preparation. Production
capture and restoration still belong to the action coordinator.

For first import, Corbomite retains the source and original transaction identity,
and calls `beginInitialImport()` with those exact values before Markoff allocates
or generates operations. The coordinator accepts an initial MAB only when its
transaction ID and decoded MDS source bytes match that intent; while it remains
pending, ordinary edits are rejected. Storing the initial action's owner history
clears the intent atomically with the private-history completion record. If an
initial attempt is proven absent, Markoff retires all of its reserved intervals
before the host supplies a fresh transaction ID for the same source. The host
commits registration with the exact accepted initial history through its
recoverable journal. Markoff generates that history once. Uncertain imports
recover their original prepared record. Missing preparation after reservation
requires absence/reservation reconciliation before generating another seed.
The initial action uses `MAB/3` and embeds an `MDS/1` source snapshot in the same
atomic record as generated operations. MDS preserves the exact original Markdown
bytes, exact separator byte slices between parsed blocks, parser version and
optional parser-derived cache. The cache is keyed by SHA-256 of the source and is
disposable; it never replaces original bytes as export authority. This keeps CRLF,
blank separators and trailing bytes intact after fresh replay. The manifest alone
never makes a document writable. The designated-bootstrap owner policy applies
until a shared registration protocol exists.

`MarkdownExportSnapshot` carries exact bytes and their accepted revision.
Corbomite writes those bytes and acknowledges that revision to the document.
`acknowledgeMarkdownExport(revision)` persists only the revision that was
written successfully. Recovery compares this watermark to the accepted head;
an absent or stale watermark leaves export pending. An acknowledgement for a
revision outside the accepted prefix is refused, and an older acknowledgement
cannot replace a newer watermark. Export failure preserves accepted history
and dirty/export state. Saving neither clears undo nor compacts history.

External/plugin replacement is an ordinary Markoff action through this pipeline.
Registered sessions must not use wholesale reset to discard
identities. Corbomite manages conflict decisions, including closed registered
notes; divergent bytes block writing until resolved. Implementation and API
acceptance of replacement actions remain open.

An accepted full-source replacement keeps the document UUID and epoch and is
one undoable MAB action with its exact replacement MDS snapshot. Every block in
the replacement receives a fresh structural identity; old anchors and
block-targeted references therefore remain stale instead of silently binding to
different text. The action removes prior block-targeted metadata and replaces
document-level frontmatter, footnotes and link-reference definitions from the
new source. Undo restores the prior shared projection through normal
compensation history; Markdown export after undo must use the source snapshot
that corresponds to that restored projection.

## Next implementation and evidence

Extend the deterministic reference provider's process, authority-loss and fault
boundary coverage; connect the Core allocators to durable sessions and implement
the action/recovery coordinator. Complete the remaining Markoff implementation/
acceptance and issue A16/M12 before Corbomite begins its local journal/storage
and application transition.
The current production engine is transitional implementation scaffolding. Replace
its production path as the candidate integration lands, then remove the old
engine and its tests when no active build or product path depends on them. Do not
preserve old state formats or APIs as a cutover condition.

Required fault traces cover reservation/prepare/append uncertainty, definitive
absence, commit-before-private-history failure, interrupted import, identity
reuse, stale revision, duplicate inspection and scoped mapping collisions.
They must prove recovery yields the original accepted identities and history,
not merely the same visible Markdown.
