# BlockAnchor (foundation) — design

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Phase:** Foundation API extension; precondition for `docs/specs/2026-04-30-live-editing-design.md`
**Status:** ready for the next agent to plan + implement
**Authoring context:** This spec was written in a single sitting against the editing-spec brainstorm transcript; it has not been brainstormed Q-by-Q with the user. The implementing agent should run a quick brainstorming pass with the user to confirm the open decisions called out in §10 before writing the implementation plan.

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

This signal is consumed by view-layer code that includes `<crdt/Clock.h>` for `Global`. To keep the view-layer header-free of CRDT, the spec adds:

**Option A — additional argument:** the signal payload includes a parallel `QList<BlockAnchor>` whose i-th element is the BlockAnchor for the i-th top-level block of `*parsed`.

```cpp
void parseUpdated(
    const Markoff::Document *parsed,
    quint64 atVersion,            ///< replaces Crdt::Global with a stable opaque id
    QList<BlockAnchor> blockAnchors);
```

Note the secondary change to drop `Crdt::Global` from the signal — replace with a `quint64` (or another opaque type) so the signal header is CRDT-free. View-layer code uses `atVersion` only for ordering (compare against `MarkoffDocument::version()` which would also need a non-CRDT companion accessor).

**Option B — separate accessor:** keep `parseUpdated`'s shape; add `MarkoffDocument::blockAnchorsForCurrentParse() → QList<BlockAnchor>` that consumers call after receiving the signal.

**Recommendation: A.** It saves consumers a round-trip; it makes the parse output self-contained. The view-layer header-free goal is also better served because consumers don't need to call back to MarkoffDocument for anchors.

The version typing question (`Crdt::Global` vs `quint64`) needs a separate small decision — `Crdt::Global` is currently exposed; replacing it across the API is broader scope. The implementing agent should brainstorm with the user on whether to do that here or defer it.

### Already-existing `MarkoffDocument` APIs that consumers reuse

- `applyLocalEdit(const QList<MarkoffEdit> &) → Crdt::Operation` — local writes.
- `undo()` / `redo()` / `undoDepth()` / `coalesceLastUndo()` — undo stack.
- `toMarkdownUtf8() → QByteArray` — read source bytes.
- `version() → Crdt::Global` — version (needs a non-CRDT companion if the version typing question is resolved per Option A).
- `anchorAt(quint32, Crdt::Bias)` / `resolveAnchor(...)` — byte ↔ anchor (CRDT-typed; foundation-internal).
- `contentsChanged(QList<MarkoffEdit>)` — change signal.
- `parsedDocument() → const Document *` — current parsed AST.

### `Selection` companion accessors

`Markoff::Selection` currently holds `CollabText::Crdt::Anchor anchor + active`. View-layer consumption needs TextAnchor companions:

```cpp
// In Selection.h
struct Selection {
    // ... existing fields stay
    CollabText::Crdt::Anchor anchor;
    CollabText::Crdt::Anchor active;
    // ...

    /// Companion accessors for view-layer code that can't include <crdt/Anchor.h>.
    TextAnchor anchorAsText() const;
    TextAnchor activeAsText() const;
};
```

Or — preferably — make `Selection`'s public fields TextAnchor-typed and translate internally where needed. The implementing agent should choose; the public API change has slightly more reach. If the latter is chosen, callers that currently take `Crdt::Anchor` from `Selection` need updating.

---

## 3. Computation

**Where BlockAnchors are computed:** in the foundation, on the worker thread, as part of the parse pipeline (`IncrementalParseSession` or `ParsePoolWorker`). For each top-level block of the freshly-parsed `Markoff::Document`, take the block's first byte (parser-reported byte range start), call the equivalent of `anchorAt(blockStartByte, Bias::Left)` to get a `Crdt::Anchor`, wrap as `TextAnchor` then `BlockAnchor`, append to the parallel `QList<BlockAnchor>`. The list is shipped with the `parseUpdated` signal.

**Caveat — parse staleness:** the parse computes against a snapshot of the source as of `atVersion`. If the source has mutated since (typing during parse), the BlockAnchors are still correct — they identify the blocks in the parse's snapshot. The view layer treats `parseUpdated` as ground truth as-of `atVersion`; reconciliation with later local edits is a view-layer concern (the editing spec discusses it).

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
    MarkoffDocument.h         new public APIs per §2
    Selection.h               new TextAnchor accessors (or field type change)
  src/
    MarkoffDocument.cpp       implement new APIs
    IncrementalParseSession.cpp OR ParsePoolWorker.cpp  compute BlockAnchors per parse
    ParsePool.cpp             pass BlockAnchor list through
    Selection.cpp             implement TextAnchor accessors
  CMakeLists.txt              new sources/tests
