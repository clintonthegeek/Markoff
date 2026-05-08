# D5 — Collab Activation Design

**Date:** 2026-05-07 (boundary design); revised 2026-05-08 (collabtext response received).
**Status:** spec-approved.
**Branch:** `exploration/new-foundation`
**Supersedes:** `docs/archive/2026-05-04-d5-collab-activation-STUB.md`
**Companion:** `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md` (Markoff-side opener).
**Companion (response):** `~/dev/collabtext/docs/specs/2026-05-08-d5-negotiation-response.md` — collabtext maintainers commit to Alternative A. See §0.1.
**Authoritative parent:** `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.3.
**Audience:** Markoff agents implementing D5 under pivot-doc §4.4.

---

## 0. TL;DR

D5 is the **boundary spec** for Markoff's collab surface. It defines the API a consumer uses to wire `MarkoffDocument` into a collaborative editing setup. It does not define a transport, a user model, a presence policy, an auth layer, or a Corbomite-specific design.

Three layers:

- **Markoff** owns the editor and its CRDT-backed document model.
- **Collabtext** owns the CRDT primitives Markoff uses internally.
- **Consumer** (Corbomite or another host) owns transport, identity, multi-user logistics, side channels, and the orchestration glue between the two widget libraries.

At runtime, Markoff and collabtext **never call each other directly**. All op routing flows through the consumer. The static dependency (Markoff `#include`s collabtext, links its CRDT primitives) stays.

**Single-user is the structural default.** A `MarkoffDocument` constructed without an explicit replica ID, with no consumer attached, runs as a fully functional offline editor. The boundary methods exist on the document but are inert.

D5 implementation produces:

1. The boundary API on `MarkoffDocument` (signals + methods + types).
2. A reference test harness in `apps/markoff-collab-testapp/` that wires two `MarkoffDocument`s in-process for two-replica dogfood and convergence testing.
3. A spec-internal "consumer wiring sketch" (§4) that grounds the API; not a full integration guide. A real integration guide gets written when there's a real consumer.

This document is paired with a **collabtext negotiation opener** addressed to the collabtext maintainers, asking for a refactor that exposes a transport-agnostic boundary on collabtext's side. The negotiation is independent of D5 implementation — Markoff implements D5 against the current collabtext shape; if maintainers refactor during the implementation window, the consumer-side wiring simplifies. The boundary API itself is shape-independent.

---

## 0.1 Negotiation outcome (revised 2026-05-08)

The collabtext maintainers responded with a commitment to **Alternative A** of the negotiation opener. The substantive update:

- **`OpStream` becomes the canonical wiring surface** once collabtext's refactor lands (~8–10 weeks from 2026-05-08; sequenced after IdList β). The "shape-independent" framing in the body of this spec was a hedge against the refactor not happening; with Alternative A committed, that hedge retires.
- **`include/collabtext/OpStream.h`** will define the boundary, with method signatures matching this spec's §4.1 `ITransport` shape verbatim. `StreamSync` adopts `OpStream` (renamed declined; the name stays).
- **`include/collabtext/Serialization.h`** becomes a public include path; `encode_operation` / `decode_operation` / `encode_idlist_operation` / `decode_idlist_operation` are public functions consumers (and the Markoff D5 implementation) call directly. Field-level ABI evolves with `schema_version` bumps; round-trip via encode/decode is the consumer contract.
- **Per-peer ack-frontier publication** is included: `lowest_peer_acked_lamport()` + advance callback, exactly the shape this spec assumes.
- **Markoff implementation can begin against partial delivery.** Items 1–2 of the response (public op API + public serialisation) land in ~2 weeks; items 3–6 (full `OpStream` + `StreamSync` adoption + ack-frontier publication + tests) land at ~8–10 weeks. D5 implementation pauses until items 1–2 are in tree, then proceeds; the wiring sketch (§4) updates to reference `OpStream` as the canonical surface once the full refactor lands. See `docs/plans/2026-05-08-d5-collab-activation.md` Phase 0 for sequencing.
- **Joint-design pass scheduled** on the public `OpStream` header and the per-peer ack-frontier file format. Markoff reviews drafts before they set; "a week of back-and-forth on the headers, no more" per the response.
- **Markoff testapp adopted as the realistic demo consumer** for collabtext (alongside or in place of `CollabPlainTextEdit`). Coordination on timing follows.

The maintainers explicitly preserved compatibility: *"if D5 ships against current shape and we land the refactor afterward, the consumer wiring simplifies but Markoff itself doesn't change."* With this spec's three-layer model, that property holds — the boundary on `MarkoffDocument` is independent of the collabtext shape; only the consumer-side wiring (the integration sketch in §4) updates.

The 2026-05-04 six-item D-evolution scope-line is unaffected. This refactor reshapes the boundary of what already exists; it does not add CRDT primitives, generalise `CollabDocument`, or add cross-CRDT coordination.

---

## 1. Frame and three-layer split

### 1.1 What D5 is

A boundary spec. The signals, methods, types, and lifecycle rules a consumer uses to attach a `MarkoffDocument` to a collaborative-editing setup. The contract Markoff commits to as a widget.

