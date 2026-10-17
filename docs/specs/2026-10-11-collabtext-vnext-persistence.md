# Markoff CollabText vNext persistence boundary

Status: M3 design with partial `MAH/1`, `MAB/3`, `MMP/1`, `MTG/1`, `MMG/1`,
`MDS/1` and typed map value codecs; shared-log integration and host storage
acceptance remain open. Relocated-package probes cover round-trip and refusal
paths for the source and terminal-gap records. The original alpha.3 design targeted
`ffe9875ee35a525f422eff3c1aaee2841f55d962`. The current alpha.4 candidate is
`1399e9533c9cfc1006854487af4e71b8b79989fd`; its Core gap API and durable CTLO
reader supersede the original upstream limitations. The
[provider contract](2026-10-12-document-storage-provider.md) incorporates
Corbomite's host response and governs the proposed storage interface. Neither
document changes the current Markoff save format or dependency pin.

## Ownership

Markoff continues to own Markdown interpretation, exact byte serialization,
action grouping and stream routing. The host owns the durable shared operation
log/transaction and supplies its append/recovery interface. CollabText supplies
checked CTBO/CTLO operation codecs and in-memory scoped engines; it is not a
canonical event store. Each device owns one private `ReplicaState` outside
shared document storage. The shared document key, stream naming and block ID
allocation still require M1 host decisions.

`serializeForSave()` remains an export of canonical Markdown bytes. It is not a
replica snapshot: reconstructing a populated engine from that text loses
operation identity, causal context, tokens and undo history. Existing files
import as a one-time initial seed under an explicit import event. No legacy
operation or raw-anchor compatibility is implied.

The isolated `ScopedTextStream` bootstrap API implements the per-stream part
of this rule. `seedInitialImport()` accepts only an untouched fresh adapter,
returns one insertion operation for nonempty bytes, and enters a pending state
(with no operation for empty bytes). While pending, local edits, undo/redo,
remote apply and local-history restore are refused. After the host durably
appends the enclosing `initial_import` MAB, `commitInitialImport()` clears the
seed from actor-local undo history. If the append is failed or uncertain, the
host discards this projection and rebuilds from its accepted log prefix; it
must not keep editing or invent a replacement operation against this instance.
This API does not coordinate seeds across streams or make the host append
atomic, and it has compile-only evidence so far.

## Shared log record

The host log stores an append-only, versioned `MarkoffActionBundle` (wire
format `MAB`, version 1). A bundle is the durable unit for a Markoff action,
including cross-block actions and undo/redo compensation. The host's monotonically
assigned delivery sequence is independent from operation identity and action
identity.

Each bundle contains:

```text
format:                 "markoff-action-bundle"
version:                1
document_key:           stable opaque host DocumentId
epoch:                  unsigned 64-bit, initially 1
delivery_sequence:      host-assigned unsigned 64-bit
action_id:              stable opaque action ID
action_kind:            edit | undo | redo | initial_import
operation_count:        bounded unsigned count
operations[]:
  stream_key:            canonical scoped stream name
  primitive:             text | id_list | markoff_map
  target_block_identity: present when the primitive targets one block; exact
                         IdList insertion identity (16-byte actor + u64 counter)
  producer_actor:        exact 16-byte ActorId
  operation_counter:     checked unsigned 64-bit
  codec:                 CTBO/0002 | CTBO/0003 | CTLO/0002 | Markoff-owned version
  payload:               exact opaque operation bytes
```

Binary payloads are byte strings in the canonical store or base64url in a JSON
store; they are never decoded as UTF-8. `producer_actor` and
`operation_counter` are redundant route/index fields, validated against the
checked decoded operation identity. The operation bytes remain authoritative and
unchanged. The envelope is required because operations do not encode Markoff's
document, stream, epoch or block route. The action record is atomic at the host
store boundary, or the host must expose a recoverable transaction/outbox whose
recovery produces the same all-or-none accepted prefix.

