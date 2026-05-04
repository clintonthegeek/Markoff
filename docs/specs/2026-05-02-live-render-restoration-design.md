# Live Render Restoration — Architecture Specification

**Date:** 2026-05-02
**Status:** Approved for plan derivation (brainstorming complete; writing-plans is the next step)
**Branch:** `exploration/new-foundation`
**Audience:** the implementer who picks this up; the user (review gate); the `collabtext` maintainers (cross-reference for the companion D-evolution proposal).
**Companion document:** `docs/specs/2026-05-02-d-evolution-proposal.md` *(to be written after this spec is approved)* — long-term per-block-CRDT direction the restoration leaves room for.

**Supersedes (in scope):**

- the *live-view* portions of `docs/specs/2026-04-29-live-render-design.md` (the read-only walking-skeleton design carries forward as L1; the rest is replaced).
- the *live-view* portions of `docs/specs/2026-04-30-live-editing-design.md` (the ten editing invariants are replaced by the §4 source-of-truth protocol and §3 cursor model below; one cycle-guard pattern survives, see §4.5).
- `docs/specs/2026-05-01-live-projection-layer.md` *in part* — the predictions half (inline-format, fence-opener) survives in modified form; the holes half is retired (the EOB-Enter case ceases to exist under Notion-style Enter semantics; the empty-list-item / empty-fence cases that genuinely need view-side intent buffering are deferred until lists land in R7).
- `docs/plans/2026-05-02-live-delegate-repair.md` — replaced as the active path forward. The repair plan's bug catalogue remains useful as a regression-target list.

The audit `docs/2026-05-02-live-view-architectural-audit.md` is the diagnostic this spec answers; read both together.

---

## 0. TL;DR

The Phase-2 live preview, in its current form, has six overlapping authority claims for "what is the content of block N right now," reconciled pairwise with ad-hoc cycle guards. This spec retires that architecture in favour of a side-by-side new library, `libs/markoff-live-render`, built layer-up around a single principled mechanism: **sequence-tagged reconciliation**. The CRDT remains the canonical source; the parser is asynchronous; the view computes per-row staleness from `editSequence`/`parseSequence` and applies parse output selectively. Speculation, focus delivery, holes-no-longer-needed, IME, and undo coalescing all dispatch off the same primitive.

The widget is a block-based editor with a text spine: per-block QML delegates, a discriminated-union cursor model that natively admits non-text-cursor blocks, and a block-API contract designed so plugin-authored block kinds (math, mermaid, video, etc.) compose cleanly. Restoration delivers, in nine phases over an estimated 18–30 weeks, a dogfood-stable editor handling paragraphs, headings, code-blocks, lists, blockquotes, images, horizontal rules, and one fully interactive block kind (math), with per-block context menus and a 16ms keystroke-to-pixel target enforced by CI benchmarks. The architecture is collab-ready (every cursor/edit/undo decision is tested against concurrent remote edits) but ships single-user; collab activation is post-restoration. The D evolution (per-block CRDT) is the long-term endpoint the architecture leaves room for and is documented separately as a proposal to the `collabtext` maintainers.

---

## 1. Premises (binding)

These decisions were made through brainstorming and are inputs to this design, not outputs. They are quoted here so the design's conclusions are auditable against its premises.

| # | Premise | Decision |
|---|---|---|
| 1 | Mental model | **B** — block-based runtime with a text spine. The editor is a list of blocks; text blocks are one kind among many. The Markdown source is the file format, not the runtime model. |
| 2 | Now feature set | I.a math, II.a lists, II.b blockquotes, III.c per-block context menu, plus paragraph / heading / hr / image / code-block carrying forward. Everything else from the Q2 list is *Admit* (architecture must not preclude them) or *Defer*. |
| 3 | Cursor shape | **Shape 1** — discriminated union: `Cursor = TextCaret | BlockSelected | BlockInternalEdit`. Block kind declares which variants apply to it. CRDT-anchored `BlockId`; within-block position is anchor-tracked. |
| 4 | Source-of-truth | **C** — sequence-tagged hybrid. CRDT canonical; parser asynchronous; per-row staleness computed from `editSequence`/`parseSequence`. **D** (per-block CRDT) is the long-term endpoint, separate doc. |
| 5 | Restoration shape | **β** — side-by-side library `libs/markoff-live-render`. Old `markoff-view-qml` keeps source mode and old-live for regression reference until new library reaches dogfood-stability, then retires. |
| 6 | Enter semantics | **N** — Notion-style: Enter creates a new block; Shift-Enter inserts a soft break (`\n`) within the current block. **(Amended A2, 2026-05-04 — supersedes A1.)** Paragraph EOB-Enter and start-of-paragraph-Enter insert `"\n\n​"` (EOB) or `"​\n\n"` (start) into the source. The U+200B ZERO WIDTH SPACE is invisible; the parser produces a real paragraph block; cursor lands via the standard parser-driven row path (`requestTextCaretAtNewRow` → `LiveBlockModel::rowsInserted`). A `MarkerScrubber` service (live-render lib) ensures the marker doesn't survive focus-out / save / load. Per `docs/specs/2026-05-03-marker-paragraph-design.md`. The v2 hole implementation (`LiveHoleLayer` / `LiveProxyBlockModel` / `BlockHole`) is permanently retired; the v0 hole implementation in `markoff-view-qml` was already retired by A1. Mid-block Enter is unchanged (parser produces both halves; cursor delivery via the same parser-driven row path). |
| 7 | Performance budget | 16 ms keystroke / 50 ms structural; 4 ms main-thread per keystroke handler; 8 ms parse-arrival on docs ≤ 64 KB. CI-enforced via `tst_benchmark`. |

Two operating decisions made in the design (subject to user pushback if surprising):

| 8 | Parser fungibility (C scope) | Tree-sitter retained. Parser API extended only as required to retire the per-delegate `TreeSitterParser` instantiation (pre-baked per-block inline span data). No grammar replacement, no engine swap in C. |
| 9 | CRDT collab horizon | Collab-ready architecture; collab-absent shipping. Every cursor/selection/edit/undo decision is tested against "remote edit lands concurrently." Wire format and presence are deferred. |

---

## 2. Architecture overview

The new library is decomposed into nine layers. Each layer has a single purpose, a well-defined dependency on the layer below, and is tested in isolation. Higher layers consume lower layers' contracts; lower layers know nothing about higher layers.

```
┌──────────────────────────────────────────────────────────────────┐
│ L8  Interactive blocks   (math; later: mermaid/video/etc.)       │  R8
├──────────────────────────────────────────────────────────────────┤
│ L7  Structured text blocks   (lists, blockquotes; II.a/II.b)     │  R7
├──────────────────────────────────────────────────────────────────┤
│ L6  Other text blocks   (heading, code-block, hr, image)         │  R6
├──────────────────────────────────────────────────────────────────┤
│ L5  Structural keys     (Enter, Backspace-edge, Tab; IME; undo)  │  R5
├──────────────────────────────────────────────────────────────────┤
│ L4  Paragraph editing   (sequence-tagged binding; cycle-collapse)│  R4
├──────────────────────────────────────────────────────────────────┤
│ L3  Cursor + selection  (Shape 1; mouse hit-test; copy)          │  R3
├──────────────────────────────────────────────────────────────────┤
│ L2  Diff-driven model   (Myers over BlockKey; per-row sequence)  │  R2
├──────────────────────────────────────────────────────────────────┤
│ L1  Read-only render    (ListView + delegates; no events)        │  R2
├──────────────────────────────────────────────────────────────────┤
│ L0  Coordinate primitives  (byte/qtPos/block-local conversions)  │  R2
└──────────────────────────────────────────────────────────────────┘

Foundation surfaces (R1, ⊂ markoff-foundation, markoff-parser):
  editSequence-at-parse-input          (new; small delta)
  per-block inline span pre-bake       (new; markoff-parser API extension)
  BlockId opaque value type            (already exists as BlockAnchor; rename trivial)
```

Layers are not arbitrary: they correspond 1:1 to the audit's L0–L9 layering. The mapping to the phasing γ in §11 is shown on the right.

**Dependency direction.** L<sub>n</sub> depends only on L<sub>k</sub> for k < n. There is no upward-pointing reference. When L4 (editing) needs to know about L0 (coordinates) it asks via L0's API; when L0 changes its internals, L4 is undisturbed.