### 1.2 What D5 is not

- A transport. Markoff doesn't talk to peers; the consumer does.
- A user model. "Replica ID" is a `quint16` to Markoff; what it represents — device, user, session — is consumer policy.
- A presence policy. Who shows, what colour, what label, when to clear — all consumer-decided.
- An auth layer.
- Server infrastructure. Markoff's collab story is peer-to-peer through the consumer's transport.
- A Corbomite-specific design. The consumer is *some* host; Corbomite is the eventual target but isn't privileged in the spec.
- A wire-protocol standard for cross-implementation interop.

### 1.3 The three layers

| Layer | Owns | Doesn't own |
|---|---|---|
| **Markoff (widget)** | Editor surface, document model (per-block CRDTs + sibling maps + UndoLog), local op production, remote op application, remote-cursor rendering, cursor-survival under concurrent edits. | Transport, identity policy, peer set, presence policy, side channels, multi-user logistics, GC scheduling. |
| **Collabtext (widget)** | CRDT primitives (`Buffer`, `IdList`, `Anchor`, `Lamport`), op types, op serialisation, convergence guarantees. | Mandating any specific transport. Mandating any specific identity model. Knowing about Markoff at all. |
| **Consumer (host)** | Transport choice + wiring. Identity, replica enrolment/eviction, peer set. Presence policy. Side channels (chat, comments). Authn/authz. Watermark ack policy across the peer set. GC scheduling. The orchestration glue. | Editor internals. CRDT internals. |

### 1.4 Static vs dynamic

Markoff `#include`s collabtext at build time and uses its CRDT primitives internally — this dependency stays. At runtime, Markoff and collabtext **never call each other directly**. All op routing flows through the consumer.

The rule of thumb: any code that knows about both Markoff *and* collabtext at runtime is consumer code.

This is what makes the boundary genuinely separable: a consumer can wire the two widgets together however it wants without either widget needing to know.

### 1.5 Single-user is the default

A `MarkoffDocument` constructed with no replica ID, with no consumer attached, runs as a fully functional offline editor. The boundary methods exist on the document but are inert. A consumer that doesn't care about collab can ignore the boundary entirely. A consumer that cares partially (e.g. wants ops logged to disk but not synced to peers) wires only what it needs.

D2's per-block CRDT shape (which the document was built on) means single-user mode is structurally trivial: the same code path that produces ops in collab mode produces ops in single-user mode; the difference is whether anything listens to `localOpsProduced` and whether `applyRemoteOps` is ever called.

---

## 2. The `MarkoffDocument` boundary API

### 2.1 Construction and replica ID

```cpp
namespace Markoff {

class MarkoffDocument : public QObject {
    Q_OBJECT
public:
    /// Construct with an explicit replica ID (collab mode).
    /// The ID is immutable for the document's lifetime.
    /// Reload/reopen to change replica identity.
    explicit MarkoffDocument(quint16 replicaId, QObject* parent = nullptr);

    /// Construct without a replica ID (single-user mode).
    /// Internally uses a sentinel ID (0x0001). Boundary methods exist
    /// but the document is dormant — applyRemoteOps is a no-op
    /// contract violation if called.
    explicit MarkoffDocument(QObject* parent = nullptr);

    quint16 replicaId() const noexcept;
    bool    isCollabConfigured() const noexcept;  // explicit replicaId at construction

    // ... existing D2/D3/D4 surface
};

}
```

**Why immutable.** Every Lamport timestamp in the document carries the producing replica's ID. A mid-session change would orphan all prior timestamps relative to the new identity. Reload to change identity is the simple, correct rule.

**Why a sentinel default.** Lets the same code path serve both modes. CRDTs work identically; only collab activity is gated by `isCollabConfigured()`.

### 2.2 Outbound signals

```cpp
signals:
    /// Emitted after a local user-action commits a transaction, with all
    /// ops produced and the bundle metadata. One emission per
    /// transaction, regardless of how many CRDTs were touched.
    ///
    /// Consumer is responsible for routing these ops to peers and
    /// (typically) for persisting them to its transport's log.
    /// Single-user mode: emissions still happen but no consumer is
    /// expected to listen.
    ///
    /// Connection type recommendation: Qt::DirectConnection (consumer
    /// is in same thread). Qt::QueuedConnection acceptable if the
    /// consumer marshals to another thread.
    void localOpsProduced(QList<Markoff::MarkoffOp> ops,
                          Markoff::MarkoffBundleMeta meta);

    /// Emitted when the document's local watermark advances past a
    /// previous snapshot point. Consumer uses this to notify peers
    /// "I've persisted up to N" so they can update their ack state.
    void localWatermarkAdvanced(quint64 watermark);

    /// Emitted when the document wants to compact CRDT history but
    /// is gated on peer acks. Consumer answers by calling
    /// notifyAcksAtWatermark(W) once peers confirm. If no consumer
    /// answers, document never compacts.
    void wantsAcksAtWatermark(quint64 snapshotWatermark);
```

### 2.3 Outbound data types