The host log must enforce operation-identity uniqueness across the entire
document/epoch/stream history, not only within one MAB bundle. Before routing a
bundle, compare every `(stream_key, producer_actor, operation_counter)` against
the durable identity index. A previously recorded identity is idempotent only
when its original full MAB envelope and operation bytes match; a different
payload or action association is an identity conflict and must be quarantined.
CollabText's `duplicate` outcome means only that the causal identity was already
observed; it does not prove that newly supplied bytes match the operation that
originally consumed that identity. The in-memory router's conflicting-ID check
covers only envelopes currently held in its queue, so it cannot replace the
durable host index. Build the index transactionally with MAB append and rebuild
it from the accepted log during recovery.

Reject an unknown envelope version, unsupported codec, invalid route binding,
wrong document/stream/epoch, malformed bytes, and resource-limit overflow
without acknowledging it as applied. Preserve the exact record and diagnostic
reason for quarantine/retry. A deferred apply keeps the record pending and
does not advance the durable applied receipt. A receipt advances only after the
host can recover the durable bundle and the Markoff projection has accepted all
operations in it (applied or duplicate). Failed or uncertain appends are
resolved by inspecting/replaying the original host transaction, never by
creating replacement identities.

The current `ActionBundleCodec` writes a fixed big-endian binary layout. It
limits a bundle to 16 MiB, 65,536 operations, 8 MiB per opaque payload, 256-byte
UTF-8 document/stream names and 256-byte action IDs. Empty operation lists are
accepted only for `initial_import`. Primitive/codec pairs are checked, and
text operation routes require an exact composite block identity; list routes
do not carry one. Decode requires the expected document and epoch and rejects
truncation, trailing bytes, unsupported versions and invalid actor/counter
metadata. It also rejects duplicate `(stream, producer actor, counter)` entries
within one bundle. Both `encode()` and `decode()` invoke payload identity
validation: CTBO/CTLO use CollabText's public codecs, and map records use MMP/1.
Each decoded identity must match the producer/counter fields; map payload names
must also match their canonical envelope stream. This codec is not a host log,
atomic append, durable receipt or quarantine store.

Only MAB/3 is supported. Versions 1 and 2 are rejected as unsupported; no
reader or writer compatibility path is provided. All integers use network byte
order. Strings are raw UTF-8 with a `u16` byte length. The magic is three ASCII
bytes `MAB` followed by version byte `0x03`.

| Field | Encoding |
| --- | --- |
| document key | `u16` length + 1–256 UTF-8 bytes |
| epoch, delivery sequence | two `u64` values; both nonzero |
| action ID | `u16` length + 1–256 opaque bytes |
| action kind | `u8`: edit 1, undo 2, redo 3, initial import 4 |
| operation count | `u32`, at most 65,536 |
| operation stream | `u16` length + 1–256 UTF-8 bytes |
| primitive | `u8`: text 1, IdList 2, Markoff map 3 |
| target block | `u8` presence (0 or 1); when present, 16 actor bytes + `u64` counter |
| producer | 16 actor bytes + nonzero `u64` operation counter |
| payload codec | `u8`: CTBO/0003 2, CTLO/0002 3, Markoff-owned 4 |
| payload | `u32` length + 1–8 MiB opaque bytes |
| source snapshot | `u32` length + exact MDS/1 bytes for initial import; zero for other actions |

