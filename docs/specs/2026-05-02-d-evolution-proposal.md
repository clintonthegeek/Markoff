# D-Evolution Proposal — Per-Block CRDT Architecture

**Date:** 2026-05-02
**Status:** Draft for evaluation; not committed.
**Authored by:** Markoff design (paired with the C-restoration spec, `docs/specs/2026-05-02-live-render-restoration-design.md`).
**Audience:**
  - **Primary:** the `collabtext` maintainers, for evaluation of practicality and roadmap fit. Self-contained; assumes no Markoff-internal context beyond what is summarised in §2.
  - **Secondary:** Markoff implementers planning post-restoration architectural moves.

**Companion:** `docs/specs/2026-05-02-live-render-restoration-design.md` — the near-term architecture, hereafter referred to as "C". This proposal is the long-term direction the C architecture deliberately leaves room for.

---

## 0. Executive summary

Markoff is a Qt6/QML Markdown editor whose runtime model is *block-based* (a list of blocks; text blocks are one kind among many) but whose persistent and CRDT model is currently *text-based* (a single character-rope CRDT carved into blocks by an asynchronous parser). The C-restoration that ships next eliminates the consequences of this mismatch in the view layer through a sequence-tagged reconciliation protocol; it does not eliminate the mismatch itself. That elimination is what this proposal calls **D**.

In D, each block holds its own independent CRDT for its content (text or otherwise), and a higher-level *structural CRDT* manages the ordering of blocks. Block boundaries become structural state; block content becomes per-block state. The parser's role shrinks dramatically — only inline content within a block, no boundary inference. Editing latency becomes O(one block) rather than O(whole document). Block-as-a-unit operations (move, delete, replace) become first-class CRDT ops rather than implicit byte-range mutations. Plugin-authored block kinds with non-Markdown content (math source, mermaid source, structured tables, embedded media) become natural rather than awkward.

The technical question for this audience is: **what is the smallest extension of `collabtext` that supports this?** This proposal explores three paths, recommends the second as the lightest practicable option, and provides a concrete migration plan and a precise list of asks. It deliberately respects collabtext's stated scope ("not a general-purpose CRDT framework") and proposes the smallest primitive that lets Markoff build the structural layer without pulling collabtext outside its mission.

We are not asking collabtext to commit to D. We are asking for an evaluation: does the proposed extension feel coherent with collabtext's design philosophy, or does it pull in directions you'd rather not go? If yes, we collaborate on the API. If no, we know to route the structural-CRDT work elsewhere (likely a small Markoff-internal CRDT or a third dependency) and adjust the design.

---

## 1. What collabtext provides today (as we understand it)

For external readers and to ground the proposal, here is what we observe collabtext to be:

- A focused **plain-text CRDT engine**, transport-agnostic, designed for asynchronous file-sync workflows (Syncthing-class, not WebSocket-class). [^1]
- The principal API is `CollabText::Crdt::Buffer` (low-level) and `CollabText::CollabDocument` (Qt-aware wrapper exposing a `QTextDocument` view).
- Operations are insert / remove / undo / redo on a single rope of UTF-16 code units.
- Stable position **anchors** with left/right bias, surviving concurrent edits.
- **Causal ordering** via version vectors; out-of-order delivery handled internally.
- **Garbage collection** primitives (local tombstone cleanup; distributed watermark-based compaction).
- The library explicitly disclaims rich-text, attributes, maps, arrays, counters, and JSON. [^2]
- The `docs/research/2026-04-06-crdt-editor-integration-patterns.md` note already describes "Three Approaches" (Zed-style single-model, Yjs-style binding, Atom-style two-model), so the team has a developed view of the editor-integration problem space. This proposal does not re-litigate that framing.

Markoff currently uses collabtext at the document level: one `Crdt::Buffer` per `MarkoffDocument`. The Markoff foundation wraps collabtext with `MarkoffDocument` and exposes a CRDT-typed-symbol-free public API (`Markoff::TextAnchor` instead of `Crdt::Anchor`, etc.) to insulate higher layers from CRDT internals. Markoff's `markoff-foundation/CLAUDE.md` documents that boundary.