```cpp
namespace Markoff {

/// What CRDT a MarkoffOp targets. The consumer routes by this tag.
enum class CrdtTarget : quint8 {
    IdList            = 0,   // structural: block insertions/removals
    Buffer            = 1,   // per-block text edits; needs blockId
    KindTagMap        = 2,   // sibling map: per-block kind (LWW)
    BlockAttrsMap     = 3,   // sibling map: per-block attrs (LWW)
    FrontmatterMap    = 4,   // sibling map: YAML frontmatter (LWW)
    LinkRefMap        = 5,   // sibling map: link references (LWW)
    FootnoteDefMap    = 6,   // sibling map: footnote definitions (LWW)
};

/// A single op crossing the boundary. Opaque payload — consumer never
/// inspects bytes; only Markoff (and collabtext, for CRDT-backed
/// targets) can deserialize.
struct MarkoffOp {
    CrdtTarget          target;
    quint64             blockId;     // valid iff target==Buffer; 0 otherwise
    QByteArray          payload;     // serialized form; format depends on target
    quint16             producerReplicaId;  // who produced this op originally
};

/// Metadata identifying the user-action this set of ops belongs to.
/// Travels with every emission; receiving replicas group ops by
/// (producerReplicaId, bundleId) for cross-replica undo coordination.
struct MarkoffBundleMeta {
    quint16             producerReplicaId;
    quint64             bundleId;          // monotonic per producer; identifies the user-action
    quint16             opCountInBundle;   // how many ops in this bundle (receiver knows when complete)
    Markoff::ActionId   actionId;          // semantic kind of action — useful for UX ("Alice typed", "Alice deleted a block")
    quint64             producerLamport;   // for ordering bundles in replay/audit logs
};

}

Q_DECLARE_METATYPE(Markoff::MarkoffOp)
Q_DECLARE_METATYPE(Markoff::MarkoffBundleMeta)
Q_DECLARE_METATYPE(QList<Markoff::MarkoffOp>)
```

