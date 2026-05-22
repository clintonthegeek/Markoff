# E4 — Tables (design)

**Status:** spec-approved (2026-05-22) — design walkthrough complete; user signed off on all sections in full and authorized autonomous spec+plan+execution.
**Parent phase:** E4 (tables, frontmatter, footnote rendering) per E-arc roadmap. This spec covers tables only; frontmatter and footnote rendering remain unscheduled.
**Phase decomposition:** E4 is the first multi-cell internally-editable atomic block. A successor (working name **E4.5**, or rolled into C-scope) will add structural ops — add/delete row, add/delete column, change alignment — without revisiting the architecture established here.

---

## 0. TL;DR

Add `BlockKind::Table` as the first multi-cell member of L8 (interactive blocks). A pipe-table in source becomes a graphical grid widget on parse; cells are individually focusable `TextEdit` instances inside one `TableDelegate.qml`; navigation is Word/Obsidian-equivalent (Tab between cells, arrow keys cross cell boundaries, Esc → BlockSelected, double-key cascade deletes the whole table); cell content supports full inline formatting via the existing `InlineHighlighter` machinery; the alignment row and pipe separators are **never visible in Live mode**.

Once a block is `BlockKind::Table`, no Live-mode edit can produce a non-table from it — only "table with modified cells" or "no block at all." Source-mode edits remain the only path that can break a table back to paragraphs (by invalidating the GFM table grammar).

L8 already exists. `BlockInternalEdit` already exists. `BlockOnlyDelegateBase` already exists. `BlockKindRegistry` already accepts new descriptors. `KindDispatch::delegateClassFor()` is a one-line addition. The architecture is ready to host tables; what's new is **multi-region internal editing inside one block** — N×M `TextEdit` instances sharing one D2 block buffer, with cell-relative qtPos translated to block-buffer byte offsets in the delegate.

---

## 1. Frame

### 1.1 Why now

E3a (wikilinks) is in dogfood; E3b/E3c/E3d (tags/embeds/callouts) are unscoped. Tables are the next core-feature gap that affects real-vault legibility: any document containing a `| col | col |` pipe table currently renders as raw paragraph text in Live mode, demoting the user to Source view to see it correctly.

Tables are also the **architectural forcing function** for "multi-region internal editing inside one block." The L8 layer was named and `BlockInternalEdit` was added precisely for this class of widget; `MathDelegate` exercises it in single-region form (one LaTeX source editor). Tables push it to multi-region. Establishing the pattern with tables makes future callouts (E3d), embeds (E3c), and frontmatter tables (E4-successor) plug-in additions rather than re-architectures.

The "atomic block" category is **not** invented here. It already exists in the codebase under the names L8 / `BlockInternalEdit` / `BlockOnlyDelegateBase` — informally established by HR, Image, and Math. Tables are a new member, not a new category.

### 1.2 What this is not

- **Not structural editing.** Add row / delete row / add column / delete column / change alignment are explicitly out of scope. They require a UI affordance (toolbar, context menu, or row/column handles) and a buffer-rewrite path that this spec does not define. The successor phase covers them.
- **Not multi-line cells.** GFM tables nominally support inline `<br>` for line breaks in cells. Live cells are single-line `TextEdit`s with `wrapMode: NoWrap`. Multi-line cell content rendering is a successor concern.
- **Not nested blocks in cells.** Markdown forbids block-level content inside table cells (no lists, no code blocks, no nested tables, no blockquotes). This spec inherits that restriction.
- **Not per-cell CRDT granularity.** The whole table is one D2 block; collabtext sees one buffer per table. Two collaborators editing different cells *will* conflict at the buffer-byte level. The constraint is documented; per-cell CRDT is the same problem CodeBlock has and accepts.
- **Not autohide-reveal of `|` separators or the alignment row.** Tables are exceptional in the editor: source delimiters are **never** visible in Live mode. The grid widget *is* the editing surface. The pipe-text representation is a serialization format, not a user-facing view. (This rule is what makes tables genuinely interactive widgets rather than "structured text.")

### 1.3 What this enables

- A document containing `| a | b |\n|---|---|\n| c | d |` renders as a graphical grid in Live, with cells editable in place.
- Tab/Shift+Tab, arrow keys, Esc, and the standard Backspace-from-first-cell / Delete-from-last-cell cascades navigate and delete tables the way every other text editor does it.
- Inline formatting (bold, italic, code, wikilinks, standard links) renders inside cells with the same styling rules as paragraph text.
- The pattern established here — multi-TextEdit-per-delegate, cell-relative-to-buffer-byte translation, internal navigation within `BlockInternalEdit` — is the template for future callout / frontmatter-properties / embed-preview interactive blocks.

---

## 2. Architecture & data flow

