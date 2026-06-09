# markoff-styled — Table rendering (read-only) design

**Status:** spec-draft (2026-05-30). Design dialogue complete; user settled
scope and the four UX forks (see §1.3). User authorized: spec → plan →
implementation autonomously.

**Scope:** Render `BlockKind::Table` blocks as real graphical grids (native
Qt `QTextTable`) in the `markoff-styled` view leaf. **Read-only in this
phase** — the table is not editable inside the styled view; to edit a table
the user drops to Source mode (which already shows editable pipe-text today)
and returns. Everything *around* the table stays fully editable as today.

**Out of scope (deferred; architecture pre-accommodates):** in-grid cell
editing, structural ops (add/delete row+column), alignment-setting UX,
source-reveal disclosure UX. Decisions for those are recorded in §9 so the
phase-1 seam is built to host them, not to be reworked for them.

---

## 0. TL;DR

Today a `BlockKind::Table` block falls through `FormatPass`'s kind switch to
`applyParagraph` (`FormatPass.cpp:525`), so a pipe table renders as raw
monospace text, one row per line. This spec replaces that fallthrough — for
Table blocks only — with **materialization of a native `QTextTable` frame**
in the view's `QTextDocument`.

The single hard problem is that the styled view's `QTextDocument` is a
**flat-text mirror** of `MarkoffDocument::widgetFlatView()`, maintained by
`SourceTextDocumentBinding`. **Verified mechanism (2026-05-30, reading
`SourceTextDocumentBinding.cpp:500-545`):** the reverse path
(`onD2DocumentChanged`) is a **whole-document** string diff — it builds
`expected = QString::fromUtf8(widgetFlatView())`, reads
`actual = m_textDocument->toPlainText()`, computes the longest common
prefix + suffix, and replaces the differing middle span with one
`QTextCursor::insertText`. It has **no** block-structure awareness. A
`QTextTable` frame's `toPlainText()` is cell-text-as-paragraphs (no `|`, no
alignment row) — so for a table block `actual` will *never* equal the
pipe-source `expected`, and on **every** model change (even to an unrelated
block) the diff flags the table region as "changed" and rewrites it to flat
pipe text, **destroying the frame**. This is confirmed, not hypothetical;
it makes the opaque seam load-bearing rather than optional.

