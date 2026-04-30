# BlockAnchor (foundation) — design

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Phase:** Foundation API extension; precondition for `docs/specs/2026-04-30-live-editing-design.md`
**Status:** decisions resolved 2026-04-30; ready for plan-writing
**Authoring context:** Initial spec written 2026-04-30 against the editing-spec brainstorm transcript. The §10 open decisions were resolved with the user the same day; §10 now records those decisions and §2 / §3 / §5 reflect them.

## 0. TL;DR

`Markoff::BlockAnchor` is a stable identity for a top-level block in a parsed `Markoff::Document`. It's backed by a CRDT anchor at the block's first byte (Left bias); it survives content edits, splits, merges, and concurrent collaborative ops. `Markoff::TextAnchor` is its companion — a CRDT-typed but CRDT-header-free wrapper of `CollabText::Crdt::Anchor` that view layers can hold opaquely.

These types unblock two consumers:

1. **`AstBlockDiff` (in `markoff-view-qml`)** changes its `BlockKey` from `(kind, content-hash)` to `(kind, BlockAnchor)`. The diff stops churning rows under typing; the editing spec is free to flip delegates to writable.
2. **`LiveSelectionView` (replacing `LiveSelectionModel`)** consumes `Session::primarySelection`'s `(anchor, active)` as opaque `TextAnchor`s and projects them to per-delegate `(BlockAnchor, offsetInBlock)` ranges via foundation APIs.

The implementation cost is mostly type-safety scaffolding plus a handful of convenience methods on `MarkoffDocument`. The hard work — stable byte-level anchors — is already in `CollabText::Crdt`.

---

## 1. Architecture overview

**Key insight: BlockAnchor is just a TextAnchor at the block's first byte.** All the stability properties fall out of the existing CRDT anchor's properties. This spec adds type safety, view-layer decoupling from `<crdt/Anchor.h>`, and a small set of convenience APIs on `MarkoffDocument` so consumers don't have to reach into raw byte coordinates.

**Type layering:**

- `CollabText::Crdt::Anchor` (existing, in collabtext) — the raw CRDT byte-position handle. 12-byte struct: `replica_id` + `char_value` + `bias`. Header-included only by collabtext-aware code.
- `Markoff::TextAnchor` (new, in `markoff-foundation`) — same byte layout, no CRDT header dependency. The view-layer-safe handle. Public-header companion (`TextAnchor.h`) deliberately does NOT `#include <crdt/Anchor.h>`. Internal conversion helpers in `src/` translate to/from `Crdt::Anchor`.
- `Markoff::BlockAnchor` (new, in `markoff-foundation`) — wraps a `TextAnchor`. Type-distinct from `TextAnchor` so that the API surface can prevent passing one where the other is expected.

**Stability properties (from CRDT anchors, restated for clarity):**

- An anchor at byte X with Left bias survives edits anywhere else in the doc; `resolveAnchor` returns the new byte position after shifts.
- An edit at byte X with Left bias still resolves to byte X if the character at X is unchanged; if it's deleted, the anchor still resolves (the CRDT keeps the Lamport reference) but the byte position is now where that character "would be".
- An anchor's identity is the Lamport timestamp `(replica_id, char_value)`. Two anchors are equal iff they refer to the same character. Bias is part of the anchor but not part of identity for our purposes.

**BlockAnchor stability across structural changes:**