```
TreeSitterParser
   │  pipe_table → BlockBoundary::Table
   ▼
MarkoffDocument::loadFromMarkdown
   │  Kind::Table → BlockKind::Table (case at MarkoffDocument.cpp:1721)
   │  block buffer = exact pipe-table source bytes (passthrough serializer)
   ▼
LiveListModelBinding::onD2Changed → buildRecords
   │  blockKindToString(BlockKind::Table) → "table"   [NEW string]
   │  delegateClassFor("table") → "table"             [NEW dispatch entry]
   ▼
LiveBlockModel row { kind: "table", delegateClass: "table", text: <full pipe source> }
   ▼
LiveView.qml DelegateChooser
   │  DelegateChoice { roleValue: "table"; delegate: TableDelegate {} }   [NEW]
   ▼
TableDelegate.qml
   │  Parse text → ParsedTable { headers, alignments, body[][] }
   │  Render Grid of TextEdit cells (N rows × M cols)
   │  Each cell: text from ParsedTable[r][c]
   │  Edit in cell → translate (r, c, qtPos) → block-buffer byte offset
   │  Call MarkoffDocument::d2ApplyBufferEdit (via TableEditBinding wrapper)
   ▼
Block buffer updated; reparse-on-buffer-change re-parses the table
   │  Re-derives ParsedTable; cell .text properties refresh
   │  Active cell's TextEdit cursorPosition is preserved through the round-trip
```

**Critical invariant:** the block buffer is **canonical**. The cells display content derived from parsing the buffer. Edits translate to byte-range mutations on the buffer. There is no parallel "model of cells" stored in the delegate — every render parses the buffer fresh. This is the same authority shape as every other text-bearing block (model wins on canonical content; delegate is the mirror).

---

## 3. The no-revert invariant

**Statement.** Once a block in the live document is `BlockKind::Table`, no Live-mode edit path can produce a non-table outcome. The available outcomes of any Live-mode edit on a table are exactly:

1. The table remains a `BlockKind::Table` block with modified cell content.
2. The block is deleted entirely (the table is gone; no demoted-to-paragraph residue).

**Enforcement.** The invariant is enforced *structurally*, not by guard code. No Live-mode edit path reaches the alignment row, the pipe (`|`) separators, the column-count grammar, or the inter-row newlines:

- The `TableDelegate` exposes only per-cell `TextEdit`s. The user cannot place the caret in, select, or edit the pipe characters or the alignment row.
- Cell-content edits translate to byte-range mutations *inside a single cell's slot* in the buffer. The translator never produces an offset that crosses a cell boundary, a pipe character, or a row delimiter.
- Block-level Backspace/Delete cascades delete the *entire* `BlockKind::Table` block; they never produce a partial table.
- Tab in the last cell of the last row is a navigation no-op in B-scope (it exits the table) — it does not modify the buffer.

There is no `Cmd::changeKind(blockId, BlockKind::Paragraph)` call path reachable from any Live-mode user gesture on a Table block.

**Source-mode is unconstrained.** A user who switches to Source mode and deletes the alignment row breaks the GFM table grammar; the next parse no longer emits a Table boundary; the kind-transition heuristic in `LiveListModelBinding::onD2Changed` issues a `Cmd::changeKind` to whatever the parser now reports (typically Paragraph); Live follows. This is by design — leaving Live mode is the user's explicit opt-in to structural editing.

**Detection trigger.** Standard GFM table grammar: a header row + an alignment row (`|---|`, `|:---|`, `|---:|`, `|:---:|`) + zero or more body rows. The parser (`tree-sitter-markdown`) implements this. When the parser emits `BlockBoundary::Table`, `MarkoffDocument` maps `Kind::Table → BlockKind::Table` and the live model issues a cross-class kind transition (Delete+Insert per `delegateClassFor`); the new `TableDelegate` instance mounts with the pipe-table source as its block buffer.

---

## 4. Block model & buffer representation

### 4.1 Block-as-table

One `BlockKind::Table` block per pipe-table in the document. The block buffer holds the **exact** pipe-table source bytes:

```
| Header A | Header B |
|----------|----------|
| cell 1   | cell 2   |
| cell 3   | cell 4   |
```

Internal newlines (between rows) live **inside** the block buffer. They are not block-buffer-terminators in the B1 sense — they are content. The B1 convention ("buffers are content, separators are the serializer's job") accommodates this without modification: the inter-block separator before/after the table is still `"\n\n"`, supplied by `serializeForSave()`; the table's *internal* newlines are part of its content, just like the internal newlines in a fenced code block.

**Serializer.** `BlockSerializers.cpp:155` already registers `BlockKind::Table → serializePassthrough` — the buffer is round-tripped byte-for-byte. No changes to the serializer.

**The parser is the writer of "what the buffer looks like."** When `loadFromMarkdown` runs, the parser emits `BlockBoundary::Table` with `[startChar, endChar)` covering the full pipe-table region; `MarkoffDocument` populates the block buffer with that byte range. The Live side never reshuffles or normalizes the buffer; it just renders it as a grid and applies cell-edit translations on top.

### 4.2 ParsedTable (delegate-local)

`TableDelegate.qml` derives a `ParsedTable` value on every buffer change:

```
ParsedTable {
    QStringList headers;                  // M columns
    QList<Qt::Alignment> alignments;      // M entries; from |:---| |---:| |:---:| |---|
    QList<QStringList> body;              // N rows × M columns
    QList<RowCellRanges> cellByteRanges;  // for each row, [startByte, endByte) per cell
                                          // within the block buffer; INCLUDES the
                                          // header and body rows; the alignment row
                                          // is tracked separately
    AlignmentRowRange alignmentRowRange;  // [startByte, endByte) for the |---|---| row
}
```