```

The implementing agent picks whether `IncrementalParseSession` or `ParsePoolWorker` is the right home for `BlockAnchorComputation` invocation; both are on the worker thread.

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

- **Multi-cursor / secondary selections:** `Session::secondarySelections` already uses CRDT anchors. Same TextAnchor wrapping pattern applies; same translation APIs. No new work.
- **Collaborative editing:** the CRDT foundation already supports it; BlockAnchors are stable across collaborators by the Lamport-equality identity. The view layer's `AstBlockDiff` would see the same identities for the same blocks regardless of which collaborator made the edit.
- **Inline anchors (mid-block stable refs):** the same TextAnchor type works for any byte position. If a future spec needs stable anchors on inline tokens (e.g. for mid-paragraph cursor preservation across reflows), no new types are needed — just new translation methods.
- **Inline-tree node identity:** if a future spec wants stable identity for inline AST nodes (e.g. each `**bold**` span gets a stable handle), the same pattern can apply at finer granularity. Not in scope for v0; flagged so the design doesn't preclude it.

---

## 8. Out of scope

- Performance optimisation of the BlockAnchor computation beyond the obvious "compute once per parse on the worker thread". If profiling reveals hotspots, future spec.
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

## 10. Open decisions for the implementing agent

These are decisions I made tentatively but where the user should weigh in via a short brainstorm before locking. The implementing agent should run a clarifying-questions pass on each before writing the implementation plan:

1. **Strong-typedef vs trivial wrapper for `BlockAnchor`.** I picked a trivial wrapper struct (one field: `firstByte: TextAnchor`) for type safety. Alternative: `using BlockAnchor = TextAnchor;` for zero-cost simplicity at the cost of not catching mix-ups at compile time. Recommend trivial wrapper unless the user prefers minimal API surface.

2. **`parseUpdated` signal change scope (Option A vs B in §2).** I recommended A (additive arg + drop CRDT type from signal). B is more conservative (additive accessor only, signal unchanged). User should weigh: header-cleanliness vs signal-stability for any current consumers.

3. **`Crdt::Global` vs `quint64` for `version`.** Tied to the above. If we keep `Global`, the view-layer header issue forces a companion accessor. If we replace with `quint64` (or another stable opaque type), we touch every consumer of `version()` and `parseUpdated`. Recommend the opaque-id replacement *if* the user wants strict view-layer header purity; otherwise keep `Global`.

4. **`Selection` field type vs accessor methods.** I leaned toward accessor methods (TextAnchor companions) to avoid breaking existing `Selection` consumers. Alternative: change the public field types to `TextAnchor` and translate internally. Bigger blast radius; cleaner API afterwards. User decision.

5. **Where in the parse pipeline does BlockAnchor computation live?** `IncrementalParseSession` knows the parsed `Document`; `ParsePoolWorker` is the wider container. Either is plausible; pick whichever has cleaner access to both the parser's block iterator and the CRDT buffer's anchor lookup.

6. **Should `blockAt(TextAnchor)` use the parsed doc as-of the most recent parse, or as-of a passed-in version?** The spec says "most recent parse". For most uses that's right. If a consumer holds a stale parse and asks `blockAt`, we'd return a BlockAnchor from a newer parse — fine for identity but possibly off-by-one for byte ranges. The implementing agent should think about whether to expose a versioned variant for race-sensitive consumers; for v0 the simple form is probably enough.

7. **Handling of orphaned BlockAnchors.** When a block's first-byte char is deleted (a tombstone case), the anchor still resolves but to an unhelpful position. The view-layer diff naturally drops the row (no block in the new tree starts there), but a clarifying question for the user: should `blockAt(orphanedAnchor)` return `std::nullopt` (current spec text) or return the *next* block whose byte range follows? Either is defensible; nullopt is simpler.

---

## 11. The bet, restated

`BlockAnchor = TextAnchor at the block's first byte` is the simplest possible answer that gets us γ-CRDT-anchor identity for the editing spec and unblocks the entire Live editing work. It's mostly type-safety scaffolding; the hard CRDT primitive already exists. The view layer gets a header-clean way to consume CRDT-stable identities; the foundation gets one new opaque type to maintain. This unlocks not just live editing but every future view that wants stable per-block handles — and it's the right architectural shape for collaborative editing when that lands.
