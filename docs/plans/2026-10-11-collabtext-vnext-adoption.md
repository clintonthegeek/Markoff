# Markoff adoption of Collabtext vNext

Prepared 2026-10-11. **Design rationale, not the task list.** Current work
is the [task board](2026-10-12-collabtext-adoption-execution.md). Read this
file when a task needs the original ownership decision. Do not update task
status here.

The sections below record the 2026-10-11 architecture and the milestone
notes written while the port was opening. Several of those notes still say
a seam is open that the task board has since closed. The board wins.

Corbomite stays paused until A16. The candidate pin and the host direction
are on the task board. The compiled provider API is
[DocumentStorageProvider.h](../../libs/markoff-collabtext-vnext/include/markoff/vnext/DocumentStorageProvider.h).

## Baseline and source documents

**Current candidate (2026-10-13):**
`collabtext-markoff-core-2026-10-13-alpha5`,
`e8a8e7d1a95dd50e23f38d6727921b47b814e44b`, package `0.2.0-alpha.5`,
manifest v17. Earlier candidate evidence below is historical; use the execution
roadmap for current reproduction commands and production acceptance limits.

Markoff reviewed at `28e9a734bf5d383ddac9a132653c3691e5e72bd1`, with Collabtext
submodule `8b9e780bc9324faa5e7de166f7a181d1e8fbd546`.
The latest local Collabtext branch is `feat/fuguemax-placement`, already checked
out at `/home/clinton/dev/collabtext/.worktrees/reliability-v2`.
Adoption tag `collabtext-wave-core-2026-10-11` resolves to
`ffe9875ee35a525f422eff3c1aaee2841f55d962`, package `0.2.0-alpha.3`, manifest v15.
The historical alpha.4 Markoff candidate tag `collabtext-markoff-core-2026-10-11` resolves
to `1399e9533c9cfc1006854487af4e71b8b79989fd`, package `0.2.0-alpha.4`,
manifest v16. Its final executable implementation is
`c0a0ef55ea9b6a3e448b9ef5c258f09933ebb127`; the tag's successor adds finalized
evidence and handoff documentation. The root build no longer uses that
legacy submodule pin; it requires the installed alpha.5 package named above.
The root Collabtext master has unrelated uncommitted work; inspect the isolated
worktree and use the immutable candidate. Local availability does not establish
that downstream hosts can fetch this SHA.

Read these together, relative to that Collabtext worktree:

- `docs/handoff/consumer-adoption.md`: shared ownership, coordinates, position,
  recovery and merge requirements.
- `docs/handoff/wave-core-response.md` and `docs/specs/wave-core-adoption.md`:
  Wave's marker, archive reader, undo and deterministic projection contracts.
- `docs/reports/2026-10-11-wave-core-candidate.md`: exact evidence and limitations.
- `docs/handoff/markoff-core-response.md` and
  `docs/reports/2026-10-11-markoff-core-candidate.md`: alpha.4 API response,
  verification record and the current Markoff-probe failure.
- `libs/collabtext/include/collabtext/ALPHA_CAPABILITIES.md`: installed capabilities
  and supported format versions.
- `docs/roadmap/2026-10-reliability/DIRECTION.md`: remaining external acceptance.

Wave's side is `/home/clinton/dev/wave/docs/handoff/collabtext-integration-feedback.md`.
Its opening amendment records successful probes against the new candidate, but
production projector/protocol/client acceptance remains open. Superseded
alpha.2 questions and the per-event projection actor experiment are historical.
Use the current stable actor per scoped stream, never one actor per event.

## Recommended architecture

Keep Markoff's D2 per-block document model: a scoped `CrdtEngine` per text block,
one scoped `IdListEngine` for order, and Markoff-owned sibling maps, parsing,
serialization, action transactions and host transport. Choose Core composition
as the integration path; hosts own durable history and writer allocation through
an explicit provider contract. Ordinary Markdown saves are visible-text exports,
not recovery checkpoints for identities, positions or undo.

Do not adopt `DocumentSession` for each block as an additional persistence owner.
Its byte-based edits are convenient, but it owns publication and its public
position queries still expose legacy anchors. It does not replace Markoff's
cross-CRDT action coordination. The shared handoff's single-text-stream suggestion
is a possible integration shape, not a requirement to replace D2.

Native FugueMax ordering, checked allocation/apply outcomes, scoped positions,
shared compensation and local history restoration are the primary benefits.
Zero-width markers are available for future persistent inline references; they
need an explicit Markoff product policy before becoming a required feature.
Physical collection, log pruning and epoch transitions stay disabled initially.

## Initial gap inventory (2026-10-11; historical)

This inventory explains the original port scope. Subsequent API removals and
identity work supersede individual entries; use A01–A16 for current state.