[^1]: collabtext README §"What it is".
[^2]: collabtext README §"What it isn't".

---

## 2. Why C is not enough (in one section)

The C-restoration architecture (see companion spec §1, §4) treats the CRDT as canonical and the parser as asynchronous. To stop the parser from clobbering in-flight typed text, C introduces a *per-row freshness rule*: parser output for row R is trusted only if no local edit to R has arrived since the parse's input was captured. The mechanism works; it eliminates a class of bugs and simplifies the view layer substantially.

It does not, however, address the structural mismatch:

1. **Block boundaries are still parser-decided.** Pressing Enter in the source editor inserts `\n\n` into a flat byte rope; the parser eventually re-derives "this used to be one block, now it's two." There is a window where the runtime block-list and the CRDT byte-list disagree, and the structural-key handler has to bridge it explicitly. C handles this gracefully; D removes the gap entirely.

2. **Edits to one block must traverse the full document.** `applyLocalEdit` operates on document-level byte offsets. The CRDT does not know that the user just typed in the third paragraph; it sees a byte-stream insert. Causality / version-vector reasoning is at document granularity. For a 100-block document, an edit to block 3 produces ops that are causally entangled with concurrent edits to blocks 7, 50, and 99 even when nothing about those blocks' content was relevant.

3. **Plugin block kinds with non-Markdown content are awkward.** A math block's content, in the Markdown source, is a `$$…$$`-fenced LaTeX string. The CRDT sees it as bytes; the parser identifies the boundaries; the math delegate edits LaTeX text *within* those boundaries. This works but ties the math block's content semantics to its serialised Markdown representation. A future block kind (a structured table, an embedded JSON object, a video annotation) does not naturally serialise as a contiguous byte string — and even when it does, mutating a structured field via byte-level CRDT ops loses semantic meaning.

4. **Block-as-a-unit operations are unrepresentable as primitive ops.** Drag-to-reorder, delete-block, duplicate-block — all of these have to be expressed as concurrent byte-range mutations in C. Concurrent moves by two users produce arbitrary byte-level interleaving rather than a coherent merge.

5. **Per-block undo is impossible.** Undo in C is document-level. A user can't say "undo my last edit in *that* block specifically" — their undo unwinds whatever happened most recently anywhere.

The C-architecture is sufficient for the restoration's stability bar (single-user, paragraph + heading + code-block + list + math, dogfood-stable). It is *not* sufficient for the long-term Markoff feature roadmap (collab + plugin-authored block kinds + structured non-text content + per-block presence + drag-to-reorder).

D is a path to that roadmap.

---

## 3. The shape of D

### 3.1 Document model

A Markoff document in D is:

```
MarkoffDocument
├─ structural CRDT          : an ordered list of BlockEntry items
└─ per-block content CRDTs  : one CRDT per BlockEntry, of a kind appropriate to the block

BlockEntry
├─ blockId        : opaque CRDT-stable identifier
├─ kindTag        : "paragraph" | "heading" | "list-item" | "math" | …
└─ contentRef     : reference to the block's own CRDT (a Crdt::Buffer for text kinds;
                    something else for non-text kinds — TBD per kind)
```

The structural CRDT is the authority on **block ordering, block existence, and block kind**. It contains no content. Content lives entirely in the per-block CRDT keyed by `blockId`.

Block-level operations (insert, delete, move, change-kind) are operations on the structural CRDT. Content-level operations (insert byte, delete byte, undo) are operations on the relevant per-block CRDT and never reach the structural CRDT.

### 3.2 Persistence (Markdown round-trip)

Save: walk the structural CRDT in order; for each `BlockEntry`, ask the per-block CRDT for its current content; format with the kind's serialiser (paragraph: as-is; heading: prefix with `#`s; math: wrap in `$$…$$`; etc.); concatenate with the kind's separator (`\n\n` between most kinds; `\n` between consecutive list items; etc.). The result is a Markdown file indistinguishable from what C would produce for the same logical state.