The resolution — the spine of this design — is the **opaque-block seam**: when
a view registers an `OpaqueBlockRenderer`, the binding's reverse path switches
from the whole-document text diff to **per-block reconciliation**. It walks the
model blocks in lockstep with the QTextDocument's top-level elements (normal
blocks are `QTextBlock`s; an opaque block is a `QTextTable` child frame of the
root frame, tagged by `setComment`) and, per block:
- **normal block, text changed** → rewrite just that block's text region
  (preserves formatting/cursor elsewhere, same goal as today's diff);
- **opaque block, model buffer changed (or first appearance / kind change)** →
  remove its frame region and re-render via the view callback;
- **anything unchanged** → leave it alone (this is what lets a frame survive a
  change to an *unrelated* block).

It also tracks each opaque block's **document-position span separately from its
flat-byte span** so following blocks' coordinates stay correct. The styled leaf
supplies the callback that builds/replaces the `QTextTable`.

**Risk isolation:** the per-block path engages *only* when a renderer is set.
`markoff-source` sets none, so it keeps the byte-for-byte current whole-document
diff — the shared-core change is inert for every existing consumer.

Read-only keeps this bounded: forward edits never originate inside the frame
(the view swallows them and points to Source mode), so only the reverse path
and the coordinate bookkeeping need the seam — not a full grid→buffer
reverse-sync. That reverse-sync is the bulk of the deferred editable work, and
it layers onto the exact same seam.

---

## 1. Frame

### 1.1 Why now

Tables are the last common block kind that renders as raw text in the styled
view. The model layer has supported them since before this leaf existed
(`BlockKind::Table`, parser `TableHandler`, `loadFromMarkdown` mapping at
`MarkoffDocument.cpp` `Kind::Table → BlockKind::Table`, and the passthrough
serializer at `BlockSerializers.cpp:172`). Save/load already round-trips pipe
source byte-for-byte. The only missing piece is *display*.

### 1.2 The prior art, and why it does and doesn't transfer

A master-era `QTextTable`-native table editor was deleted at commit `309f9ce`
(readable at `309f9ce^`): `TableConverter.{h,cpp}`, `TableSerializer.{h,cpp}`,
plus distributed cell-navigation / row-col ops in the old leaf's `Editor.cpp`
and `TextControl.cpp`. It targeted a `QTextDocument` — *our* substrate.

**Transfers (pure `QTextCursor`/`QTextTable`, no scene, no CRDT):**
- `TableConverter::convert`'s insert mechanics — build a `QTextTable` with a
  `QTextTableFormat` (border, cell padding, spacing) and fill cells. Used in
  this phase for materialization.
- `TableSerializer::parseAlignments` — parse the `:---:` separator line into
  `Qt::Alignment` per column. Used in this phase to render alignment.
- `TableSerializer::serialize` (QTextTable → padded pipe markdown) — **not**
  used in read-only phase 1 (the model buffer is canonical; we never serialize
  the frame back). Reserved for the editable phase.

**Does NOT transfer — the authority is inverted.** The old design made the
`QTextDocument`/`QTextTable` *canonical* ("once a table, always a table"; pipe
text is serialization-only; it deliberately removed any pipe↔doc-position
bridge). Our model keeps the **GFM block buffer in `MarkoffDocument`
canonical** and the `QTextDocument` a derived view. So the old
`reconcile`/`TableRecord`/150 ms reparse loop, `SelectableItem`, and
`buildHighlightingSource` are discarded. (Cited per INVARIANTS §1.)

**Landmine inherited from history:** a tree-sitter grammar bug once let an
empty pipe row be accepted as a delimiter cell, splitting one table into two
`pipe_table` nodes — which caused data loss in the old leaf. The current
`markoff-parser/TableHandler` must be checked against that case; verification
work-unit in the plan.

### 1.3 Settled scope & UX forks (from design dialogue)

- **Read-only this phase.** Edit a table via Source mode, not in the grid.
- **Render approach: real `QTextTable` grid** (chosen over monospace
  prettify-as-text — that would be ragged on unpadded source and reintroduce
  caret drift if re-padded).
- **Alignment:** the read-only grid *honors* parsed alignment (cheap: per-cell
  `QTextBlockFormat::setAlignment`). *Setting* alignment is deferred; chosen
  future mechanism is a **context menu** (right-click column → L/C/R). The
  styled view has no context menus yet, so that is net-new later.
- **Source reveal (deferred):** chosen future mechanism is **in-place flip** —
  peel the table back to raw pipe-text where it sits (literally today's
  un-materialized rendering) and re-materialize on caret-leave/toggle. "The
  grid is a lens; the markdown is always underneath."
- **Editable later:** full structural ambition (cell edit + add/del row+col),
  ported from the master-era ops onto the opaque seam.

---

## 2. Architecture & data flow

```
MarkoffDocument (canonical: Table block buffer = raw GFM pipe source)
   │  d2DocumentChanged
   ▼
SourceTextDocumentBinding::onD2DocumentChanged   (REVERSE path, runs FIRST)
   │  for each changed block:
   │    • normal block  → incremental prefix/suffix text-diff (today)
   │    • OPAQUE block  → if buffer changed, call view's render callback;
   │                       else leave the document region untouched   [NEW]
   ▼
StyleApplier::onD2Changed → FormatPass::apply    (FORMAT pass, runs AFTER)
   │  normal block  → applyHeading/Paragraph/... (today)
   │  Table block   → SKIP block-format walk (the frame owns its formatting)
   │                  [the frame was built by the binding callback, not here]
   ▼
QTextEdit renders the QTextDocument (Qt lays out QTextTable natively)

User keystroke inside a table frame
   ▼
StructuralTextEdit::keyPressEvent → swallow (read-only) + hint "edit in Source"
   ▼
(no model mutation; forward path never fires from inside a frame)
```

**Authority (L4, decided in writing per INVARIANTS §2):** the Table block
**buffer is canonical**. The `QTextTable` frame is a *derived view* of that
buffer, rebuilt by the binding's opaque-render callback whenever the buffer
changes and never the other way around in this phase. This matches every other
block kind (model wins; document mirrors). The new piece is that the mirror is
an object frame instead of text — handled entirely behind the opaque seam.

**Who builds the frame — the binding callback, not FormatPass.** This is the
key ordering decision. The binding's reverse path runs on `d2DocumentChanged`
and is the *sole writer of document structure* for the table region. If
FormatPass also tried to materialize, two handlers would write the same region
on every change → the dual-authority race INVARIANTS §3 forbids. So:
- The **binding** owns *what text/object occupies a block's document region*
  (it already does, for normal blocks). For opaque blocks it delegates the
  "what" to the view callback but stays the single writer.
- **FormatPass** owns *character/paragraph formatting of normal blocks*. For a
  Table block it does nothing (the frame is self-formatting). The Table
  fallthrough at `FormatPass.cpp:525` becomes an explicit `continue`/skip.

---

## 3. The opaque-block seam (markoff-core)

This is the one change to shared `markoff-core`. It is deliberately
**view-agnostic** — the binding gains no `QTextTable` knowledge.

### 3.1 The interface

```cpp
// New, in markoff-core, consumed by view leaves.
class OpaqueBlockRenderer {
public:
    virtual ~OpaqueBlockRenderer() = default;

    /// Is this block rendered as an opaque object (not flat text)?
    /// Called by the binding during reverse sync and coordinate math.
    virtual bool isOpaque(BlockId id, BlockKind kind) const = 0;

    /// (Re)build the opaque representation for `id` at the cursor's current
    /// position. The cursor is positioned at the block region start and the
    /// old region (if any) has been selected+removed by the binding. The
    /// callback inserts its object (e.g. a QTextTable) and leaves the cursor
    /// at the region end. Returns the number of QTextDocument characters the
    /// inserted representation occupies (the binding records this as the
    /// block's document-position span).
    virtual int renderOpaque(QTextCursor &at, BlockId id) = 0;
};
```

The binding gets `void setOpaqueRenderer(OpaqueBlockRenderer*)`. `nullptr`
(the `markoff-source` case) → today's behavior unchanged.

### 3.2 Coordinate bookkeeping — the load-bearing detail

Today the binding (and `FormatPass`) assume **doc char count for a block ==
its flat char count**, so `byteOffsetToQtPos(flatBytes, byteOffset)` is a pure
function of `flatBytes`. A frame breaks that: a 3×2 table is dozens of
pipe-source bytes in `flatView` but a fixed, different number of *document*
positions.

The binding must therefore maintain, while opaque blocks exist, a mapping from
flat-byte block boundaries to document-position block boundaries that accounts
for each opaque block's actual document span (returned by `renderOpaque`). All
position translation for blocks *after* an opaque block must go through this
mapping, not raw `byteOffsetToQtPos`.

`FormatPass` consumes the same mapping: when it walks to the block *after* a
table, its `startQt` must come from the binding's doc-position map, not from
summing flat byte sizes. The simplest implementation exposes the binding's
per-block document ranges to `FormatPass` (FormatPass already imports the
binding for `byteOffsetToQtPos`). **Decision:** the binding owns a
`documentRangeForBlock(BlockId) -> {startPos, length}` query, populated during
reverse sync; `FormatPass` uses it for `startQt`/`endQt` instead of computing
from `flatBytes` when any opaque block is present. When no opaque blocks exist,
both paths are byte-identical to today (regression-safe).

### 3.3 Reverse-path behavior — two modes

`onD2DocumentChanged` branches once at the top:

```cpp
if (m_opaqueRenderer)  reverseSyncPerBlock();   // NEW path (§3.3b)
else                   reverseSyncWholeDoc();   // EXISTING code, verbatim
```

**3.3a Whole-doc mode (existing, unchanged).** The current lines 510–544 move
into `reverseSyncWholeDoc()` with zero edits. This is the `markoff-source` path
and the regression-safe default.

**3.3b Per-block mode (new).** Walk the model's `iterateBlocks()` in lockstep
with the QTextDocument's top-level elements (root-frame iterator: each model
block pairs with either a `QTextBlock` run or, for opaque blocks, a
`QTextTable` child frame identified by `setComment("markoff-table:<id>")`). Per
model block:
- **Not opaque** → if the block's current document text ≠ `blockText(id)`,
  replace that block's document region via `QTextCursor` (bounded prefix/suffix
  diff within the block to keep cursor/formatting stable). Else skip.
