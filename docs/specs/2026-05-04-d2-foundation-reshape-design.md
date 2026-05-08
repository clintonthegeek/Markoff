# D2 — Markoff foundation reshape (per-block CRDT)

**Date:** 2026-05-04
**Branch:** `exploration/new-foundation`
**Status:** Approved for plan derivation (brainstorming complete; writing-plans is the next step).
**Audience:** the implementer who picks this up; the user (review gate); future fresh-context agents picking up D3, D4, or D5.
**Predecessors (read first, in order):**

1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — the D-arc orientation doc; reads first.
2. `docs/specs/2026-05-02-d-evolution-proposal.md` — the proposal Markoff sent to collabtext maintainers (the architectural premise behind D).
3. `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` — the maintainer commitment to Option β + the six "won't do" lines D2 must respect.
4. `docs/archive/c-restoration-arc/2026-05-04-c-restoration-bookend-d-pivot.md` — the cut from C-restoration to D, what carries forward, what doesn't.
5. `docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` — the C-restoration spec; its lower-layer decisions (L0–L3, the discriminated cursor model, the layered library structure, the per-layer test contract) carry forward to D2 as authoritative inputs.

**Supersedes (in scope):**

- `docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` §4 (source-of-truth protocol; sequence-tagged staleness retires under D2 — there is no async parse race to gate against).
- `docs/archive/c-restoration-arc/2026-05-03-marker-paragraph-design.md` (already retired by the bookend; D2 deletes its mechanism by retiring the parser-vs-CRDT race that motivated it).

**Convention used throughout this spec.** Every major section ends with a **"Why this and not the alternatives"** subsection recording the options considered and the reason for the choice. The reasoning is preserved in writing so future contexts (especially fresh-context agents picking up D3/D4/D5) can audit decisions without re-running the brainstorm.

**Operating constraint, recorded once for D2 → D5.** The collabtext maintainers committed to Option β with six explicit "won't do" lines that bind every piece of the D arc:

1. No `moveAfter` in `IdList` v1 (express moves as remove + insert).
2. No per-element values in `IdList`, ever.
3. No cross-CRDT undo log primitive in collabtext (application owns it).
4. No cross-CRDT GC coordination primitive in collabtext (application owns watermark coordination).
5. No `CollabDocument` generalization (Markoff composes directly on `IdList` + `Buffer` + existing `StreamSync`).
6. No precedent for further primitives (`Map`, `Counter`, `Tree`, etc. would be separate proposals; the answer may be "build outside collabtext or fork").

Recorded canonically at `docs/d-arc/collabtext-scope-line.md`.

---

## 0. TL;DR

The Markoff foundation library `markoff-foundation` is reshaped in-place from a single-CRDT-rope-with-async-parser model to a per-block-CRDT model coordinated by a structural CRDT. `Markoff::MarkoffDocument` retains its name and most of its public API surface; internals swap entirely.