`cellByteRanges` is the **translator's lookup table**: given a cell (r, c) and a cell-relative qtPos K, the buffer-byte offset is `cellByteRanges[r][c].start + utf8ByteOffsetForChar(K, cellContent)`.

The `ParsedTable` derivation happens in JS inside the delegate. It's a pure function of the block buffer text. On any buffer change the derivation re-runs. (For C-scope, this is a candidate for moving to C++ if profiling shows the JS parse is hot.)

### 4.3 What the parser is *not* asked to provide

The Live delegate does its own pipe-table tokenization for `ParsedTable`. We do not ask `markoff-parser` for a structured `Table` AST. Reasons:

1. The parser's job in D2 is block boundaries + inline spans, not block-internal structure. Adding "structured table contents" to the public parser surface would be a public-API expansion for one block kind.
2. The pipe-table tokenization is trivial (line split on `\n`, cell split on `|`, alignment from `:---` patterns). Reimplementing it in the delegate keeps the parser API narrow.
3. The translator needs **byte ranges within the buffer** for each cell — information the parser doesn't currently produce (`SourceSpan` covers inline spans, not cell positions). Computing them while we tokenize is cheaper than re-deriving them.

If a future need arises for parser-side table structure (e.g., the source widget wants column-aware editing), the delegate-side tokenizer is a candidate to lift into `markoff-parser`. Not today.

---

## 5. Delegate architecture

### 5.1 L8 placement and registry registration

`BlockKind::Table` joins `Math` and `Image` in declaring `BlockInternalEdit` as a supported cursor variant. Registry entry (added to `BlockKindRegistry::registerBuiltins`):

```cpp
// Table: text-bearing through cells, BlockSelected + BlockInternalEdit.
// Multi-cell internal editing — the first such atomic block.
{
    BlockKindDescriptor d;
    d.id = BlockKind::Table;
    d.acceptsTextRoleUpdates = true;       // cell text comes from buffer
    d.isBlockOnly = false;                  // has internal text-bearing cells
    d.supportedCursorVariants = {
        QStringLiteral("BlockSelected"),
        QStringLiteral("BlockInternalEdit"),
    };
    d.internalEditModes = { QStringLiteral("editing-cell") };
    d.consumedStructuralKeys = {
        Qt::Key_Delete, Qt::Key_Backspace,
        Qt::Key_Up, Qt::Key_Down,
        Qt::Key_Return, Qt::Key_Enter,
        Qt::Key_Tab, Qt::Key_Backtab,
        Qt::Key_Escape,
    };
    d.delegateUrl = QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/render/delegates/TableDelegate.qml");
    // Passthrough serializer: buffer IS the markdown source.
    d.serializer = [](const QByteArray &text,
                      const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
        return text;
    };
    m_descriptors.insert(d.id, d);
}
```

### 5.2 KindDispatch entry

`KindDispatch.cpp::delegateClassFor`:

```cpp
if (kind == QStringLiteral("table"))      return QStringLiteral("table");
```

`LiveView.qml`'s `DelegateChooser`:

```qml
DelegateChoice { roleValue: "table"; delegate: TableDelegate {} }
```

`Markoff::Live::BlockKind::Table` (live-side string constant): `"table"` — added to `libs/markoff-live/include/markoff/live/BlockKind.h` and the corresponding `.cpp`.

`blockKindToString(BK::Table)`: returns `BlockKind::Table` — added to the switch in `LiveListModelBinding.cpp`'s anonymous helper.

### 5.3 `TableDelegate.qml` skeleton

Top-level: a `Rectangle` with `width: ListView.view.width`, `implicitHeight` driven by the grid's natural height. Owns:

- `blockAnchor` capture + `delegateAvailable` / `delegateGoingAway` registration with `LiveCursorState` (same pattern as `CodeBlockDelegate`).
- `isSelected` binding to `cursorKind === "BlockSelected" && focusedAnchorRow === modelIndex` (same as `BlockOnlyDelegateBase`).
- `parsedTable` derived `Item.property var` recomputed on `model.text` changes.
- A `GridLayout` (or `Grid`) of `TextEdit` cells. Each cell is a thin component that:
  - Binds its `text` to `parsedTable.body[row][col]` (or `parsedTable.headers[col]` for the header row).
  - Has `wrapMode: TextEdit.NoWrap`, `textFormat: TextEdit.PlainText`, `selectByMouse: true`.
  - Has `horizontalAlignment` bound to `parsedTable.alignments[col]`.
  - Has an `InlineHighlighterAttached` for per-cell inline-format styling.
  - On `cursorPositionChanged` / `contentsChange`, routes through the cell-buffer translator.