**Test contract.** Each layer L<sub>n</sub> has a test suite that exercises L<sub>n</sub>'s public API with all of L<sub>k<n</sub> as real implementations and L<sub>k>n</sub> absent. Layer tests do not mock dependencies; they mock the absence of dependents. This is what enables the audit's "L4 was decided implicitly under dogfood pressure" failure mode to be caught next time: each layer is forced to make its decisions explicitly because it has no upper-layer feedback to react to.

---

## 3. The cursor model (Shape 1)

The cursor is the load-bearing primitive of the architecture. Every part of the system that reasons about "what is focused" or "what does this key do" dispatches off the cursor's variant.

### 3.1 Type definition

```cpp
namespace Markoff::LiveRender {

/// Opaque block identity. **(Amended A2, 2026-05-04 — supersedes A1.)**
/// `BlockId` is `Markoff::BlockAnchor` (the foundation type), restored
/// to the original C-spec definition. The marker-paragraph design
/// (`docs/specs/2026-05-03-marker-paragraph-design.md`) eliminates the
/// need for a discriminated union: every block (including the marker
/// paragraphs created by EOB-Enter / start-of-paragraph Enter) is
/// parser-real and CRDT-anchored from creation. `TextCaret::block`
/// always references a real CRDT-anchored block. IDs survive remote
/// edits inside the block; invalidate if the block is fully deleted
/// (collapsed to nearest neighbor by §3.5).
using BlockId = Markoff::BlockAnchor;

/// Caret inside a text-bearing block, at a CRDT-anchored position
/// within the block's bytes. Block-local; rendered as a blinking I-beam.
struct TextCaret {
    BlockId block;
    Markoff::TextAnchor positionAnchor;     ///< CRDT-anchored byte position
    quint32 cachedByteOffset;               ///< resolved offset, refreshed on use
};

/// Block focused as a unit, no caret. Rendered as a focus ring.
/// Used by non-text blocks in their default state (math in render mode,
/// mermaid, video, image-with-controls).
struct BlockSelected {
    BlockId block;
};

/// Block in its own internal-edit mode. The block kind owns interpretation
/// of `mode` and any state attached. Examples:
///   ("math",  "editing-latex")  — LaTeX source becomes editable
///   ("table", "cell-1-3")       — table cell at (1,3) is editing
struct BlockInternalEdit {
    BlockId block;
    QString mode;                           ///< block-kind-defined token
};

using Cursor = std::variant<TextCaret, BlockSelected, BlockInternalEdit>;

/// Selection is two cursors. Anchor is where extension started; active is
/// where it ends. Collapsed when anchor == active. A range whose endpoints
/// are different variants (e.g. TextCaret → BlockSelected) covers all
/// blocks between them inclusively as whole-block units; partial selection
/// is meaningful only within text variants.
struct Selection {
    Cursor anchor;
    Cursor active;
    bool isCollapsed() const noexcept;
    bool isCaret() const noexcept;          ///< collapsed && holds TextCaret
};

}  // namespace Markoff::LiveRender
```

### 3.2 Block-kind variant declarations

Each block kind declares which variants apply to it:

| Block kind | TextCaret | BlockSelected | BlockInternalEdit |
|---|---|---|---|
| paragraph | ✓ | — | — |
| heading | ✓ | — | — |
| code-block | ✓ | — | — |
| list-item | ✓ | — | — |
| blockquote | ✓ | — | — |
| hr | — | ✓ | — |
| image | — | ✓ | `("image", "alt-edit")` (post-R6, optional) |
| math | — | ✓ (default) | `("math", "editing-latex")` |
| (future) mermaid | — | ✓ | `("mermaid", "editing-source")` |
| (future) video | — | ✓ | `("video", "playing")` |
| (future) table | — | ✓ | `("table", "cell-r-c")` |

**Invariant.** The variant a block kind does not declare is *invalid* for that kind; the focus protocol (§5.4) refuses to route a key event that would produce an invalid variant. Block-kind extension authors declare their variants in their `BlockKindDescriptor` (§5.1).

### 3.3 Anchor stability

`TextCaret::positionAnchor` is a CRDT anchor (`Markoff::TextAnchor`), already exposed by the foundation. Local edits use `cachedByteOffset` for arithmetic; remote edits trigger a translation pass via `MarkoffDocument::resolveTextAnchor` and refresh the cache. Within-block edits that move the cursor's anchor produce a re-anchored `positionAnchor` rather than a cached-offset adjustment.

`BlockId` is `BlockAnchor`, which is a CRDT anchor at the block's first byte. Stable across remote edits within the block; invalidated only by deletion of the entire block. The view layer never inspects, constructs, or compares CRDT internals — these are passed back to the foundation's `resolveTextAnchor` / `blockByteRange` for translation.

### 3.4 Selection invariants