Underneath the public type, `MarkoffDocument` composes one `CollabText::Crdt::IdList` (block ordering and structural ops) with one `CollabText::Crdt::Buffer` per block (content) plus six Markoff-owned causal-LWW sibling maps (kind tag, per-block attrs, frontmatter, link reference defs, footnote defs, and the application-owned undo log's metadata layer).

The edit API is two-layer: a low-level `applyBlockEdit(BlockEdit)` + `applyStructural(StructuralOp)` for the keystroke hot path; a high-level `Cmd::*` namespace for compound user intents (Enter, Backspace-merge, paste, kind change). Every `Cmd::*` call opens a transaction; every CRDT op emitted during it gets the same `actionId`; on close, an `UndoEntry` records the touched CRDTs in op-order. Document undo pops the latest entry and dispatches `.undo()` to each target in reverse op-order. Per-block undo finds the most recent entry whose targets *include* this block and dispatches the entire entry — atomic at the transaction level, expectation-matching at the API.

Save is touch-aware: each per-block `Buffer` carries its load-time bytes; unmutated blocks save bytes-verbatim; mutated blocks save canonically via per-kind serializers. Per-block inline parsing is synchronous on read, cached per `BlockId` and invalidated by per-block edit-counter. GC is save-triggered: after a successful fsync, the watermark advances and `compact()` dispatches to every CRDT.

The result is an architecture where block boundaries are structural-CRDT state rather than parser-decided, the parser is no longer in the keystroke hot path, per-block undo is natural, and plugin-authored block kinds with non-Markdown content compose cleanly. The C-restoration's lower-layer decisions (discriminated cursor, layered library, inline span pre-bake) carry forward unchanged; its sequence-tagged staleness mechanism (L4 freshness gate, three cycle guards, focus-delivery retry loops, marker-paragraph machinery) all retire — the underlying race they fought no longer exists.

---

## 1. Premises (binding)

These decisions came out of the 2026-05-04 brainstorming round and are inputs to this design, not outputs. They are quoted here so the design's conclusions are auditable against its premises.

| # | Decision | Resolution |
|---|---|---|
| 1 | **Scope** | D2 only — the foundation reshape. D3 (view-layer adaptation), D4 (parser scope reduction), D5 (collab activation) are stubbed in `docs/specs/` and orient an arc-roadmap, but their substantive design happens later. |
| 2 | **Foundation shape** | A: in-place evolution of `MarkoffDocument`. Public type name preserved; internals swap; public API mirrors where the analogue is exact, reshapes where the document-level model breaks. |
| 3 | **Kind-tag storage and merge rule** | A: causal-LWW sibling map keyed by `BlockId`. Same pattern applies to other doc-level sibling state (frontmatter, link refs, footnote defs, per-block attrs). |
| 4 | **Block granularity** | B: first-class `list-item` entries in `IdList`; sublists deferred (a `list-item` containing a nested list is one Buffer holding the indented sublist source). Lists are runs of consecutive `list-item` entries; tight/loose + marker style live in `BlockAttrsMap`. Tables follow the same deferred-recursion pattern. |
| 5 | **Edit API** | C: two-layer. Low-level per-block + structural primitives; high-level `Cmd::*` for compound intents. View layers pick per use case. |
| 6 | **Undo log** | B: Markoff-owned `UndoEntry(actionId, targets[])`. Each `Cmd::*` opens an RAII transaction; ops emitted during it share an `actionId`; on close, the `UndoEntry` records targets in op-order. |
| 6a | **Per-block undo permissiveness** | Permissive: pop the most recent entry whose targets *include* this block; dispatch the whole entry atomically. (Resolved via the "rigor below the API, expectation-matching at the API surface" principle — see §4.5.) |
| 7 | **Save/load round-trip** | C: touch-aware. Each per-block `Buffer` carries `loadTimeBytes`; unmutated blocks save bytes-verbatim; mutated blocks save canonically. New blocks always canonical. |
| 8 | **Per-block inline parse** | A: synchronous on read, per-block `InlineParseCache` keyed by per-block edit-counter. Calling thread runs the parse on cache miss. |
| 9 | **Garbage collection** | A: save-triggered. After successful fsync, advance the watermark and dispatch `compact()` to every CRDT. Trigger predicate evolves to `&& all-replicas-ack'd` for D5; mechanism unchanged. |
| 10 | **Signal and counter API** | B: per-CRDT signals primary; derived `documentEditSequence()` and `documentChanged` for convenience. `parseUpdated`, `parseSequence`, `parseInputEditSeq` retire entirely. |

---

## 2. Architecture

### 2.1 Composition

```
MarkoffDocument                 (public type, name preserved)
├─ IdList<uint64>               (collabtext)  — block ordering, structural ops
├─ map<BlockId, Buffer>         (collabtext)  — per-block content CRDTs
├─ KindTagMap                   (Markoff)     — causal-LWW: BlockId → BlockKind
├─ BlockAttrsMap                (Markoff)     — causal-LWW: (BlockId, AttrName) → AttrValue
├─ FrontmatterMap               (Markoff)     — causal-LWW: FrontmatterKey → FrontmatterValue
├─ LinkRefMap                   (Markoff)     — causal-LWW: LinkRefId → (URL, title)
├─ FootnoteDefMap               (Markoff)     — causal-LWW: FootnoteId → content
├─ UndoLog                      (Markoff)     — UndoEntry(actionId, targets[]); coalescing rule
├─ WatermarkCoordinator         (Markoff)     — save-triggered compact dispatch
└─ InlineParseCache             (Markoff)     — per-BlockId, edit-counter-keyed; sync on read
```

### 2.2 The sibling-map pattern

Five of the Markoff-owned components (KindTagMap, BlockAttrsMap, FrontmatterMap, LinkRefMap, FootnoteDefMap) share one underlying primitive: a templated `Markoff::CausalLwwMap<Key, Value>` that stamps each entry with a (replica-id, local-counter) tuple drawn from the relevant CRDT's clock. Concurrent writes to the same key resolve by stamp comparison — highest stamp wins.

```cpp
namespace Markoff {

template <typename Key, typename Value>
class CausalLwwMap {
public:
    explicit CausalLwwMap(uint16_t replicaId);

    void set(const Key &, Value, CausalStamp);
    std::optional<Value> get(const Key &) const;
    void remove(const Key &, CausalStamp);

    using ChangeCallback = std::function<void(const Key &, std::optional<Value> oldV, std::optional<Value> newV)>;
    void setOnChange(ChangeCallback);

    void undo();
    void redo();
    void compact(CausalStamp watermark);
    void applyRemote(const RemoteOp &);
};

}  // namespace Markoff
```

One implementation, five instantiations. The primitive is small (~150 LOC + tests) and isolates all "how do sibling maps merge?" reasoning into one place.

### 2.3 Lifecycle

- **Construction:** `MarkoffDocument(replicaId)` — empty document with empty `IdList`, no per-block Buffers.
- **Load:** `loadFromMarkdown(QByteArray src)` — see §5.1.
- **Edit (low-level):** `applyBlockEdit(BlockEdit)` and `applyStructural(StructuralOp)` — see §4.
- **Edit (high-level):** `Cmd::*(...)` namespace functions — see §4.
- **Save:** `save(path)` — see §5.2.
- **Undo:** `undo()`, `redo()`, `undoForBlock(BlockId)` — see §4.4 / §4.5.
- **GC:** triggered post-save via `WatermarkCoordinator` — see §7.
- **Destruction:** RAII; per-block Buffers `unique_ptr`'d from the per-block map.

### 2.4 Why this and not the alternatives

In-place evolution of `MarkoffDocument` (rather than a parallel `Markoff::DocumentStructure` type or a facade-pattern inversion) is right because nothing currently *ships* against the old API surface — `markoff-live-render` is in-tree and unreleased, the C-restoration is bookended, and breaking-change migration is exactly what the foundation-exploration branch was created to enable. Parallel structure would add migration overhead (two type hierarchies, two test suites) for no live consumer that benefits. Facade-pattern inversion would force the synthesis layer to fake document-level edits as multi-block ops, reintroducing the document-level-byte-offset thinking D was supposed to eliminate.

The five-sibling-map pattern (rather than a single all-purpose Markoff-owned CRDT framework) honors the collabtext maintainers' "no precedent for further primitives" line at the Markoff-internal level too — one templated primitive, five instantiations, no escalation to "a generalized Markoff CRDT framework."

---

## 3. Public types

### 3.1 Type table

| Type | Today | D2 | Notes |
|---|---|---|---|
| `BlockId` | wraps `Markoff::BlockAnchor` (CRDT anchor at block's first byte) | wraps `IdList::Anchor` (an opaque uint64 with bias) | More stable than today — survives intra-block remote edits without re-resolution. The view layer does not inspect, construct, or compare its internals. |
| `TextAnchor` | wraps `Crdt::Anchor` on the document-level `Buffer` | wraps `(BlockId, Crdt::Anchor)` on the relevant per-block `Buffer` | The two-level composition matches the discriminated cursor model in C-spec §3. |
| `Selection` | composes two `TextAnchor`s | composes two `Cursor`s — discriminated union per C-spec §3.1 (`TextCaret \| BlockSelected \| BlockInternalEdit`) | Carries forward from C-restoration unchanged. |
| `BlockEdit` | — (new) | `{blockId, withinBlockByteOffset, removedBytes, insertedUtf8}` | Low-level edit primitive. |
| `StructuralOp` | — (new) | variant: `InsertEntry{afterBlockId, kind}` \| `RemoveEntry{blockId}` \| `ChangeKind{blockId, newKind}` | The three primitive structural verbs. |
| `MarkoffEdit` | document-byte `{offset, removed, inserted}` | **retires** | Sole consumer was the C-era doc-level apply path. |
| `Cmd::*` namespace | doc-level semantic helpers translating to `MarkoffEdit` | preserved as the high-level layer (§4) | Each function opens an `UndoLog` transaction and decomposes intent into the appropriate mix of `BlockEdit` + `StructuralOp`. |
| `Theme`, `LinkService`, `CompletionRegistry`, `Origin`, `Cmd` | preserved | preserved unchanged at the type level | Internals adapt to per-block iteration; public types unchanged. |
| `RenderedBlock`, `RenderPhases` | view-render-phase types | likely retire | D3 view layer reshape will own block rendering; flagged for D3 review, not deleted in D2. |
| `parseUpdated` signal | doc-wide parse delivery | **retires** | Replaced per §8. |
| `editSequence`, `parseSequence` | document-wide counters | per-block counters primary; `documentEditSequence()` derived | `parseSequence` retires (no doc-level parses to count). |

### 3.2 The `BlockKind` enum

A small, stable enum covering kinds that materialize as visible `IdList` entries:

```cpp
enum class BlockKind : uint8_t {
    Paragraph,
    Heading,
    CodeBlock,
    ListItem,
    BlockQuote,
    HorizontalRule,
    Image,
    Math,
    Mermaid,
    HtmlBlock,
    Table,
};
```

Per-kind metadata (heading level, code-fence info string, list marker style + tight/loose, image src+alt, math display-vs-inline, table column count) lives in `BlockAttrsMap` keyed by `(BlockId, AttrName)`. Plugin block kinds (future, post-D5) add new enum values via a registration mechanism scoped in D3.

**Parser-emitted kinds that do NOT become `BlockKind` entries:**

- `LinkReferenceDefinition` — populates `LinkRefMap`, not `IdList`.
- Frontmatter — populates `FrontmatterMap` before block parsing begins.
- Footnote definitions — populate `FootnoteDefMap`.
- Tree-sitter's `Other` (parser fallback for unclassified content) — wrapped as a `Paragraph` with the original bytes preserved verbatim, on the principle that touch-aware save will preserve unedited "weird" content as-is.

### 3.3 Why this and not the alternatives

`BlockId`-as-`IdList`-element-id (rather than retaining today's `BlockAnchor`-at-first-byte) is right because the structural CRDT now owns block identity natively — a `BlockId` is literally the structural CRDT's element id, with stability guaranteed by `IdList`'s anchor model. The old `BlockAnchor` had to re-resolve through a CRDT anchor on every doc edit; the new `BlockId` is stable until the block is fully deleted from the structural CRDT.

Splitting `MarkoffEdit` into `BlockEdit` + `StructuralOp` (rather than retaining a unified document-level edit type) is right because the underlying CRDT model genuinely has two distinct edit kinds. A unified type would either be a discriminated union (the same shape, different name) or it would force document-level byte arithmetic that D was designed to eliminate.

The kind-tag-vs-attrs split (small enum + sibling attrs map) keeps the kind enum stable across plugin additions while letting per-kind metadata grow. A monolithic kind tag with embedded attrs (e.g. `heading-1`, `heading-2`, `list-item-tight-bullet-asterisk`) would explode the enum and tangle merge rules.

---

## 4. The edit and undo system

### 4.1 Two layers

```
View layer
   │
   ├── keystroke hot path ────────► doc.applyBlockEdit(BlockEdit)
   │                                   ↓
   │                                Buffer.applyLocal(...)
   │                                UndoLog: open transaction(actionId), record target, close
   │
   └── compound intents ──────────► Cmd::enterAtEnd(blockId)
                                       ↓
                                    UndoLog::transaction t(actionId);
                                       IdList.insertAfter(...)        ── targets += idList
                                       KindTagMap.set(newId, kind)    ── targets += kindTagMap
                                       allocate Buffer for newId      ── targets += newBuffer
                                       request cursor delivery
                                    /* destructor closes transaction */
                                       ↓
                                    UndoEntry{actionId, targets[idList, kindTagMap, newBuffer]}
```

**Every edit, low-level or high-level, opens a transaction.** Low-level direct calls produce single-target entries (degenerate transaction); `Cmd::*` calls produce multi-target entries. The `UndoLog::transaction` is RAII — constructor opens, destructor closes and commits one entry. Nesting is allowed: a `Cmd::*` that internally calls another `Cmd::*` joins the outer transaction rather than producing two.

### 4.2 Transaction lifecycle

```cpp
namespace Markoff {

class UndoLog {
public:
    class Transaction {
    public:
        // Opens a new transaction (or joins the currently-open one if nested).
        explicit Transaction(UndoLog &, ActionKind = ActionKind::Generic);
        // Destructor closes; if outermost, commits one UndoEntry.
        ~Transaction();

        // Each CRDT op emitted during the transaction calls this to register
        // itself as a target. Idempotent per (target, op-id).
        void registerOp(CrdtTarget target, OpId opId);

        // Cancel the transaction without commit (rare; used for failed
        // multi-op compounds that need to roll back).
        void rollback();
    };

    void undo();                      // §4.4
    void redo();
    void undoForBlock(BlockId);       // §4.5
    void redoForBlock(BlockId);

    void setCoalescePolicy(CoalescePolicy);
};

}  // namespace Markoff
```

`CrdtTarget` is a discriminated reference: `BufferTarget(BlockId)`, `IdListTarget`, `KindTagMapTarget`, `BlockAttrsMapTarget`, `FrontmatterMapTarget`, `LinkRefMapTarget`, `FootnoteDefMapTarget`. Each can dispatch its own `.undo(opId)`.

### 4.3 Coalescing

Before opening a fresh transaction for a printable-character `BlockEdit`, the log checks: previous entry's single target is the same `Buffer`, previous entry was also a printable-character edit, focus context unchanged since the previous edit, and last edit was <1000 ms ago? If yes, **extend the previous entry's `targets[]`** rather than commit a new one.

Non-printables, structural ops, focus changes, paste, kind changes, and 1000 ms idle break the chain. Identical to the C-restoration's `UndoCoalescer` rule — just relocated to live inside the foundation's `UndoLog` rather than the view layer.

The 1000 ms threshold is fixed in D2 (matches today's value); exposing it as a Setting is out of scope.

### 4.4 Document undo

`MarkoffDocument::undo()`:

1. Pop the latest `UndoEntry` from the stack.
2. For each target in **reverse op-order**, dispatch `target.undo(opId)`.
3. Push the inverse onto the redo stack as one entry.

`IdList`, `Buffer`, and the five sibling maps each provide their own `undo()`/`redo()` per their own causal models. Markoff just dispatches; correctness within a CRDT is the CRDT's job.

### 4.5 Per-block undo (permissive)

`MarkoffDocument::undoForBlock(BlockId)`:

1. Find the most recent `UndoEntry` whose `targets` *include* this block's `BufferTarget(BlockId)`.
2. Dispatch `target.undo(opId)` to each target in reverse op-order — same mechanism as document undo, restricted only by which entry it picks.
3. Push the inverse onto the redo stack.

The cost: a per-block undo may have document-shape effects — a block disappearing (if the matched entry was the Enter that created it) or content moving in from a neighbor (if the matched entry was a Backspace-merge that pulled content from this block). This is the right behavior — those structural changes are part of the same atomic user action that touched this block; undoing the action restores the prior state in full.

### 4.6 Worked decompositions

| User intent | Transaction body (in op-order; closes one transaction) |
|---|---|
| Type "x" mid-paragraph | `applyBlockEdit{block, off, 0, "x"}` → `BufferTarget(block)` (low-level direct; degenerate txn; coalesces with prior printable in same focus) |
| Enter at end of paragraph | `IdList::insertAfter(thisBlock, newId)` → `KindTagMap::set(newId, Paragraph)` → allocate empty `Buffer` for `newId` → `cursorState.requestTextCaretAtNewBlock(newId, 0)` |
| Backspace at start of block N | `Buffer[N-1].applyLocal(append(Buffer[N].text()))` → `KindTagMap::remove(N)` → `IdList::removeAt(N)` → defer `Buffer[N]` disposal to next watermark (§7.3) → cursor to (N-1, old-end-of-N-1) |
| Promote paragraph to heading via leading `# ` | `KindTagMap::set(thisBlock, Heading)` → `BlockAttrsMap::set((thisBlock, "level"), 1)` → `Buffer.applyLocal(remove(0, 2))` (strip the `# ` source-marker) |
| Split list at item N (Enter on empty list-item N) | `IdList::removeAt(N)` → `KindTagMap::remove(N)` → `IdList::insertAfter(N-1, newPara)` → `KindTagMap::set(newPara, Paragraph)` → allocate empty Buffer for `newPara` → cursor to `(newPara, 0)` |
| Paste a 3-block markdown chunk into mid-paragraph | parser parses paste source → split current block (`Buffer.applyLocal(splitAt(qtPos))` plus structural for the split tail) → for each parsed paste block: `IdList::insertAfter` + `KindTagMap::set` + `BlockAttrsMap::set` for any attrs + `Buffer` allocated and populated |

Every row is one `UndoEntry`; one Ctrl-Z reverts the whole row.

### 4.7 Remote edits and `Origin`

The existing `Origin` enum (already in `markoff-foundation/include/markoff-foundation/Origin.h`) is preserved: `Local`, `Remote`, `Undo`, `Redo`, `Load`, `Programmatic`.

**Only `Origin::Local` ops produce `UndoEntry`s.** Remote ops apply to their target CRDT directly via `applyRemote(...)`, never enter the local undo log. Each CRDT's own merge logic handles concurrent local-vs-remote correctness within its scope; cross-CRDT remote-vs-local interaction is collab-time work scoped to D5.

Edge case: a remote op deletes a block while a local transaction is mid-flight against that block. The local transaction's targets still include the now-orphaned `BufferTarget`. Resolution: the per-CRDT undo no-ops cleanly when its target is gone; other targets in the same entry undo correctly; net effect is "undo restored what the local edit changed where it could; the deleted block stays deleted." Matches collab user intuition.

### 4.8 Why this and not the alternatives

The two-layer edit API (rather than low-level only or high-level only) matches today's foundation layering — `MarkoffDocument::applyLocalEdit` + `Cmd::*` exists today for the same reason — and lets the keystroke hot path stay lean while compound intents are authored once in the foundation rather than re-implemented per view backend (Qt Widget, future TUI, etc.).

The Markoff-owned `UndoLog` (rather than per-CRDT stacks alone) is required because compound user actions cross multiple CRDTs and the user-facing semantics of "Ctrl-Z reverses one user action" demand atomic grouping. Per-CRDT-only would break for any compound intent (Enter, Backspace-merge, paste, kind change) — which is most user actions above plain typing.

Permissive per-block undo (rather than strict refusal on multi-target entries) follows the principle that **rigor lives below the API surface, expectation-matching lives at the API.** Strict per-block undo would express substrate properties (atomicity boundaries, transaction targets) as user-facing semantics — the user doesn't care that an Enter touched the IdList; they care that the new block they made appeared, then they typed in it, and now they want to undo that. The right rule treats the transaction as the atomic unit of undo, regardless of how many CRDTs it touches; per-block undo is just a filter on which entry to pop. Substrate guarantees the transaction is reversible in full; the API exposes "undo this user action" as one primitive.

Coalescing-by-extending-the-previous-entry (rather than committing then merging) keeps the undo stack representation simple — the stack only ever grows by one per user action. The C-restoration's `UndoCoalescer` lived view-side and had to coordinate with the document-level undo; relocating it into `UndoLog` consolidates the policy into one place.

---

## 5. Load and save

### 5.1 Load

`MarkoffDocument::loadFromMarkdown(QByteArray src)`:

1. **Frontmatter and footnote extraction.** `Markoff::Document::extract(src)` (existing) strips frontmatter, harvests footnote metadata. Body bytes pass through; populates `FrontmatterMap` and `FootnoteDefMap`.
2. **Top-level parse.** Tree-sitter parses the body for top-level blocks (existing `Document::topLevelBlocks`). At load time the parser is full-fat; D4 trims the surface, but D2 just consumes it as today.
3. **Per-block materialization.** For each top-level block:
   - **List unwrapping.** If the parser yields a `ListTight` or `ListLoose` container, expand it: emit one `list-item` `BlockId` per item; record marker style (`bullet-asterisk` / `bullet-dash` / `bullet-plus` / `ordered-period` / `ordered-paren` + start number) and tight/loose flag in `BlockAttrsMap`. The container itself does not materialize as a `BlockId` — runs of consecutive `list-item` entries ARE the list (per the granularity decision).
   - **Other kinds.** One `BlockId`, kind from parser into `KindTagMap`, attrs (heading level, fence info string, image src+alt, etc.) into `BlockAttrsMap`, content bytes into a fresh per-block `Buffer`.
   - **Load-time bytes.** Each `Buffer` records its source slice as `loadTimeBytes` via `Buffer::setLoadTimeBytes(QByteArray)`. Set-once; later mutations don't update it; touch-aware save (§5.3) reads it.
4. **Link reference defs.** Parsed `LinkReferenceDefinition` blocks populate `LinkRefMap` rather than appearing as `BlockId` entries. (Their presence in the source determines render-time link resolution; they're document-level metadata, not visible block content.)
5. **Edit-counter baseline.** Each CRDT records its current sequence as the load baseline. Anything above this counts as "post-load" — used by the touch test (§5.3).
6. **Signal.** Emit a single `documentLoaded` signal (one-shot; not an `editSequence` bump).

The parser is invoked exactly once. After load, no document-wide parser invocation ever happens again until the next `loadFromMarkdown` (i.e., file reload).

### 5.2 Save

`MarkoffDocument::save(const QString &path)`:

1. **Frontmatter.** `serializeFrontmatter(FrontmatterMap)` → bytes. Output buffer begins with this.
2. **Walk `IdList` in order.** For each `BlockId`:
   - Look up kind in `KindTagMap`, attrs in `BlockAttrsMap`.
   - **Touch test** (see §5.3). If untouched, emit `Buffer.loadTimeBytes` verbatim. If touched, emit `kindSerializer(kind, attrs, Buffer.text())`.
3. **Inter-block separators.** Join with kind-pair-aware separators:
   - Most pairs: `\n\n`.
   - Two consecutive `list-item` entries with matching marker style and tight semantics: `\n` (tight) or `\n\n` (loose).
   - `LinkRefDef` chunks (from `LinkRefMap`) and footnote defs (from `FootnoteDefMap`) appended at the end with `\n\n` separators.
4. **Atomic write.** `QSaveFile` (write-temp + rename) to `path`. If the write fails (disk full, permission, etc.), abort: do NOT advance the watermark, do NOT trigger compact. Caller sees the failure via `save()`'s return.
5. **fsync.** After successful write, call `fsync()` on the destination (or platform-equivalent durability barrier).
6. **Trigger GC.** On successful fsync return, hand off to `WatermarkCoordinator::onSaveSucceeded()` per §7.

Built-in `BlockSerializer`s for paragraph / heading / code-block / list-item / blockquote / hr / image / math / mermaid / html-block / link-ref / table. Each is a free function: `QByteArray serialize(BlockKind, const BlockAttrs &, const QByteArray &content)`. Plugin block kinds register their own at the kind-registration site (D3 will own the registry interface; for D2 the built-in serializers are inline in `Markoff::Detail::Serializers`).

### 5.3 Touch test

A block is considered **untouched** if and only if all the following hold:

- Its per-block `Buffer.editSequence()` equals the load baseline (no local or remote ops since load).
- Its `KindTagMap[BlockId].stamp` equals the load baseline (kind has not been changed).
- No `BlockAttrsMap` entry for `(BlockId, *)` has a stamp past the load baseline.
- The block was not the target of a neighbor-merge during this session — i.e., no `Cmd::backspaceMerge` or `Cmd::deleteMerge` deposited content from a neighbor into this block. (Tracked via a small per-session "merged-into" set that clears on save.)
- The block was not born after load (a born-after-load block has no `loadTimeBytes`; falls into the "always canonical" path).

Otherwise, the block is **touched** and saves canonically.

### 5.4 Round-trip determinism

The contract:

- `load(X) → save(X')`: bytes-identical (X' == X) when no editing has occurred. This is the primary `tst_roundtrip` guarantee.
- `load(X) → mutate one block → save(X')`: byte-diff between X and X' restricted to the mutated block's bytes (and inter-block separators if the block's serialized form changes width). No other block's bytes may differ.
- `load(X) → save(X') → load(X') → save(X'')`: X' == X'' (idempotence after first save through Markoff).

The corpus: `tests/foundation/roundtrip/corpus/` holds representative real-world Markdown samples — GFM examples, Obsidian vault excerpts, CommonMark spec edge cases, real `docs/` files from this project. Each is loaded → saved unmodified → asserted byte-identical.

### 5.5 Why this and not the alternatives

Touch-aware save (rather than always-canonicalize or bytes-identical-strict) is right because the typical Markoff user is editing files maintained outside Markoff — git repos, Obsidian vaults, shared notes that other tools and people also edit. Always-canonicalize produces a noisy first-save diff in their git history that buries actual changes; bytes-identical-strict fails to canonicalize ever, leaving non-canonical formatting forever and behaving inconsistently the moment editing begins. Touch-aware respects existing files and canonicalizes only what the user actually touched — the pattern users want from a respectful editor, with the per-block flag falling naturally out of the per-block CRDT model.

The list-unwrapping at load (rather than preserving the parser's list container as a single `BlockId`) is required by the granularity decision — list-items are first-class IdList entries because the alternative leaves the parser-vs-CRDT race intact for inside-list editing, which is exactly the failure mode D was designed to eliminate.

Loading link reference defs into a sibling map (rather than into `BlockId` entries) is right because they're document-level metadata used by render-time link resolution; they're not visible block content. Storing them as `BlockId` entries would force the render layer to filter them out of every block walk and would conflate "blocks the user sees" with "metadata the document carries."

---

## 6. Parser scope contract

### 6.1 The two surfaces D2 commits to using

| Time | Parser surface |
|---|---|
| Load | `Markoff::Document::fromMarkdown(QByteArray)` for top-level walk + per-block content + frontmatter + footnotes. Today's full extract pipeline; reused as-is by D2. |
| Per-block inline parse (cache miss) | `parser.inlineSpansFor(QByteArray blockContent, BlockKind kind) → InlineSpanTree`. Synchronous, called on the calling thread per the prior decision. |
| Save | **none.** Save reads CRDT state, doesn't re-parse. |
| Document-wide parse at runtime | **none. Ever.** |

### 6.2 What retires from today's parser usage

The existing `ParsePool` (the worker thread that owns a long-lived `IncrementalParseSession`) loses all its callers:

- `MarkoffDocument::applyLocalEdit` no longer schedules document-wide reparses — there is no document-wide rope to reparse.
- The `parseUpdated` signal retires (§8); nothing subscribes to it.
- The `IncrementalParseSession` itself becomes unreachable from D2 code.

D2 leaves `ParsePool` and `IncrementalParseSession` in the parser library but unused. D4's job is to delete them (and the API surface they expose) entirely.

### 6.3 D4 commitment

By taking the contract above, D2 commits D4 to the following deletions in the parser library:

- `ParsePool::schedule(QByteArray)` and `ParsePool::scheduleReset(QByteArray)`.
- `IncrementalParseSession` and its `parseIncremental({edit}, newBody)` API.
- Any document-wide incremental reparse infrastructure (block-tree edit/parse, inline-tree shifting) that exists solely to serve the runtime hot path.

What survives in the parser library after D4: `Document::fromMarkdown` (used at load), and a small `inlineSpansFor` API (used per-block on cache miss). The library shrinks substantially.

### 6.4 Why this and not the alternatives

Per-block inline parse (rather than retaining document-wide parse) is the canonical D shape — the proposal designed it that way and the C-restoration's audit identified the document-wide parse as the actual perf cost driving the L4 cycle-guard mess. Retaining document-wide parse would defeat D's purpose.

Synchronous on read (rather than asynchronous worker per cache miss) is right because per-block inline parse is microseconds for paragraph-sized blocks, the worry case (large fenced code blocks) isn't a typing-hot-path concern (users don't type 100KB code blocks character-by-character), the hitch is only on first paint after content change, and the worker plumbing exists in N places (one per block) for work that's microseconds-each. A is the simplest correct implementation of the proposal's "bounded; fast" framing; B can be added as a follow-up if real workload data shows it matters.

Retiring `ParsePool` rather than reusing it for per-block work is right because per-block parses are synchronous (no pool needed) and deleting unused infrastructure prevents drift back to document-wide parsing under future feature pressure.

---

## 7. Garbage collection

### 7.1 `WatermarkCoordinator`

```cpp
namespace Markoff {

class WatermarkCoordinator {
public:
    explicit WatermarkCoordinator(MarkoffDocument &);

    // Called by MarkoffDocument::save(...) on successful fsync return.
    // Refuses (returns false) if any UndoLog::transaction is currently open.
    bool onSaveSucceeded();

    // The current per-CRDT watermark snapshot.
    Watermark currentWatermark() const;

private:
    void advanceAndCompact();
    void disposeOrphans();

    MarkoffDocument &m_doc;
    Watermark m_watermark;
};

struct Watermark {
    quint64 idListSeq;
    QHash<BlockId, quint64> bufferSeqs;
    quint64 kindTagMapSeq;
    quint64 blockAttrsMapSeq;
    quint64 frontmatterMapSeq;
    quint64 linkRefMapSeq;
    quint64 footnoteDefMapSeq;
};

}  // namespace Markoff
```

### 7.2 Trigger sequence (single-user, D2)

1. `MarkoffDocument::save(path)` writes to disk.
2. `fsync()` returns successfully.
3. `WatermarkCoordinator::onSaveSucceeded()` is invoked.
4. **Quiesce check.** Refuse (return false) if any `UndoLog::transaction` is currently open. (User-initiated saves serialize against user-initiated edits in practice; the gate is correct against any concurrent-transaction case including future async ones.)
5. **Snapshot.** Capture `currentWatermark()` — each CRDT's current sequence number.
6. **Dispatch.** Call `compact(seq)` on each CRDT with its respective sequence.
7. **Trim `UndoLog`.** Drop any `UndoEntry` all of whose target ops have been collapsed past their respective sequences (i.e., the CRDT can no longer undo that op because its history was compacted).
8. **Dispose orphaned Buffers** (§7.3).

### 7.3 Orphaned `Buffer` disposal

When `IdList::removeAt(BlockId)` is called (e.g., via `Cmd::backspaceMerge`), the per-block `Buffer` for that `BlockId` is **not** disposed immediately. It stays in the per-block map so that `undo()` can resurrect it.

Disposal happens in `WatermarkCoordinator::disposeOrphans()` (called as the last step of `onSaveSucceeded`):

- For each `BlockId` in the per-block map but absent from the current `IdList`:
  - If the `IdList::removeAt` op for this block has been compacted past the watermark (i.e., undo can no longer resurrect it), drop the `Buffer` from the per-block map and free its memory.
  - Otherwise, keep it. Next save will re-evaluate.

### 7.4 Collab-evolution path (D5)

D5 modifies step 4 of §7.2 to: **Refuse if any transaction is open OR any local op above the snapshot watermark has not yet been ack'd by all known replicas.** The mechanism is unchanged; the predicate is stricter. `WatermarkCoordinator` gains a `setReplicaAckCallback(...)` that the transport layer wires up at collab activation.

The implementation in D2 leaves room for this: `onSaveSucceeded()` is the single trigger point; adding `&& replicasHaveAckedUpTo(snapshot)` to step 4 is a one-line change.

### 7.5 Why this and not the alternatives

Save-triggered (rather than idle-triggered or manual) GC is right because save is a natural sync point ("everything I've persisted to disk is collapsable"), bounded memory growth is the primary GC goal (and save-triggered guarantees it as a function of save cadence rather than session age), and the trigger predicate evolves cleanly to collab without changing the mechanism. Idle timers add complexity (and long never-idle sessions still accumulate); manual relies on application discipline that's easy to forget.

Deferring orphaned-Buffer disposal to the next watermark crossing (rather than disposing at structural-CRDT delete time) is required because undo must be able to resurrect a deleted block until its delete op is compacted. Otherwise, Backspace-merge → Ctrl-Z would dispatch undo to a Buffer that no longer exists.

Quiescing against open transactions (rather than allowing concurrent compact) is the simplest correctness gate. Save is user-initiated; user actions are user-initiated; in practice they don't overlap. The gate handles future cases (async transactions, plugin background work) without further design.

---

## 8. Signal and counter API

### 8.1 Per-CRDT primary

| Signal / accessor | Source | Bumps on |
|---|---|---|
| `Buffer::editSequence()` | per-block | every op against that Buffer (Local, Remote, Undo, Redo) |
| `Buffer::inlineSpansChanged` | per-block | next read of `InlineParseCache` for this block will recompute (i.e., the cache-line invalidated) |
| `IdList::editSequence()` | structural | every `insertAfter`, `removeAt`, `applyRemote` |
| `IdList::structureChanged` | structural | granular: `rowsInserted(int after, int count)`, `rowsRemoved(int at, int count)`, `kindChanged(BlockId)`. Consumers can subscribe to specific shapes. |
| `KindTagMap::changed`, `BlockAttrsMap::changed`, `FrontmatterMap::changed`, `LinkRefMap::changed`, `FootnoteDefMap::changed` | each sibling map | per-key, with `(key, std::optional<oldValue>, std::optional<newValue>)` |

### 8.2 Document-level derived

| Accessor / signal | Definition | Use case |
|---|---|---|
| `MarkoffDocument::documentEditSequence()` | monotonic sum across `IdList::editSequence()` + every `Buffer::editSequence()` + every sibling map's edit count | Dirty-tracking for window title; autosave-timer reset; "is this newer than what I rendered?" checks |
| `MarkoffDocument::documentChanged` | debounced fan-out from any per-CRDT signal; emitted on the next event-loop spin after one or more per-CRDT signals fire | Search-result-cache invalidation; plugins that watch the document as a whole |
| `MarkoffDocument::documentLoaded` | one-shot at load completion | Replaces today's `parseUpdated`-after-load convention |
| `MarkoffDocument::beforeSave`, `afterSave(bool success)` | save lifecycle | Autosave coordination; plugin sync hooks |

The derived accessors are computed, not stored — single source of truth is at the per-CRDT level. `documentEditSequence()` is cheap (a sum across a small fixed set of counters). `documentChanged` debouncing collapses fan-out: one Cmd that touches 5 CRDTs produces 1 `documentChanged` emission, not 5.

### 8.3 What retires

- `parseUpdated` signal — no document-wide parse to deliver.
- `parseSequence` accessor — no document-wide parses to count.
- `parseInputEditSeq` (today's 4th arg of `parseUpdated`) — no async parse race to gate against, so no staleness mechanism, so no need for the captured-input-edit-sequence.

### 8.4 Why this and not the alternatives

Per-CRDT primary + doc-level derived (rather than per-CRDT only or doc-wide retained) respects the new shape — per-CRDT is the truth of where state lives — while giving existing services (Search/Replace, dirty-tracker, autosave timer, plugin systems) the doc-level convenience they have legitimate use for. Forcing them to re-implement aggregation N+1 times would be friction for no architectural gain. Retaining doc-wide as primary would double-store (every per-CRDT op also bumps a doc-wide counter) for a model that's no longer document-scoped.

Retiring `parseUpdated` cleanly (rather than keeping it as a derived signal that fires when "any parse happened") is right because there are no async parses anymore — per-block inline parses are synchronous-on-read, and structural parsing is gone. Anything `parseUpdated` consumers wanted, they get from `documentChanged` or the per-CRDT signals directly.

---

## 9. Migration

### 9.1 Layer-by-layer transition (`libs/markoff-live`)

| Layer | Today | D2 migration |
|---|---|---|
| L0 coordinate primitives | byte ↔ qtPos ↔ block-local | unchanged (block-local conversions are the same; doc-level byte conversions vanish) |
| L1 read-only render | `ListView` + delegates from parser-driven `LiveBlockModel` | unchanged shape; model now sources rows from `IdList` directly |
| L2 diff-driven model | Myers over `BlockKey(kind, BlockAnchor)` | Myers over `(kind, BlockId-as-IdList-element)` — IDs are stabler; diff churn drops substantially |
| L3 cursor model | Shape 1 discriminated union; `BlockId = BlockAnchor`; `TextCaret::positionAnchor = TextAnchor` over doc Buffer | `BlockId = IdList element id`; `TextCaret::positionAnchor = TextAnchor` over per-block Buffer; structurally identical |
| L4 `LiveEditBinding` | `applyLocalEdit(MarkoffEdit)` + freshness gate + 3 cycle guards + previousText cache | replaced by direct `applyBlockEdit(BlockEdit)` calls; freshness gate, cycle guards, previousText cache all delete (the staleness window vanishes) |
| L5 `LiveStructuralKeyHandler` | issues `MarkoffEdit`s for Enter / Backspace-merge / Delete-merge / Shift-Enter | calls `Cmd::enterAtEnd` / `Cmd::backspaceMerge` / `Cmd::deleteMerge` / `Cmd::insertSoftBreak`; handler shape preserved, dispatch target changes |
| Marker-paragraph machinery | `MarkerScrubber`, atomic-bundled-edit primitive, ZWSP scrubbing, no-op stacked-Enter rule, marker-aware initial-qtPos rule | **deletes entirely.** Enter is `Cmd::enterAtEnd` → `IdList::insertAfter`. No source marker. No race. |
| `UndoCoalescer` | view-layer coalesce policy | replaced by `UndoLog`'s extend-rather-than-open rule (§4.3); same predicates; relocated into the foundation |

Most of this migration is mechanical: change call sites, delete dead workarounds. The C-restoration's L0–L3 architectural decisions survive without modification; L4–L5 simplify by losing the freshness-gate / cycle-guard infrastructure they no longer need.

### 9.2 Other in-tree consumers

- **Search / Replace controllers.** Adapt internally to iterate per-block (via a new `MarkoffDocument::iterateBlocks(visitor)` API) rather than walking a global byte range. Public API of `SearchEngine` / `ReplaceController` unchanged for their own consumers.
- **LinkService.** Today consumes parsed links and link-reference-defs from the document parse. In D2 it consumes from `LinkRefMap` directly plus per-block inline parse (for link references). API unchanged.
- **CompletionRegistry.** Today triggers on document-byte-position. In D2 triggers on `(BlockId, withinBlockOffset)`. Public types adapt minimally; `CompletionContext` carries `BlockId` instead of byte offset.
- **SyntaxHighlightService**. Today operates per fenced-code-block via parsed inline data. In D2 unchanged shape — the inline data still arrives via per-block inline parse; the source changes (per-block cache rather than doc-wide parse) but the consumer's interface is preserved.

### 9.3 Migration ordering

D2 implementation proceeds (the writing-plans skill will detail this; sketched here for the spec):

1. New types (`BlockId`, `TextAnchor` reshape, `BlockEdit`, `StructuralOp`, `BlockKind`, `CausalLwwMap`, `WatermarkCoordinator`, `UndoLog`).
2. `MarkoffDocument` new internals (composition of IdList + per-block Buffers + sibling maps); old internals deleted in lockstep.
3. `Cmd::*` namespace re-implementation against new internals.
4. Load / save reshape.
5. `markoff-live-render` L4 / L5 migration.
6. `markoff-live-render` marker-paragraph machinery deletion.
7. Search / Replace / Link / Completion / SyntaxHighlight services adapt.
8. Round-trip corpus tests.

Each step is a logical commit boundary. The plan (writing-plans output) refines this into TDD task units.

### 9.4 Why this and not the alternatives

Per-step migration with the C-restoration L0–L3 carrying forward (rather than rewriting `markoff-live-render` from scratch) is right because the audit's failure modes were concentrated in L4 — the source-of-truth race and the cycle-guard pattern that grew around it. L0–L3 are sound and were designed against a "collab-ready" premise (C spec premise 9) that already presumes per-CRDT thinking. Rewriting them would discard work that's correct.

Deleting marker-paragraph machinery wholesale (rather than keeping it gated behind a feature flag) is right because it's a workaround for a problem D removes. Keeping it would invite the temptation to use it as a fallback if D2 hits an early bug, which would re-instantiate the very layering pattern D was designed to escape.

---

## 10. Test strategy

### 10.1 Foundation unit tests

In-tree, against `MarkoffDocument` and its components. Coverage required:

- **`CausalLwwMap` semantics.** Concurrent set/remove on the same key; stamp-based resolution; undo/redo correctness; compact semantics.
- **`UndoLog` transactions.** Single-target transactions; multi-target transactions; nested transactions joining outer; rollback; coalescing extends; coalesce-breaks (focus change, structural op, idle).
- **`UndoLog` document undo.** Pop-and-dispatch-in-reverse; redo round-trip; undo across mixed BlockEdit + StructuralOp.
- **`UndoLog::undoForBlock` permissive.** Picks most recent entry whose targets include the block; dispatches whole entry; structural-shape effects observable and asserted.
- **`MarkoffDocument` per-block APIs.** `applyBlockEdit`, `applyStructural`, `iterateBlocks`, `blockKind`, `blockAttrs`, `blockTextAnchor`, `resolveTextAnchor`.
- **`Cmd::*` decompositions.** Each Cmd's transaction body matches §4.6.
- **Touch test.** Edit a block, save, assert kind serializer ran. Don't edit, save, assert load-time bytes returned. Kind change without content edit, save, assert canonical. Neighbor-merge into block, save, assert merged-into block saves canonical.
- **Save-triggered GC.** Save succeeds → watermark advances → compact dispatched. Save fails → watermark unchanged. Open transaction at save time → quiesce check refuses.
- **Orphaned Buffer disposal.** Delete block → Buffer retained for undo. Save crosses watermark → Buffer disposed. Re-undo before disposal works; re-undo after disposal returns null.

### 10.2 Convergence tests

Using collabtext's IdList convergence fixtures (the maintainers committed these would be public alongside D1):

- **Two-replica structural ops.** Crossing inserts at the same anchor; ordering by replica tiebreak; no missed inserts.
- **Two-replica per-block ops.** Crossing edits in the same block; UTF-8-aware merge.
- **Mixed structural + content.** Replica A inserts a new block while replica B edits the previous block's content; both survive; ordering is causal.
- **Remove-vs-edit race.** Replica A removes block N while replica B edits block N's content; B's edits become orphaned (block is gone); orphaned Buffer is disposable per §7.3.
- **Cross-CRDT undo.** Local Cmd::enterAtEnd → remote edit lands in the new block → local Ctrl-Z undoes the structural insert; the remote edit becomes orphaned (collabtext per-CRDT semantics); cursor falls back per §3.5 of the C-spec.

### 10.3 Round-trip corpus

`tests/foundation/roundtrip/corpus/` contains representative files:

- GFM specification examples (~100 short test cases)
- Obsidian vault excerpts (real notes with frontmatter, internal links, callouts)
- CommonMark spec edge cases (corner cases of markdown ambiguity)
- Real `docs/` files from this project (e.g., `docs/2026-05-02-live-view-architectural-audit.md` — a file Markoff itself dogfoods on)

Each is loaded → saved unmodified → asserted byte-identical. Then loaded → mutate one block → saved → assert only that block's bytes (and its inter-block separators if width changed) differ.

The corpus is a living artifact: when a real-world file surfaces a round-trip failure, it joins the corpus.

---

## 11. Open questions

These are issues that need resolution at writing-plans time or during implementation. They are not blockers to spec approval.

1. **Per-block inline parse cache eviction policy.** D2 specifies the cache exists and is keyed by per-block edit-counter. It does not specify when entries are evicted. Options: never-evict (memory grows with block count); LRU with a configurable max; time-based after N seconds of no read. Decision deferred to plan-time, informed by real workload.
2. **Plugin block-kind registration timing.** D3 will own the registry interface; D2 needs to know whether the registry is constructed before or after `MarkoffDocument`. Tentative: a global `BlockKindRegistry` singleton populated at process start; `MarkoffDocument` looks up serializers at save time. Confirmed in D3.
3. **`tst_roundtrip` corpus license.** Some corpus candidates (Obsidian vault excerpts) need licensing review before checking into a GPL-3.0-or-later repository. Plan-time work to curate a clean corpus.
4. **`BlockAttrsMap` value type.** D2 shows `(BlockId, AttrName) → AttrValue` but doesn't pin `AttrValue`. Tentative: a `std::variant<int, QString, bool>` covering the known attrs (level, info-string, marker-style, src/alt, display-vs-inline). Confirmed in D3 once block kinds are reviewed.
5. **`Cmd::pasteMarkdown` parser invocation.** Paste decomposition (§4.6) requires re-parsing the pasted source through the load-time parser. This is the one place where parser is invoked outside load. Acceptable; document it explicitly. Plan-time decision: synchronous on the calling thread (paste is user-initiated, latency-tolerant).

---

## 12. References

### D-arc

- `docs/d-arc/2026-05-04-d-arc-roadmap.md` — orientation doc
- `docs/d-arc/d-arc-status.md` — active status board
- `docs/d-arc/collabtext-scope-line.md` — the six "won't do" items, verbatim from the maintainer response

### Antecedent designs

- `docs/specs/2026-05-02-d-evolution-proposal.md` — Markoff's proposal to collabtext
- `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` — collabtext's response (Option β commitment + scope lines)
- `docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md` — C-restoration spec (lower layers carry forward to D2)
- `docs/2026-05-02-live-view-architectural-audit.md` — diagnostic that drove C, and now drives D
- `docs/archive/c-restoration-arc/2026-05-04-c-restoration-bookend-d-pivot.md` — the cut

### Future D-arc designs (stubs)

- `docs/specs/2026-05-04-d3-view-layer-adaptation-STUB.md`
- `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`
- `docs/specs/2026-05-04-d5-collab-activation-STUB.md`

### External

- collabtext `IdList` API (shipped 2026-05-04; consult `~/dev/collabtext/include/...` for current header)
- Yjs `Y.XmlFragment` / `Y.XmlText` composition pattern (cited as a working example of structural-CRDT-over-content-CRDTs; not mirrored)
- Automerge tree types (similar pattern; cited)

---

*End of D2 spec. Implementation plan to follow via `superpowers:writing-plans` skill.*