- **Opaque, buffer unchanged** (hash matches last render) → **skip** — the frame
  stays. This is the frame-survival guarantee for edits to other blocks.
- **Opaque, buffer changed / first appearance / kind→Table** → select the
  block's current document region, remove it, position the cursor, call
  `renderer->renderOpaque(cursor, id)`, record the returned length as the
  block's document span.

All mutations run under the existing `m_applyingRemoteEdit` guard so the
forward `onQtContentsChange` is suppressed (binding line 404).

**Lockstep robustness.** The walk assumes a 1:1 ordering between model blocks
and doc top-level elements. It is rebuilt from scratch each reverse pass (no
persistent fragile handles); a mismatch (counts diverge) falls back to a full
`syncQtDocumentFromMarkoff()` + re-render-all — correct, if heavier. The
fallback is logged (not silent) so we learn if it ever fires in practice.

### 3.4 Why a general seam, not a table special-case

`Image`, `Math`, `Mermaid`, and `HtmlBlock` *also* fall through
`FormatPass.cpp:525` to `applyParagraph` today. Every one of them is a future
opaque-object candidate (an image thumbnail, a rendered equation, a diagram).
Building the seam generically — "blocks the view declares opaque get
object-rendered behind a callback" — means those are later additions to the
styled `isOpaque` switch, not re-architectures. (This is the same instinct as
the live leaf's L8 "interactive blocks" category, adapted to the QTextEdit
substrate.) We do **not** build those now; we only ensure the seam isn't
table-shaped.