Text operations require a target BlockId; IdList operations forbid one; map
operations may name the affected block. CTBO payloads are text primitives,
CTLO payloads are IdList primitives, and Markoff-owned payloads are map
primitives. The text stream must canonically encode the same BlockId as the
target field; IdList operations must use `markoff/order/v1`; map stream names
must be `markoff/map/v1/<map-name>`. Map names are 1–128 lowercase ASCII
letters, digits, `-`, `_` or `.`, starting with a letter. Operation array
order is the action order and its zero-based index
is the bundle index; all entries share the enclosing action ID and delivery
sequence. The source-snapshot field is mandatory in every MAB/3 record. It is
nonempty only for `initial_import`, so source bytes and generated operations
share the same atomic accepted record. An ordinary edit, undo or redo carries a
zero source-snapshot length.
MDS/1 contains scoped document/epoch, exact original UTF-8 Markdown bytes, ordered
separator slices `(u64 offset, u32 length, exact bytes)`, parser version, a
32-byte SHA-256 of the source when a parser cache exists, and up to 4 MiB of
opaque parser-derived cache. A present cache carries both a 32-byte SHA-256 of
the original source and a 32-byte SHA-256 of the cache payload. Source is limited
to 8 MiB, cache to 4 MiB, at most 65,536 separator slices, and the whole record
to 16 MiB. Separator slices must be ordered, nonoverlapping exact substrings of
the original; parser cache is disposable and must never replace exact source
bytes as export authority.

Markoff-owned sibling-map operations use the separate `MMP/1` payload codec.
Its fixed big-endian layout is magic `MMP` plus version `0x01`, map-name length
(`u16`) and 1–128 canonical lowercase ASCII map-name bytes, key length (`u32`)
and up to 1,024 opaque key bytes, tombstone flag (`u8`), value length (`u32`)
and up to 1 MiB
opaque value bytes, producer ActorId (16 bytes), and nonzero operation counter
(`u64`). A tombstone must carry an empty value; an ordinary set may carry an
empty value. Map/key/value semantics remain Markoff-owned. The MAB target block
route applies to that map operation; map payloads do not repeat it.

The typed value codecs cover Markoff's current block-kind, block-attribute and
link-reference values inside MMP's opaque value field. `MBK/1` is `MBK`, version byte `0x01`,
and one `BlockKind` enum byte. `MAV/1` is `MAV`, version byte `0x01`, followed
by a value tag: `1` plus a signed 32-bit integer encoded as big-endian two's
complement; `2` plus a `u32` UTF-8 byte length and string; or `3` plus a single
boolean byte (`0` or `1`). Attribute strings are limited to 1 MiB minus the
codec header and length field. Both codecs reject unsupported versions, invalid
enum/tag values, malformed UTF-8 and trailing or truncated bytes. These codecs
do not yet translate old 16-bit `CausalStamp`s for every map family; map
ordering and action/undo integration remain separate migration work. MAB
identity validation binds `kind-tag` operations to a target block and empty MMP
key, and checks live values with MBK/1. It binds `block-attrs` operations to a
target block and nonempty UTF-8 attribute key, and checks live values with
MAV/1. Tombstones must use empty MMP values as usual. `frontmatter` and
`footnote-def` operations are document-scoped; their `QByteArray` keys and
values remain raw opaque bytes so source representation is retained.

`MLR/1` stores a `LinkRefValue` as `MLR`, version byte `0x01`, then URL UTF-8
byte length (`u32`) and bytes, followed by title UTF-8 byte length (`u32`) and
bytes. The combined string bytes are limited to 1 MiB minus the 12-byte codec
header/length overhead; malformed UTF-16 input, malformed UTF-8, overlong
fields and trailing bytes are rejected. MAB requires link-reference map
operations to be document-scoped and checks each live value with MLR/1. These
bindings specify payload shape. The Markoff-owned stream slice below supplies
the vNext map ordering and projection behavior.