- A header-styling layer: `Rectangle` background tint for the header row, 1-px grid lines via `Rectangle` strips. Colors come from `Markoff::Theme` via `liveBinding.themeColorFor(...)` per the E2.6 color-binding convention. Slot reuse to start: `CodeBlockBackground` for cell background (same accent strategy code blocks and image placeholders use), `Quote` for grid lines (multi-purpose accent slot per E2.6 convention). If a downstream dogfood pass shows table colors need their own slots, splitting them off is a follow-up (parallel to the documented `Quote` and `CodeBlockBackground` future-split possibilities).
- A `takeFocus(qtPos)` function — invoked by `LiveCursorState::tryResolvePending` when focus is being delivered to this block. Default behavior: place focus in cell (0, 0) at qtPos clamped within the first cell's content length. Cross-block nav (down arrow into the table, etc.) sets `pendingVisualLineHint`; `takeFocus` honors it (top-row first cell for "down-into," bottom-row first cell for "up-into").
- A `positionAt(x, y)` function — for the `LiveView.qml` hit-test path. Computes which cell the click landed in, then returns a *flat* qtPos derived from the cell's byte range. (LiveView's hit machinery already treats `qtPos` as "block-buffer position"; the table delegate computes that from cell (r, c, intra-cell-qtPos) on click and returns it.)

### 5.4 Cell focus and `BlockInternalEdit` lifecycle

**Click on a cell** → cell's `TextEdit.forceActiveFocus()`; the cell's `onCursorPositionChanged` calls `cursorState.syncFromTextEdit(blockAnchor, flatQtPos)` where `flatQtPos` is the cell-relative position translated to block-buffer position. `LiveCursorState` enters `TextCaret` state (block, flatQtPos).

**Decision (L4 in writing).** For tables, **the buffer is canonical for cell content**. `m_cursor` in `LiveCursorState` is `TextCaret { block, qtPos }` where `qtPos` is the *block-buffer* position translated from the active cell's TextEdit position. The delegate maintains cell-relative cursor positions in the per-cell TextEdits, but **the cursor state's qtPos is block-buffer-relative**. This matches how every other text-bearing block works — `qtPos` in `m_cursor` is always interpreted in the block buffer's frame.

This means: when `LiveCursorState` programmatically places the caret (via `establishFocus(anchor, qtPos)`), the delegate's `takeFocus(qtPos)` decodes the block-buffer qtPos back to (cell, cellQtPos) and forces focus on the corresponding cell. The decode is the inverse of `cellByteRanges` lookup.

**Why NOT extend the cursor variant with sub-block coords.** Adding `TableCellCaret { block, cellRow, cellCol, qtPos }` would force every call site that handles `m_cursor` (selection logic, structural-key handler, nav controller, find adapter) to know about table cells. The `BlockInternalEdit` variant exists for "I'm inside the block's interactive widget" — that's the state we use for "the table is being edited." Cell granularity is purely a delegate-local concern, translated to/from buffer qtPos at the chokepoint.

**Esc from a cell** → cell loses focus; delegate emits `cursorState.establishBlockSelected(blockAnchor)` (or whatever the existing path is — see §10 verification work-unit). Cursor state moves from `TextCaret` to `BlockSelected`.

**Click outside the table** → standard LiveView hit-test machinery; cursor state transitions to whatever the click resolved to.

### 5.5 No new shared base class

`TableDelegate` does **not** extend `BlockOnlyDelegateBase` (BOD assumes no internal text editing). It does **not** share a base with `CodeBlockDelegate` (CodeBlock has one TextEdit; Table has N×M). Extraction comes opportunistically when a second multi-cell delegate exists (callouts? embed previews?). Invariant 3 ("a new authority retires the old in the same plan") in reverse: don't introduce an abstraction you don't yet need.

---

## 6. Cell-buffer coordinate translation

This is the genuinely new piece. Every other text-bearing delegate has the identity translation: cell qtPos == block-buffer qtPos. Tables have an N×M-to-flat translation owned by the delegate.

### 6.1 Forward: cell-edit → buffer-byte mutation

When a cell's `TextEdit` fires `contentsChange(qtPos, removed, added)`:

1. Lookup the cell's byte range from `cellByteRanges[row][col]`.
2. Compute UTF-8 byte offset of `qtPos` within the cell's content (via `Coordinates::qtPosToByte` against the cell's text, *not* the block buffer).
3. Buffer-byte-offset of the edit = `cellByteRanges[row][col].start + cellByteOffsetForQtPos`.
4. Removed UTF-8 byte count = `Coordinates::qtPosToByte(qtPos + removed) - Coordinates::qtPosToByte(qtPos)` over the cell content **as it was before the edit**.
5. Added UTF-8 bytes = `addedText.toUtf8()`.
6. Call `MarkoffDocument::d2ApplyBufferEdit(blockId, bufferByteOffset, removedBytes, addedBytes, transaction)`.

Steps 1–5 live in a new `TableEditBinding` helper (Q_INVOKABLE-callable from the QML delegate; pattern after `LiveEditBinding`). Step 6 is the existing core API.

### 6.2 Reverse: buffer change → cell content refresh

When `model.text` changes (the block buffer was mutated, whether by our own edit, a Source-mode edit, or a remote D2 op):