- A `Selection` whose `anchor` and `active` are in *the same block* and *both `TextCaret`* is a within-block range, rendered as a contiguous text highlight inside that delegate.
- A `Selection` whose endpoints are in *different blocks*, both `TextCaret`, is a cross-block range. The selection covers `anchor.block` from `anchor.cachedByteOffset` to block-end, every block strictly between, and `active.block` from block-start to `active.cachedByteOffset`. When rendered, the start/end blocks show partial highlights and intermediate blocks show whole-block highlights.
- A `Selection` whose `anchor` is `BlockSelected(B)` and `active` is `TextCaret(B', n)` (or vice versa) covers `B` and every block between `B` and `B'`, and `B'` from start to `n` (or `n` to end). Block boundaries are determined by the parse-current top-level block ordering at the time the selection is rendered.
- A selection that contains a `BlockInternalEdit` variant is not a valid selection — internal-edit mode owns its own selection model (e.g. selecting LaTeX source inside the math block's edit overlay). The block-kind defines that internal selection; it does not propagate back to `Selection`.

### 3.5 Survival under remote edits (collab-ready)

When a remote edit arrives via `applyRemoteOps`:

1. For each `Cursor` held by the local view:
   1. Translate `positionAnchor` (if `TextCaret`) through the new buffer state. Refresh `cachedByteOffset`.
   2. If `block` is fully deleted (its `BlockAnchor` no longer resolves to any block in the new parse), collapse to a `TextCaret` at the nearest surviving neighbor's `(blockStart, anchor)`. Preference: previous block end; fall back to next block start; fall back to document-start.
   3. If `block` was *split* by a remote edit (e.g. their `\n\n` insertion mid-block), the old `BlockId` resolves to one of the two resulting blocks (the one containing the original `firstByte`). Cursor stays in that one; the cached offset is recomputed.
2. For `Selection`, apply step 1 to both endpoints. If endpoints invert (anchor was before active, becomes after), collapse to `active` as a caret.

These rules are mechanical and state-local; no user prompt, no conflict UI. Restoration ships them functioning even though no remote-edit source exists; collab activation later is a wire-format-and-presence problem, not a correctness problem.

---

## 4. The source-of-truth protocol (C: sequence-tagged hybrid)

This is the architecture's load-bearing mechanism. It retires the audit's "six sources of truth" by establishing one principle that every consumer dispatches off.

### 4.1 The principle

> **The CRDT is canonical. The parser is asynchronous. Each parse output is tagged with the `editSequence` of its input. The view trusts parser output for a given row only if no local edits to that row have arrived since the parse's input was captured.**

That sentence is the design. Everything below is consequence.

### 4.2 Definitions

- **`editSequence`** *(foundation, exists)*: a monotonic counter on `MarkoffDocument`, incremented on every state-change op (`applyLocalEdit`, `undo`, `redo`, `applyRemoteOps`, `resetContent`).
- **`parseSequence`** *(foundation, exists)*: a monotonic counter incremented each time `parseUpdated` fires.
- **`parseInputEditSequence`** *(foundation, **to be added**)*: the value of `editSequence` at the moment the parser captured its input bytes. Carried as the fourth argument of `parseUpdated`. Required because the parser is asynchronous: a parse whose `parseSequence` is N may carry input from `editSequence = M < currentEditSequence` if the user kept typing during the round-trip.
- **`row.lastEditEditSequence`** *(view-layer, per-row)*: the `editSequence` after the most recent local edit that affected this row's text.

### 4.3 The freshness rule

When `parseUpdated(parsed, parseSeq, anchors, parseInputEditSeq)` arrives, for each row R in the diff result:

```
parseFreshForRow(R) = (R.lastEditEditSequence <= parseInputEditSeq)
```

The diff's structural ops (Insert / Delete) are always applied — block boundaries are the parser's authoritative concern. The diff's text-role updates are applied only for rows where `parseFreshForRow(R) == true`.

For a stale row, the existing TextEdit content is the canonical view of the CRDT for that row's bytes (the CRDT received those keystrokes and they are committed there); the parse will catch up on its next round-trip.

### 4.4 Consequences (cycle-guards retired by C)

Each of these cycle guards in the current architecture is replaced by an instance of the freshness rule:

| Existing guard | Site | C replacement |
|---|---|---|
| `if (textEdit.activeFocus) return` skip in `onBlockTextChanged` | `ParagraphDelegate.qml:264` | Per-row `parseFreshForRow` check; focus-independent. |
| `commitBlockHole` rowsInserted listener leak | `LiveProjectionLayer.cpp:155-177` | **(Amended A2, 2026-05-04 — supersedes A1.)** Marker design — see `docs/specs/2026-05-03-marker-paragraph-design.md` §5; atomic-bundled-edit eliminates the hole authority. There is no `commitBlockHole`, no listener, no leak. The marker paragraph is parser-real from creation; `LiveCursorState::requestTextCaretAtNewRow` resolves on `LiveBlockModel::rowsInserted` directly. One named predicate (`isMarkerOnlyParagraph`) shared across three deterministic event points (focus-out, pre-save, post-load). |
| Mid-block-Enter local TextEdit truncation hack | `ParagraphDelegate.qml:144-174` | Structural-key handler emits the edit; parse-back updates both halves; freshness rule means each row updates as soon as its own parse is fresh. No truncation. |
| Speculative-kind registry duplication (model + layer) | `LiveBlockModel::m_speculativeOriginals` + `LiveProjectionLayer::m_blockKindPredictions` | Single home: model only. Each prediction tagged with its source-snapshot `editSequence`; cleared when a parse with `parseInputEditSequence >= prediction.editSequence` arrives. |
| Inline-prediction wholesale-clear on every parse | `LiveProjectionLayer::onParseUpdated` | Per-prediction sequence-tag; cleared selectively. |
| Detach/reattach hole around `applyOps` | `LiveListModelBinding.cpp:175-194` | **(Amended A2, 2026-05-04 — supersedes A1.)** Marker design — there are no holes, no proxy model, and no detach/reattach. `applyOps` runs directly against `LiveBlockModel` (parser-pure); the QML `ListView` binds directly. Marker paragraphs are normal paragraph rows. See `docs/specs/2026-05-03-marker-paragraph-design.md` §2. |

### 4.5 Cycle guards that **survive** C (and why)

Three cycle guards are *not* divergence-races; they are synchronous-event-loop patterns and remain in the new library in the same shape:

| Guard | Why it must survive |
|---|---|
| `setPlainText`-echo suppression (`m_applyingModelUpdate` in the binding) | When the view updates a delegate's TextEdit programmatically (parse-arrival fresh-row update), `QTextDocument::contentsChange` fires synchronously. Without the guard, the binding interprets that echo as a user edit and re-applies it to the CRDT. This is a same-event-loop-tick reentrancy issue, not a freshness issue. |
| IME composition deferral | Qt fires `inputMethodComposingChanged` and `contentsChange` interleaved during preedit. CRDT edits cannot be applied per-preedit-character (the preedit is not committed text). The composition-deferred path holds a pending state until the IME signals commit. Sequence-tagging doesn't help here. |
| Selection-projection echo (`m_applyingSessionSelection`) | The selection projection (§3) writes to `Session::primarySelection` when the view's cursor changes, and reads from `Session::primarySelection` to render. Without the guard, the write triggers a read that re-writes. Same synchronous loop. |

These three are cleanly named, scoped to one binding each, and well understood. The audit's complaint was about the *seven* cycle guards layered over each other; reducing to three principled ones is the win.

### 4.6 Foundation delta

The foundation needs one addition to support the protocol:

```cpp
// markoff-foundation/MarkoffDocument.h, signal change:
//   was: parseUpdated(parsed, parseSequence, blockAnchors)
//   now: parseUpdated(parsed, parseSequence, blockAnchors, parseInputEditSequence)
void parseUpdated(const Markoff::Document *parsed,
                  quint64 parseSequence,
                  QList<Markoff::BlockAnchor> blockAnchors,
                  quint64 parseInputEditSequence);
```

This is a single signal-shape change. The internal plumbing is also small: `ParsePool` already snapshots input bytes when scheduling a parse; it captures `editSequence` at the same moment and threads it through the result. Implementation cost: a few hours of foundation work.

The signal change is breaking for `markoff-view-qml` (the source-mode consumer). That library will be updated in R1 to accept the new fourth argument and ignore it; source mode does not need freshness tracking. Compatibility risk is bounded — there is one consumer of this signal in the repo, and we own it.

---

## 5. The block-API contract

The contract every block delegate exposes upward and the rest of the system relies on. Designed to admit current Now block kinds (paragraph, heading, code-block, hr, image, list-item, blockquote, math) and future plugin-authored kinds (mermaid, video, table, callout, etc.) without architectural change.

### 5.1 `BlockKindDescriptor`

A static value-type describing a block kind:

```cpp
struct BlockKindDescriptor {
    QString id;                                         ///< "paragraph", "math", etc.
    QString delegateUrl;                                ///< qrc:/.../FooDelegate.qml
    QSet<QString> supportedCursorVariants;              ///< {"TextCaret"}, {"BlockSelected", "BlockInternalEdit"}, etc.
    QStringList internalEditModes;                      ///< {"editing-latex"} etc.; empty if no internal edit
    bool acceptsTextRoleUpdates;                        ///< false for non-text blocks
    QStringList contextMenuActions;                     ///< III.c surface
    // ... structural-key declarations (§5.4)
};

class BlockKindRegistry {
public:
    void register_(BlockKindDescriptor);
    const BlockKindDescriptor *find(const QString &id) const;
    QList<QString> kinds() const;
};
```

Built-in kinds are registered at library init. Plugin authors register their kinds via the same API. The registry is the single place block-kind metadata is consulted; no `if (kind == "paragraph")` scattered through the codebase.

### 5.2 Delegate-to-system surface

Every delegate, regardless of kind, exposes the following QML properties / methods:

| Property / method | Type | Purpose |
|---|---|---|
| `blockId` | `BlockId` | Immutable; set at incubation. |
| `blockKind` | `string` | Mirrors descriptor `id`. |
| `cursorVariant` | enum-like string | `"TextCaret"`, `"BlockSelected"`, `"BlockInternalEdit"`, `"None"` (not focused). |
| `internalEditMode` | string | Empty unless `cursorVariant == "BlockInternalEdit"`. |
| `cursorState` | object | Variant-specific payload (text-caret: `{byteOffset, hasComposition}`; selected: empty; internal-edit: block-defined). |
| `applyTextUpdate(newText)` | invokable | Called only when `descriptor.acceptsTextRoleUpdates` and freshness rule passes. |
| `routeFocusIn(variant, payload)` | invokable | Focus protocol (§5.3) routes here; delegate accepts or rejects per its declared variants. |
| `relinquishFocus()` | invokable | Focus protocol asks the delegate to give up focus before routing to a neighbor. |
| `selectionRangeForBlock(blockSel)` | invokable | Returns the (start, end) qtPos pair for highlighting within this delegate. |
| `serializeForCopy()` | invokable | Returns the canonical Markdown bytes for this block's source range. |

The delegate's QML implementation is otherwise unconstrained: it composes whatever child items it wants (TextEdit, Image, Loader, jkqtmathtext, etc.), draws its own chrome, and handles its own internal events as long as it routes structural ones through `LiveStructuralKeyHandler`.

### 5.3 Focus protocol

Focus state is canonical at one place: `LiveCursorState` (a `QObject` owned by the binding). It holds the current `Cursor` value and emits `cursorChanged()`. Every consumer of focus (delegate, structural-key handler, context menu, selection projection, status display) reads from `LiveCursorState`.

To move focus:

1. The mover (mouse handler, structural-key handler, neighbor-routing helper, etc.) constructs a new `Cursor` value and calls `LiveCursorState::request(newCursor)`.
2. `LiveCursorState` validates: target block exists (resolves through `MarkoffDocument::blockByteRange`), variant is supported by the target's `BlockKindDescriptor`. Invalid request → reject silently and log.
3. If a delegate currently holds active focus, `LiveCursorState::request` calls its `relinquishFocus()`. The delegate releases its TextEdit's active focus, returns.
4. `LiveCursorState` updates its `Cursor` value, emits `cursorChanged()`.
5. The target delegate, listening to `cursorChanged()`, sees its `blockId` is now focused, calls its own `routeFocusIn(variant, payload)`. The delegate is responsible for translating "I'm now focused as a TextCaret at offset N" into the appropriate child-item state (set TextEdit cursor position, force active focus, etc.).
6. If the target delegate is not yet incubated by the ListView (R3 hit-test, R5 structural-edit cases), `LiveCursorState` watches `LiveBlockModel::rowsInserted` for the target block's row and re-fires the focus on incubation. Bounded retry replaced by deterministic signal.

This retires:

- The five concurrent retry loops in `LiveView.qml` (`holeReified`, `holeCreated`/`holeDropped`, `focusAfterStructuralEdit`, `focusRowReady`/`requestFocusOnRowInserted`, `focusRestoreRequested`).
- The bounded-attempt `Qt.callLater × 10` polling loops.
- The `setFocusProxy`-vs-no-setFocusProxy invariant violation.

The single signal `LiveCursorState::cursorChanged()` is the only focus event in the system.

### 5.4 Structural keys

Each `BlockKindDescriptor` declares the structural keys it consumes:

```cpp
// In BlockKindDescriptor:
QSet<int> consumedStructuralKeys;       // {Qt::Key_Return, Qt::Key_Tab, ...}
```

`LiveStructuralKeyHandler` is the single dispatcher. Every text-bearing delegate forwards structural keys to it via QML `Keys.onPressed` with `Keys.priority: BeforeItem`. The handler:

1. Looks up the focused block's descriptor.
2. If the key is in the descriptor's `consumedStructuralKeys`, dispatches to the kind-specific handler for that key. (Kind-specific handlers are registered alongside the descriptor; they receive `(cursor, key, modifiers)` and return one of `Handled | NotHandled | DeferToCharacterInsertion`.)
3. If `NotHandled` or kind doesn't consume the key, falls back to the default handler (e.g. character-insertion via the binding).

This admits list-item's Tab/Shift-Tab indent/outdent, blockquote's nesting Enter, code-block's literal Tab insertion, math's F2-toggles-edit-mode, etc. — each block kind owns its own structural-key behaviour, the dispatcher is content-free.

**(Amended A2, 2026-05-04 — supersedes A1.)** Paragraph EOB-Enter and start-of-paragraph-Enter follow the source-edit contract in `docs/specs/2026-05-03-marker-paragraph-design.md` §4: `applyLocalEdit` inserts `"\n\n​"` (EOB) or `"​\n\n"` (start) — note the byte order difference: the marker leads at start-of-paragraph and trails at EOB so the inserted block lands in the right topological position. Cursor delivery uses the standard `requestTextCaretAtNewRow` mechanism resolved on `LiveBlockModel::rowsInserted`. Mid-block Enter is unchanged (parser produces both halves; same cursor delivery). There is no hole-row dispatch — every block is parser-real; a marker-only paragraph is a normal paragraph row whose text happens to be a single ZWSP. A stacked-Enter no-op rule applies when the focused block already contains only the marker (per design §4.5).

---

## 6. Components

The new library's principal C++ classes and QML files. All classes are in namespace `Markoff::LiveRender`; QML module URI is `org.markoff.live-render 1.0`.

### 6.1 C++ components (by layer)

**L0 — coordinate primitives**
- `Coordinates.{h,cpp}` — pure functions for `(byteOffset ↔ qtPos) × (whole-doc ↔ block-local)`. No dependencies beyond foundation. Heavily tested; CI-enforced no-allocation in steady-state.

**L1 — read-only render**
- (No new C++; consumes L2's model. QML files own L1.)

**L2 — diff-driven model**
- `BlockWalker.{h,cpp}` — thin shim over `Markoff::Document::topLevelBlocks()`. Existing class, mostly carries forward.
- `AstBlockDiff.{h,cpp}` — Myers/LCS over `BlockKey`. Existing class, carries forward unchanged.
- `LiveBlockModel.{h,cpp}` — `QAbstractListModel` over `BlockRecord`. Per-row `lastEditEditSequence` field. Single speculative-kind registry. No hole row.
- `LiveListModelBinding.{h,cpp}` — subscribes to `parseUpdated`; runs diff; applies ops; freshness-checks per row. Owns the `LiveSpeculationLayer` (predictions only), `LiveCursorState`, `LiveSelectionView`.

**L3 — cursor + selection**
- `LiveCursorState.{h,cpp}` — single canonical `Cursor` value; `cursorChanged()` signal; focus protocol (§5.3).
- `LiveSelectionView.{h,cpp}` — projects `Session::primarySelection` ↔ `Selection` in Shape-1 form. Existing class, reshaped to Shape 1.
- `BlockHitTester.{h,cpp}` — pure C++ hit-test for `(mouseX, mouseY) → Cursor`. Replaces the spike-validated `hit()` JS function in `LiveView.qml` with a tested C++ implementation. The math is unchanged; the home is changed.

**L4 — paragraph editing**
- `LiveEditBinding.{h,cpp}` — per-delegate; routes `QTextDocument::contentsChange` to `MarkoffDocument::applyLocalEdit`. Sequence-tagged: tags affected row's `lastEditEditSequence` on edit. Composition-deferral pattern. Cycle guards: `m_applyingModelUpdate` only.
- `BlockKindRegistry.{h,cpp}` — registry of `BlockKindDescriptor`. Built-in kinds registered at init; plugin kinds register through public API.

**L5 — structural keys**
- `LiveStructuralKeyHandler.{h,cpp}` — dispatch by `BlockKindDescriptor::consumedStructuralKeys`. Existing class, restructured to dispatch table.
- `UndoCoalescer.{h,cpp}` — view-side policy (consecutive printables in same focus context; broken by non-printables, movement, mode switch, paste, structural change, idle threshold). Calls `MarkoffDocument::coalesceLastUndo()` per its rules. Pure mechanism; no ad-hoc per-event checks.

**L6 — predictions and phantom rows (the surviving half of the old projection layer, plus the audit's L9 phantom-rows component)**
- `LiveSpeculationLayer.{h,cpp}` — corresponds to the old `LiveProjectionLayer`, renamed to reflect the surviving role. Owns inline-format predictions + block-kind predictions. Each prediction tagged with source-snapshot `editSequence`.
- `InlineFormatHighlighter.{h,cpp}` — per-delegate `QSyntaxHighlighter` for inline format application. **No longer constructs a fresh `TreeSitterParser` per delegate**: consumes pre-baked per-block span data from the parse output. Predictions consulted from the speculation layer.
- `LiveSpeculativeFenceController.{h,cpp}` — paragraph→code-block kind speculation. Carries forward, sequence-tagged.
- **(Amended A2, 2026-05-04 — supersedes A1.)** `LiveHoleLayer` and `LiveProxyBlockModel` are retired. The marker-paragraph design eliminates phantom rows entirely: paragraph EOB-Enter / start-of-paragraph-Enter insert a parser-real marker paragraph via `applyLocalEdit`, so there is nothing for L6 to compose. The QML `ListView` binds directly to `LiveBlockModel`.
- **(Added A2, 2026-05-04)** `MarkerScrubber.{h,cpp}` — *not* a layer; a stateless service callable from L4 (`LiveEditBinding`'s focus-out path), L5 (post-structural-edit if needed), and the host (pre-save / post-load). Strips marker-only paragraphs (and runs of them) by emitting batched `applyLocalEdit` ops. See `docs/specs/2026-05-03-marker-paragraph-design.md` §6.

**L7 — structured text blocks**
- `ListItemDelegate` (QML, see below) and supporting C++ helpers for nested list mutation.
- `BlockquoteDelegate` (QML).

**L8 — interactive blocks**
- `MathBlockDelegate` (QML, see below) and C++ glue to jkqtmathtext.

### 6.2 QML files

```
qml/
  LiveView.qml                        — top-level; ListView + delegate dispatch
  delegates/
    ParagraphDelegate.qml             — TextCaret only; KSyntaxHighlighter optional
    HeadingDelegate.qml               — TextCaret; level-driven font sizing
    CodeBlockDelegate.qml             — TextCaret; KSyntaxHighlighter language-keyed
    HorizontalRuleDelegate.qml        — BlockSelected only
    ImageDelegate.qml                 — BlockSelected (default) + optional alt-edit
    ListItemDelegate.qml              — TextCaret; structural-keys for indent/outdent
    BlockquoteDelegate.qml            — TextCaret + nested-content composition
    MathBlockDelegate.qml             — BlockSelected + BlockInternalEdit{"editing-latex"}
  ContextMenu.qml                     — III.c host; consumes BlockKindDescriptor.contextMenuActions
```

**Comparison to current.** `LiveView.qml` shrinks from 417 lines to roughly 100–150 (the five `Connections` blocks for focus routing collapse to one consuming `LiveCursorState::cursorChanged`; the ad-hoc `Keys.onPressed` empty-doc path becomes one early-route through the structural-key handler; the `MouseArea.hit()` JS moves to C++). Each delegate shrinks similarly: `ParagraphDelegate.qml` from 312 lines to ~150 once the hole branches and `m_applyingModelBuffer` cycle guard are gone.

---

## 7. Data flow

### 7.1 Steady-state typing (the hot path; 16 ms budget)

```
User presses 'A' in focused paragraph delegate:
  ▼
QTextEdit fires QTextDocument::contentsChange(qtPos, 0, 1)
  ▼
LiveEditBinding::onContentsChange (per-delegate)
  ├─ guard m_applyingModelUpdate? skip if true
  ├─ guard composing? skip if true (composition path takes over)
  ├─ resolve block byte range via Coordinates (L0; allocation-free)
  ├─ build MarkoffEdit (single insert, one byte)
  ├─ MarkoffDocument::applyLocalEdit
  │     ▼
  │   editSequence bumps  (S_e += 1)
  │   contentsChanged signal (synchronous, same tick)
  │   ParsePool::schedule (worker thread; non-blocking)
  ├─ tag row R: row.lastEditEditSequence = S_e
  └─ UndoCoalescer.recordPrintable (in-context; coalesces with prior)

Total main-thread time: < 1 ms target. No whole-document toUtf8().
```

Concurrently, on the parser worker thread (not on the main thread; not in the keystroke budget):

```
ParsePool worker thread:
  ▼
captures input bytes; snapshots editSequence at capture (S_pi)
  ▼
TreeSitterParser::parseIncremental
  ▼
buildDocumentQueries + per-block span pre-bake  ← new in R1
  ▼
result QObject::invokeMethod-queued back to main thread
```

On parse-arrival (main thread; 8 ms budget):

```
parseUpdated(parsed, S_p, anchors, S_pi) fires
  ▼
LiveListModelBinding::onParseUpdatedAt
  ├─ AstBlockDiff over BlockKey
  ├─ for each Equal op: if row.lastEditEditSequence ≤ S_pi, dispatch
  │      text-role update (delegate's applyTextUpdate); else skip text,
  │      apply non-text fields only.
  ├─ for each Insert/Delete op: apply structurally regardless of freshness
  ├─ speculation layer: drop predictions tagged with editSequence ≤ S_pi
  └─ cursor: re-resolve TextCaret.cachedByteOffset; collapse on dead block
```

The "skip if focused" rule and its cousin sin (mid-block-Enter local truncation) are gone. The freshness check is per-row, not per-focus-state.

### 7.2 Structural edit (50 ms budget)

User presses Enter mid-paragraph:

```
TextEdit Keys.onPressed (Keys.priority BeforeItem)
  ▼
LiveStructuralKeyHandler::dispatch
  ├─ look up BlockKindDescriptor for current cursor's block
  ├─ Qt::Key_Return ∈ descriptor.consumedStructuralKeys ?
  │     paragraph: yes; code-block: no (literal newline)
  ├─ kind handler: enter-mid-paragraph
  │   ├─ MarkoffDocument::applyLocalEdit({insert "\n\n" at byte}) ← single edit
  │   ├─ editSequence bumps; tag both halves' rows when they appear
  │   └─ schedule LiveCursorState::request(TextCaret(B_new, 0))
  │       — block B_new doesn't exist yet; LiveCursorState waits on
  │         rowsInserted for the target row deterministically.
  ▼
parseUpdated arrives (round-trip):
  AstBlockDiff produces: Equal(B_old, truncated), Insert(B_new)
  rowsInserted fires on B_new
  LiveCursorState's pending request resolves; routes focus into B_new
  Delegate routeFocusIn called; TextEdit cursor at qtPos 0
  Caret visible.
```

No retry loops, no truncation hack, no second-level `Qt.callLater`.

**(Amended A2, 2026-05-04 — supersedes A1.)** The above flow is the *mid-block* Enter case. The end-of-paragraph and start-of-paragraph cases use the same source-edit-then-parse-back shape, with the addition of an invisible marker character (U+200B ZWSP) so that tree-sitter sees a real paragraph for the otherwise-blank region:

```
TextEdit Keys.onPressed (Keys.priority BeforeItem)
  ▼
LiveStructuralKeyHandler::dispatch (paragraph EOB-Enter / start-Enter)
  ├─ resolve cursor's CRDT-anchored byte offset → byteOffset
  ├─ MarkoffDocument::applyLocalEdit({{ byteOffset, byteOffset, payload }})
  │     payload = "\n\n​"  for EOB        (separator then marker)
  │     payload = "​\n\n"  for start      (marker then separator)
  ├─ editSequence bumps; setRowEditSequence on the row
  ├─ LiveCursorState::requestTextCaretAtNewRow(targetIndex, qtPos=0)
  │   — pending; resolves on the next LiveBlockModel::rowsInserted for
  │     the new block.
  │
  │ Parse-back arrives ~30–100 ms later:
  │   AstBlockDiff produces Insert(B_marker) — a real paragraph whose
  │   text is the ZWSP marker.
  │   rowsInserted fires; pending cursor request resolves; delegate
  │   incubates with text = "​"; cursor lands at qtPos 0 (before the
  │   marker).
  │
  │ User types 'x':
  │   LiveEditBinding::onContentsChange sees the focused block's
  │   pre-edit content was exactly the marker → atomic-bundled-edit:
  │   one MarkoffEdit replaces the marker bytes with "x".
  │   editSequence bumps; parse-back yields Equal(B_marker, text="x").
  │   No race window; the marker never escapes to a save or to a
  │   parse-back of any consequence.
  │
  │ MarkerScrubber covers leakage paths (focus-out without typing,
  │ pre-save, post-load) by emitting batched applyLocalEdit ops that
  │ remove marker-only paragraphs and their leading "\n\n".
```

The user-visible trace: press Enter, see new paragraph with cursor in it, type, paragraph fills in. From the architecture's POV, every change is a single CRDT edit; the marker never reaches a save or a parse-back of any consequence. See `docs/specs/2026-05-03-marker-paragraph-design.md` for the full design.

### 7.3 Click into math block (BlockSelected → BlockInternalEdit)

```
Mouse press on math delegate's render area:
  ▼
BlockHitTester::hit(x, y) returns Cursor::BlockSelected(B_math)
  ▼
LiveCursorState::request(BlockSelected(B_math))
  ├─ validate: math kind supports BlockSelected? yes.
  ├─ relinquishFocus on previously focused delegate
  ├─ update; cursorChanged()
  ▼
MathBlockDelegate observes cursorChanged, routeFocusIn("BlockSelected", {})
  ├─ shows focus ring; no caret
  ├─ context menu now keyed on math kind's actions

User presses F2 (or double-clicks):
  ▼
LiveStructuralKeyHandler dispatches to math kind's F2 handler:
  ├─ LiveCursorState::request(BlockInternalEdit(B_math, "editing-latex"))
  ├─ Math delegate routeFocusIn("BlockInternalEdit", {mode: "editing-latex"})
  │   shows LaTeX TextEdit overlay with current source text
User edits LaTeX. The math delegate's internal TextEdit edits the
LaTeX-source bytes through its own LiveEditBinding instance pointed at
the math block — same machinery as paragraph editing.

User presses Esc:
  ▼
math kind's Esc handler:
  ├─ LiveCursorState::request(BlockSelected(B_math))
  ├─ Math delegate routeFocusIn re-renders.
```

Note: math's *internal* edit reuses the same paragraph-editing machinery — `LiveEditBinding`, sequence-tagging, parse round-trip, freshness rule. No special path.

---

## 8. Error handling and seam invariants

The C protocol's freshness rule is a safety net for the most common error class (parse-vs-edit divergence). The remaining edge cases:

### 8.1 Block deleted under the cursor

Local case (user presses Backspace at start of block, merging into previous): structural-key handler issues the merge edit, re-anchors cursor to `TextCaret(prev_block, prev_block_end)` *before* the parse-back arrives. The parse confirms; AstBlockDiff produces a Delete; the cursor's block is already the surviving one.

Remote case (collab future): per §3.5, the cursor collapses to the nearest surviving neighbor synchronously on `applyRemoteOps`. No conflict UI in restoration scope.

### 8.2 Parse output diverges from local edit

After `applyLocalEdit`, the local CRDT has bytes the parser hasn't seen. If the user has typed e.g. `[**bold**](url)` and the parser arrives with input from before the close `)`, the Equal/Insert/Delete diff is computed against the parser's view of block ordering. The freshness rule guarantees the user's TextEdit content is preserved on the focused row; the inline-format predictions for unclosed delimiters cover the visual gap.

### 8.3 IME composition during parse

Parser arrives mid-composition: composition-deferred path skips text-role updates for the composing row regardless of freshness (composition state owns that row's content until commit). Parser's structural ops are still applied. On composition end, the binding applies a single edit reflecting the composed text.

### 8.4 ListView delegate not yet incubated

Focus protocol's `LiveCursorState` handles this deterministically via `rowsInserted`. No Qt.callLater retry loop; no bounded attempts; the focus delivery either resolves (delegate appears) or remains pending indefinitely (the row genuinely isn't going to appear in this parse cycle, in which case the next parse cycle's diff ops will produce it or not). If a focus request remains unresolved across two parse cycles, log and drop; cursor falls back to the last valid position.

### 8.5 BlockId resolves to two different blocks across parse cycles

The CRDT anchor at byte N may, after a structural edit splits the block, anchor to either the prefix or the suffix block depending on bias. Foundation's `blockAt(TextAnchor)` returns the unique containing block (or `nullopt` for separator regions). Cursor migrations during structural edits are handled by the structural-key handler explicitly, not left to anchor resolution.

### 8.6 Speculation contradicted by parse

User types ```` ``` ````; speculative-fence flips paragraph → code-block kind. Parser arrives, decides the line is *not* a code block (e.g. it's inside an already-open fence higher up). Speculation cleared by sequence-tag; model snaps back. No "stuck speculative" state survives.

---

## 9. Performance budget and CI enforcement

### 9.1 Targets (from premise 7)

| Path | Target | Hard cap |
|---|---|---|
| Steady-state typing keystroke → next paint | 16 ms | 33 ms (2 frames) |
| Structural edit (Enter, etc.) → caret in new block | 50 ms | 100 ms |
| Main-thread time per keystroke handler | 4 ms | 8 ms |
| Main-thread time per parse-arrival on doc ≤ 64 KB | 8 ms | 16 ms |
| Main-thread time per parse-arrival on doc 64–256 KB | 32 ms | 64 ms |
| Main-thread time per parse-arrival on doc > 256 KB | (graceful degradation; not budgeted in CI) | — |

### 9.2 Architectural choices forced by the budget

- **No whole-document UTF-16/UTF-8 round-trip per keystroke.** L0 coordinate primitives operate on the foundation's CRDT-side byte buffer with allocation-free walk operations. The current `m_document->toMarkdownUtf8()` per `onContentsChange` is forbidden.
- **No fresh `TreeSitterParser` instantiation per delegate per keystroke.** Inline span data is pre-baked by the parser's per-block walk and consumed read-only by `InlineFormatHighlighter`. R1's foundation delta ships the API.
- **Parser runs on a worker thread; main-thread parse-arrival processing is bounded.** The diff + applyOps + cursor re-resolve must complete in the budget. AstBlockDiff is already O(min(M, N)) Myers; applyOps is O(diff); cursor re-resolve is O(1) per cursor.
- **No Qt.callLater retry loops.** Deterministic signals only. (`LiveCursorState`'s pending request via `rowsInserted` is one event handler, not a polling loop.)
- **No JS-side per-row text rebuild in `LiveContextMenuHandler.blockTexts`.** Replaced by a C++ accessor that returns an opaque per-row handle.

### 9.3 CI enforcement

The existing `tst_benchmark` is repurposed. Add scenarios:

1. **Keystroke-burst (paragraph)**: 100 chars/sec into a moderate doc; assert each keystroke handler ≤ 4 ms (p99); assert no main-thread block > 16 ms during the burst.
2. **Keystroke-burst (mixed-content)**: same, but the doc contains a math block, a code-block, a list, and a blockquote.
3. **Structural-edit stress**: 10 Enters in a row at end of paragraph; assert each completes within 100 ms; assert caret is in the new block on each.
4. **Parse-arrival load**: synthetic large doc (~64 KB, ~256 KB); assert parse-arrival processing within budget.

Benchmarks fail the build if exceeded. They run in CI on every commit to the new library.

---

## 10. Testing strategy

### 10.1 Test layering (mirrors the architecture)

Each layer L<sub>n</sub> has tests that:
- Assume L<sub>k<n</sub> are real and working.
- Mock or stub *upper* layers' presence (so L4 tests don't depend on L5–L8 existing).
- Use fixtures over real bytes (CRDT, parser) rather than mocks.

This is what enables the new library to land in phases — each phase's tests verify the layer up through that phase, against real foundation behaviour.

### 10.2 Per-layer test surfaces

| Layer | Test executable | Coverage |
|---|---|---|
| L0 | `tst_live_render_coords` | byte/qtPos/block-local conversions; allocation-free assertions |
| L2 | `tst_live_render_block_model` | diff ops; per-row sequence tagging; speculative-kind reconciliation |
| L3 | `tst_live_render_cursor` | Shape 1 invariants; selection construction; collab-survival rules; hit-test math (ported from spike) |
| L4 | `tst_live_render_paragraph_edit` | sequence-tagged binding; freshness rule; cycle guards (the surviving 3) |
| L5 | `tst_live_render_structural` | per-kind structural-key dispatch; IME composition; undo coalescing |
| L6 | `tst_live_render_speculation` | inline-format predictions; fence-opener kind speculation; sequence-tagged drop |
| L7 | `tst_live_render_lists_blockquotes` | Tab/Shift-Tab; nested structures; Enter-on-empty outdent |
| L8 | `tst_live_render_math` | BlockSelected ↔ BlockInternalEdit transitions; F2 / Esc / double-click; LaTeX edit roundtrip |
| Integration | `tst_live_render_qml` | QML smoke + QSignalSpy-driven scenarios; full stack |
| Benchmark | `tst_live_render_bench` | the §9.3 scenarios |

### 10.3 Dogfood as the verification gate

Per the repair plan's anti-goal #2: `QTest::keyClick` does not reproduce the async parse round-trip's timing. Each phase's acceptance criterion includes a manual dogfood pass on `markoff-live-render-app` (a copy of the existing `markoff-view-qml-app` test app, repointed at the new library) by the user, with concrete scripts:

- R4 (paragraph): "Type a 200-word paragraph at 100+ wpm into a 5-page document; cursor never jumps; characters never scramble."
- R5 (structural): "Press Enter at the end of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time; Backspace at the start of each merges back, restoring the original." **(Amended A2, 2026-05-04 — supersedes A1.)** All Enter cases — mid-block, end-of-paragraph, start-of-paragraph — are delivered by R5 + R5.5 (marker-paragraph). R5 ships mid-block Enter, Backspace-merge, Delete-merge, and Shift-Enter; R5.5 ships EOB-Enter and start-of-paragraph-Enter via the marker design (`docs/specs/2026-05-03-marker-paragraph-design.md`).
- R5.5 (marker-paragraph): "Press Enter at end of every paragraph in a 10-block doc; a marker paragraph appears with caret; type 5–10 characters into each; the marker is replaced atomically with the typed bytes (no two-step commit). Type 200 words at 100+ wpm across multiple Enter-created paragraphs; no character scramble; focus-out without typing scrubs the marker; saved file equals on-screen content; reload of a marker-bearing file yields a marker-free document. Per `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md`."
- R7 (lists): "Type a 5-level nested list with Tab/Shift-Tab; toggle bullet/numbered; Enter on empty outdents one level."
- R8 (math): "Type `$$\int_0^\infty e^{-x} dx$$`, click outside to render, double-click to re-edit, Esc to deselect; selection across paragraph + math + paragraph copies cleanly."

Test passes ≠ acceptance. Both required.

---

## 11. Phasing (γ)

Phases. Each phase has a scope, an acceptance criterion (test passes + dogfood scripts), and an explicit exit handover.

### R1 — Foundation surfaces (1–2 weeks)

**Scope.**
- `MarkoffDocument::parseUpdated` extended to carry `parseInputEditSequence` (4th argument).
- `markoff-parser` extended: per-block inline span data pre-baked into a public-API value type (e.g. `TopLevelBlock::inlineSpans`). Replaces `InlineFormatHighlighter::rebuildSpans`'s per-delegate parser instantiation.
- `markoff-view-qml`'s consumers updated to accept the new signal shape (no behaviour change; just a parameter ignored).
- New library scaffold: `libs/markoff-live-render/` with CMakeLists, public-include skeleton, namespace `Markoff::LiveRender`, empty test executable, empty test app.
- Decision pinned: namespace `Markoff::LiveRender`, QML URI `org.markoff.live-render 1.0`, test prefix `tst_live_render_*`.

**Acceptance.** Existing 78/78 foundation+parser+view-qml test pass. New library configures and builds (zero functionality, zero tests yet). Foundation delta has a unit test confirming `parseInputEditSequence` is the captured value.

### R2 — Read-only render with diff + L0 coordinates (1–2 weeks)

**Scope.** L0 + L1 + L2.
- `Coordinates.{h,cpp}` with full unit-test coverage; CI no-allocation assertions on hot paths.
- `LiveBlockModel` implemented; per-row `lastEditEditSequence` stored but not yet used. Diff via `AstBlockDiff` (port).
- `LiveListModelBinding` implemented (subscribes to `parseUpdated`; runs diff; applies ops). No editing yet; no cursor.
- `LiveView.qml` minimal: `ListView` + 5 read-only delegates (paragraph, heading, code-block, hr, image). No selection, no cursor, no keys.
- `BlockKindRegistry` implemented; built-ins registered.

**Acceptance.** Test app loads any Markdown file; renders correctly; resizing works; KSyntaxHighlighting on code blocks works. `tst_live_render_block_model` passes for diff scenarios.

### R3 — Cursor + selection (TextCaret + BlockSelected) (2–3 weeks)

**Scope.** L3.
- `LiveCursorState` implemented; focus protocol live (§5.3); routes through `LiveBlockModel::rowsInserted` deterministically.
- `LiveSelectionView` projects `Session::primarySelection` ↔ Shape 1. `TextCaret` and `BlockSelected` variants both live (so hr and image become focusable as units in this phase). `BlockInternalEdit` deferred to R8 — until then no block declares an internal edit mode.
- `BlockHitTester` (C++ port of `hit()` JS); spike-pinned tests preserved. Click on hr/image yields `BlockSelected`; click on a text block yields `TextCaret`.
- `LiveClipboardController` carries forward; `serializeForCopy` routed through delegates.
- Cross-block selection rendering working, including ranges that include `BlockSelected` endpoints (per §3.4 invariants).

**Acceptance.** Test app: click into any block, caret or focus ring appears; arrow keys move within and across blocks (including hr/image which navigate as a unit); click-drag selects across blocks of mixed variants; Ctrl-C copies. `tst_live_render_cursor` passes. Dogfood script: select across a paragraph, an hr, a heading, an image, and a code-block; copy; paste into another editor; bytes match source.

### R4 — Paragraph editing through sequence-tagged binding (2–4 weeks)

**Scope.** L4.
- `LiveEditBinding` per-delegate; sequence-tagging of affected rows.
- Freshness rule applied in `LiveListModelBinding::onParseUpdatedAt`.
- The three surviving cycle guards (4.5) implemented; documented per-class.
- `applyTextUpdate` invokable on text-bearing delegates.

**Acceptance.** R4 dogfood script (§10.3) passes. `tst_live_render_paragraph_edit` covers the audit's bug class as regression tests:
- Typing during an in-flight parse never scrambles (no "skip if focused" exception).
- Mid-block content is not duplicated on parse-back.
- Speculative-fence kind speculation reconciles cleanly on parse arrival.
- IME composition deferral works.
**Crucially: no `Qt.callLater` retry loops anywhere in the QML.**

### R5 — Structural keys (2–3 weeks)

**Scope.** L5.
- `LiveStructuralKeyHandler` with descriptor-based dispatch.
- Paragraph kind: Enter (split / new), Backspace at start (merge), Delete at end (merge), Shift-Enter (soft break).
- Heading and code-block kinds: their structural-key declarations.
- `UndoCoalescer` policy; calls `coalesceLastUndo()`.

**Acceptance.** R5 dogfood script. `tst_live_render_structural`. Cursor focus on the right block after every structural edit, deterministically. **(Amended A1 → A2, 2026-05-04.)** End-of-paragraph and start-of-paragraph Enter cases are scoped out of R5 and into R5.5 (marker-paragraph; see below). R5 closes correctness-clean for mid-block split, Backspace-merge, Delete-merge, and Shift-Enter; the EOB-Enter / start-of-paragraph-Enter cases are documented limitations until R5.5 lands.

### R5.5 — Marker-paragraph (2–3 weeks) — *(Amended A2, 2026-05-04 — supersedes A1's "paragraph holes" scope.)*

**Scope.** Marker-character source-edit for paragraph EOB-Enter and start-of-paragraph-Enter. Specified at `docs/specs/2026-05-03-marker-paragraph-design.md`. Plan at `docs/plans/2026-05-03-live-render-r5-5-marker-paragraph.md`.

- `Marker.h` — single named constant `kMarkerChar = U+200B` (ZWSP), 3 UTF-8 bytes.
- `MarkerScrubber.{h,cpp}` — stateless service with `scrubOnFocusOut(blockIndex)`, `scrubBeforeSave()`, `scrubAfterLoad()`, `isMarkerOnlyParagraph(blockIndex)` predicate.
- `LiveStructuralKeyHandler` extension — paragraph EOB-Enter inserts `"\n\n​"`; start-of-paragraph-Enter inserts `"​\n\n"` (note byte order); cursor delivery via `requestTextCaretAtNewRow`. Stacked-Enter on a marker-only paragraph is a no-op. Backspace at qtPos 0 of a paragraph following a marker-only block scrubs the marker.
- `LiveEditBinding` extension — atomic-bundled-edit primitive: on focus-in to a marker-only paragraph, set `m_pendingMarkerScrub`; on the next `onContentsChange`, build *one* `MarkoffEdit` that replaces the marker bytes with the user's typed bytes — no two-step "type-then-scrub."
- `LiveListModelBinding` extension — owns the `MarkerScrubber` instance; connects `MarkoffDocument::documentReloaded → MarkerScrubber::scrubAfterLoad` (a no-op at load time per design §6.4 — the host must call `scrubAfterLoad()` again post-parse to actually clean disk-loaded markers).
- `UndoCoalescer` — single regime (CRDT undo only); the v2 hole-routing branches are removed.
- QML clipboard scrubber — `serializeForCopy` strips ZWSP from copy output.
- All v2-holes code retired: `LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, the `aboutToCommit` / `holeReified` / `holeBufferChanged` / `holeAbandoned` / `holeInserted` signals, the `BufferTextRole` / `IsHoleRole` / `HoleIdRole` model roles, the `HoleBlockId` discriminator on `BlockId`. `LiveBlockModel` is restored as the model the QML `ListView` binds to directly.

**Acceptance.** R5.5 dogfood script (above, in §10.3). All §13 unit tests + harness-driven tests in the marker-paragraph design pass. Manual dogfood of ≥200 words across ≥10 paragraphs in `markoff-live-render-app` with no character scramble, clean save, marker-free reload.

### R6 — Other text blocks + speculation refresh (2–3 weeks)

**Scope.** L6.
- `LiveSpeculationLayer` (renamed from `LiveProjectionLayer`) implemented; predictions only, sequence-tagged.
- `InlineFormatHighlighter` refactored to consume pre-baked spans (no fresh parser per delegate).
- `LiveSpeculativeFenceController` carried forward, sequence-tagged.
- Heading + code-block + hr + image full delegate work (BlockSelected for hr/image; TextCaret for heading/code-block).

**Acceptance.** Inline-format predictions for `**bold**`, `*italic*`, `` `code` ``, `~~strike~~`, `==highlight==` apply during typing without flicker. Fence speculation flips paragraph→code-block before parse confirms. No `tst_realistic` regressions.

### R7 — Lists + blockquotes (3–4 weeks)

**Scope.** L7. The hardest phase by some margin.
- `ListItemDelegate`; nested ordered/unordered/task list semantics.
- `BlockquoteDelegate`; nested blockquote composition.
- Structural-key kind handlers: Tab indent / Shift-Tab outdent / Enter sibling / Enter on empty outdents / type-cycle (` -` → `*` → ` 1.` etc., post-restoration).
- Per-kind `BlockKindDescriptor` entries.

**Acceptance.** R7 dogfood script. Full nested-list editing without surprising parse-back behaviour.

### R8 — Math block + BlockInternalEdit support (2–3 weeks)

**Scope.** L8. Adds the `BlockInternalEdit` cursor variant (the R3 baseline shipped only `TextCaret` and `BlockSelected`).
- `LiveCursorState` extended to handle `BlockInternalEdit` transitions; focus protocol covers entry/exit and key routing during internal-edit mode.
- `MathBlockDelegate` with BlockSelected ↔ BlockInternalEdit{"editing-latex"} transitions.
- jkqtmathtext integration for render mode.
- Internal LaTeX edit reuses `LiveEditBinding` against the math block's source bytes.
- F2 / double-click to enter edit; Esc to exit.
- Selection across math is tested (range endpoints in adjacent text blocks; math covered as a whole-block unit per §3.4).

**Acceptance.** R8 dogfood script.

### R9 — Per-block context menu (1–2 weeks)

**Scope.** III.c.
- `ContextMenu.qml` consumes `BlockKindDescriptor::contextMenuActions`.
- Each kind declares actions; default actions (copy, cut, delete, format-as-X) are descriptor-shared.
- Context menu is selection-aware (acts on the selection if non-collapsed; on the focused block otherwise).

**Acceptance.** Right-click on any block produces sensible menu; actions execute correctly.

### R10 — Hardening (2–4 weeks)

**Scope.**
- `tst_live_render_bench` scenarios; CI enforcement.
- Profile-guided optimisation against the 16/50/4/8 budget.
- Dogfood iteration with bug fixes against an explicit regression-target list (the repair plan's §3 catalogue acts as the acceptance regression list).
- Deprecate `markoff-view-qml`'s live mode; the test app's `--live` flag now points at `markoff-live-render`.
- Retire `markoff-view-qml`'s live-rendering files (delegates, projection layer, etc.); source mode stays.

**Acceptance.** All §9.3 benchmarks green. Dogfood pass on all R4/R5/R7/R8 scripts. Old live-mode files removed.

### Phase dependencies

```
R1 → R2 → R3 → R4 → R5 → R5.5 → R6 → R7 → R10
                                   ↘
                                     R8 → R9 → R10
```

R5.5 (marker-paragraph) sits between R5 and R6. R7 and R8 can parallelise after R6; R9 wants R8 (math context menu actions), so it follows R8. R10 depends on all preceding.

### Total budget

20–33 weeks (was 18–30; R5.5 adds 2–3 weeks). Phase boundaries are gates with explicit acceptance criteria and dogfood pass; they are not "merge-and-move-on."

---

## 12. Out of scope for restoration

Explicitly *not* delivered by the work this spec covers:

- **III.a Drag-to-reorder.** The CRDT-operation-shape problem (mapping a user-initiated block move to an applyLocalEdit that produces the same diff the parser would) is its own design effort; restoration deliberately does not preempt it.
- **III.b Per-block toolbar surface on focus.** Decoration on the same hooks as III.c; trivially addable post-restoration but not in restoration.
- **III.d Wikilinks / embeds.** The link service exists in the foundation; the live-render side hooks for hover-preview and click-to-navigate are post-restoration.
- **III.e Footnotes.** Same: post-restoration.
- **III.f Find-in-file across blocks.** `SearchBackend` exists; live-mode wiring is post-restoration.
- **I.b–I.e** (mermaid, video, embedded image-with-caption, tables): Admit only. Architecture must allow them; we don't build them.
- **II.c–II.d** (callouts, frontmatter): Admit only.
- **Multi-cursor / secondary selection UI.** Foundation supports it; restoration ships single-cursor only.
- **Replace UI.** `ReplaceController` exists; live-mode UI is post-restoration.
- **Async completion in live mode.** Emoji is synchronous; cross-thread completion deferred.
- **CommandFacade-driven block toolbar.** The dispatch hooks exist post-R9 (context menu); a toolbar UI is post-restoration.
- **Saved file format mapping for non-Markdown blocks.** Math, mermaid serialise to standard Markdown forms (`$$…$$`, ` ```mermaid `); custom block kinds register their own (de)serialisers via `BlockKindDescriptor`; this spec does not enumerate custom-block file formats.

---

## 13. Future work — D evolution

The companion document `docs/specs/2026-05-02-d-evolution-proposal.md` will outline the per-block-CRDT architecture: each block holds its own CRDT for text content, a higher-level structural CRDT manages the block list, and the parser's role shrinks to inline-only. The C-restoration architecture this spec describes is *compatible with* D in the sense that:

- The Shape 1 cursor model (§3) lifts directly into D; `BlockId` simply becomes the structural CRDT's stable identity.
- The sequence-tagging mechanism (§4) is a degenerate single-block-CRDT case in D — each block CRDT has its own edit/parse sequence; the freshness rule applies per-block instead of per-row.
- The `BlockKindDescriptor` contract (§5) is unchanged; it's already defined in terms of "what does this block do," not "where does the text live."
- Structural-key dispatch (§5.4) is unchanged; structural keys mutate the structural CRDT in D instead of inserting `\n\n` into a doc-level rope, but the dispatch layer is the same.

D is therefore not a rewrite of restoration; it is a foundation re-shaping that keeps the live-render library mostly intact. The D doc proposes the foundation reshape as a concrete ask of the `collabtext` maintainers for their evaluation.

---

## 14. References

### Diagnostic (read first)

- `docs/2026-05-02-live-view-architectural-audit.md` — code-only audit; identifies the six-sources-of-truth root cause.
- `docs/plans/2026-05-02-live-delegate-repair.md` — bug catalogue; this spec supersedes its plan portion but inherits its bug list as a regression target.

### Prior specs (read critically; partly superseded)

- `docs/specs/2026-04-29-live-render-design.md`
- `docs/specs/2026-04-30-live-editing-design.md`
- `docs/specs/2026-04-30-block-anchor-foundation-design.md`
- `docs/specs/2026-05-01-live-projection-layer.md`
- `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md`

### Foundation surfaces

- `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h` — public API; `editSequence`/`parseSequence` already exposed.
- `libs/markoff-foundation/CLAUDE.md` — public-boundary types; CRDT-internal vs CRDT-free split.
- `libs/markoff-parser/include/markoff-parser/Document.h` — `topLevelBlocks()` (post-C-7).

### Spike artefacts

- `.spike/cross-block-selection/` — the `hit()` math; ports to `BlockHitTester` in R3.

---

## 15. Open questions deferred to the implementation plan

Things this spec deliberately does not pin precisely; the next step (writing-plans) resolves them:

1. The exact API shape of `BlockKindDescriptor::consumedStructuralKeys` and the kind-handler-registration mechanism (function-pointer table vs interface vs `std::function` map).
2. The exact form of `applyTextUpdate(newText)` — whether it takes raw bytes, a CRDT anchor + length, or a delta record.
3. Whether `BlockKindRegistry` is a singleton, a per-binding instance, or a service-locator. (Singleton is simplest; per-binding is more testable.)
4. The undo-coalescing policy's idle threshold (currently 1 second in `LiveEditBinding`); whether to surface it as a Setting or pin it as a constant.
5. Test-app shape: is `markoff-live-render-app` a copy of `markoff-view-qml-app` with the URI changed, or a fresh app? Resolved at R1.
6. Which tests in `markoff-view-qml`'s suite migrate to the new library as behaviour contracts (vs tests that probe the old architecture's internals and are deliberately dropped).
7. How `LiveSelectionView` projects multi-block selections containing non-text variants (for now: collapses to whole-block coverage; precise rendering rules at R8).
8. The rename moment: when does the test app's `--live` flag default to the new library? Proposed: end of R5 (paragraph + structural keys done) for opt-in; end of R10 for default.
9. **(Resolved by A2, 2026-05-04 — supersedes A1.)** Marker-paragraph scope — paragraph-only initially; whether the marker pattern generalises to other block kinds (list-items, fence interiors, blockquote, callout, table cells, links, wikilinks, footnotes, math) is a separate spike when those phases land. Same boundary the v2 holes design had.

**Resolved by A1 (2026-05-03), then re-resolved by A2 (2026-05-04):**

- §3.1 BlockId type — A1 answered "discriminated union with `HoleBlockId`"; A2 reverts to the original C-spec definition `using BlockId = Markoff::BlockAnchor`. The marker-paragraph design eliminates the need for a discriminator because every block (including marker paragraphs) is parser-real and CRDT-anchored from creation. See `docs/specs/2026-05-03-marker-paragraph-design.md` §12.

These do not block plan derivation; they are flagged so the plan's task list can pick them up explicitly rather than smuggle decisions in implicit code.

---

*End of specification.*