`ReplicatedMapStream` is the first Markoff-owned LWW map engine slice. Each
named map uses its canonical `markoff/map/v1/<name>` stream, a host-private
writer lease and durable per-stream `ReplicaState` counter reservations. Local
set/remove is refused until the caller explicitly completes the replay barrier;
each write returns one encoded MMP/1 operation for the host's MAB append.
`toActionOperation()` carries its canonical map stream, optional block target,
producer/counter and exact MMP bytes into the MAB operation fields.
The shared route/value-binding validator runs before counter reservation on local
writes, so a missing block target, malformed typed value or invalid attribute
key cannot consume an operation identity and create an avoidable counter gap.
Remote replay checks the map name, observes its counter, and applies the greatest
`(counter, 16-byte actor value in lexicographic order)` stamp per key; tombstones
remain in the hash-indexed projection. A projection key is the pair of optional
MAB target block identity and opaque MMP key; this keeps equal attribute names
on different blocks, and empty-key `kind-tag` entries, independent. Enumeration
order is unspecified, so callers must sort keys before producing canonical
Markdown. A stale operation
is accepted without changing that key. The durable host identity index remains responsible for conflicting reuse
of an actor/counter after its original key has been superseded. This adapter is
not yet wired into `MarkoffDocument`, Markdown serialization, or document undo;
an uncertain append must resolve against its exact MAB action. A definitive
append failure consumes a reserved counter and must block this actor's writes
to the map stream, including after restart, until a durable Core gap path is
available. Rebuilding the projection alone does not make that actor safe to
continue.

The serialized manifest carries no engine internals, C++ object layout,
allocator state or derived Markdown. Do not add physical pruning/checkpoints to
this first format; replay from the retained initial import and complete
operation prefix is the recovery path. Collection and epoch translation remain
disabled.

## Device-private persistence

The host gives each device a private state directory, outside any shared vault
or synced document folder. CollabText `ReplicaState` persists the device ActorId,
counter reservations and writer leases there. Reopen the same private state to
continue the same writer. If private state is copied to create an independent
device, explicitly fork it to a fresh actor before acquiring writers. A shared
Markdown/document copy never copies this actor. State corruption, actor
conflict, failed reservation or writer conflict is a hard error; never silently
mint replacement state.

### Reserved-counter gaps — Core API available; host recovery integration open

`ReplicaState::reserve_run()` durably consumes counters before an operation is
returned. If a generated operation is definitively absent from the shared log,
the next operation on that actor/stream can leave peers waiting on the missing
identity. Alpha.4 now exposes scoped text/list `apply_counter_gap()` for
host-committed canonical CTGP/0001 bytes and retains their durable reader.
Decoding still does not authorize absence or certify publication. MAB/3 carries
only document operations. Markoff's `MTG/1` record binds a canonical CTGP/0001
interval to document, stream, primitive, epoch and the accepted-prefix revision
against which absence was established. The adapter checks that scope and token
before applying the embedded Core payload. Provider-backed absence proof,
durable publication and restart recovery remain A06 work.

Do not treat a durable reservation, a later operation, or an allocator high
water mark as proof that peers can skip the hole. Before enabling local writers,
Markoff needs a durable, idempotent gap event bound to document,
stream, epoch and actor using the provided Core apply path. Recovery must
distinguish an uncertain operation append (inspect/replay that exact bundle)
from a definitively failed append (publish the reserved interval as terminal
gap metadata before continuing the stream). Acceptance must cover a peer that
misses the original failed write, process restart, and retries with conflicting
gap bytes. Until then, a definitive append failure requires stopping writes on
that stream; discarding the projection alone does not fill the causal gap.

Markoff-owned map streams do not use a Core gap primitive. `MMG/1` binds the
full actor interval to document, map name, epoch and accepted-prefix revision.
Replay must retain this terminal coverage, refuse later operations inside the
interval, and advance durable counter observation before allowing that actor's
next map write. Its wire codec exists; this replay/application enforcement
remains open.