| Seam | Initial implementation | Required transition |
| --- | --- | --- |
| Build | Root CMake adds vendored Collabtext; core uses source headers | Installed `find_package(collabtext CONFIG REQUIRED COMPONENTS core)` and `CollabText::Core`; exact SHA lock and relocated acceptance |
| Engine ownership | `MarkoffDocumentPrivate.h` stores `Crdt::Buffer` and `Crdt::IdList` | Private adapters over scoped public facades; retain QObject proxies |
| Identity | Constructors accept `quint16`; operation IDs pack replica/counter into 64 bits | Durable 128-bit actor, checked 64-bit allocation, stable document/stream/epoch scope; no truncation |
| Anchors | `TextAnchor`, `AnchorConversion`, `AnchorJson`, session scroll and folds expose raw fields | Opaque scoped token wrappers, binary CTPT storage and checked typed resolution |
| Edits | `d2ApplyBufferEdit` uses UTF-8 ranges; old Buffer edits take bytes | Checked conversion into facade UTF-16 edits against the current block revision |
| Operations | `MarkoffDocument.cpp` visits private variants to read timestamps | Public identity accessors and checked codecs; route opaque bytes in a versioned envelope |
| Delivery | Remote methods skip malformed input and return no per-op outcomes; unknown block queue is unbounded | Explicit applied/duplicate/deferred/rejected status, durable receipt policy, bounded routing queue and quarantine |
| Undo | `UndoLog` coordinates individual per-engine stack steps; `undoForBlock` selects an earlier document action | Explicit action-to-engine-group mapping, checked compensation publication and failure semantics |
| Save/GC | `WatermarkCoordinator` compacts with local versions, clears undo and disposes orphan buffers | Save tracks dirtiness/export only; retain history and removed streams during initial adoption |
| Sibling maps | `CausalLwwMap` uses 16-bit stamps and restores prior entries locally on undo | Review actor/counter protocol and replicated compensation separately; Core does not supply a map CRDT |

The old pending-payload key uses target type plus packed operation ID, without
the block stream. Replace it with full stream and operation identity so matching
counters from different blocks cannot collide. Audit block ID creation too:
fixed local ranges are not proof of uniqueness across independent writers.
Keep `uint64_t` list application values as data; use full insertion identities
for BlockId and routing. No application-value mapping may substitute for identity.

**ASK-M2 reproduced and fixed (2026-10-11):** one flat paste spanning several
blocks can create buffer operations with identical per-buffer Lamport IDs. The
old pending-payload map routed each committed operation to the same slot, so
only one block's payload reached the peer. Its key now includes target type,
BlockId and OpId; `multiBlockPaste_routesEveryBufferOperation` reproduces the
six-block paste and verifies both replicas converge. This closes the current
host routing defect, while M6 still needs the equivalent scoped-stream routing
and malformed/deferred-operation policy on the new Core.

## Implementation sequence and exit gates

The M0–M12 checklist that used to sit here was a second status board. It
disagreed with the engine cutover. A01–A16 on the
[task board](2026-10-12-collabtext-adoption-execution.md) replaced it on
2026-10-17. The milestone notes under this heading are historical.

### M5 CTLO archive-reader gate — historical; the structural port has landed

The initial alpha.3 candidate labeled CTLO/0002 checked alpha interchange and
did not declare a durable-reader obligation. Alpha.4 promised the CTLO/0002
reader and recorded literal insert/remove/undo identity fixtures. The Markoff
structural port later landed on alpha.5; see the task board. The instructions
below that tell a host to withhold CTLO bundles describe this gate before that
port. They are not a current prohibition. The prior alternatives were:

- a CollabText release/capability promise that retains the CTLO/0002 reader and
  semantics for old durable archives, backed by literal insert/remove/undo
  archive fixtures and replay assertions; or
- a Markoff-owned independent CTLO/0002 archival decoder/application path with
  the same fixture and replay evidence.

Physical pruning and checkpoint-based replacement stay disabled. Replay from
the accepted operation prefix remains the recovery path.

**M0 evidence (2026-10-11):** candidate
`ffe9875ee35a525f422eff3c1aaee2841f55d962`; Release Core package verifier passed
and relocated its install to `/tmp/markoff-ct-candidate-package-final/prefix-b`.
Producer/package evidence is in
`/tmp/markoff-ct-candidate-package-final/results.json`. The Markoff-owned probe
configured and built from `tests/collabtext-vnext` with
`CMAKE_PREFIX_PATH=/tmp/markoff-ct-candidate-package-final/prefix-b`, then
passed `ctest --test-dir /tmp/markoff-vnext-probe-build --output-on-failure
--no-tests=error` (1/1). It used two durable `ReplicaState` identities; encoded
and replayed a block edit; resolved the original CTPT boundary after replay;
and delivered the undo compensation once. This verifies the package and narrow
Core seam, not MarkoffDocument integration, cross-stream undo, or production
consumer acceptance.

**Alpha.4 update (2026-10-12):** the CollabText report records alpha.4 package
verification and an installed public `consumer_markoff_core` trace covering
visible list identities, CTLO archives and counter gaps. The exact candidate
tag is `collabtext-markoff-core-2026-10-11` at
`1399e9533c9cfc1006854487af4e71b8b79989fd`; its executable implementation is
`c0a0ef55ea9b6a3e448b9ef5c258f09933ebb127`. See the linked
[`markoff-core-candidate report`](../../../collabtext/.worktrees/reliability-v2/docs/reports/2026-10-11-markoff-core-candidate.md)
and [`upstream response`](../../../collabtext/.worktrees/reliability-v2/docs/handoff/markoff-core-response.md)
in the candidate worktree. These upstream results do not pass Markoff's own
consumer gate.

**Historical M10 alpha.4 probe finding (superseded by follow-up below):** the candidate report says the Markoff-owned
probe configures/builds, then fails before Core construction because
`tests/collabtext-vnext/main.cpp` supplies `stream = "block-1"` while
`ScopedTextStream::create` enforces `parseBlockTextStreamName`. The same source
also failed against alpha.3 for this adapter-level reason. Correct the fixture
to derive and reuse one canonical stream via
`blockTextStreamName(BlockIdentity{actor, counter})`; retain canonical-name
validation. This is the known next integration fix, not evidence that alpha.4's
new APIs have passed through Markoff.