Load: parse Markdown with the existing tree-sitter pipeline (which carves into top-level blocks); for each top-level block, allocate a per-block CRDT and populate it with the block's content; populate the structural CRDT with `BlockEntry` records. The parser's role at load time is identical to today; it simply does not have a runtime role.

This is the same observation that makes Yjs's Y.XmlFragment + Y.XmlText composition work — the persistent format is one thing, the runtime CRDT shape is another, and a thin (de)serialiser bridges them.

### 3.3 Editing flow

Local text edit inside a block:
1. User types in block B's delegate.
2. Delegate's binding sends insert/remove to B's per-block CRDT.
3. Per-block CRDT emits ops; they're stored in B's op log.
4. Delegate re-renders from B's content.
5. **No global parser invocation.** The block's content is then locally re-parsed for inline structure (within-block; bounded; fast).

Local structural edit (Enter at end of block B):
1. Structural-key handler issues a structural op: "insert new BlockEntry of kind=paragraph after B."
2. Structural CRDT emits ops; new `BlockEntry` materialises.
3. View receives the structural ops; ListView gains a row; new delegate incubates with focus.
4. **No global parser invocation.**

Remote edit:
1. Wire delivers a batch of ops, possibly mixed structural + per-block.
2. Each op is routed to its target CRDT (structural or block-by-id).
3. CRDTs apply concurrently; signals propagate; view updates.

The latency model: **the parser is no longer in the keystroke hot path at all.** Inline parsing within a block runs locally per-block on the worker thread, with O(block-size) cost rather than O(document-size). The 16 ms target the C-restoration enforces becomes a much wider headroom in D.

### 3.4 Block kinds with non-text content

The per-block CRDT type is per-kind:

| Kind | Per-block CRDT type |
|---|---|
| paragraph, heading, code-block, list-item, blockquote, callout | `Crdt::Buffer` (plain text) |
| math | `Crdt::Buffer` (LaTeX source as plain text) |
| mermaid | `Crdt::Buffer` (mermaid source as plain text) |
| image, hr | trivial; possibly a tiny attribute-bag CRDT or just a frozen value |
| table | structured (deferred; plausibly multiple `Crdt::Buffer`s for cells, plus a structural CRDT for rows/columns — recursion into the same pattern) |
| (future) interactive widgets | per-kind, defined by the plugin |

For most kinds, **the per-block CRDT is just another `Crdt::Buffer`.** The only structurally new CRDT type in D is the **structural CRDT for block ordering**.

### 3.5 The structural CRDT — what it actually needs

The structural CRDT manages an *ordered list of opaque IDs*. Operations:

- `insertEntryAfter(prevId, newEntry)` — insert a new entry after a given entry (or `null` for "at the start").
- `deleteEntry(id)` — remove an entry.
- `moveEntry(id, newPrevId)` — move an entry to a new position. (Optional in v1; can be expressed as delete+insert at the cost of losing intent.)
- `changeKind(id, newKind)` — mutate an entry's kind (e.g. paragraph → heading).
- `entriesInOrder()` — read out the current ordering.
- Iterator-style change notifications, mirroring `Crdt::Buffer`'s `ChangeCallback`.

It does **not** need to know about block content; that lives in per-block CRDTs.

Concurrent semantics:

- Two replicas inserting after the same entry: both inserts survive; ordering is deterministic (e.g. by replicaId tiebreak — same as collabtext's existing tiebreak rules).
- Replica A deletes block X while replica B edits inside block X: B's content edits become orphaned (the block is gone). Delivery can be detected (the structural CRDT signals the block's death) and the orphaned per-block CRDT can be either garbage-collected or surfaced as a recoverable artifact.
- Replica A moves block X while replica B edits inside block X: edits land in the moved block. Move is structural; edit is content. They commute.

These semantics are exactly those of a *list CRDT over opaque elements*. The literature has several solutions (RGA, LSEQ, treedoc, Logoot, fugue). collabtext's existing `Crdt::Buffer` is a list CRDT over UTF-16 code units; the same algorithmic primitives that make it work apply here, with `BlockEntry` substituted for a code unit.