Actor-local undo snapshots use a separate Markoff-owned binary versioned record
(`MAH`, version 1), keyed by document, epoch, actor and stream. The
`LocalHistoryCodec` emits the bytes `MAH` followed by version byte `0x01`, then
big-endian fixed-width integers and length-prefixed UTF-8 scope strings. It
serializes `LocalHistorySnapshot` fields: cursor, ordered active entries,
pending retired entries, each group identity, operation IDs, inserted item
identities, deletion operation identities, and retained-byte charge. Each
identity is actor bytes plus checked 64-bit counter. Validate array sizes,
integer bounds, snapshot budget and actor/stream ownership before restore. Do
not serialize the C++ structs by memory layout. The codec rejects records over
8 MiB, scope strings over 256 bytes, more than 100,000 combined history groups,
more than 400,000 identities, unsupported versions, truncated records, and
trailing bytes. Retired groups are not acknowledged until a durable host event
records their retirement handling.

Replay all accepted shared bundles into fresh engines first. Then restore the
matching actor's private `MAH` snapshots, followed by Markoff's cross-stream
action-to-engine-group index. Only then enable local undo/redo. Remote replay
never populates local history. A corrupt or stale history snapshot disables
local history restoration with a diagnosable error; it does not alter shared
document state or discard the raw snapshot.

The Markoff-owned relocated-package probe round-trips a live text-engine history
snapshot and compares every serialized field. It also round-trips a synthetic
retired entry and rejects unsupported-version, truncated, wrong-stream and
wrong-actor records. The probe reopens the same private `ReplicaState`, replays
shared operations, restores the snapshot, and applies its resulting undo and
redo compensations on a peer. This proves the codec and engine restore seam;
host file/DB write atomicity remains unverified.

The action-to-engine-group index and consistent cursor/compensation commit
policy are M8. `MAH/1` does not claim multi-stream undo atomicity is solved.

`MAI/1` is the original owner-private cross-stream index stored beside the per-stream
MAH snapshots. It binds one document/epoch/owner and the accepted-prefix token
against which the proposed action was prepared to ordered action events. Each
event records an action ID and kind, its undo/redo parent when applicable, and
each affected stream's primitive, engine group ID,
and operation identities. Markoff map operations use their operation identity
as the group identity. Retirement associations name the group, stream, primitive
and accepted action that records the retirement. The reader rejects duplicate
actions, duplicate per-action group or operation claims, unknown parents,
orphan retirements, foreign actors/routes, unsupported versions, truncation and
trailing bytes. `MAI/1` and `MAI/2` are historical and are rejected by the
current reader. The only accepted format is `MAI/3`, which adds
owner-private prior map state to each map operation's singleton history group:
whether the key previously existed and, when it did, its exact prior value. The
group and operation identity must match. This lets a reopened owner emit a fresh
replicated inverse instead of restoring the projection locally. MAI/3 also stores
the exact pre-replacement MDS source snapshot in replacement history so owner
undo can restore the prior export bytes after restart. MAI/3 is bounded to
8 MiB, 100,000 actions, 200,000 group references and 400,000 combined
identities/retirements; each `MAI/3` map prior value is limited to 1 MiB and a
replacement source snapshot is limited to 16 MiB. New map actions require this
prior-state record. Capture and restoration from live
actions remain coordinator work.

`MOH/1` composes those per-stream MAH snapshots with the MAI index into one
bounded owner-history value. It binds the prepared transaction ID and expected
accepted-prior token, and requires the MAI event list to end at that transaction.
After shared commit, `PrivateHistoryRecord` separately binds these exact bytes to
the provider's returned committed revision. This avoids predicting opaque
revision tokens during preparation. The composite codec is implemented;
capturing it from production actions and restoring it remain coordinator work.

## Markoff action grouping and selective undo (M8 gate)

Markoff's `UndoLog::undoForBlock` selects the newest document action whose
targets include the requested block, then dispatches that action's full target
list in reverse order. The current `MarkoffDocument` dispatcher invokes
stack-based `undo()`/`redo()` on Buffer, IdList and sibling-map engines and
discards the logged `opId`. The selected Markoff action therefore is not
necessarily the group those shared engines undo when later actions on other
blocks touched them in the meantime.