---

## 4. The styled-side table code

A new translation unit in `markoff-styled`, e.g. `TableFrame.{h,cpp}`, owns all
`QTextTable` knowledge. Pure functions over a `QTextCursor` + a GFM buffer.

### 4.1 Parse: GFM buffer → grid model

`parsePipeTable(const QByteArray&) -> ParsedTable` where
```cpp
struct ParsedTable {
    QStringList               header;       // M columns
    QList<QStringList>        body;         // N rows × M
    QList<Qt::Alignment>      alignments;   // M; from the :---: row
    bool                      ok = false;   // false → degrade to text
};
```
Tokenization: split buffer on `\n`; the first line is the header, the second is
the alignment/separator row (ports `TableSerializer::parseAlignments`), the
rest are body rows. Ragged rows are padded with empty cells to the header
column count (GFM rendering rule). If the buffer doesn't parse as a table
(`ok == false`), the view degrades that block to today's text rendering rather
than risk a malformed frame — this is the safety valve for the inherited
grammar landmine (§1.2) and for any future parser drift.

### 4.2 Materialize: ParsedTable → QTextTable

`materialize(QTextCursor &at, const ParsedTable&) -> int` (returns doc-char
span, for the opaque seam). Ports `TableConverter::convert`'s insert mechanics:
- `QTextTableFormat`: `setBorder(1)`, `setBorderStyle(BorderStyle_Solid)`,
  `setCellPadding(…)`, `setCellSpacing(0)`, `setBorderCollapse(true)`.
- `setComment("markoff-table")` so the frame is identifiable on the next pass
  (`rootFrame()->childFrames()` + comment match).
- `cursor.insertTable(rows, cols, fmt)`; fill header (row 0) + body via
  `table->cellAt(r,c).firstCursorPosition().insertText(...)`.
- Header row: bold cell text.
- Alignment: per cell, `QTextBlockFormat::setAlignment(alignments[c])` on the
  cell's block (Qt has no per-column alignment; we apply per cell).
- Theming via `Markoff::Theme` slots where available, matching the existing
  `FormatPass` placeholder-color convention (header tint, grid lines). Exact
  slots are a wiring detail, not load-bearing for read-only.