---

## 4. Three options for the structural CRDT

### Option α — Build it on top of `Crdt::Buffer` ("structure-as-text")

Express the structural list as a `Crdt::Buffer` whose content is a sentinel-delimited sequence of opaque block-id tokens, e.g.:

```
\x00<id-1234>\x00<id-5678>\x00<id-9abc>\x00
```

Inserts and deletes at sentinel boundaries become block insertions/deletions. Each block-id is unique; the kind tag is stored separately (in a sibling map keyed by id, with its own merge rules — e.g. last-writer-wins on a per-id timestamp).

**Pros.**
- Zero changes required in collabtext. Markoff implements D entirely as a wrapper over multiple existing `Crdt::Buffer` instances (one structural, N per-block).
- Anchors-into-the-structural-list (e.g. "the cursor is at block 5") fall out for free.
- Causal ordering, GC, undo all inherit from `Crdt::Buffer`.

**Cons.**
- The structural CRDT's content semantics (a list of opaque ids) are obscured by the encoding (a UTF-16 byte stream with custom delimiters). Markoff would write a fragile parser for the structural buffer's content.
- Mutations require careful boundary handling. A move op decomposed into delete+insert risks producing intermediate states (briefly two delimiters, briefly zero) that other replicas observe — fine if the CRDT's per-op semantics handle that, but obscures intent.
- The kind tag, being external, is not under the same merge regime as the structural list. Concurrent kind-change vs delete races would need ad-hoc handling.
- Garbage collection: when a block is deleted from the structural list, its per-block CRDT (a separate `Crdt::Buffer`) is now orphaned. Markoff must manage that lifecycle externally; collabtext doesn't know two `Buffer`s are related.

**Practicality.** Markoff can ship α without any collabtext changes. It is an *escape hatch* — viable if the answer to "should we extend collabtext for this?" is no. Quality is acceptable but not great.

### Option β — Add a minimal `IdList` primitive to collabtext (recommended)

Extend collabtext with one new primitive: a CRDT-shaped ordered list of opaque elements, separate from the existing `Crdt::Buffer`. A sketch of the API:

```cpp
namespace CollabText::Crdt {

/// CRDT-shaped ordered list of opaque uint64 elements with stable position
/// anchors. Independent of Buffer; shares the same op-causality model and
/// transport. Element identity is the uint64 itself; element value carries
/// no merge semantics (the application owns it).
class IdList {
public:
    explicit IdList(uint16_t replicaId);
    ~IdList();

    /// Insert `id` after the element with the given anchor. Pass a left-edge
    /// anchor for "insert at start." Returns an Operation.
    Operation insertAfter(const Anchor &after, uint64_t id);

    /// Delete the element identified by anchor.
    Operation removeAt(const Anchor &);

    /// Move the element identified by anchor to a new position after `after`.
    /// Optional in v1; can be expressed as remove + insert.
    Operation moveAfter(const Anchor &target, const Anchor &after);

    /// Read out the current ordering.
    std::vector<uint64_t> ids() const;

    /// Anchor at the position of `id`, with left or right bias.
    Anchor anchorOf(uint64_t id, Bias) const;

    /// Apply a remote operation.
    void applyRemote(const Operation &);

    /// Change notification, same shape as Buffer.
    using ChangeCallback = std::function<void()>;
    void setOnChange(ChangeCallback);
};

}
```

The application (Markoff, in this case) owns:
- The mapping from `uint64 id` to per-block CRDT.
- Block kind tags (stored in a separate `IdList`-aligned sibling map, or as a per-element value-bag if a slightly richer primitive is offered).
- Garbage collection coordination (when `IdList` deletes an id, the application disposes the per-block `Crdt::Buffer`).