**M10 host-response follow-up (2026-10-12):** both fixture streams are now
canonical, derived with `blockTextStreamName()` from one actor and distinct block
counters. Release configure/build against
`/tmp/ct-markoff-release-live-c0a0ef5/prefix-b` succeeds in
`/tmp/markoff-host-boundary-alpha4`; CTest fails **0/1** at private lease
acquisition (`invalid document/stream lease key`). The provider header also
compiles in this build. This reveals the direct allocator's storage-key mismatch;
it does not justify weakening canonical route validation. M10 remains open.

The adapter has a library-owned CMake target in
[`libs/markoff-collabtext-vnext/CMakeLists.txt`](../../libs/markoff-collabtext-vnext/CMakeLists.txt).
The installed-package probe consumes that target rather than maintaining a
second source list. The repository's normal Markoff build remains on the legacy
submodule until the M1/M5 identity and format gates close.

**M2 evidence (2026-10-11):** added `TextUnits::checkedByteToQtPos` and
`checkedQtPosToByte`; these reject malformed UTF-8, out-of-range offsets, split
UTF-8 scalars and UTF-16 surrogate pairs. `tst_text_units` covers `A😀é`, empty
text, invalid encodings and bounds. `cmake --build build-dev --parallel 4
--target tst_text_units` passed, followed by
`scripts/run-tests.sh -R '^tst_text_units$'` (1/1). Existing permissive helpers
remain for their current view callers; migration adapters should use the new
checked API.

**M3 design and codec slice:** [`2026-10-11-collabtext-vnext-persistence.md`](../specs/2026-10-11-collabtext-vnext-persistence.md)
defines a versioned shared action-bundle envelope, device-private `ReplicaState`
ownership, local-history snapshot fields and recovery order. The action-bundle
codec now emits and accepts only canonical MAB/2, with ordinary-action
round-trip and version-refusal coverage in the relocated-package consumer.
Added
[`LocalHistoryCodec`](../../libs/markoff-collabtext-vnext/include/markoff/vnext/LocalHistoryCodec.h)
for binary `MAH/1` snapshots and connected it to `ScopedTextStream`. The
relocated-package probe encodes/decodes a real snapshot, rejects unsupported,
truncated, wrong-stream and wrong-actor records, compares every serialized
snapshot field, and covers a retired entry. It reopens the private writer
state, replays the shared operation prefix, restores history, and successfully
publishes undo/redo compensations to a peer. Build and ctest passed (1/1). M3 remains
partial: Markoff has no host private-record persistence adapter or shared MAB
log integration. MAB `encode()` and `decode()` now invoke checked payload
identity validation. Added the bounded MMP/1 map-operation payload codec and
canonical map-name-to-stream binding, plus MBK/1 and MAV/1 codecs for current
block-kind and attribute values, plus an MLR/1 URL/title value codec for
`LinkRefMap`. MAB validates `kind-tag`, `block-attrs` and `link-ref` routes,
targets, keys and typed live values. Added an isolated `ReplicatedMapStream`
that reserves durable scoped identities and applies Markoff's per-key LWW
ordering across generic MMP operations with hash-indexed `(target block, key)`
lookup.
Its MAB conversion helper carries route, target, identity and bytes together.
Frontmatter/footnote values remain raw
bytes and are document-scoped. MAB and the map adapter now share one route/value
validator, which checks before durable counter reservation. Production map
aliases, Markdown serialization,
local undo and `MarkoffDocument` wiring remain open; map ordering uses the full
actor ID instead of legacy 16-bit causal stamps.
Codec round-trip/rejection tests, host
commit/recovery and fault coverage remain open. The candidate's CTGP/0001 codec
did not provide Core gap application in alpha.3. Alpha.4 now exposes scoped
text/list `apply_counter_gap()` and commits to CTGP/0001 reader compatibility;
see the candidate response/report above. This supplies engine semantics, not
Markoff's durable gap event, absence proof, accepted-prefix rebuild, exact
reservation tracking or receipt policy. M3 remains partial until the host log
persists/replays gaps and failure recovery is integrated. M1 host identity and
M8 cross-stream undo remain open; CTLO/0002's upstream reader policy is now
resolved for alpha.4, with Markoff-side acceptance still pending.

**M4 adapter slice (2026-10-11):** added
[`ScopedTextStream`](../../libs/markoff-collabtext-vnext/include/markoff/vnext/ScopedTextStream.h),
compiled only by the isolated relocated-package consumer. It requires an
explicit text `PositionScope`, private `ReplicaState` and exclusive stream
writer lease; validates Markoff UTF-8 byte ranges and converts them to one
UTF-16 `try_replace`; returns each local operation and encoded bytes as the
single publication source; validates remote document/stream/epoch and decoded
operation identity; and wraps CTPT boundary encode/resolve. Creation now also
rejects invalid document keys and noncanonical per-block stream names. That
scope-validation change is compile-checked only; the earlier relocated probe
does not cover it. The probe covers
Unicode insertion, split-scalar rejection without mutation, replay, duplicate,
wrong-route/forged identity rejection, token resolution, undo and redo. Build
and `ctest --test-dir /tmp/markoff-vnext-probe-build --output-on-failure
--no-tests=error` passed (1/1) against the relocated candidate prefix. M4 stays
partial: `MarkoffDocument` still uses the old engine, Markoff Markdown
serialization is not yet exercised through this adapter, and host publication
and cross-stream undo remain M8/M10 gates. The probe also replays the full
insert/undo/redo prefix into a fresh reader, resolves the original token after
that replay, reopens the writer's private state, verifies its ActorId is stable,
replays the accepted prefix, and confirms the next local operation counter is
above the prior redo counter.