1. Re-tokenize the buffer into `ParsedTable`.
2. Update each cell's `.text` binding (QML reactivity handles the rest — cells whose content unchanged don't re-render; cells whose content changed update).
3. Preserve focus: if a cell currently has focus, translate the focused cell's stored cursorPosition through the new `cellByteRanges` — if the focused (r, c) still exists with the same content prefix, restore the cell's cursorPosition. Otherwise, focus is dropped to the closest cell that still exists.

The re-tokenize-on-every-edit cost is **O(buffer-size)** per edit. For typical tables (a few rows, a few columns), the buffer is hundreds of bytes; the cost is well below a frame. Profiling can promote the tokenizer to C++ if a pathological case surfaces.

### 6.3 What about whitespace padding in the source?

GFM tables often have whitespace padding inside cells: `| cell 1   | cell 2 |`. Two policy options:

- **(p1) Preserve padding bytewise.** `cellByteRanges[r][c]` covers `[byte_after_left_pipe, byte_before_right_pipe)` including spaces. The cell's `text` includes leading/trailing spaces. Display the spaces in the TextEdit. User typing replaces spaces.
- **(p2) Trim padding for display; restore on serialization.** `cellByteRanges[r][c]` still covers the byte slot including padding. The cell's *displayed* text is `.trimmed()`. On any cell edit, we re-pad to match the longest cell in the column.

(p2) is the auto-pretty-format behavior that Obsidian and most table editors implement. It's also a constant source of cursor-position weirdness (where does the caret go when the displayed text and buffer text differ in length?).

**Choice (p1) for B**. Preserve padding exactly. The cell shows the spaces. User keystrokes mutate the buffer byte-for-byte. Users editing tables typed by Markoff's own UI get whatever padding the user typed; users editing tables typed externally get whatever padding was there. This avoids the whole class of "cursor jumped after my edit" surprises. (p2) is a successor concern.

This implies a small UX wart: when a user adds text to a cell, the column doesn't auto-grow to keep the table visually rectangular. The source becomes `| cell1typed| cell 2 |` with mismatched column widths. The *parsed* table is still valid (column count is unchanged); the *rendered* grid still shows aligned columns (cell width is content-driven, not padding-driven, per §7); only the *source markdown on disk* is ragged. We accept this for B; an auto-format pass on save or on cell-blur is a successor option.

### 6.4 What about column-count drift?

If a Source-mode edit produces a malformed table (e.g., a body row with fewer cells than the header), the parser may still emit `BlockBoundary::Table` (tree-sitter is tolerant) or may demote it to paragraph. If the parser still calls it a Table:

- The tokenizer pads short rows with empty cells to match the column count of the header (per GFM rendering rules).
- `cellByteRanges` for missing cells is `(rowEnd, rowEnd)` — zero-width insertion slot.
- Editing such a "missing" cell inserts text at that slot; for B we accept that this produces source like `| a | b | newtext|` — visibly imperfect but consistent.

If the parser demotes to paragraph, the live block kind transitions to Paragraph, the TableDelegate goes away, and the user sees the raw text. The "broken table in Source mode → text in Live" case is a normal kind-transition, not a special path.

---

## 7. Navigation and edit affordances

The full set, per the brainstorm:

### 7.1 In-cell typing

Character keys, Backspace, Delete, arrow keys (when not at cell edge), Home, End — all behave as standard `TextEdit` operations on the cell's content. The `contentsChange` translator (§6.1) converts the resulting cell-text mutation into a buffer-byte edit. The cell's `InlineHighlighterAttached` provides format styling on `[bold]`, `*italic*`, `code`, `[[wikilink]]`, `[link](url)`, etc.

### 7.2 Tab / Shift+Tab

- **Tab from cell (r, c)** → focus moves to (r, c+1). If c+1 ≥ column-count, focus moves to (r+1, 0). If r+1 ≥ row-count, focus exits the table downward (caret lands at start of the next block; if no next block, a new paragraph is created — same existing `structuralKeyHandler` behavior).
- **Shift+Tab** is symmetric in reverse.
- Tab does **not** create new rows in B. (C-scope adds "Tab in last cell creates a new row.")

`Qt::Key_Tab` and `Qt::Key_Backtab` appear in the registry's `consumedStructuralKeys` for Table to signal that the editor "cares" about these keys for this kind — but the actual dispatch happens *inside the delegate*, not through `LiveStructuralKeyHandler`. The cell's `Keys.priority: Keys.BeforeItem` handler intercepts Tab/Backtab and moves focus to the next/previous cell directly. The structural-key handler is unaware of cell-internal navigation; including the keys in `consumedStructuralKeys` is purely for documentation + capability-listing (the registry is queryable for which keys a block "owns").

### 7.3 Arrow keys at cell edges