**Pros.**
- Honours collabtext's stated scope: this is still a single-purpose ordered-list primitive, not a general-purpose CRDT framework. It does one thing.
- Implementation reuses all of `Crdt::Buffer`'s machinery (causal ordering, op log, anchors, undo, GC). Algorithmically it is `Buffer` over `uint64` instead of UTF-16 code units. Substantial code sharing is plausible.
- The application's structural semantics are explicit (it sees a list of ids, not a delimited byte string).
- Cross-CRDT lifecycle (block deletion → per-block CRDT disposal) becomes a clean callback the application registers, rather than an external bookkeeping problem.

**Cons.**
- Extends collabtext's scope, however minimally. Maintainers must accept the precedent that collabtext provides *more than one* CRDT primitive.
- A new primitive is more API surface to maintain, document, and test. Even if the implementation reuses internals, the public API is a long-term commitment.
- The line "what should this primitive support out of the box?" is debatable. Should it carry per-element values? Per-element kind tags? Or stay strictly opaque-id and force the application to maintain sibling maps? (Our recommendation: strictly opaque-id; richer semantics live in application maps that can be added compositionally later.)

**Practicality.** This is the proposal. It gives Markoff what it needs with the smallest possible extension to collabtext.

### Option γ — Full structural CRDT framework

The Yjs / Automerge model: collabtext grows to provide arrays, maps, counters, and arbitrary nesting. This is what collabtext's README explicitly disclaims. We list it for completeness; we do not advocate it.

If maintainers want to consider it, the design space is well-explored elsewhere; we have no special insight to add and would defer to whatever the maintainers want.

**Practicality.** Out of scope per collabtext's stated philosophy. We agree with that scoping.

---

## 5. Recommended path: β

We propose Option β: a single new primitive `Crdt::IdList`.

Reasoning:

1. **Scope-honouring.** β is one new primitive of comparable complexity to `Buffer`, doing one focused thing. It does not pull collabtext toward becoming Yjs.
2. **Algorithm sharing.** A list CRDT over opaque elements is structurally identical to a list CRDT over code units, modulo "what is the element type." Implementation can be a templated abstraction over `Buffer` or, if templates feel heavy, a parallel implementation that shares code by composition. Either way the wire format, causal ordering, anchor model, and GC machinery already exist.
3. **Predictable surface.** The API is small, the semantics map cleanly onto well-understood list-CRDT literature, and the application contract (opaque ids; values managed externally) is unambiguous.
4. **Markoff carries the application complexity.** The per-block-CRDT lifecycle, kind-tag sibling map, content-edit routing, and Markdown (de)serialisation are all Markoff's problem. collabtext provides the structural primitive and is done.

Markoff would build the D-shape architecture on top of:
- `Crdt::Buffer` (existing) — for per-block text content.
- `Crdt::IdList` (new) — for block ordering.
- `Markoff::DocumentStructure` (new, internal to markoff-foundation) — composes one `IdList` with N `Buffer`s, manages lifecycle, exposes a CRDT-free public API analogous to today's `MarkoffDocument`.

---

## 6. Cross-cutting concerns

### 6.1 Undo

C has document-level undo on `MarkoffDocument`. D wants both:

- **Document-level undo** (the default; matches every text editor's Ctrl-Z expectation): unwinds the most recent edit anywhere in the document. Mechanism: each CRDT (structural and per-block) has its own undo stack; a thin application-level "edit log" records (timestamp, target-CRDT) tuples; document-level undo pops the most recent entry and dispatches an undo to the named target.
- **Per-block undo** (optional UI feature): unwinds only edits within one block. Direct call to that block's per-block CRDT undo.

collabtext's existing undo is per-`Crdt::Buffer`. `IdList` would need its own undo stack, structurally analogous. The thin application-level edit log is Markoff-internal.

Concurrent undo with remote edits: collabtext handles this for `Buffer` via parity-based conflict resolution. The same mechanism applies to `IdList`. Cross-CRDT undo (e.g. "undo the structural insert that created block 5") is a one-CRDT-at-a-time operation; the application's edit log dispatches each.

### 6.2 Anchors

A cursor in D is `(BlockId, anchor-within-block)`. The block-id is anchored in the structural CRDT (left or right bias selecting which neighbouring block we collapse to if this block is deleted). The within-block anchor is anchored in the relevant per-block CRDT.

This composes anchors at two levels rather than one. Each level uses collabtext's existing anchor primitives. The application (Markoff) composes them into `Markoff::Cursor` (Shape 1, see C-spec §3) without collabtext needing to know about the composition.

### 6.3 Presence (live cursors)

When a transport is low-latency (post-restoration: collab activation), each replica broadcasts its current cursor to peers. The cursor's two-level anchor structure serialises naturally. Receiving peers translate through their local CRDTs and render the remote cursor.

`IdList`'s anchor model needs the same wire-format treatment as `Buffer`'s. The collabtext team has already thought about live cursors in `docs/research/2026-04-06-multi-cursor-widget-research.md`; the same patterns apply.

### 6.4 Garbage collection

`Crdt::Buffer` has tombstone GC and watermark-based compaction. `IdList` would mirror these.

The cross-CRDT lifecycle (a block being deleted means its per-block `Buffer` is orphaned and should be disposed) is application-level. The proposed `IdList::setOnChange` callback is the hook; Markoff observes element removals and disposes the corresponding per-block `Buffer`. If a remote replica still references the deleted block's per-block CRDT in undo or pending ops, the application must coordinate disposal — likely via a watermark across all CRDTs in the document, where a per-block `Buffer` is fully GC'd only when all replicas have observed its removal *and* committed any per-block undo entries that reference it.

This is more complex than single-`Buffer` GC. The collabtext maintainers are best placed to evaluate whether the existing watermark machinery generalises naturally or needs extension.

### 6.5 Transport / sync

Today's transport carries `Crdt::Operation` records for `Buffer`. In D, the transport carries `Crdt::Operation` records for both `Buffer` and `IdList`. Either:

- The op type carries a discriminator (which CRDT it's for) and a target identifier (which `Buffer` instance, by `BlockId`).
- The transport groups ops per-CRDT and the application demuxes.

The first is cleaner; it's also a small extension to the existing op format. Maintainer call.

---

## 7. Migration: post-C → D

Restoration (the C-spec phases R1–R10) ships first. D is post-restoration work.

### 7.1 Phase D0 — Joint design (this document, a follow-up exchange)

This proposal lands in the Markoff repo. The collabtext maintainers evaluate. A reply (could be a return doc, an issue thread, a meeting summary) confirms or adjusts the option choice. The outcome of D0 is a yes/no/different on β, and if yes, a sketch of the API and timeline collabtext is willing to commit to.

### 7.2 Phase D1 — `IdList` lands in collabtext

Standalone work in `collabtext`. Public API per §4 option β. Tests. Documentation. Released as a minor version.

Markoff does not consume `IdList` until D1 is published. Markoff's restoration work continues independently.

### 7.3 Phase D2 — `Markoff::DocumentStructure` and the foundation reshape

A new foundation type `DocumentStructure` composes one `IdList` with N `Buffer`s. The public API mirrors today's `MarkoffDocument` (CRDT-free types, sequence accessors, anchor APIs) so the view layer's surface is unchanged or only minimally different. Internal implementation is the per-block-CRDT model.

This is multi-week foundation work. It is not a view-layer change.

### 7.4 Phase D3 — View-layer adaptation

The view library (`markoff-live-render`, post-C) adapts to D's foundation. Per the C-spec §13 cross-references:

- Shape 1 cursor model is unchanged.
- Sequence-tagging mechanism becomes per-block-CRDT-scoped instead of document-scoped — degenerate, simpler.
- `BlockKindDescriptor` contract is unchanged.
- Structural-key dispatch is unchanged in protocol; the *implementation* of "Enter at end of block" mutates the structural CRDT (an `IdList::insertAfter`) instead of inserting `\n\n` into a doc-level rope.

The adaptation is mostly internal-implementation work; consumers above the view library see no contract change.

### 7.5 Phase D4 — Parser scope reduction

Now that block boundaries are structural, the parser is no longer authoritative on them. Its role shrinks to inline-within-a-block. The Markoff parser library is simplified: tree-sitter still parses each block's content for inline structure, but the top-level walk and BlockAnchor computation are no longer load-bearing for runtime correctness — they're only used at file-load time for materialising the structural CRDT from a parsed Markdown file.

This is parallelisable with D2/D3 once D1 ships.

### 7.6 Phase D5 — Collab activation

With per-block CRDTs and a structural CRDT, the collab story is genuinely well-shaped. Wire format, presence, conflict semantics — all benefit. D5 is where collab actually ships, with the architecture having been collab-ready from C onwards.

### Estimated D budget

D1: 8–16 weeks of collabtext work (their estimate; we have no visibility).
D2 + D3: 12–20 weeks of Markoff foundation + view work.
D4: 2–4 weeks of parser work.
D5: open-ended depending on collab feature scope.

These are post-restoration; Markoff's own timeline is gated on collabtext's D1 timeline.

---

## 8. Concrete asks of `collabtext` maintainers

Listed in approximate order, with a clear "no" being a perfectly acceptable answer for any item:

### 8.1 Evaluate the proposal

Read this document in full; respond with:

- Whether option β feels coherent with collabtext's design philosophy or whether it represents scope creep you'd rather avoid.
- If β is acceptable: rough sketch of what the API would actually look like (your design judgement, not ours), and rough timeline.
- If β is not acceptable: which path you'd suggest instead — α (structure-as-text), γ (full framework), a different hybrid, or "build the structural CRDT outside collabtext."

A "no" is genuinely fine. Markoff has α as a fallback, and there are independent CRDT libraries that could provide structural primitives if needed. We are asking, not demanding.

### 8.2 If β: agreement on the public-API surface

Specifically:

- Element type: opaque `uint64`, or templated, or `std::string` for ergonomics?
- Move semantics: in v1 or v2?
- Anchor semantics: same `Anchor` type as `Buffer` (with bias), or a list-specific `ListAnchor`?
- Change callback granularity: per-op, per-batch, or both?
- Op format: shared `Operation` type with a discriminator, or separate `IdListOperation`?

We are happy to draft a more concrete sketch of the API once direction is agreed.

### 8.3 Documentation contract

If `IdList` ships, we'd value documentation comparable to `Buffer`'s on:

- Concurrent semantics (especially insert-after-deleted-anchor, move-vs-delete, etc.).
- Memory complexity per element and per op.
- Wire-format compatibility / version expectations.
- The undo model.

These will inform Markoff's `DocumentStructure` design.

### 8.4 Testing fixtures

A small fixture set demonstrating concurrent scenarios (two replicas, crossing inserts, move-vs-delete, rapid-fire structural edits) at the `IdList` level would let Markoff's foundation tests use them as ground truth.

### 8.5 Joint review of `Markoff::DocumentStructure`

Once D2 is in design, we'd benefit from a review pass by collabtext maintainers on the composition pattern (`IdList` over `Buffer`s with cross-CRDT lifecycle) — partly to catch errors, partly as a forward-compatibility check (are we using `IdList` in ways that constrain its future evolution?).

---

## 9. Open questions for joint discussion

These are questions where Markoff has a tentative position but the right answer benefits from collabtext maintainer input.

### Q1. Element value richness

Should `IdList` elements carry an opaque value (e.g. `IdList<Value>` where Markoff stores a `BlockKindTag` per element), or stay strictly opaque-id with siblings managed externally?

Markoff's tentative preference: strictly opaque-id. Sibling kind-tag map managed by `Markoff::DocumentStructure`. Reasoning: the kind-tag has its own merge rules (most-recently-written-by-anyone, possibly), and entangling it with the list CRDT's identity merge rules invites confusion. But we're open if the maintainers see a cleaner shape.

### Q2. Cross-CRDT undo

Document-level undo wants to unwind the most recent edit anywhere. The "most recent" is determined by the application's edit log, which records per-edit (target-CRDT, timestamp). collabtext could provide:

- (a) Nothing; application owns the log entirely.
- (b) A small "edit log" primitive that aggregates ops across multiple CRDTs and supports "what was the most recent op?" queries.
- (c) Something in between.

Markoff's tentative preference: (a). The cost is moderate; the alternative is collabtext growing into something like a transaction manager, which is scope creep.

### Q3. GC across block deletion

When a block is deleted, its per-block `Buffer` is orphaned. Two strategies:

- (a) Application disposes the orphaned `Buffer` immediately (aggressive). Risk: a peer replica still references the block in pending ops or undo; the application must wait until everyone has acknowledged.
- (b) Application keeps the orphaned `Buffer` until a watermark is reached. Mirrors collabtext's existing `compact(watermark)` GC on `Buffer`; needs a similar primitive on `IdList` and a coordination mechanism between them.

Markoff's tentative preference: (b), implemented via a per-document watermark across all CRDTs that participate.

### Q4. Anchor portability across structural moves

If block X is moved, an anchor "at byte 5 of block X" stays valid (the per-block CRDT didn't change). But "after block X" in the structural anchor might mean different things before and after the move.

We believe this is OK — `IdList`'s anchor model treats moves as delete+insert at the structural level (semantically), which is well-defined. But it's worth confirming that anchors inside a moved block do *not* destabilise.

### Q5. Wire format compatibility post-D1

If `IdList` ships, existing `Buffer`-only consumers of collabtext continue to work unchanged. But Markoff will produce ops mixing both. Should the wire format carry a version flag distinguishing "this op stream uses Buffer only" from "this op stream uses Buffer + IdList"? Or is this entirely transport-layer and not collabtext's concern?

Markoff's tentative preference: transport-layer concern; collabtext's serialisation is per-op, and the application owns multiplexing.

---

## 10. A word about timing

This proposal is forward-looking. C-restoration is the active Markoff project; D is a post-restoration direction. The collabtext maintainers should not feel any pressure to respond on a tight schedule. The Markoff team's restoration work will not be blocked on D in any way.

What we want from this document, near-term:

- An acknowledgement that the proposal has been read.
- A rough indication of whether β looks reasonable to pursue eventually, or whether we should plan around α / a different solution.
- An open door for future collaboration when D becomes the active project on our side.

We're framing this proposal early so both projects can shape their roadmaps with mutual visibility. Restoration is currently expected to take 18–30 weeks; the joint D-design conversation can begin any time during that window.

---

## 11. References

### Markoff context

- `docs/specs/2026-05-02-live-render-restoration-design.md` — the C-restoration spec; the architecture this document evolves from.
- `docs/2026-05-02-live-view-architectural-audit.md` — the diagnostic that drove restoration; useful for understanding the historical context that shaped the C decisions.
- `libs/markoff-foundation/CLAUDE.md` — the foundation public-API boundary; describes how Markoff today wraps collabtext to keep CRDT internals out of the view layer.

### collabtext context (read for grounding; not authored by us)

- `libs/collabtext/README.md` — scope, philosophy, what it is/isn't.
- `libs/collabtext/docs/research/2026-04-06-crdt-editor-integration-patterns.md` — the "Three Approaches" framing; the foundational thinking on editor-CRDT sync that this proposal builds on top of.
- `libs/collabtext/docs/research/2026-04-06-multi-cursor-widget-research.md` — relevant for §6.3 (presence).
- `libs/collabtext/docs/specs/sumtree-optimizations.md`, `direct-channel-interface-design.md`, `transport-elevation-roadmap.md` — relevant background for §6.5 (transport).

### Adjacent literature

- Yjs's Y.XmlFragment / Y.XmlElement / Y.XmlText composition pattern: a working example of "structural CRDT over content CRDTs" in production. We deliberately do *not* propose Markoff/collabtext mirror Yjs's API surface; we cite it as a reference point for the design pattern's viability.
- Automerge's tree types: similar pattern; different implementation choices.
- Zed's editor architecture (cited in the collabtext research note): single-model approach which D approximates at the per-block level.

---

*End of proposal. We look forward to the maintainers' evaluation and reply when their schedule allows.*