For the production port, the serializer compatibility baseline is concrete:
retain the D2 byte-identical `setext_untouched_roundtrip_is_byte_identical`
case, blockquote round-trip cases in `tst_block_buffer_invariant`, the
`tst_d2_reset_content` reload/replacement cases, and D5's two-document
convergence assertions. Run those against the migrated `MarkoffDocument` path,
not only the isolated adapter. Add coverage for untouched CRLF, trailing
whitespace and final-newline behavior before claiming exact-byte preservation;
the current adapter probe exercises text edits, not Markdown serialization.

The installed Core API has no direct populated-text restore/seed method.
`ScopedTextStream` now exposes `seedInitialImport()` and
`commitInitialImport()`: a nonempty first seed returns one insertion operation,
empty input enters pending import with no operation, and local edits, undo/redo,
remote applies and history restoration are refused until the host commits the
`initial_import` bundle and clears that seed from local undo; history snapshots
are empty while the import is pending. Other devices
replay those operations; independently inserting the same imported text on
each writer would mint unrelated content identities. This per-stream seam is
compile-checked only; a multi-stream host transaction, recovery after uncertain
append, and regression coverage remain open. On append failure the host must
inspect/replay the original intent if the append is uncertain. If it is
definitively absent, discard and rebuild from the accepted prefix but stop
writes on that stream until the consumed counter is durably published as a
terminal gap peers can apply. The current candidate exposes no such Core apply
path, so this initial-import failure case remains a writer enablement gate.

**M6 router slice (2026-10-11):** added
[`ScopedOperationRouter`](../../libs/markoff-collabtext-vnext/include/markoff/vnext/ScopedOperationRouter.h)
for one document epoch's text streams. It validates document/stream/epoch,
primitive, delivery sequence, bounded action-bundle index, route-string sizes,
and decoded operation identity; retains unknown
stream and causally deferred envelopes under operation-count and byte budgets;
returns backpressure without consuming caller input; retains rejected engine
operations for diagnosis; and reports complete original envelopes when an
operation becomes applied or duplicate. The relocated-package probe verifies
unknown-stream drain, caller retry after a one-item capacity limit, out-of-order
causal delivery, both completion receipts and final text. Build and ctest passed
(1/1). A compile-checked [`ScopedMapRouter`](../../libs/markoff-collabtext-vnext/include/markoff/vnext/ScopedMapRouter.h)
adds equivalent bounded routing for map streams, including unknown-stream
retention, drain on stream registration, applied/stale/duplicate outcomes and
a quarantine acknowledgment seam. This map slice has no regression coverage;
the text probe does not exercise it. M6 remains partial: structural routing,
bundle-atomic application, durable quarantine and host receipt commits are not
implemented here. Added a
rejected-envelope enumeration and exact-envelope acknowledgment seam; the host
must persist the raw bytes and reason before acknowledging to free router
capacity. Repeated identical conflicting envelopes are deduplicated instead
of consuming more queue space. These latest changes compile but do not yet have
regression coverage. The router's queue is an in-memory bounded projection aid;
the host log remains the recovery authority. CollabText reports `duplicate`
when a causal identity is already covered without comparing the new bytes to
the original operation. Therefore the host's durable MAB log must maintain a
transactional `(stream, actor, counter) -> exact envelope` index and reject any
identity reused with different operation bytes or action metadata before the
router can advance an applied receipt. M6 is still open until this host boundary
and its recovery path are implemented.

**M7 token slice (2026-10-11):** `ScopedTextStream` stores and restores CTPT
bytes as `QByteArray` without text conversion. The installed-package probe
confirms the bytes contain NUL, a token fails to resolve in a different stream,
and the same original token resolves after replay. It mints left- and
right-affinity boundaries, applies a concurrent insertion into their original
gap, then verifies left resolves before it and right after it. This validates
the adapter contract only; Markoff caret, selection, scroll, fold and reference
registries remain on existing position types, so M7 is partial.

### M1 working contract

Consumer-facing CollabText requests are collected in
[`2026-10-11-collabtext-vnext-consumer-asks.md`](../handoff/2026-10-11-collabtext-vnext-consumer-asks.md).
They are proposals pending upstream discussion, not accepted CollabText
commitments.

Use the host's stable document key as `DocumentId`. Use a separate structural
stream for block order and one text stream per stable block identity; include
epoch `1` at initial adoption. Stream names must be canonical and collision
free, scoped to the document by the engine. Do not use a display name, session
ID, current block index, or 16-bit replica number as identity. A block's stream
must survive reorder, close/reopen and replay.

Under the recommended composite identity, use structural stream
`markoff/order/v1` and text stream
`markoff/block/v1/<32-lowercase-actor-hex>/<16-lowercase-counter-hex>`. The
document key remains a separate scope component. The inserted IdList operation
identity is the BlockId; visible order is read by entry identity, never by the
duplicate-capable application value. `BlockId` retains the full 128-bit ActorId
and 64-bit counter without packing or truncation.

The host's private `ReplicaState` is the durable writer authority, outside shared
document storage. A thin per-stream `IdentityAllocator` adapter acquires that
document/stream writer lease, reserves counter runs durably before operations,
and passes remote observations back to the matching stream. Reopen with the same
private state; copying shared Markdown/document data must not copy the device
actor. Counter, epoch, transport delivery sequence and user action ID remain
separate values. Never pack actor/counter into the old 64-bit `OpId` convention.