The alpha.4 API provides stack `try_undo()`/`try_redo()`, group inspection and
snapshots, but no selective historical-group compensation. The pinned alpha.5
Core candidate adds checked `try_undo_group(group_id)` and
`try_redo_group(group_id)` for text and IdList. The operation refuses unknown,
retired or wrong-state groups without reserving an identity. It moves the exact
group across the existing snapshot cursor while preserving normal undo/redo
ordering, including after replay and restore; new edits still truncate the redo
suffix. Regression tests cover an older group selected under a newer active
group and verify operation replay plus history restore.

Those Core semantics are now available for shared structural order and block
text streams. Markoff's selected per-block action must map to the full
`(canonical stream, primitive, group_id)` tuple for each touched stream. The
consumer trace now demonstrates selective A/B behavior across the shared order
stream and separate text streams. It is still an adapter regression: the durable
action-to-group index, atomic multi-stream publication, Markoff-owned map
compensation, and allocation-failure rebuild are not implemented. Preserve
existing per-block behavior; do not silently replace it with document-wide LIFO.

For the first path, Markoff's durable action index maps each action ID to the
exact engine group identity for every touched stream. Document undo uses the
global action stack; per-block undo selects the latest action containing that
block and requests exactly its mapped group on each stream. Markoff assembles
the returned compensations into one durable action bundle. If a later stream
cannot allocate a compensation after earlier engines mutated, discard and
rebuild the disposable projection from the unchanged durable prefix, restore
private history, then resume. Do not publish a partial multi-stream undo.

Acceptance requires an interleaving trace where action A affects block A and a
shared stream, action B later affects block B and the same stream, and
`undoForBlock(A)` leaves B's work visible while publishing only A's inverse;
then verify redo, restart-restored action mapping, and failure recovery. Global
LIFO undo/redo and per-block selective undo are separate cases.

Regression trace: start with blocks A and B and a shared order/map state. Commit
action A that edits A's text and changes a shared order/map group. Commit action
B that edits B's text and later changes that same shared group. Selectively undo
A. The accepted compensation bundle must name only A's recorded groups; B's text,
position and shared-map effect must remain. Ordinary undo then targets the newest
still-active user action; redo of A restores A without hiding or reordering B.
Close/reopen and repeat using persisted group-to-action mappings. Inject an
allocator refusal on the second affected stream and require the Markoff projection
to rebuild from the unchanged accepted prefix before any partial compensation is
published. Until CollabText exposes and tests this selective-group primitive,
this trace is a required regression, not a behavior Markoff can claim to pass.

## Reader and upgrade policy

This is a clean-start alpha format. Its outer envelope and Markoff-owned
metadata are versioned independently from CollabText operation codecs. Retain
the exact candidate SHA in each installed consumer lock. The candidate may
expose multiple Core operation codecs; Markoff selects one version for each
operation family and validates that choice in its consumer tests. The selected
candidate is the dependency contract for this pre-release. Markoff refuses
unsupported versions and does not preserve them as opaque records for later
rewriting or replay. Structural operation storage remains gated until the
chosen CTLO reader is verified against the pinned candidate.

Adoption acceptance must include literal operation/token fixtures, a fresh
engine replay from the complete log, restart restoration of local undo metadata,
and injected truncated/corrupt/unsupported records. The host's durable append,
recovery and applied-receipt behavior must be verified in the actual consumer;
the installed Core probe cannot establish those host guarantees.

## Open decisions

- M1: stable host `DocumentId` and copy policy. Recommended BlockId is the
  composite IdList insertion identity; current installed API needs a public
  visible-entry-identity query because `ids()` returns duplicate-capable
  application values. The host-owned stream grammar is recorded in the
  adoption plan.
- M8: all-or-none multi-stream undo/redo and commit-uncertain recovery, given
  engine mutation precedes returned operation publication.
- Host contract: atomic action append or a proven recoverable outbox, and when
  an inbound deferred operation becomes eligible for a durable receipt.
- CTLO/0002 reader lifetime before structural operations are committed to an
  archive.