Inline formatting **inside cells** (bold/italic/links) is **out of scope for
read-only phase 1** — cells render as plain text. (Cell inline-span filtering
via `inlineSpansFor` is part of the editable phase.)

### 4.3 Read-only enforcement

The frame is editable by Qt's default. We forbid edits:
- `StructuralTextEdit::keyPressEvent`: before the existing structural-key
  routing, if `textCursor().currentTable() != nullptr`, swallow all editing
  keys (printable input, Backspace/Delete/Enter/Tab) and surface a one-time
  affordance ("Edit tables in Source view"). Navigation keys (arrows, Home/End,
  PageUp/Down) pass through so the caret can move across the table and out.
- The binding's forward path therefore never receives a `contentsChange`
  originating inside a frame. (If one ever did — e.g. paste — the
  `isApplyingChange()` guard plus the no-op-on-opaque reverse handling contains
  it; defensively, the forward path can early-return when the resolved position
  lands in an opaque block region.)

---

## 5. FormatPass & StyleApplier changes

- **`FormatPass.cpp`**: the `else` fallthrough at line 525 gains an explicit
  `BlockKind::Table` branch that **skips** block-format application (the frame
  is self-formatting) and **skips** the inline-span loop for that block. The
  hash gate still tracks the table block (so an unchanged table is skipped
  wholesale). The list-membership reconciliation (`manageListMembership`)
  already treats non-ListItem kinds as list-breakers — correct for tables.
- **Coordinate source**: when the binding reports any opaque block present,
  `FormatPass` takes `startQt`/`endQt` from the binding's
  `documentRangeForBlock` (§3.2) rather than the flat-byte sum. Behind a guard
  that is a no-op when no opaque blocks exist.
- **`StyleApplier`**: unchanged in shape. It already drives `FormatPass` on
  `d2DocumentChanged`, owns the hash gate, defers kind-suggestions, and
  preserves scroll. Materialization happens in the binding (which connects to
  the same signal and, per §6, must run *before* `StyleApplier`).

---

## 6. Signal ordering

`MarkoffDocument::d2DocumentChanged` drives both the binding (reverse sync,
incl. opaque render) and `StyleApplier` (FormatPass). Qt delivers slots in
FIFO connection order. **Required order: binding first, StyleApplier second** —
the frame must exist before FormatPass computes positions for following blocks.
The styled `Editor` must guarantee this connection order in `setDocument`
(verification + a test that asserts it; INVARIANTS §5 — exercise the real
wiring, not a synonym).

---

## 7. Tests & falsifiable invariants (INVARIANTS §4)

Each invariant gets a test that is **proven to fail** against a stub before the
real implementation lands.

1. **Frame-survival invariant (THE spike — first plan step).** Load a doc:
   paragraph, table, paragraph. Materialize. Then mutate an *unrelated*
   paragraph block and emit `d2DocumentChanged`. Assert the `QTextTable` frame
   still exists (`rootFrame()->childFrames()` count, comment match) and its
   cell contents are intact. Falsifiable: with the opaque seam stubbed out
   (binding does a normal text-diff), the frame is clobbered and the test
   fails. **This test gates the whole design — if the seam can't make a frame
   survive, the architecture is wrong and we stop and rethink before building
   more.**
2. **Render-correctness.** A 3×2 GFM table buffer materializes to a `QTextTable`
   with rows()==3, columns()==2, and cell texts matching the parsed cells.
3. **Alignment honored.** `| :--- | ---: | :---: |` produces left/right/center
   per-cell block alignment.
4. **Ragged-row tolerance.** A body row with fewer cells than the header pads to
   the column count; an unparseable buffer degrades to text (no crash, no
   partial frame).
5. **Read-only.** With the caret in a cell, printable keys and
   Backspace/Delete/Enter/Tab do not mutate the model (`d2EditSequence`
   unchanged) and do not change the frame. Arrow keys still move the caret.
   Driven through the real `StructuralTextEdit::keyPressEvent`.
6. **Coordinate integrity.** Inline formatting on the paragraph *after* a table
   lands on the correct characters (proves the doc-position map in §3.2). A
   wikilink/bold span in the trailing paragraph is styled at the right range.