For text and list operations, define a versioned Markoff envelope containing:
document key, stream key, epoch, target kind, optional block identity, producer
actor and counter, action/bundle ID and ordering/count metadata, plus the exact
opaque CTBO or CTLO bytes. Derive and compare actor/counter with
`Crdt::op_identity` or `Crdt::idlist_op_identity` after checked decode; the
operation value itself does not supply Markoff's document/stream route. Bind
target, block and stream consistently before apply. Unknown versions, malformed
bytes, wrong document/stream/epoch and rejected/deferred applies retain the
original bytes and must not advance the host's durable applied receipt. Markoff
continues to own the canonical action transaction and publishes each returned
operation once.

**M1 remains open:** Markoff's production `BlockId` is still a `uint64_t`, and
Corbomite has no stable document key separate from its renameable path. Alpha.4
now supplies the ordered composite IdList identity query, terminal-gap apply
path and CTLO/0002 reader commitment, but these Core additions do not define
host document identity/copy behavior or integrate Markoff persistence. Resolve
the remaining ownership contracts before enabling the production path. Do not
mark M1 done based only on this envelope sketch or upstream API delivery.

#### M1 initial host/API findings and later updates

Host requests and proposed acceptance criteria are collected in the
[Corbomite adoption handoff](../handoff/2026-10-12-to-corbomite-collabtext-adoption.md).

The findings below describe the existing production API. Corbomite's subsequent
[host response](../../../Corbomite/docs/handoff/2026-10-12-to-markoff-collabtext-host-response.md)
supplies proposed lifecycle/storage policies, now accepted as design directions
in Markoff's [provider reply](../handoff/2026-10-12-to-corbomite-provider-contract.md).
The production code still needs the provider and lifecycle implementation.

Corbomite's `NoteDocument` stores `vaultRoot` and `relativePath`; the path
changes on rename, and the inspected API exposes no separate durable document
ID. Its constructor currently supplies Markoff replica ID `1`, with a comment
that this is a workaround pending collaboration needs. Treat this as evidence
about the current integration, not a reason to use paths or a shared constant as
vNext identity. The host needs to persist and supply a stable opaque document key
plus private per-device `ReplicaState` outside the shared Markdown file.
Markoff's public construction path must make missing scope/allocation authority
explicit.

Further host inspection confirms this is not hidden in the vault model:
`TAbstractFile` stores `path`/`name`, `Vault::m_fileMap` is keyed by normalized
relative path, and the cached metadata store's `file_cache` primary key is
`path`. These values identify a current location, not a durable document.
Corbomite needs an explicit persisted document-ID registry or file metadata
contract, with rename/move updates and copy/fork semantics. Until chosen, do not
derive `DocumentId` from path, file hash, or Markdown contents.

Keep the identities in different persistence scopes. The stable document ID
must travel with the shared document so another device addresses the same
streams; a per-document sidecar or shared document metadata is a better fit than
an app-local registry. The private `ReplicaState` and actor key must remain in
device-local application data and must never travel with a copied/synced vault.
Vault rename/move should carry the document ID. Explicit duplicate/copy should
mint a new one; external copies that also duplicate a sidecar need a detectable
fork/reconciliation rule. This is the recommended ownership split, not an
adopted host contract.

The current Markoff implementation has numeric counters in its structural and
load paths. Replacing those counters with a host-allocated numeric ID would
preserve a second identity path, so that option is closed. **Selected contract:**
derive each `BlockId` directly from the structural
IdList insertion identity. Alpha.4's ordered entry-identity query makes this
path available without hashing or mapping into `uint64_t`. No numeric block-ID
allocator, actor hash, truncation, process-static counter or best-effort random
value is part of the new contract. The current Corbomite `NoteDocument` has no
collaboration transport contract, so its present API cannot establish stable
document scope or private writer authority; host acceptance must specify those
before M1 closes.

Markoff's isolated adapter now defines `BlockIdentity` as the full ActorId plus
nonzero counter and provides canonical `markoff/order/v1` and
`markoff/block/v1/<actor-hex>/<counter-hex>` names. The MAB codec binds text
target identity to that stream name and IdList operations to the order stream.
Markoff maps use `markoff/map/v1/<map-name>` with lowercase ASCII route names;
MAB encode/decode bind that route to the MMP payload's map name and operation
identity.
These helpers compile against the installed candidate; they are not wired into
MarkoffDocument. Alpha.4 now supplies the previously missing ordered
visible-entry identity API. M1 still lacks the host document-key/copy policy,
Markoff-owned acceptance of that API, and direct name/parser regression
coverage.

The original alpha.3 candidate confirmed this API boundary: `IdListEngine` inserts and
looks up caller values as `uint64_t`; the value may duplicate and is not itself
the CRDT element identity. Its durable `ListElement` token is distinct from
that application value. The public engine already accepts identity-based
`Crdt::Anchor` values for checked insertion/removal, but it does not expose
visible entry identities in order. Alpha.4 addresses that limitation with
`visible_entries()`; Markoff should now use this API and the exact-identity
anchor overloads in its production port. This historical alpha.3 limitation is
not a reason to choose a serialized `uint64_t` allocator, which would give up
independent offline block creation.