- **Edit within block:** content-hash changes, but the block's first byte is unchanged → BlockAnchor unchanged. Diff sees `dataChanged` for that row.
- **Split** (one block becomes two): the block containing the original first byte keeps the BlockAnchor. The other (newly-introduced) block gets a new BlockAnchor at its own first byte. Diff: one row has identity preserved (data updated for shorter content); one row inserted with a new identity.
- **Merge** (two blocks become one): the surviving block keeps its first-byte BlockAnchor (typically the upper block's). The other block's BlockAnchor still resolves to its original byte position, which is now mid-block in the merged result — but no block in the new tree starts at that byte, so the diff sees that anchor as orphaned and removes the row.
- **Block deleted entirely:** anchor still resolves but no block in the new tree has its first byte at that position. Diff removes the row.
- **Concurrent collaborative edit (CRDT future):** anchors survive by design. Two collaborators agree on block identities because they agree on Lamport timestamps. (This is the long-term win that CRDT-anchor identity buys over byte-overlap heuristics.)

---

## 2. API surface

### New types — `markoff-foundation/include/markoff-foundation/`

```cpp
// TextAnchor.h — does NOT include <crdt/Anchor.h>
#pragma once
#include <QtGlobal>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Opaque view-layer-safe wrapper for a CRDT byte anchor. Same byte layout
/// as CollabText::Crdt::Anchor; zero-cost conversion in foundation src.
/// Consumers may hold and pass; must NOT inspect, construct from raw fields,
/// or compare except via operator==. All translations go through
/// MarkoffDocument APIs.
struct MARKOFF_FOUNDATION_EXPORT TextAnchor {
    quint16 replicaId = 0;
    quint32 charValue = 0;
    quint8  bias = 0;            ///< 0 = Left, 1 = Right (matches Crdt::Bias enum)

    bool operator==(const TextAnchor &) const = default;
};

}  // namespace Markoff

// BlockAnchor.h — includes TextAnchor.h
#pragma once
#include <markoff-foundation/TextAnchor.h>

namespace Markoff {

/// Stable identity for a top-level block in a parsed Markoff::Document.
/// Equal iff the underlying first-byte TextAnchors refer to the same
/// character (Lamport-equal).
struct MARKOFF_FOUNDATION_EXPORT BlockAnchor {
    TextAnchor firstByte;

    bool operator==(const BlockAnchor &) const = default;
};

}  // namespace Markoff
```

### Modifications to `MarkoffDocument`

Existing methods unchanged. Add these:

```cpp
// in MarkoffDocument.h, in the public section

// ===== Parse sequence =====
/// Locally-monotonic parse-sequence number for the most recent parse
/// delivered via parseUpdated. View-layer code uses this for parse-
/// ordering ("is this a newer parse than what I rendered?") without
/// holding a Crdt::Global. Decoupled from the CRDT version vector.
quint64 parseSequence() const noexcept;

// ===== Edit sequence =====
/// Locally-monotonic edit-sequence number that increments on every
/// state-change operation (applyLocalEdit, undo, redo, resetContent).
/// Public-boundary consumers use this for dirty-tracking ("has the
/// doc changed since the last save?") without holding a Crdt::Global.
///
/// Note the monotonic semantics: a doc that was edited and then
/// undone to a byte-equivalent of its saved state still reports a
/// different editSequence than at save time. This matches the prior
/// `version() != savedVersion` behaviour; if a future consumer wants
/// byte-equivalence dirty-tracking, that's a separate concern (e.g. a
/// content fingerprint).
quint64 editSequence() const noexcept;

// ===== Anchors (extended) =====
/// Returns a TextAnchor at the given byte offset with the given bias.
/// Companion to existing anchorAt(quint32, Crdt::Bias) — same semantics
/// but in TextAnchor terms so view layers can call without including
/// <crdt/Anchor.h>.
TextAnchor textAnchorAt(quint32 byteOffset, bool rightBias) const;

/// Resolve a TextAnchor to its current byte offset.
quint32 resolveTextAnchor(const TextAnchor &) const;

// ===== Block anchors =====
/// Returns the BlockAnchor for the top-level block containing the given
/// TextAnchor's byte position, or std::nullopt if the byte position is not
/// inside any top-level block (e.g. anchor resolves before/between/after
/// blocks). Block boundaries come from the most-recent parse — callers
/// race against parses-in-flight at their own risk; the BlockAnchor itself
/// is stable across parses regardless.
std::optional<BlockAnchor> blockAt(const TextAnchor &) const;

/// Returns the offset (in UTF-8 bytes) of the given TextAnchor's byte
/// position relative to the BlockAnchor's first byte. If the TextAnchor
/// resolves outside the block's byte range, the result clamps: negative
/// returns 0; past-end returns block's byte length.
int offsetInBlock(const BlockAnchor &, const TextAnchor &) const;

/// Returns a TextAnchor at `offset` UTF-8 bytes from the BlockAnchor's
/// first byte. Companion to textAnchorAt that operates in block-local
/// coordinates instead of doc-global ones.
TextAnchor textAnchorAt(const BlockAnchor &, int offset, bool rightBias) const;

/// Convenience: returns the BlockAnchor for the top-level block at index `i`
/// in the most-recent parse. Out-of-range returns std::nullopt.
std::optional<BlockAnchor> blockAnchorAt(int blockIndex) const;

/// Convenience: returns the byte range [start, end) of the BlockAnchor's
/// block in the current parse. End is exclusive of any structural separator
/// to the next block — i.e. the parser's reported byte range for the AST
/// node, NOT the editing-spec's "delegate range" which extends to next
/// block's start. Consumers needing the delegate-range semantics compute
/// it themselves.
std::optional<std::pair<quint32, quint32>>
    blockByteRange(const BlockAnchor &) const;
```

The existing `anchorAt(quint32, Crdt::Bias)` and `resolveAnchor(const Crdt::Anchor &)` methods stay. Foundation-internal callers continue to use them; view layers use the `TextAnchor` overloads.

### `parseUpdated` signal payload

The current signal is:

```cpp
void parseUpdated(const Markoff::Document *parsed, CollabText::Crdt::Global atVersion);
```

The new shape ships a parallel `QList<BlockAnchor>` and replaces the
CRDT version with a *parse sequence* — a locally-monotonic `quint64`
managed by the foundation, semantically distinct from the CRDT version
vector and not derived from it:

```cpp
void parseUpdated(
    const Markoff::Document *parsed,
    quint64 parseSequence,        ///< local-monotonic; foundation-managed
    QList<BlockAnchor> blockAnchors);
```

The parse sequence is *not* a flatten of `Crdt::Global`. It's a
view-layer-ordering primitive: "this is the i-th parse to return on
this MarkoffDocument instance." It supports the only ordering question
the view layer naturally asks ("is this parse newer than the last one
I rendered") without exposing or copying CRDT version-vector state on
the public boundary. This dovetails with the 2026-04-30 collabtext
join-perf handoff: every avoided `Global` copy on a public boundary
is one fewer call site that may need to fan out into a hot
`Global::join`.

CRDT-aware questions — e.g. "did this parse include the edit I just
submitted?" — are answered by foundation-internal helpers on
`MarkoffDocument` rather than by exposing `Global` to view-layer
consumers. v0 of this spec does not specify those helpers; they're
introduced when the editing spec or a downstream consumer demands
them.

`MarkoffDocument::version()` continues to return `Crdt::Global` for
foundation-internal callers; nothing on the public boundary holds
`Global`.

### Already-existing `MarkoffDocument` APIs that consumers reuse

- `applyLocalEdit(const QList<MarkoffEdit> &) → Crdt::Operation` — local writes.
- `undo()` / `redo()` / `undoDepth()` / `coalesceLastUndo()` — undo stack.
- `toMarkdownUtf8() → QByteArray` — read source bytes.
- `version() → Crdt::Global` — version (foundation-internal). Public-boundary consumers use `parseSequence() → quint64` for parse-ordering, and `editSequence() → quint64` for "has the doc changed since save?" — neither leaks `Global`.
- `anchorAt(quint32, Crdt::Bias)` / `resolveAnchor(...)` — byte ↔ anchor (CRDT-typed; foundation-internal).
- `contentsChanged(QList<MarkoffEdit>)` — change signal.
- `parsedDocument() → const Document *` — current parsed AST.

### `Selection` field type change

`Markoff::Selection` currently holds `CollabText::Crdt::Anchor anchor +
active`. The fields change to `TextAnchor`:

```cpp
// In Selection.h — drops #include <crdt/Anchor.h>.
struct Selection {
    TextAnchor anchor;
    TextAnchor active;
    // ... other fields unchanged ...
};
```

Foundation-internal call sites that today pass `sel.anchor` /
`sel.active` to `MarkoffDocument::resolveAnchor(Crdt::Anchor)` switch
to the `TextAnchor`-typed `resolveTextAnchor` overload. The ~10–15
affected call sites are in `CommandFacade.cpp`, `SearchEngine.cpp`,
`SourceTextDocumentBinding.cpp`, `Cmd/Helpers.cpp`, `Session.cpp`,
`EditorBackend.cpp`, and the qml editor-backend tests — each is a
one-line typedef-style change.

The header change (`Selection.h` no longer includes `<crdt/Anchor.h>`)
removes another transitive include of the CRDT clock/anchor surface
from view-layer compilation units.

---

## 3. Computation

**Where BlockAnchors are computed:** in `MarkoffDocument`, on the
main thread, in the lambda that relays the worker's `parsed(Document
*, generation)` signal to the public `parseUpdated`. The CRDT buffer
is held by `MarkoffDocument` and is most easily accessed from there;
giving the worker thread CRDT access would require either a
thread-safe buffer snapshot or per-byte Lamport plumbing across the
parse boundary, neither of which earns its weight at v0.

For each top-level block of the freshly-parsed `Markoff::Document`,
take the block's first byte (parser-reported byte range start), call
`anchorAt(blockStartByte, Bias::Left)` against the current CRDT
buffer to get a `Crdt::Anchor`, wrap as `TextAnchor` then
`BlockAnchor`, append to the parallel `QList<BlockAnchor>`.

**Caveat — one-cycle staleness window:** the parse arrives carrying
block byte ranges that were correct as-of the version the parse was
*scheduled* against. If the CRDT buffer has advanced between
parse-schedule and parse-return (one or more keystrokes during the
parse-in-flight window), the freshly-computed BlockAnchor for each
block identifies the *current* character at that block's first byte
rather than the parse-snapshot's character. For blocks whose first
byte was not edited during the window, this is identical. For a block
whose first byte *was* edited, the BlockAnchor for one parse cycle
identifies a slightly different character than the "ideal" anchor;
the next parse cycle delivers a corrected anchor.

This is acceptable because BlockAnchor identity is robust: the next
parse re-issues a corrected list, and the AstBlockDiff sees one row
identity-flip rather than a churn cascade. If profiling later shows
the staleness window matters in practice, the alternative is to ship
a per-byte Lamport snapshot (or a thread-safe CRDT buffer reference)
to the worker — that's a future refactor, not a v0 concern.

**Cost:** one CRDT anchor lookup per top-level block. The existing CRDT data structures support `O(log n)` byte-to-anchor translation, so a 1000-block doc adds ~10 ms of anchor computation in the worst case (in practice much less). For a 50KB typical doc with ~100 top-level blocks this is sub-millisecond and well below the parse cost.

---

## 4. Identity semantics — exhaustive cases

For each kind of source mutation, what happens to BlockAnchors:

| Mutation | First-byte anchor of affected block | New anchors introduced | Anchors orphaned |
|---|---|---|---|
| Insert character within block (not at byte 0) | Unchanged | None | None |
| Insert character at byte 0 of block (Left bias on the new char makes it part of this block) | Unchanged (the original first byte still exists, just shifted right by one position; resolveAnchor returns the new position, which is still the block's first byte) | None | None |
| Insert character at byte 0 with Right bias (CRDT-edge case) | Anchor now resolves *before* the block's first byte; new byte 0 is a new char without an anchor | One — the new block's first byte if a new block emerges from the insert | None |
| Delete character within block (not first or last byte) | Unchanged | None | None |
| Delete first byte of block | Tombstoned. Anchor still resolves to the byte position where it used to be — which is now the block's first byte (the next surviving byte). For most editing scenarios, the block now has a "new" first byte that doesn't have a BlockAnchor entry yet | Possibly one — the block needs a new BlockAnchor at its new first byte. The tombstoned old anchor becomes orphaned (no longer used). | One — the tombstoned anchor |
| Delete last byte of block | Unchanged for first-byte anchor | None | None |
| Split block (insert structural separator mid-block) | Unchanged for the upper half (which keeps the original first byte) | One — the lower half's new first byte | None |
| Merge two blocks (delete structural separator) | Surviving block keeps its first-byte anchor | None | One — the deleted block's anchor (resolves to mid-merged-block, no longer at a block start) |
| Add block at top of doc | Unchanged for existing blocks | One — the new top block's first byte | None |
| Add block at bottom of doc | Unchanged for existing blocks | One — the new bottom block's first byte | None |
| Delete an entire block | Unchanged for surrounding blocks | None | One — the deleted block's anchor |

**View-layer takeaway:** the `AstBlockDiff` consuming `(kind, BlockAnchor)` keys will see a *minimal* edit script for each of these mutations. Identity-preserving edits are just `dataChanged`; identity-changing edits are `Insert`/`Remove`/`Move`. No false positives where identity churns under content-only edits.

---

## 5. File structure & components

### Files created

```
libs/markoff-foundation/
  include/markoff-foundation/
    TextAnchor.h
    BlockAnchor.h
  src/
    AnchorConversion.h     (internal helpers Crdt::Anchor ↔ TextAnchor)
    AnchorConversion.cpp
    BlockAnchorComputation.h (internal helper for parse-pipeline integration)
    BlockAnchorComputation.cpp
  tests/
    tst_foundation_text_anchor.cpp
    tst_foundation_block_anchor.cpp
    tst_foundation_block_anchor_stability.cpp     (the §4 cases)
```

### Files modified

```
libs/markoff-foundation/
  include/markoff-foundation/
    MarkoffDocument.h         new public APIs per §2 (parseSequence,
                              textAnchorAt overloads, blockAt,
                              offsetInBlock, blockAnchorAt,
                              blockByteRange); parseUpdated signal
                              shape change
    Selection.h               field type change: anchor/active → TextAnchor
                              (drops #include <crdt/Anchor.h>)
  src/
    MarkoffDocument.cpp       implement new APIs; compute BlockAnchors in
                              the parsed→parseUpdated relay lambda;
                              maintain parseSequence counter
    Selection.cpp             update for TextAnchor field types
    CommandFacade.cpp         resolveAnchor → resolveTextAnchor at ~5 sites
    SearchEngine.cpp          resolveAnchor → resolveTextAnchor at ~2 sites
    SourceTextDocumentBinding.cpp  same shape, ~3 sites
    Cmd/Helpers.cpp           same shape, ~2 sites
    Session.cpp               selection-equality compares TextAnchor fields
                              instead of Crdt::Anchor fields
  CMakeLists.txt              new sources/tests

libs/markoff-view-qml/src/
    EditorBackend.cpp         m_selectionAnchor / m_cursorAnchor become
                              TextAnchor (drops <crdt/Anchor.h>)

libs/markoff-view-qml/tests/
    tst_view_qml_editor_backend.cpp  update to compare TextAnchor
```

`BlockAnchorComputation` is a small internal helper in
`markoff-foundation/src/`. It's invoked from the `parsed→parseUpdated`
lambda inside `MarkoffDocument` (per §3), not from the parse worker.

---

## 6. Testing strategy

**Unit tests, pure C++:**

| Test target | Coverage |
|---|---|
| `tst_foundation_text_anchor` | TextAnchor ↔ Crdt::Anchor round-trip preserves all fields. operator== identity. min/max sentinels translate correctly. |
| `tst_foundation_block_anchor` | BlockAnchor wraps TextAnchor; equality compares first-byte anchor. |
| `tst_foundation_block_anchor_stability` | The §4 table, each mutation as a test case. Set up a doc with N blocks; perform mutation X; assert the right anchors stable / new / orphaned. |

**Integration tests with parse pipeline:**

Add to existing `tst_foundation_*` suites (or new `tst_foundation_parse_block_anchors`):

- After `applyLocalEdit` + parse return, the `parseUpdated` payload's `blockAnchors` list has the right size and identities.
- Type a character in block 2 of 5; observe that block 2's BlockAnchor is unchanged across the parse return.
- Split block 3 by inserting `\n\n`; observe block 3's BlockAnchor preserved on the upper half, new BlockAnchor on lower half.
- Merge blocks 3+4 by deleting their separator; observe block 3's BlockAnchor preserved, block 4's BlockAnchor not in the new list.
- Concurrent edit simulation (apply two non-overlapping `applyLocalEdit`s, confirm anchors interleave correctly).

**Performance test:**

Add to existing `tst_realistic` or `tst_benchmark`:

- A typing session over a 50KB doc with 100 top-level blocks shouldn't add more than 1 ms of BlockAnchor computation per parse return (measured via `RenderPhaseTaps` or a simple `QElapsedTimer`).

---

## 7. Future evolution

- **Multi-cursor / secondary selections:** `Session::secondarySelections` is a `QList<Selection>` and inherits the TextAnchor field-type change automatically. Multi-cursor projection through `blockAt`/`offsetInBlock` falls out of the same APIs.
- **Worker-thread BlockAnchor compute (alternative path).** §3 keeps anchor compute on the main thread for v0. If profiling later shows the one-cycle staleness window or the per-parse main-thread cost matters, the alternative is to ship a thread-safe CRDT-buffer reference (or a per-byte Lamport snapshot) into the parse worker so it can compute anchors against the version the parse saw. Strictly a future refactor; flagged so the v0 design isn't taken as final.
- **Collaborative editing:** the CRDT foundation already supports it; BlockAnchors are stable across collaborators by the Lamport-equality identity. The view layer's `AstBlockDiff` would see the same identities for the same blocks regardless of which collaborator made the edit.
- **Inline anchors (mid-block stable refs):** the same TextAnchor type works for any byte position. If a future spec needs stable anchors on inline tokens (e.g. for mid-paragraph cursor preservation across reflows), no new types are needed — just new translation methods.
- **Inline-tree node identity:** if a future spec wants stable identity for inline AST nodes (e.g. each `**bold**` span gets a stable handle), the same pattern can apply at finer granularity. Not in scope for v0; flagged so the design doesn't preclude it.

---

## 8. Out of scope

- Performance optimisation of the BlockAnchor computation beyond the obvious "compute once per parse, in the main-thread relay lambda" (per §3). If profiling reveals hotspots — including the staleness window or main-thread budget pressure — future spec.
- API for *all* views to consume; this spec targets view-layer consumption (`markoff-view-qml`). The Source widget already uses `Crdt::Anchor` directly via `SourceTextDocumentBinding` and may continue to; if it later wants TextAnchor, that's an opportunistic touch.
- Save / load of BlockAnchors. They're ephemeral (tied to a specific CRDT replica state); they shouldn't be persisted. The `Selection` JSON serialisation already handles persistence of `Crdt::Anchor` for cursor position; no analogue is needed for `BlockAnchor`.
- Documentation for plugin authors. The view-layer spec covers it where relevant; no foundation-side public-doc work in v0.

---

## 9. References

- **Driving consumer:** `docs/specs/2026-04-30-live-editing-design.md`
- **Walking-skeleton parent (for `(kind, content-hash)` BlockKey context):** `docs/specs/2026-04-29-live-render-design.md`
- **Existing foundation API:** `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`, `Session.h`, `Selection.h`
- **CRDT anchor type:** `libs/collabtext/src/crdt/Anchor.h` (sibling-symlinked from `/home/clinton/dev/collabtext/`)
- **Anchor JSON serialisation precedent:** `libs/markoff-foundation/include/markoff-foundation/AnchorJson.h`

---

## 10. Decisions resolved (2026-04-30)

The seven open decisions the original draft left for the user were
resolved during a brainstorming pass on 2026-04-30. The §2/§3/§5
prose has been updated to reflect them; the resolutions are recorded
here for traceability.

1. **`BlockAnchor`: trivial wrapper struct, not alias.**
   `struct BlockAnchor { TextAnchor firstByte; };`. Type-distinct from
   `TextAnchor` so signatures self-document and mix-ups become compile
   errors. Cost is negligible (same layout, trivially copyable).

2. **`parseUpdated` signal: ship BlockAnchors and replace
   `Crdt::Global` (Option A).** The signal argument list grows by a
   `QList<BlockAnchor>`; the version argument changes from
   `Crdt::Global atVersion` to `quint64 parseSequence` (see §2). No
   existing production consumers of `parseUpdated` on this branch, so
   the API change is cheap.

3. **Version typing: locally-monotonic parse sequence (`quint64`),
   not a flatten of `Crdt::Global`.** The signal's version argument
   is a foundation-managed sequence number — "this is the i-th parse
   on this MarkoffDocument instance" — semantically decoupled from
   the CRDT version vector. CRDT-aware questions ("did this parse
   include the edit I just submitted?") are answered by foundation-
   internal helpers on `MarkoffDocument`; the view layer never holds
   a `Global`. This dovetails with the 2026-04-30 collabtext
   join-perf handoff: fewer `Global` copies on hot public boundaries.

4. **`Selection`: change public field types to `TextAnchor`** (not
   accessor companions). `Selection.h` drops `#include
   <crdt/Anchor.h>`. Foundation-internal call sites switch to
   `resolveTextAnchor`; ~10–15 sites total, each a one-line
   typedef-style change. View-layer call sites in `EditorBackend.cpp`
   and the qml tests update with them.

5. **Compute BlockAnchors on the main thread**, in `MarkoffDocument`'s
   `parsed → parseUpdated` relay, not on the parse worker. The CRDT
   buffer is held by `MarkoffDocument`; routing it to the worker
   would need a thread-safe snapshot or per-byte Lamport plumbing
   that does not earn its weight at v0. The trade is a one-cycle
   staleness window for blocks whose first byte was edited during a
   parse-in-flight window (see §3); the next parse cycle issues a
   corrected anchor. Revisit if profiling shows it matters.

6. **`blockAt(TextAnchor)` resolves against the most-recent-parse**,
   no versioned variant in v0. Qt's main-thread event loop sequences
   parse delivery and slot handlers, so within a single slot-handler
   stack frame, "most recent parse" is well-defined. Multi-user CRDT
   collaboration does not change this — BlockAnchor identity is
   Lamport-stable across replicas, which is the cross-replica
   invariant that matters; per-replica `blockAt` semantics are a
   local concern. Revisit if a future consumer holds a parse
   out-of-band (e.g. background analysis).

7. **Orphaned `blockAt` returns `std::nullopt`.** When the resolved
   byte falls outside any top-level block (past last block, in
   between-block whitespace, or before the first block), `blockAt`
   returns `nullopt`. Callers handle "no block here" explicitly. If
   a future consumer wants always-Some clamping semantics, add a
   separate `nearestBlockAt(TextAnchor)` then; do not preemptively
   split the API.

8. **`editSequence() → quint64` for dirty-tracking** (added during
   the cross-check against `docs/plans/2026-04-30-live-editing.md`).
   The test app's "[modified] in window title" flow records
   `lastSavedSequence = doc.editSequence()` on save and recomputes
   dirty as `doc.editSequence() != lastSavedSequence`. Distinct from
   `parseSequence()` (parse-ordering) and from `version()` (CRDT
   version vector, foundation-internal). Increments on every
   state-change op — applyLocalEdit, undo, redo, resetContent — so
   the semantics match the prior `version() !=` behaviour. Resolves
   the otherwise-conflict between decision 3 ("view layer never holds
   `Global`") and the live-editing plan's task 14 (which reached for
   `doc.version()` to drive dirty-tracking).

---

## 11. The bet, restated

`BlockAnchor = TextAnchor at the block's first byte` is the simplest possible answer that gets us CRDT-anchor identity for the editing spec and unblocks the entire Live editing work. It's mostly type-safety scaffolding; the hard CRDT primitive already exists. The view layer gets a header-clean way to consume CRDT-stable identities; the foundation gets one new opaque type to maintain. This unlocks not just live editing but every future view that wants stable per-block handles — and it's the right architectural shape for collaborative editing when that lands.