7. **No-opaque regression.** A document with **no** tables produces byte- and
   format-identical output to today (the seam is inert). Run the existing
   `tst_styled_*` suite green.
8. **Save round-trip.** `serializeForSave()` on a doc whose table was
   materialized (and never edited) returns the original pipe source byte-for-
   byte (the buffer was never touched).

Tests live under `libs/markoff-styled/tests/` (prefix `tst_styled_table_*`),
plus one `markoff-core` test for the opaque-seam mechanism
(`tst_binding_opaque_block`).

---

## 8. Engineering discipline (INVARIANTS 1–8)

- **§1 developmental record cited:** §1.2 cites the deleted `309f9ce^` prior
  art and the inherited grammar landmine.
- **§2 L4 in writing:** §2 — Table buffer canonical; frame is derived;
  one-directional in read-only phase.
- **§3 new authority retires old in same plan:** the binding becomes the single
  writer of the table region (via the opaque callback). FormatPass's Table
  fallthrough (the old "authority" that text-rendered it) is removed in the
  same change, not deferred. No dual writer is introduced.
- **§4 falsifiable tests first:** §7.1 is the gating spike, written to fail
  against a stub before implementation.
- **§5 production callsite:** read-only and ordering tests drive the real
  `StructuralTextEdit`/`Editor` wiring, not direct slot calls.
- **§6/§7 timer/guard smells:** the design adds **no** `singleShot(0)` or new
  re-entrance guard. It reuses the binding's existing `isApplyingChange()`
  guard. If materialization forces a deferral (e.g. frame insertion must follow
  layout settle), it is justified in the commit and logged to the Discipline
  Log — not added silently.
- **§8 Discipline Log:** any smell met while implementing is logged in
  `docs/queue.md`.

---

## 9. Deferred work (pre-accommodated, not built)

The opaque seam (§3) and `TableFrame` (§4) are the foundation for all of it:

- **In-grid cell editing.** Cells become editable; each cell `contentsChange`
  translates (cell, qtPos) → buffer byte offset and calls `d2ApplyBufferEdit`;
  the opaque-render callback's "buffer changed → re-render" path already exists.
  Reverse-sync uses ported `TableSerializer`. Undo routes through
  `MarkoffDocument` `undoD2`/`Cmd::*` (not native QTextDocument undo).
- **Structural ops** (add/del row+column): port master-era `Editor::table*()`
  bodies (pure `QTextTable::insertRows`/`removeColumns` + `beginEditBlock`),
  each followed by serialize → `d2ApplyBufferEdit`.
- **Alignment setting:** context menu (right-click column → L/C/R) — needs the
  styled view's first context-menu infrastructure.
- **Source reveal:** in-place flip (un-materialize a single table to editable
  pipe-text on demand; re-materialize on caret-leave) — the un-materialized
  state is literally today's text rendering, so the flip is `isOpaque`
  returning false for that one block transiently.
- **Cell inline formatting**, cross-cell selection, multi-line cells: as in the
  live-leaf E4 deferral list.

---

## 10. Risks

- **R1 (highest): the frame-survival spike fails.** If `QTextTable` frames
  can't be kept alive across reverse syncs even with the opaque seam (e.g. some
  Qt layout behavior re-flattens frames, or the coordinate map proves
  intractable), the whole approach is wrong. Mitigation: §7.1 is the first
  thing built; we do not proceed until it's green.
- **R2: coordinate map complexity** (§3.2) leaks bugs into *non-table* blocks
  after a table. Mitigation: invariant 6 + the no-opaque regression
  (invariant 7) bound it; the map is inert when no opaque blocks exist.
- **R3: parser disagreement / the grammar landmine** (§1.2). Mitigation:
  `ok==false` degrade-to-text valve (§4.1) + a plan verification step against
  the empty-pipe-row case.
- **R4: shared-binding blast radius.** The seam touches `markoff-core`'s
  binding, used by `markoff-source` too. Mitigation: the seam is inert when no
  renderer is set; invariant 7 + the existing `tst_source_*` suite guard the
  source leaf.
```