The requested additive API is a public ordered value/identity projection,
for example `visible_entries() -> vector<VisibleIdListEntry>`, where each entry
contains the caller's `uint64_t application_value` and its full insertion
`Crdt::Identity`. The returned sequence must contain visible entries only and
preserve current document order; equal application values must remain separate
entries. This is preferable to parallel `ids()` and identity arrays because
they cannot be safely correlated if either projection changes. The candidate's
private `Crdt::IdList::IdListEntry` already stores `id` and `origin`, so this is
an additive facade over existing state, not a new wire field or identity rule.
Markoff switches its `BlockId` and block-scoped map to the composite identity.
The IdList API's required `uint64_t` application-value slot is opaque list
payload only; it is never an ID, route, cache key or fallback. The query must
include entries created remotely and retain the original
insertion identity when deletion is undone. Add candidate tests for repeated
values, concurrent inserts, delete/undo visibility and exact order before
reopening M5.

This was cross-checked against the candidate's 2026-10-11
`docs/handoff/consumer-adoption.md` and `docs/handoff/wave-core-response.md`.
The shared handoff assigns Markoff the same persistence, routing, recovery and
undo ownership as Wave when using Core directly, while explicitly leaving each
consumer's model and acceptance to that consumer. Wave's selected EventStore,
marker and archive-reader policies are not Markoff decisions. The shared
handoff also warns consumers not to hash or truncate application identities
into IdList's `uint64_t` values, which rules out treating that field as a
composite BlockId surrogate. Markoff therefore keeps the M1 API and host
identity questions open; the Wave response does not close them by analogy.

Corbomite's current constructor receives only `(vaultRoot, relativePath)`, and
constructs Markoff with replica ID `1`; neither is a durable writer identity.
The migration constructor should require a host-provided stable document key
and private per-device `ReplicaState`, with no default collaboration identity.
The host must define how the document key and block allocation survive rename,
move, copy and offline concurrent creation before M1 can close.

The public `element_of(uint64_t)` implementation returns the first visible
matching value, and `try_anchor_of(uint64_t)` also addresses by value. Although
`ListElement` can persist one exact identity after minting, there is no public
visible-identity enumeration. Duplicate application values therefore cannot
provide a hidden composite-ID workaround. This is an API capability gap, not
only a serialization choice.

#### Selected block identity path

For the intended multi-writer/offline CRDT use, use a composite BlockId
derived directly from the structural IdList insertion operation's
`Crdt::Identity` (ActorId bytes plus checked counter). The pinned candidate
already makes an inserted entry's origin identity equal to its insertion
operation identity, exposes `idlist_op_identity()`, and accepts identity-based
`Crdt::Anchor` values for structural mutation. No 64-bit surrogate or random
mapping is needed. Canonical text-stream names can encode the exact actor bytes
and counter under the stable document scope.

This requires an additive installed IdList query such as
`visible_item_identities()` that returns entry identities in visible order;
`ids()` returns only duplicate-capable application values. IdList insertion
values remain an unused constant because Markoff addresses structural items
only by their insertion identity. Markoff widens
its public/internal `BlockId` and route buffers, attributes, anchors, undo and
remote envelopes by the composite identity. Remote insert routing can use the
decoded insertion operation identity, then reconcile order from the new query.
Keep CTLO/0002's archival reader policy as a separate gate.

Suggested additive signature: `Result<std::vector<Crdt::Identity>>
visible_item_identities() const`, in the same visible order as `ids()`. Return
only visible entry origins; callers can create checked structural anchors from
those identities. Preserve duplicate application values and expose no private
IdList payload/layout.

#### Markoff composite-BlockId impact inventory

The production conversion is an end-to-end identity migration, not a change to
`BlockId` alone. The current `BlockId::raw()` assumptions are concentrated in
these seams; update them together after the installed IdList query exists:

- **Document allocation and structural replay:**
  `MarkoffDocumentPrivate.h`'s `BlockIdHash`, per-document allocation and
  `MarkoffDocument.cpp`'s structural insert/remove, `ids()` projection and
  remote `StructuralOp` decode. Structural operations must preserve exact
  inserted identities and reconcile visible order from IdList's ordered
  identity query.
- **Text and UI routing:** `blockBuffers`, pending operation keys,
  `blockEditSequences`, load-time snapshots, `BlockId`, `TextAnchor`,
  cursor/selection, `InlineParseCache`, search and source-text bindings. All
  must key by the composite identity without converting through a list
  application value. The unused `BlockAnchorComputation` helper and its
  unpopulated `MarkoffDocument` range/anchor cache were removed on 2026-10-13;
  active consumers resolve block-qualified `TextAnchor`s against the actual
  block buffer, so the helper is not part of the remaining migration surface.
- **Undo and sibling maps:** `UndoLog` buffer targets, kind-tag and block-attr
  keys, link/reference targets, and D2 action payloads. Their persisted forms
  use versioned full identities. Since no Markoff data is deployed, reject
  experimental numeric-ID forms; do not add readers or migration rules for
  them.
- **Consumer surfaces:** public APIs and QML-facing values use an opaque stable
  representation. Remove numeric `BlockId::raw()` identity surfaces as part of
  the production port; no source-compatibility wrapper is needed. View indices
  remain transient positions and must not become identity.

Use this list as the M1/M4/M5/M8 implementation boundary. Do not start by
changing only `BlockId` or by translating composite identities into random or
hashed 64-bit list values: that would leave remote replay, undo and map routing
unable to recover the same block identity.

The selected contract is the full structural insertion identity, which supports
independent offline block creation. There is no second host-allocated numeric
identity path. Alpha.4 introduced the identity query. The installed alpha.5
consumer now verifies repeated application values and simultaneous offline
inserts preserve distinct actor/counter identities and converge in visible
order (`tests/collabtext-vnext/tst_document_storage_provider.cpp`,
`runConcurrentOfflineListIdentity`). This closes candidate API acceptance; the
production MarkoffDocument port remains open under A07/A08.