- **Right at cell-end (caret at cell's last position)** → focus moves to (r, c+1) at qtPos 0. If c+1 ≥ column-count, focus moves to (r+1, 0). If r+1 ≥ row-count, **exits the table** to next block.
- **Left at cell-start (caret at 0)** → focus moves to (r, c-1) at qtPos = previous-cell-length. If c-1 < 0, focus moves to (r-1, last-col) at end. If r-1 < 0 (first cell of table), **exits the table** to previous block.
- **Down from any cell in last row** → exits table; **Down from any other row** → moves to (r+1, c) at qtPos preserving `desiredVisualX` (same column).
- **Up** symmetric.

Up/Down within the table are dispatched by the delegate's per-cell Keys handler — they're cell-to-cell focus moves, not standard `LiveNavigationController` cross-block nav. The delegate consults `cursorState.desiredVisualX` to preserve column when moving up/down across rows.

Up at top row / Down at bottom row exit the table by **deferring to `LiveNavigationController`**: the delegate calls `navigationController.tryHandle(Qt::Key_Up | Qt::Key_Down, mods, modelIndex, ..., null, model.text)` after determining we're at the table boundary. The nav controller's existing cross-block logic places the caret in the previous/next block.

### 7.4 Enter inside a cell

**Inert in B.** The cell's `Keys.onPressed` swallows `Qt::Key_Return` / `Qt::Key_Enter` without modifying the buffer or moving focus. (C-scope decides whether Enter creates `<br>`s, advances to next row, or stays inert.)

### 7.5 Backspace at cell-start

- **Backspace at qtPos 0 in cell (0, 0)** (the very first cell of the table) → cursor state transitions to `BlockSelected`. The next Backspace deletes the entire Table block (existing `structuralKeyHandler` cascade for atomic blocks).
- **Backspace at qtPos 0 in any other cell** → **inert.** No content merging with the previous cell. This is the conscious break from the "Backspace at block start merges with previous block" rule that applies everywhere else in the editor: tables are exceptional, and merging cell content would either silently destroy the table structure or require a structural op that's out of scope for B.

### 7.6 Delete at cell-end

Symmetric to Backspace at cell-start:

- **Delete at qtPos = cellLength in cell (last-row, last-col)** → `BlockSelected`; next Delete deletes the block.
- **Delete at qtPos = cellLength in any other cell** → inert.

### 7.7 Esc and block-level selection

- **Esc from any cell** → cursor state transitions to `BlockSelected` (`focusedAnchorRow = modelIndex`, no cell focused, whole-table selection ring visible). The delegate's per-cell Keys handler intercepts `Qt::Key_Escape` and calls the appropriate cursor-state API.
- **From `BlockSelected`:** Backspace / Delete deletes the block (existing structuralKeyHandler path for HR / Image). Up / Down navigates to the adjacent block. Enter re-enters the block at cell (0, 0) — the same "enter the block by going from BlockSelected to BlockInternalEdit" gesture that Math uses.
- **Click on table border / non-cell region** → `BlockSelected`. The TableDelegate's outer `MouseArea` (covering padding around the grid) catches these clicks; click *on a cell* falls through to the cell's standard mouse handling.

### 7.8 Inline formatting in cells

Each cell's `TextEdit` has an `InlineHighlighterAttached` configured exactly as `UnifiedInlineTextDelegate` configures one. The per-cell highlighter receives the cell's `inlineSpans` — which we *derive in the delegate* from `MarkoffDocument::inlineSpansFor(blockId)`.

The block-level `inlineSpansFor(blockId)` returns spans for the *whole* table buffer. The delegate filters: for cell (r, c), the spans whose byte ranges fall within `cellByteRanges[r][c]` get translated to cell-relative byte ranges and passed to the cell's highlighter. This is the only inline-span filtering that happens; everything else uses the existing E1 machinery.

`[[wikilink]]` clicks inside cells route through the same E3a `LinkService` path as paragraph wikilinks. The delegate's per-cell `TapHandler` (matching the one in `UnifiedInlineTextDelegate`) wires `activateLinkAt(qtPos)` against the cell's local qtPos; `activateLinkAt` is delegated to the binding's existing path with the cell-relative position translated to block-buffer position first.

### 7.9 Mouse drag selection inside the table

**Out of scope for B.** Selection within a single cell uses the cell's `TextEdit.selectByMouse: true`. Cross-cell selection (drag from cell (0,0) to cell (1,2)) is **not supported in B**. The cell-to-cell selection model is a successor concern; it interacts with the `LiveCursorState` selection-anchor mechanism in non-obvious ways (selection anchor is a block + qtPos pair; for a cross-cell selection inside one block, that's well-defined, but the *display* of "this rectangular cell range is selected" requires the delegate to override the standard selection-painting path). Document the gap; defer.

---

## 8. Save / load and Source ↔ Live consistency

### 8.1 Save

`MarkoffDocument::serializeForSave()` walks `iterateBlocks()`, applies the per-kind serializer, and joins with the B1 inter-block separator. For Table blocks, the serializer is passthrough — the buffer **is** the markdown source. The output is byte-identical to the buffer as long as no cell has been edited; cell edits land in the buffer directly, so the next save round-trips the user's exact pipe-text content.

### 8.2 Load

`MarkoffDocument::loadFromMarkdown` parses the document with tree-sitter; pipe-table regions become `BlockKind::Table` blocks with the pipe-table source as the block buffer. Live mounts a `TableDelegate` for each; grid renders.

### 8.3 Source-mode edits

Source mode renders the table as its raw pipe-text source (`SourceTextDocumentBinding` populates the source widget from the flat document representation, which interleaves block buffers with B1 separators). A user editing the source freely *can*:

- Modify cell content (a normal Source edit produces a normal `applyFlatEdit` that lands inside the Table block's buffer). Live's TableDelegate re-tokenizes on the next `d2DocumentChanged` and the rendered grid updates.
- Add/delete rows by inserting/deleting `|...|\n` lines. Tokenization handles arbitrary row counts; the grid layout absorbs the change.
- Add/delete columns by changing the cell count in the header + alignment rows + body rows in lockstep. Column-count mismatch handling per §6.4.
- **Break the table** by deleting the alignment row or inserting non-table content into the middle. The parser no longer emits `BlockBoundary::Table`; the kind-transition heuristic issues `Cmd::changeKind` to whatever the new parser shape is (likely Paragraph); Live drops the `TableDelegate` and mounts a paragraph delegate; the user sees raw pipe text.

The last case is the **only** path that produces "table → not-table" in the data model. It requires the user to leave Live mode explicitly, edit non-cell content, and switch back. That's by design.

---

## 9. Tests and invariants

### 9.1 Falsifiable invariant tests (per invariant 4)

Each new invariant gets a test that *first fails when the rule is violated by a stub*, then passes on the real implementation. Documented in the plan; summarized here:

1. **Table block creation invariant** — `loadFromMarkdown` on a document containing a pipe table produces exactly one `BlockKind::Table` block whose `blockText()` equals the original pipe-table source. Falsifiable by stubbing the parser→core kind mapping.
2. **No-revert invariant** — every Live-mode keystroke / mouse event reachable from a `TableDelegate` instance, applied to a starting `BlockKind::Table` block, results in either (a) the same block kind, or (b) the block deleted. Realistic-input harness drives this.
3. **Cell-buffer round-trip invariant** — typing a character X at cell (r, c) qtPos K produces a buffer mutation at byte offset corresponding exactly to (cellByteRanges[r][c].start + utf8BytesUpTo(K)). Falsifiable by stubbing the translator to identity.
4. **Cross-cell navigation invariant** — Tab from cell (r, M-1) lands focus on cell (r+1, 0); Shift+Tab from (r, 0) lands on (r-1, M-1); arrow keys honor `desiredVisualX` across rows. Per-key falsifiability proofs.
5. **Block-level delete cascade invariant** — Backspace at qtPos 0 of cell (0, 0) → `BlockSelected`; second Backspace → block deleted. Symmetric for Delete at last cell.
6. **Inline formatting in cells** — typing `**bold**` in a cell produces a span at the right cell-relative byte range; the highlighter renders the bold style. Falsifiable by passing the unfiltered block-level spans.
7. **Hit-test inside a table** — `LiveView.hit(x, y)` on a click inside a cell returns the cell's flat qtPos (block-buffer-relative). Cross-checked against round-tripping `positionAt` ↔ `cellByteRanges`.

### 9.2 Existing test classifications affected

`tst_live_render_kind_transition_invariant` adds a slot for paragraph → table transition. The transition is cross-class (`text-inline` → `table`), so Delete+Insert is expected — no special handling.

`tst_realistic_input_harness` gets a new fixture: a small markdown vault snippet containing a pipe table, with scripted inputs for the seven invariants above. Drives the QML integration harness.

### 9.3 Tests that probably need updating

The `MarkdownSplitter` currently has `Q_UNREACHABLE()` on `BlockBoundary::Table` and a comment "Tables stay in text; the editor converts them to QTextTable inside a text item" — that's master-era thinking carried into new-foundation. `MarkdownSplitter` is used by the source-widget path for paragraph segmentation; it does not feed the D2 block model directly. Verification work-unit: confirm `MarkdownSplitter`'s table-skip does not affect the D2 block-creation path (it shouldn't — `MarkoffDocument::loadFromMarkdown` uses `iterateBlocks` against the parser's `findBlockBoundaries`, not `MarkdownSplitter`'s segments). If the splitter is used by anything we care about for tables, decide between (a) updating the splitter to emit Table segments, or (b) leaving it alone since the source widget renders the table as raw pipe text and that's fine.

---

## 10. Engineering discipline (invariants 1–8 per docs/INVARIANTS.md)

### 10.1 Developmental record cited (invariant 1)

- `libs/markoff-live/docs/archive/07-atomic-blocks-and-tables.md` (master era) — original "atomic block" framing. The category is named here. The new foundation arrived at the same concept independently under the L8 / `BlockInternalEdit` / `BlockOnlyDelegateBase` names. We cite this doc as historical provenance; we do not adopt its specific architecture (QTextDocument/QGraphicsScene-based) because the substrate is different.
- `libs/markoff-live/docs/archive/2026-04-01-tables-design.md` and `libs/markoff-live/docs/specs/2026-04-16-editable-tables-design.md` (master era) — previous attempts at table editing in the QTextDocument era. We adopt:
  - The "once pipe text becomes a table, it stays a table" rule (the no-revert invariant of §3).
  - The Tab/Shift+Tab/Esc navigation model (§7).
  - The "plain text cells in v1, inline formatting later" choice is *not* adopted — we have the inline-highlighter machinery and we use it for B.
  We do not adopt:
  - The `QTextTable` substrate; we are in QML/D2.
  - The `TableConverter` / `TableSerializer` C++ classes; the D2 buffer is the serialization format, and the delegate-local `ParsedTable` is the only structured representation needed.
- `libs/markoff-live/CLAUDE.md` §L8 — "Interactive blocks (Math + BlockInternalEdit)". The category exists here. Tables are a new member.
- `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md` — `delegateClassFor()` and the cross-class Delete+Insert rule. We use it for paragraph→table.
- `docs/specs/2026-05-18-b1-buffer-convention-design.md` — block buffers are content; separators are the serializer's job. Tables conform: the buffer is the pipe-table content (including internal newlines, which are content for this block kind).

### 10.2 L4 decided in writing (invariant 2)

For tables, **model wins on canonical content**. The block buffer is the source of truth for cell text. The delegate's `parsedTable` is a derived view, recomputed on every buffer change. Cell-level `TextEdit.text` bindings are mirrors of `parsedTable.body[r][c]`. The cursor state's `qtPos` is block-buffer-relative; cell-relative cursor positions exist only inside the delegate, translated at the chokepoint.

This matches the L4 decision for every other text-bearing block (UnifiedInlineTextDelegate, CodeBlockDelegate): model is canonical; delegate mirrors. The new piece is the *translation layer* — but the authority direction is unchanged.

### 10.3 New authority retires the old in the same plan (invariant 3)

No old authority is being retired by this work. Tables are net-new. (The master-era `TableConverter` / `TableSerializer` are not present in foundation-exploration; the question doesn't arise here.)

### 10.4 Falsifiable invariant tests come first (invariant 4)

§9.1 enumerates seven invariants. The plan sequences each as: (a) write the failing test against a stub; (b) implement; (c) test passes; (d) falsifiability proof commit (stub-then-revert) recorded in history. Pattern per the tier-3/tier-4 precedent.

### 10.5 Tests exercise production callsite (invariant 5)

All test paths drive the QML integration fixture (`LiveRealisticInputHarness`); no slot is tested via direct C++ call when the production path reaches it through QML. This is non-negotiable per `tst_live_render_theme_toggle_propagation`'s precedent.

### 10.6 `Qt.callLater` / `QTimer::singleShot(0, ...)` (invariant 6)

The cell-focus-on-buffer-change path (re-establishing cell focus after a re-tokenize) is a candidate for needing `Qt.callLater` if delegate child creation is not synchronous with the parent's `Component.onCompleted`. The plan calls this out: the first implementation pass attempts the synchronous path; if Qt's component lifecycle requires a deferral, the deferral is documented in the commit message and logged to the Discipline Log.

### 10.7 Re-entrance guards (invariant 7)

`TableEditBinding`'s buffer-edit flow may need an `isApplyingTextUpdate()`-style guard if the cell's `onContentsChange` fires synchronously inside our own `d2ApplyBufferEdit` round-trip. We replicate the existing pattern from `LiveEditBinding` — including its guard — rather than invent a new one. If the guard turns out to be unnecessary (the per-cell flow may avoid the re-entrance the global-edit flow has), it's elided.

### 10.8 Discipline Log (invariant 8)

Any smell encountered while writing the table code is logged in `docs/queue.md` § Discipline Log with `file:line`, invariant number, and one-phrase context. No fix required in the same session.

---

## 11. Out of scope (deferred to successor)

The following are explicitly out of scope for E4. They become the candidate scope for **E4-successor** (working title; final name TBD):

- **Add row.** Tab in last cell of last row, or a UI affordance, inserts a new empty row.
- **Delete row.** Right-click context menu or keyboard shortcut.
- **Add column.** UI affordance only (no keyboard shortcut is unambiguous).
- **Delete column.** Right-click context menu or keyboard shortcut.
- **Change column alignment.** Right-click context menu cycling left/center/right.
- **Cell-content multi-line (`<br>`).** Enter inside a cell inserts a `<br>` markdown token, displayed as a line break in the cell's wrapped rendering.
- **Cross-cell selection.** Drag from one cell to another, or Shift+arrow extending selection across cells, produces a rectangular cell-selection visual + copy/cut/paste semantics.
- **Auto-pretty-format padding.** On cell edit or on save, re-pad all cells in a column to align the source pipe text. Either the (p2) policy on every cell edit, or a one-shot on-save normalizer.
- **Column resize drag handles.** Manual column width control.
- **Row drag-to-reorder.** Move row up/down.
- **Spreadsheet-style features.** Formulas, sorting, CSV import/export. Mentioned in master-era spec; defer indefinitely.

---

## 12. Test app demo

The test app (`markoff-live-app`) gains no explicit table-demo command. A sample document containing a few pipe tables is added to `tests/fixtures/` and used by the realistic-input harness; the test app users can open it manually to dogfood.

---

## 13. Tag plan

On dogfood pass: `v0.7.0-e4`. Held until user runs the dogfood checklist (which the plan will produce as `docs/handoff/2026-05-??-e4-dogfood-request.md`).