**Wire format.** `payload` is canonical-Markoff bytes. For `Buffer`/`IdList` targets, the payload wraps the collabtext-serialized op (collabtext's existing format, used as-is). For sibling-map targets, the payload is Markoff-defined (see §3.1). The consumer treats all payloads as opaque and routes by `target` + `blockId`. Markoff ships a canonical (de)serializer (`MarkoffSerializer::encode/decode`) so consumer-to-consumer interop works.

**Why one envelope across CRDT-backed and Markoff-defined ops.** Receivers route uniformly by `target`; the application path inside `applyRemoteOps` dispatches by `target` to the right CRDT or sibling map. One wire-format type, one consumer code path.

**Bundle invariant (D5).** Within a single bundle, every `MarkoffOp::producerReplicaId` MUST equal the bundle's `MarkoffBundleMeta::producerReplicaId`. Multi-producer bundles are out of scope; each user-action originates on exactly one replica. The redundancy lets receivers process individual ops outside bundle context (e.g., for logging or per-op audit) without having to unpack metadata. Receivers MAY validate the invariant defensively on `applyRemoteOps` and treat a mismatch as a malformed-bundle error (skip the bundle, warn the consumer).

### 2.4 Inbound methods

```cpp
public:
    /// Apply remote ops in arrival order. The consumer must preserve
    /// per-stream order for Buffer (per blockId) and IdList streams;
    /// sibling-map streams are order-independent (LWW resolution by
    /// Lamport, see §3.2). Bundle reassembly happens internally —
    /// the document accumulates ops by (producerReplicaId, bundleId)
    /// for cross-replica undo bookkeeping.
    ///
    /// Idempotency: re-delivery of an already-applied op is silently
    /// ignored (CRDT op identity guarantees this; the document does
    /// not have to track separately).
    ///
    /// Origin is set to Origin::Remote internally; remote ops do not
    /// enter the local UndoLog (per D2 §4.7).
    void applyRemoteOps(QList<Markoff::MarkoffOp> ops,
                        Markoff::MarkoffBundleMeta meta);

    /// Tell the document that all known peers have ack'd the local
    /// watermark up to W. Allows GC compaction up to W. If never
    /// called, the document never compacts.
    /// Monotonic: calls with W' < currentAckedWatermark are ignored.
    void notifyAcksAtWatermark(quint64 watermark);
```

### 2.5 Presence

```cpp
public:
    /// Set or update the rendered cursor for a remote replica.
    /// `cursor` uses the same Shape-1 discriminated form as local
    /// cursors (TextCaret | BlockSelected | BlockInternalEdit) per
    /// the C-spec §3.5 carry-forward (preserved through D2 §4.7).
    /// `color` and `label` are consumer-chosen; Markoff just renders
    /// what it's told.
    /// Calling with a new cursor for the same replicaId replaces the
    /// previous (no flicker; Markoff animates the transition if the
    /// move is small).
    void setRemoteCursor(quint16 replicaId,
                         Markoff::Cursor cursor,
                         QColor color,
                         QString label);

    /// Stop rendering the named replica's cursor.
    void clearRemoteCursor(quint16 replicaId);

    /// Convenience: clear all remote cursors (used when consumer detaches).
    void clearAllRemoteCursors();
```

**Cursor survival under local edits.** Markoff runs the C-spec §3.5 cursor-survival logic on remote cursors automatically. If the local replica edits the document such that a remote cursor's anchor shifts, the remote cursor stays attached to the right textual position. The consumer doesn't have to re-push remote cursors on every keystroke.

**Connection-state cosmetic hooks (out of D5 scope).** A consumer may want to render "Alice is typing" or "Alice disconnected (last seen X ago)" indicators. Those are presence-policy concerns and live entirely consumer-side.

### 2.6 Lifecycle

**Single-user.** Construct with no replica ID. Edit normally. `localOpsProduced` still emits but if nothing connects, the emission is a no-op fan-out. Don't call `applyRemoteOps`. Either don't call `notifyAcksAtWatermark` (memory grows monotonically with edit history; acceptable for finite sessions) or call it once with `UINT64_MAX` to permit full compaction whenever the document wants.

**Collab from start.** Construct with replica ID. Connect to `localOpsProduced` and `wantsAcksAtWatermark` *before* the first edit (otherwise the first edit's ops are emitted into the void). Consumer can also bootstrap by calling `applyRemoteOps` with the catch-up log from peers before unblocking the user's first edit.

**Mid-session attach (consumer connects late).** The consumer must replay any local-op log it kept (or queried from the transport). Markoff does not keep an in-memory op-replay buffer — that's transport's job. Document state is the union of all ops applied so far; the consumer's job at attach is to bring the op stream forward from wherever it knows.

**Mid-session detach (consumer disconnects).** Harmless. Document continues editing locally, future `localOpsProduced` emissions are dropped on the floor. On re-attach, see "mid-session attach" above.

**`applyRemoteOps` while a local transaction is open.** Blocked until the local transaction commits, then applied. Markoff serializes via its existing transaction lock.

---

## 3. Sibling-map sync and watermark/ack

### 3.1 Sibling-map wire format

Markoff's five sibling maps (`KindTagMap`, `BlockAttrsMap`, `FrontmatterMap`, `LinkRefMap`, `FootnoteDefMap`) are causal-LWW maps owned application-side, not collabtext primitives. Their ops cross the boundary in `MarkoffOp`s with `target` set to one of `{KindTagMap, BlockAttrsMap, FrontmatterMap, LinkRefMap, FootnoteDefMap}`.

Common envelope for all five (encoded into `MarkoffOp::payload`):

```cpp
namespace Markoff {

struct SiblingMapOpHeader {
    QByteArray  key;             // map-specific encoding (see table)
    QByteArray  value;           // map-specific encoding; empty == tombstone
    quint64     lamportCounter;  // producer's Lamport counter at op time
    quint16     lamportReplicaId;// producer's replica ID
    bool        isTombstone;     // true == delete-key
};

}
```

Per-map key/value codecs:

| Map | Key encoding | Value encoding |
|---|---|---|
| `KindTagMap` | 8-byte little-endian `blockId` | 1-byte `BlockKind` enum; for `Heading`, additional 1-byte level |
| `BlockAttrsMap` | 8-byte `blockId` ‖ length-prefixed attr name | length-prefixed typed value (`int32` / `bool` / UTF-8 string / UTF-8 string list) with a 1-byte type tag |
| `FrontmatterMap` | length-prefixed YAML key path (`a.b.c`) | length-prefixed typed value (same tag set as `BlockAttrsMap`) |
| `LinkRefMap` | length-prefixed normalised link label | length-prefixed `(href, title)` pair |
| `FootnoteDefMap` | length-prefixed footnote label | length-prefixed footnote body (UTF-8 source text) |

**Why one envelope.** Receivers route by `target` to the right map but the op-application code (LWW: compare lamports, replace or ignore) is identical across the five.

### 3.2 LWW resolution and ordering invariants

**On receive,** for each sibling-map op:

1. Decode header → `(key, value, lamport, isTombstone)`.
2. Look up current entry for `key` in the target map.
3. If no entry, or `(lamportCounter, lamportReplicaId)` lex-order-greater than the current entry's lamport: replace (or, for tombstones, mark as deleted with that lamport).
4. Otherwise: ignore.

**Ordering invariant.** Sibling-map streams **do NOT require per-stream arrival order**. LWW resolution is defined by lamport, not arrival order; out-of-order arrival just means a higher-lamport op might apply first and a later-arriving lower-lamport op gets ignored. Convergence holds either way.

This **differs** from the per-stream-order invariant for `Buffer` and `IdList` streams:

| Target | Per-stream order required? |
|---|---|
| `Buffer` (per blockId) | **Yes** — collabtext's op-based CRDT requires causal order within a stream |
| `IdList` | **Yes** — same reason |
| Any sibling map | **No** — LWW resolution is order-independent |

Consumers routing via collabtext's `StreamSync`-shaped transport get per-stream order for free for `Buffer`/`IdList`. For sibling-map ops, consumers can use any stream layout — one stream per map, all sibling maps multiplexed together — without correctness consequences.

### 3.3 Edge cases for sibling-map ops

**Op for a deleted block.** A `KindTagMap` or `BlockAttrsMap` op arrives keyed to a `blockId` that's been removed from `IdList`. Apply the op anyway — the entry lives in the map without an associated block ("orphan entry"). On the next watermark crossing, orphan entries with lamport ≤ W can be compacted. Until then they're harmless: nothing reads them; the map is still convergent.

**Op for an unseen block.** A sibling-map op arrives keyed to a `blockId` we haven't yet seen via `IdList`. Apply the op to the sibling map regardless. When the corresponding `IdList::insertAfter` op arrives later (cross-stream order is unconstrained), the block materialises with its kind/attrs already set.

**Concurrent kind change for the same block.** Two replicas both call `Cmd::changeKind(block, X)` and `Cmd::changeKind(block, Y)` concurrently. Both ops carry independent lamports; the higher-lamport wins; the loser is silently overwritten. **No conflict UI surfaces** — LWW is the convergence semantics, not a "conflict" — but a brief visual flicker of "I changed kind to X, now it's Y" is possible. Documented as expected behaviour in §5.

### 3.4 Watermark / GC ack mechanics

D2 §7.4 specified that `WatermarkCoordinator`'s collab-evolution gate is "all known peers have ack'd up to the snapshot watermark." D5 wires that gate.

**Lifecycle:**

1. **User saves the document** (`MarkoffDocument::save()` succeeds).
2. **`WatermarkCoordinator` advances** local snapshot watermark to the current Lamport-counter ceiling, call it `W`.
3. **Document checks the gate.** Single-user (`notifyAcksAtWatermark` called with `UINT64_MAX` or any value ≥ `W`): compaction proceeds immediately. Collab: gate check.
4. **If gated, document emits `wantsAcksAtWatermark(W)`.** Consumer is now expected to confirm peer acks up to `W`.
5. **Consumer queries its peer set.** For each peer, "have you persisted/observed all my ops with lamport ≤ W?" Mechanism is consumer-owned. With collabtext's `StreamSync`, this maps to per-peer segment-file frontier tracking. With a network transport, explicit ack messages.
6. **Once all known peers ack**, consumer calls `notifyAcksAtWatermark(W)`.
7. **Document compacts** all CRDTs (Buffer/IdList ops below `W`, sibling-map tombstones with lamport ≤ `W`, orphan sibling-map entries for deleted blocks).

**`notifyAcksAtWatermark(W)` is monotonic.** Calls with `W' < currentAckedWatermark` are ignored.

**The document does not retry.** If `notifyAcksAtWatermark` is never called for a given `W`, the document just doesn't compact past `W`. Memory grows monotonically with edit history. The consumer is responsible for *eventually* answering — the document doesn't time out or warn.

**Re-emission policy.** `wantsAcksAtWatermark(W)` fires **once** when the watermark advances to `W`. It does NOT re-fire on every save if the gate hasn't opened yet — the consumer is expected to remember the outstanding watermark. The next emission is for `W' > W`, after a subsequent save advances the watermark.

### 3.5 Consumer responsibilities for ack tracking

The D5 spec doesn't dictate the consumer's ack-tracking implementation, but flags the design surface:

- **Peer set membership.** Who's in the document's collab session right now? Adds when a peer joins, removes when a peer is evicted. Consumer policy.
- **Per-peer watermark tracking.** What's the highest `W` each peer has confirmed seeing? Consumer-side bookkeeping; typically read off the transport's per-peer log frontier.
- **The "all ack'd" predicate.** When `wantsAcksAtWatermark(W)` fires, has every member of the peer set confirmed `W`? If yes, call `notifyAcksAtWatermark(W)`. If no, wait for more peer updates.
- **Silent-peer eviction policy.** A peer that goes offline indefinitely will block GC forever if kept in the peer set. Consumer decides: evict after N minutes? After explicit user action? Never (treat as data corruption)?
- **Disconnect/reconnect semantics.** A peer that disconnects then reconnects must be brought back up to date (consumer reads recent log entries, applies via `applyRemoteOps`). The watermark may need recomputing.

These are consumer-shaped, not Markoff-shaped. The wiring sketch (§4) gives a hand-wave reference for "here's roughly how a typical consumer would handle this."

### 3.6 Single-user mode is structurally trivial

Two patterns:

```cpp
MarkoffDocument doc;  // no replica ID, no consumer
// edit normally; localOpsProduced fires but no one listens.
// Document never gates GC because notifyAcksAtWatermark is never called
// → memory grows with history (acceptable for a session; load/save resets).
```

Or:

```cpp
MarkoffDocument doc;  // single-user
// At construction (or any time), open the gate forever:
doc.notifyAcksAtWatermark(std::numeric_limits<quint64>::max());
// Document now compacts on every save without further coordination.
```

Both work. The first is "simplest possible single-user use"; the second is "single-user with the full GC story."

---

## 4. Consumer wiring sketch

This section replaces a dedicated integration guide. It is **deliberately illustrative, not normative** — enough to ground the spec, not enough to over-specify the consumer's job. A real integration guide gets written when there's a real consumer with real requirements.

### 4.1 Minimum viable consumer

A consumer that wires a `MarkoffDocument` to a generic transport. Pseudocode; assumes a transport interface like:

```cpp
class ITransport {
public:
    /// Push a serialized op blob to peers under a logical stream name.
    virtual void push(QString streamName, QByteArray blob) = 0;

    /// Inbound callback: transport delivers a blob received from a peer.
    using OnInboundFn = std::function<void(QString streamName,
                                            QByteArray blob,
                                            quint16 producerReplica)>;
    virtual void setOnInbound(OnInboundFn fn) = 0;

    /// Inspect the peer-set ack state for GC. Returns the highest lamport
    /// counter that all currently-enrolled peers have confirmed seeing
    /// from the local replica.
    virtual quint64 lowestPeerAckedLamport() const = 0;
    virtual void onAckUpdate(std::function<void(quint64)> fn) = 0;
};
```

A concrete implementation backed by collabtext's `StreamSync` would satisfy this; a network-backed implementation likewise; an in-memory mock for tests likewise.

The consumer:

```cpp
class MarkoffCollabConsumer : public QObject {
public:
    MarkoffCollabConsumer(MarkoffDocument* doc, ITransport* transport)
        : m_doc(doc), m_transport(transport)
    {
        // Outbound: route local ops to transport.
        connect(m_doc, &MarkoffDocument::localOpsProduced,
                this,  &MarkoffCollabConsumer::onLocalOps);

        // Watermark/ack: forward gate queries to transport, route
        // transport's ack updates back into the document.
        connect(m_doc, &MarkoffDocument::wantsAcksAtWatermark,
                this,  &MarkoffCollabConsumer::onWantsAcks);
        m_transport->onAckUpdate([this](quint64 newAckedLamport) {
            m_doc->notifyAcksAtWatermark(newAckedLamport);
        });

        // Inbound: transport delivers, deserialize, apply.
        m_transport->setOnInbound([this](QString stream, QByteArray blob, quint16 producer) {
            this->onInboundBlob(stream, blob, producer);
        });

        // Presence: transport's presence updates → document.
        // (omitted; depends on transport's presence shape)
    }

private slots:
    void onLocalOps(QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
        QByteArray blob = MarkoffSerializer::encode(ops, meta);
        QString stream = streamNameForBundle(meta);
        m_transport->push(stream, blob);
    }

    void onWantsAcks(quint64 watermark) {
        const quint64 currentAck = m_transport->lowestPeerAckedLamport();
        if (currentAck >= watermark) {
            m_doc->notifyAcksAtWatermark(currentAck);
        }
        // Otherwise: wait for transport's onAckUpdate to fire.
    }

    void onInboundBlob(QString /*stream*/, QByteArray blob, quint16 /*producer*/) {
        QList<MarkoffOp> ops;
        MarkoffBundleMeta meta;
        if (!MarkoffSerializer::decode(blob, &ops, &meta)) {
            qWarning() << "Markoff bundle decode failed; dropping";
            return;
        }
        m_doc->applyRemoteOps(ops, meta);
    }

private:
    MarkoffDocument* m_doc;
    ITransport*      m_transport;
};
```

That's the entirety of the routing layer. Single-user mode just doesn't construct a `MarkoffCollabConsumer`; the document runs unchanged.

### 4.2 What the sketch deliberately omits

- **Identity policy.** Where does the replica ID come from? Per-device persisted setting? Per-user login? Vault config? Sketch assumes the `MarkoffDocument` was constructed with a replica ID already chosen.
- **Peer enrolment.** The sketch's `ITransport` already has a peer set; how peers join/leave is transport-internal.
- **Presence rendering wire-up.** The transport's presence shape is omitted; a real consumer translates the transport's presence events into `setRemoteCursor` / `clearRemoteCursor` calls. Cursor positions arrive through some side channel and the consumer translates them into Markoff `Cursor` values.
- **Conflict UX.** The sketch silently drops decode failures with a warning. A real consumer might surface a banner or log to telemetry.
- **Bootstrap.** When a fresh consumer attaches to a long-running document, the transport must replay any peer ops the local replica hasn't seen. Sketch assumes the transport's `setOnInbound` fires for back-log entries on first attach. Real transports vary.
- **Side channels.** Chat, comments, awareness — fully outside the sketch.

### 4.3 The collabtext-side picture

When the consumer's transport is built on collabtext's `StreamSync` (current shape) or its post-refactor equivalent, the wiring on that side mirrors the Markoff side:

```
[local Markoff edits]
        │
        ▼
MarkoffDocument::localOpsProduced
        │
        ▼   (Markoff serialization)
QByteArray blob
        │
        ▼
ITransport::push("stream-X", blob)
        │
        ▼   (collabtext serialization layer)
collabtext::Crdt::StreamSync::push(...)  — appends to local segment file
        │
        ▼   (filesystem / Syncthing / network)
peer machine's StreamSync segment readers pick up the blob
        │
        ▼
peer's ITransport::OnInboundFn fires with (stream, blob, producer)
        │
        ▼   (Markoff deserialization)
MarkoffDocument::applyRemoteOps(ops, meta)
        │
        ▼
[peer's Markoff applies the bundle]
```

The two serialization layers — Markoff's outer envelope and collabtext's inner per-CRDT format — are nested. Markoff's envelope says *what target this op is for*; collabtext's inner bytes say *what the op does to that target*. Consumers don't unwrap either; they route blobs.

### 4.4 Reference test harness (specced now, built later)

The implementation plan that comes out of writing-plans (pivot doc §4.4) will include a reference test harness as a phase. Brief spec:

- **Location:** `apps/markoff-collab-testapp/`.
- **What it does:** Wires two `MarkoffDocument` instances within a single process via an in-memory `ITransport` mock. Allows manual two-replica dogfood ("type in window A, watch the change land in window B"). Drives convergence tests by spawning two test docs, applying divergent edit scripts, then asserting equality.
- **What it doesn't do:** Real network. Real filesystem. Real auth. Real peer enrolment. Real presence policy beyond a hardcoded colour-per-replica.
- **Optional secondary purpose:** if collabtext's demo app currently uses `CollabPlainTextEdit`, this testapp is a candidate replacement that gives the maintainers a richer test bed for their refactor. Whether they adopt it is their call; Markoff ships the testapp regardless.

A real Corbomite plugin (or future third-party consumer) will be meaningfully different — more layers, real auth, real presence — but will hit the same Markoff API the testapp hits.

---

## 5. Out of scope

### 5.1 Consumer-owned (out of scope for D5)

- Auth / authz (who can join the document).
- Identity policy (what a replica ID represents — device? user? session?).
- Peer-set membership: enrolment, eviction, silent-peer policy.
- Transport choice: file-based, network, in-memory, hybrid.
- Multi-document sessions (one peer set across many documents).
- Side channels: chat, comments, awareness.
- Presence policy: when remote cursors appear/disappear, colours, labels, "Alice is typing" affordances outside the editor.
- Bootstrap / catch-up replay (what backlog to deliver to a freshly-attaching consumer).
- Disconnect/reconnect semantics.
- Conflict notification UX (banners, modals, inline annotations) — beyond cursor rendering Markoff already provides.
- Schema versioning across replicas running different Markoff versions.

### 5.2 Collabtext-owned (out of scope for D5; addressed in the negotiation opener)

- Wire format of `Buffer` ops, `IdList` ops, internal CRDT serialisation.
- Convergence guarantees per-CRDT.
- Anchor stability under concurrent inserts.
- Per-CRDT GC compaction primitives.

### 5.3 Punted to D6 or beyond

- `moveAfter` semantics for blocks (per collabtext-scope-line item 1; structural moves stay remove+insert, intent loss accepted).
- Plugin block-kind handling on a replica without the plugin (renders as Paragraph or error block; D5 doesn't try to negotiate plugin sets across replicas).
- High-replica-count performance (>10 peers); D5 designs for small-team scale and trusts that the per-stream-order requirement scales linearly.
- Selection broadcast (a remote replica's selection *range*, not just its cursor caret). Infrastructure exists if a future revision wants it; D5 ships caret only.
- Multi-cursor presence (one replica with multiple cursors). One cursor per remote replica in D5.

### 5.4 Known limitations documented (won't-fix in D5)

- **LWW kind-change flicker.** "I changed kind to X, peer changes to Y, my screen flickers X→Y" — convergent but visually unstable. Expected behaviour.
- **Cross-replica undo is bundle-level, not intent-level.** If Alice deletes a block while Bob is typing in it, Alice's undo restores the block but doesn't know about Bob's typing; Bob's ops will be present but possibly re-deleted by the next interleaving.
- **Memory grows monotonically until first save with peer-acks settled.** A long collab session that never saves never compacts. Documented; not a bug.
- **Replica ID is `quint16`** (~65k namespace). Sufficient for any real Markoff use case; mentioned for completeness.

---

## 6. Test strategy

The tier-0 confidence test for D5 is **two-replica in-process convergence**: spawn two `MarkoffDocument` instances in one test, route their `localOpsProduced` signals into each other's `applyRemoteOps`, run divergent edit scripts, assert structural + content equality at the end. **No transport required** — direct in-process routing. This becomes feasible from the moment §2.4 (`applyRemoteOps`) lands and is the foundational verification for every subsequent phase.

**Test categories:**

| Tier | Lib | What |
|---|---|---|
| 0 | `markoff-core` | Two-doc convergence: divergent edit scripts → assert equal state. ~10 scripts covering structural / buffer / sibling-map / mixed. |
| 0 | `markoff-core` | Three-doc convergence (catches order-dependence the two-doc case can miss). |
| 0 | `markoff-core` | Cross-replica undo: A edits, B applies, A undoes, B applies undo, assert states match. |
| 1 | `markoff-core` | `MarkoffOp` / `MarkoffBundleMeta` serialization round-trip per target. |
| 1 | `markoff-core` | Sibling-map LWW resolution under concurrent writes (lamport ordering, replica-ID tiebreak). |
| 1 | `markoff-core` | Watermark gate: emit `wantsAcksAtWatermark`, no compaction; call `notifyAcksAtWatermark`, compaction proceeds. |
| 1 | `markoff-core` | Idempotency: re-deliver a bundle, assert no double-apply. |
| 1 | `markoff-core` | Edge cases: op-for-deleted-block, op-for-unseen-block, orphan sibling-map entries. |
| 2 | `markoff-live` | Remote cursor rendering: `setRemoteCursor`, render, edit locally, cursor follows, `clearRemoteCursor`, gone. |
| 2 | `markoff-live` | Cursor survival: remote cursor at line 5; local insert at line 3 shifts it to line 6 visually without consumer intervention. |
| 3 | `markoff-collab-testapp` | Two-window manual dogfood. Type in A, observe in B, undo in A, observe undo in B. |
| 3 | `markoff-collab-testapp` | Convergence test driven through the in-memory `ITransport` mock (proves the boundary works through a router, not just direct method call). |

**What we don't test in D5:**

- Real transport (file-system or network). Manual end-to-end using a real Syncthing pair is acceptance-level only, not regression-suite-level.
- Performance (latency, throughput, scale beyond ~3 replicas).
- Crash recovery, partial writes, segment file corruption.

---

## 7. Implementation sequencing

This section guides the **writing-plans** output (pivot doc §4.4); it is not the implementation plan itself. It enumerates the dependency order the plan should follow. Each phase ends with a green tree.

| Phase | Lib | Surface |
|---|---|---|
| 1 | `markoff-core` | `MarkoffOp`, `MarkoffBundleMeta`, `CrdtTarget` types + `Q_DECLARE_METATYPE`. `MarkoffSerializer::encode/decode`. Round-trip tests. |
| 2 | `markoff-core` | `MarkoffDocument` constructor with replica ID + `replicaId()` / `isCollabConfigured()`. Single-user default sentinel works. No emissions yet. |
| 3 | `markoff-core` | `MarkoffDocument::localOpsProduced` signal + emission from the transaction commit path. Bundle-meta construction (bundleId monotonic counter, opCountInBundle from transaction target list). Tier-1 tests for emission counts/contents. |
| 4 | `markoff-core` | `MarkoffDocument::applyRemoteOps` method + dispatch to per-CRDT `applyRemote(...)`. **Tier-0 two-doc convergence tests land here.** This is the most important phase. |
| 5 | `markoff-core` | Sibling-map ops: per-map encode/decode, emission on map writes, application on receive, LWW resolution. Tier-1 LWW tests. |
| 6 | `markoff-core` | Watermark/ack gate: `wantsAcksAtWatermark` signal from `WatermarkCoordinator`, `notifyAcksAtWatermark` method, gated compaction. Tier-1 gate tests + idempotency tests. |
| 7 | `markoff-live` | Remote cursor rendering: `setRemoteCursor` / `clearRemoteCursor` / `clearAllRemoteCursors`, the QML delegate hooks, the cursor-survival propagation. Tier-2 tests. |
| 8 | `apps/markoff-collab-testapp` | In-memory `ITransport` mock, two-window testapp, convergence tests routed through the mock. Tier-3 tests. |
| 9 | (dogfood) | Manual two-window dogfood pass driven by user. Acceptance gate for D5 complete. |

**Critical-path note:** Phase 4 is where convergence becomes verifiable. Phases 5–8 ride on top of that confidence. If Phase 4 reveals an architectural flaw, that flaw is much easier to fix at Phase 4 than at Phase 9.

**Parallelisation opportunity:** Phase 7 (presence) is structurally independent of Phases 5–6 (sibling maps + watermark). If two agents are working, one can do 5+6 while the other does 7. The plan doesn't need to assume this but the spec leaves room for it.

**Negotiation-opener interaction.** The collabtext negotiation opener (companion document) is an independent track. If the maintainers refactor during the implementation window, the consumer wiring sketch (§4) updates to reflect the new collabtext shape; the Markoff-side API does not change. If they don't, §4 stays as-is. Implementation does not gate on the negotiation outcome.

---

## 8. References

**Foundational:**

- `docs/specs/2026-05-04-d2-foundation-reshape-design.md` — the D2 foundation. Of particular interest: §3 (BlockId stability), §4.7 (Origin enum + remote-edit handling), §7.4 (collab-evolution path for GC), §8 (signal API).
- `docs/specs/2026-05-02-d-evolution-proposal.md` — the D-arc kickoff document; D5 is the activation phase the proposal anticipated.
- `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items, binding cross-arc constraints.
- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — the unified-direction posture; this spec is §4.3 of that doc.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` — pipeline-feature provenance, cited under operating principle 4.

**Cross-arc:**

- `~/dev/collabtext/include/collabtext/StreamSync.h` — the existing transport primitive (consumer-side reference).
- `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` — the maintainer response that produced the scope-line; this spec's negotiation opener is a follow-up.
- `~/dev/collabtext/docs/research/2026-04-06-multi-cursor-widget-research.md` — collabtext maintainers' existing thinking on live-cursor presence.

**Companion:**

- `docs/handoff/2026-05-07-collabtext-d5-negotiation-opener.md` — the maintainer-facing negotiation document. Independent track; D5 implementation does not gate on its outcome.

**Carry-forward (lower-layer decisions still authoritative):**

- `docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` §3.5 — cursor "survival under remote edits" rules.