#### M1 closure checklist

- [ ] Consumer document key is stable across rename, move, reopen and device copy.
- [ ] Block identity is unique across concurrent creation and is addressable in
      the structural stream without lossy conversion.
- [ ] CollabText exposes visible IdList entry identities (not only
      application values) through the installed public API; checked structural
      mutations can then address those identities directly.
- [ ] Per-device private state, actor lifetime, writer leases and per-stream
      counter reservations have an owner and persistence location.
- [ ] Every consumed-but-uncommitted per-stream counter is durably represented
      by a terminal gap that peers can apply before later operations proceed.
- [ ] Markoff envelope version and binding checks are specified for text, list
      and Markoff-owned sibling-map operations.
- [ ] Markoff uses exactly one pinned CTLO reader for structural operations;
      reject other experimental versions. A shipped-data upgrade policy is
      outside this pre-release adoption.

#### M1 dependency handoffs

These are separate deliverables. Completing one unblocks its dependent Markoff
work but does not close M1 by itself.

| Handoff | Owning codebase | Deliverable | Acceptance evidence | Status | Unblocks |
| --- | --- | --- | --- | --- | --- |
| H1: ordered structural identity | CollabText candidate | Add and install a visible IdList entry identity query, preserving visible order, duplicate application values, remote inserts and delete/undo identity | Public consumer probe and fixtures for duplicate values, concurrent insert order, delete/undo, and exact identity after replay | Delivered in alpha.4; Markoff adoption remains | Markoff's composite `BlockId` implementation and M5 structural projection |
| H2: document identity and private writer state | Corbomite/host | Persist a shared stable document key across rename/move/reopen; define duplicate/fork recovery; provide device-local `ReplicaState` storage and lifecycle | Host API contract plus reopen/rename/copy/fork acceptance record; demonstrate actor state is not copied with shared document data | Open | Markoff production constructor and stream creation |
| H3: operation-gap recovery | CollabText + host persistence | Provide a durable terminal-gap publication/apply path after reserved identities cannot be committed | Crash/reopen probe: failed append consumes a counter, gap is durably replayed, then a later operation is accepted by peers | Core path delivered in alpha.4; Markoff durable log/recovery open | Enabling local text/list/map writes after definitive append failure |
| H4: Markoff identity conversion | Markoff | Use full `BlockId` identity at every route/key/persistence seam; remove numeric identity APIs and reject experimental old forms | Full Markoff consumer build plus D2 identity/replay/undo/Markdown acceptance against the exact candidate | Open | Production M4/M5/M8 work |
| H5: structural archive reader | Markoff | Select one CTLO reader from the pinned candidate and validate it in Markoff; refuse other versions | Literal insert/remove/undo fixtures replayed within the selected installed candidate after restart | Alpha.5 consumer acceptance remains open | Persisting IdList operations in durable MAB bundles |

H1 and H2 can proceed independently. H1 and H5 now have alpha.4 candidate
evidence, and H3 has a Core apply path; Markoff must still accept them through
its own relocated consumer and implement the host-side durable contracts. H3
must be integrated before enabling writes that may survive a definitive host
append failure. H4 depends on H1 and H2 and is production migration work after
the identity contract is accepted. Close M1 only when H1–H3 have accepted
contracts, the MAB binding/version contract is fixed, and H5 has one selected
reader verified against the pinned candidate. Verify Markoff's obligations
with its reference provider while Corbomite stays paused. H2's actual Corbomite
implementation follows M12; it must not become a circular prerequisite for the
readiness signal. H4 remains open under M4/M5/M8 until Markoff's consumer
acceptance passes.

1. **Freeze the consumer contract and run a small public probe.** Record the
   candidate SHA, component, persistence owner and host provider interface.
   Build an isolated Markoff-owned installed-package probe before changing the
   production submodule. Exercise one real block edit/serialization path, Unicode,
   original-token replay and compensation. Coordinate production rollout with
   Wave's still-open Q06; preparing this plan does not close that upstream gate.

2. **Introduce identity, scope, envelope and coordinate types.** Specify stable
   document IDs, block/order stream names, initial epoch 1, per-stream allocator
   reservation and private writer state. Display/presence IDs remain distinct
   from operation authority. Widen Markoff-owned stamps and identifiers explicitly.
   Delivery sequence, action ID and operation counter are separate domains.
   Add checked UTF-8 scalar/UTF-16 conversions; current `TextUnits` helpers clamp
   offsets and tolerate malformed input, so they are insufficient validation.
   Gate: `A😀é` ends at byte 7 / UTF-16 4; after emoji is byte 5 / UTF-16 3;
   split scalars/surrogates and malformed input are rejected without mutation.

3. **Port the D2 storage and edit seam.** Replace private Buffer/IdList access
   with public scoped facades. Keep block parsing, separators, load-time source
   snapshots, serializer registry and structural maps owned by Markoff.
   Use `try_replace` for one range replacement as one undoable operation.
   Create initial shared state once and replay it on peers; do not independently
   seed the same writable document under each device actor. Reopen/reload into
   fresh engines; `discard_local_view` cannot replay already-observed operations.
   Audit the legacy flat-buffer API and move remaining callers to D2 before
   removing its storage. Gate: untouched Markdown round-trips byte for byte;
   local/remote edits preserve existing rendering and structural behavior.

4. **Port positions and view subscriptions.** Wrap `TextBoundary` for carets and
   flexible endpoints; use strict attachments only where loss is intentional.
   Store CTPT as QByteArray or base64url in versioned JSON, with routing context.
   Start with right-affinity carets and left/right selection endpoints, subject
   to explicit view behavior checks. Collapsed boundaries retain usable offsets;
   detached strict references have none. Preserve original tokens across replay.
   Migrate folds and scroll with block identity included: a token identifies CRDT
   content, not a stable parser node. Keep engines on the document owner thread,
   retain RAII subscriptions, and coalesce invalidations after apply batches.
   Suppress editor feedback and refresh parse/layout caches for deferred drains.
   Cover active canvas/accessibility and shared session consumers; inventory
   source/styled exported API impact before any leaf changes.

5. **Resolve action undo and durable publication before collaborative cutover.**
   Coalesce engine groups per Markoff action where supported, and persist the
   cross-stream action mapping plus local history snapshots. Shared replay runs
   before local metadata restoration. Select one publication source; stage
   returned operations in the action bundle is the recommended starting point.
   Every undo/redo compensation must travel through that same publication path.
   Verify `undoForBlock` when another action touches the same stream: Core has
   no arbitrary historical-operation undo, so do not assume the old action can
   be addressed directly. Settle the behavior or request a concrete public API
   addition before declaring parity. Specify multi-stream partial failure and
   rejected durable append recovery; `UndoLog::Transaction::rollback` currently
   discards bookkeeping rather than reverting mutated engines. Gate: structural
   edits and cross-block actions undo/redo consistently on peers and after reopen;
   failed writes cannot create an acknowledged phantom action.

   **M8 source audit:** Markoff's `UndoLog::undoForBlock` chooses the latest
   document action whose targets contain that block, then dispatches every
   target in reverse order. `MarkoffDocument`'s dispatcher calls each engine's
   stack `undo()`/`redo()` and discards `opId`; it does not select the
   corresponding historical operation/group. This is unsafe for shared
   structural/sibling-map streams when a later action on another block has
   pushed a newer entry on that same engine. The public CollabText facade
   exposes only stack `try_undo()`/`try_redo()` plus group inspection; it has no
   arbitrary group undo. M8 must either add a checked/selective group operation
   to CollabText and use it for both document and per-block undo, or record an
   explicit product-level behavior change. Do not silently map
   `undoForBlock` to document-wide LIFO. Evidence: Markoff
   [`UndoLog.cpp`](../../libs/markoff-core/src/UndoLog.cpp),
   [`MarkoffDocument.cpp`](../../libs/markoff-core/src/MarkoffDocument.cpp),
   and the candidate's public
   [`CrdtEngine.h`](../../../collabtext/.worktrees/reliability-v2/libs/collabtext/include/collabtext/CrdtEngine.h)
   undo surface.

   Required M8 regression: action A touches block A and a shared structural or
   metadata engine; action B later touches block B through that same engine;
   per-block undo of A must preserve B and must publish compensations for
   exactly A's stream groups. Also test global LIFO across several streams,
   redo, restart-restored grouping, and failed allocation/append midway through
   a multi-stream compensation. The last case must recover the original durable
   action prefix and must not publish only a subset of the compensation bundle.

6. **Replace save compaction and complete real consumer acceptance.** Retain
   removed streams, pins, operation history and local metadata; disable all old
   save-triggered and explicit physical compaction routes. Save/export must not
   clear undo. Bound queues and handle unknown streams, wrong scope, resource
   limits and uncertain commits without silently advancing receipts. Run Markoff's
   actual editing/routing suites against a relocated installation using no source
   includes, then record results and downstream host API impact. Measure typing,
   paste, multi-block edits, reference resolution, reopen and retained memory.
   Upstream benchmarks make no broad latency claim; edits still rebuild storage.

## Persistence decisions that must be explicit

- New alpha adoption is a clean start: do not decode pre-vNext operations or
  convert raw anchors into scoped identities. Importing existing Markdown
  creates a new CRDT document. No migration path for abandoned collaborative
  history is part of this work.
- Markoff writes and reads one selected version of each Markoff-owned record
  family and refuses other versions. It retains original Markdown bytes for
  exact export. Verify the selected Core candidate's operation readers through
  the installed consumer; do not add Markoff migration readers for abandoned
  experimental formats. New writer-byte determinism and general private-
  checkpoint upgrades remain separate, unpromised contracts.
- Sibling-map schemas, Markoff envelopes and local history serialization remain
  consumer contracts. Define their versions, readers and compensation semantics.
- Persisting a token alone registers no durable pin. If durable references are
  enabled, log pin creation/release transactionally and replay the pin projection.
  Keep collection disabled regardless until the complete provider gate exists.

## Acceptance record and release boundary

Record Markoff revision, exact dependency SHA, installed components, compiler,
configuration, nonempty test selection, results, durability owner, coordinate
adapter, affinity policy and enabled lifecycle features. Include duplicate and
reordered delivery, concurrent typing/paste, original-token reopen, strict loss,
block deletion, exact Markdown bytes, feedback suppression and failed writes.
Existing repository guidance records 213/213 at the September close; that is
historical evidence, not a test result from this review. No tests were run here.

Both Wave and Markoff must ultimately test the same final candidate. Fixes create
a new candidate tag rather than moving this one. Update Markoff's dependency pin
only as part of the validated migration. Collabtext master merge, publication and
downstream host rollout are distinct release actions under the shared handoff.

Current order and the finish line are A01–A16 on the task board. Selective
undo and durable publication have landed; the open tail is listed there.
Global same-candidate release checks and explicit merge or publication
authorization remain separate obligations.
