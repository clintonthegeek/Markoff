# Editable Tables via QTextTable Layout Graft

**Date:** 2026-04-16
**Status:** IMPLEMENTED (v1) — see `TableConverter`, `TableSerializer`, `Editor::table*()` ops. Column-alignment UI from §"Future" still deferred. Notable deviation from this spec: no `PlainTextDocumentLayout` fork was needed — `QTextDocument`'s stock `QTextDocumentLayout` already handles `QTextTable` rendering correctly, reducing the implementation surface dramatically.
**Scope:** V1 (minimal editable tables)

## Overview

Tables move from standalone read-only `BlockItem` scene objects into
`MarkdownTextItem`'s `QTextDocument` as native `QTextTable` frames. The forked
`PlainTextDocumentLayout` gains a table-aware layout branch harvested from Qt's
`QTextDocumentLayout`. This gives us native cell editing, cursor navigation,
undo/redo, and selection without overlay hacks or proxy widgets.

### Design Principle

Once pipe text converts to a `QTextTable`, it stays that way. The table widget
*is* the editing surface — there is no "reveal source" mode. Pipe text is a
serialization format, not a user-facing representation.

## Scope

### V1 — This Spec

- Cell editing with native cursor/undo/redo
- Tab / Shift+Tab / Enter / Escape navigation
- Auto-detect pipe tables on parse + explicit `insertTable(rows, cols)`
- Smart cursor entry from surrounding text (x-position → nearest column)
- Context menu: insert/delete row/column, select row/column
- Auto-formatted pretty-pipe serialization
- Plain text cells only (no inline markdown rendering in v1)
- Public API: table signals + operation slots for consumer toolbar wiring
- Unified undo stack; structural operations grouped in edit blocks
- Reparse reconciliation with "once a table, always a table" invariant
- `TableBlockItem` deprecated and removed
- Theme-ready `TableStyle` struct with sensible hardcoded defaults

### Scope (b) — Future

- Column alignment controls (UI + API)
- Row/column move (up/down/left/right)
- Column resize drag handles
- Auto-format on every cell edit (live width normalization)
- Header row bold styling, theming options

### Scope (c) — Future

- Spreadsheet formulas (cell references, sum, mean, conditionals)
- Column sorting (ascending/descending)
- CSV import/export
- Table transpose
- Rectangular paste-into-table (copy A1:C3, paste at D2 → fills D2:F4)
- Full inline objects in cells (U+FFFC math, checkboxes, links)
- Multi-cell selection with shift+click/shift+arrow

## Architecture

### What Changes

| Component | Before | After |
|-----------|--------|-------|
| `MarkdownSplitter` | Splits tables into `Table` segments | Tables stay inside `Text` segments |
| `SceneCoordinator` | Creates `TableBlockItem` for table segments | No `TableBlockItem` creation; runs `TableConverter` post-load |
| `PlainTextDocumentLayout` | Line-only layout, no frame awareness | Adds `layoutTable()` branch for `QTextTable` frames |
| `TextControl` | Already has Tab/Shift+Tab/Enter/Escape table nav | Enter behavior revised (see Navigation) |
| `MarkdownHighlighter` | Ignores table content (separate item) | Skips table frame blocks (guard clause) |
| `TableBlockItem` | Read-only renderer | Removed |
| `Editor` | No table awareness | Gains table signals + operation slots |

### What Doesn't Change

- `BlockItem` base class (still used by `ImageBlockItem`)
- `SelectionManager` and cross-boundary selection protocol
- `SceneCoordinator` item lifecycle for non-table items
- Reparse debounce pipeline (150ms timer)
- `MarkdownTextItem` public interface

## Layout Engine

**Key discovery:** `MarkdownTextItem` uses a plain `QTextDocument` with Qt's
default `QTextDocumentLayout` (the rich-text layout engine) — not a forked
`PlainTextDocumentLayout` as originally assumed. This means `QTextTable`
layout, rendering, and hit-testing work out of the box with zero harvesting.

No layout engine modifications are needed. The work reduces to:
1. Stop splitting tables out of text segments (`MarkdownSplitter`)
2. Convert pipe text → `QTextTable` after load (`TableConverter`)
3. Serialize `QTextTable` → pipe text on save (`TableSerializer`)
4. Fix `allMarkdown()` block iteration to handle table frames
5. Navigation tweaks and public API

### Grid Rendering

Visual styling (theme-ready defaults):

| Element | Default | Themeable |
|---------|---------|-----------|
| Header background | `QColor(240, 240, 240)` | Yes, via `TableStyle` |
| Grid lines | 1px, `QColor(208, 208, 208)` | Yes |
| Header separator | 2px, `QColor(160, 160, 160)` | Yes |
| Cell padding | 8px | Yes |
| Header font weight | Normal (bold deferred to scope (b)) | Yes |

All visual constants live in a `TableStyle` struct (or a `Theme` subsection)
so the consumer can override them later without touching the layout code.

## Table Lifecycle

### Creation

**Path 1 — Auto-detect on parse:**

1. Tree-sitter parser identifies pipe-table node with byte ranges
2. `TableConverter` locates the corresponding text range in the
   `MarkdownTextItem`'s `QTextDocument`
3. `QTextCursor` selects the pipe text
4. `cursor.beginEditBlock()` (single undo step)
5. `cursor.insertTable(rows, cols, tableFormat)` replaces pipe text
6. Cell contents populated from parsed data
7. Alignment stored as custom property on `QTextTableFormat`
8. `cursor.endEditBlock()`
9. `TableRecord` created (document position, rows, cols) for reconciliation

**Path 2 — Explicit insert:**

`Editor::insertTable(rows, cols)` inserts a `QTextTable` directly at the
cursor position. No intermediate pipe text. Same `QTextCursor::insertTable()`
path.

### Editing

Cell text editing is native `QTextDocument` editing. Cursor movement,
undo/redo, IME, and text input all work through the existing `TextControl`
fork with no additional code.

### Serialization

`MarkdownTextItem::toMarkdown()` delegates to `TableSerializer` when it
encounters a `QTextTable` frame:

1. Walk `QTextTable` — read cell text and alignment
2. Compute column widths (max content width per column, minimum 3)
3. Emit auto-formatted pipe text with 1-space padding per side
4. Alignment markers in separator: `:---` (left), `:---:` (center),
   `---:` (right), `---` (none/default)

Example output:
```
| Name    | Age | City      |
| ------- | --- | --------- |
| Alice   |  30 | Melbourne |
| Bobbert |  42 | Portland  |
```

`TableSerializer` is a standalone utility in `src/TableSerializer.h/.cpp` —
depends only on `QTextTable` and `QString`. Testable in isolation.

### Destruction

- Backspace at the line after a table: selects the table frame (already in
  `TextControl` lines 1000-1041)
- Second backspace: deletes the selection
- Undo restores the table (standard `QTextDocument` undo)

### Reparse Reconciliation

**Invariant:** Once a table, always a table.

On each reparse (150ms debounce):

1. `toMarkdown()` serializes any `QTextTable`s as pipe text
2. Tree-sitter re-identifies table regions
3. `TableConverter` compares parser output against stored `TableRecord`s:
   - **Match** (same position, same structure) → no action (common case)
   - **Table deleted** (parser sees no table) → remove `TableRecord`
   - **New table** (user typed valid header + separator) → convert to `QTextTable`
   - **Structure changed externally** (paste replaced region) → reconvert

Cell editing does not trigger reconversion — the `TableRecord` match
short-circuits.

## Navigation

### Keyboard Bindings Within a Table

| Key | Context | Action |
|-----|---------|--------|
| Tab | Any cell except last | Move to next cell (left→right, then wrap to next row) |
| Tab | Last cell of last row | Insert new row, move to first cell of new row |
| Shift+Tab | Any cell except first | Move to previous cell |
| Shift+Tab | First cell of first row | No action |
| Enter | Any row except last | Move to same column in row below; select cell content |
| Enter | Last row | Insert new row below, move to same column in new row |
| Escape | Any cell | Move cursor to line after table (insert blank line if none) |
| Up arrow | First row, top of cell | Exit table upward (insert blank paragraph if at doc start) |
| Up arrow | Other rows | Move to same column in row above |
| Down arrow | Last row, bottom of cell | Exit table downward |
| Down arrow | Other rows | Move to same column in row below |
| Left/Right | Within cell | Native cursor movement |

### Smart Cursor Entry

**From above (down-arrow into table):**
1. Read cursor x-position via `cursorRect().x()`
2. Map against table column geometries from `QTextTableData`
3. Land in first-row cell whose x-range contains the cursor x
4. Fallback: last column if x is past the right edge

**From below (up-arrow into table):**
Same logic, lands in last row.

**Smart column memory:**
`m_preferredColumn` (int) tracked on `TextControl`. Set on horizontal cursor
movement. Consulted on vertical movement within a table to maintain column
position across rows.

If smart cursor entry proves too complex during implementation (column
geometry detection from outside the table), fall back to landing in
first/last cell of the entry row. Note the intent in a comment and revisit.

## Context Menu

Right-click inside a table shows:

```
Insert Row Above
Insert Row Below
─────────────────
Insert Column Left
Insert Column Right
─────────────────
Delete Row
Delete Column
─────────────────
Select Row
Select Column
```

All operations use `QTextTable` API:

| Operation | Implementation |
|-----------|---------------|
| Insert row above | `table->insertRows(currentRow, 1)` |
| Insert row below | `table->insertRows(currentRow + 1, 1)` |
| Insert column left | `table->insertColumns(currentCol, 1)` |
| Insert column right | `table->insertColumns(currentCol + 1, 1)` |
| Delete row | `table->removeRows(currentRow, 1)` |
| Delete column | `table->removeColumns(currentCol, 1)` |
| Select row | Position cursor across row cells |
| Select column | Iterate rows, union cell ranges |

**Guard rails:**
- Cannot delete the last row or last column (minimum 1×1 table)
- Deleting row 0 promotes the next row to header (markdown semantics)

Each structural operation is wrapped in `beginEditBlock()` / `endEditBlock()`
for single-step undo.

## Public API

### Signals (on `Editor`)

```cpp
void tableEntered(int rows, int cols);
void tableExited();
void tableCursorMoved(int row, int col);
```

### Slots (on `Editor`, no-op if cursor not in a table)

```cpp
void tableInsertRowAbove();
void tableInsertRowBelow();
void tableInsertColumnLeft();
void tableInsertColumnRight();
void tableDeleteRow();
void tableDeleteColumn();
void tableSelectRow();
void tableSelectColumn();
```

The built-in context menu calls the same slots. The consumer (Corbomite) can
wire these to toolbar buttons, menu items, or keyboard shortcuts.

## Selection & Copy

| Selection type | Mechanism |
|----------------|-----------|
| Within a single cell | Native `QTextCursor` |
| Across multiple cells | `QTextCursor` spanning table frame; `cursor.selectedTableCells()` gives rectangular region |
| Text + table + text (one item) | Native `QTextCursor` over the full range |
| Cross-item spanning into item with table | `SelectionManager` cross-boundary protocol (unchanged) |
| Select Row / Select Column | Programmatic cursor positioning |

**Copy:** Serializes selected region via `toMarkdown()`. Full table selection
produces complete pipe-table markdown. Partial selection produces a sub-table.

**Paste into cell:** Plain text only. Pasting pipe-table text into a cell is
treated as literal text (no nested table conversion).

<!-- FUTURE: Rectangular paste-into-table (scope (c)). Copy a cell range from
one table region and paste at a target cell — the pasted rectangle fills
cells starting at the target position, expanding the table if needed. This
requires clipboard format awareness (detect table-structured content) and
a target-cell-relative insertion algorithm. -->

## Highlighter Integration

**V1:** The `MarkdownHighlighter`'s `highlightBlock()` callback fires for
blocks inside table cell frames (Qt calls it automatically). For v1, the
highlighter detects "this block is inside a `QTextTable` frame" and skips
its normal span-map-based formatting. Cells display as plain text.

**Optional follow-up:** A lightweight per-cell inline format detector (~50
lines) can handle `**bold**`, `*italic*`, `` `code` ``, `~~strike~~` without
the span map. This is decoupled from the main highlighter and can be added
independently.

**Scope (c):** Full span-map integration for table cells, enabling U+FFFC
inline objects (math, checkboxes), link rendering, and delimiter hiding
inside cells. Requires per-cell parsing with cell-local span maps (see
tables-guide.md "Architectural notes for scope (c)").

## Undo/Redo

Unified `QTextDocument` undo stack. Cell text edits merge naturally (same
granularity as regular text editing). Structural operations (insert/delete
row/column) are wrapped in `beginEditBlock()` / `endEditBlock()` so each is
a single Ctrl+Z step.

Table creation (pipe text → `QTextTable` conversion) is also a single edit
block, so Ctrl+Z reverts to the original pipe text. However, per the "once a
table, always a table" principle, the next reparse will reconvert it. This is
acceptable — the user would need to also edit the pipe text to break the
table pattern if they truly want to undo the conversion.

## New Files

| File | Purpose | ~Lines |
|------|---------|--------|
| `src/TableConverter.h` | Pipe text ↔ `QTextTable` conversion + reparse reconciliation | 40 |
| `src/TableConverter.cpp` | Conversion logic + `TableRecord` management | 200 |
| `src/TableSerializer.h` | `QTextTable` → auto-formatted pipe markdown | 20 |
| `src/TableSerializer.cpp` | Serialization with column-width-aware padding | 120 |
| `src/TableStyle.h` | Visual constants struct (colors, padding, line widths) | 30 |

**Estimated total new code:** ~410 lines

NOTE: Layout engine harvesting (`TableLayoutData`, `TablePainter`) is NOT
needed. `QTextDocument` already uses Qt's full `QTextDocumentLayout` which
handles `QTextTable` layout and rendering natively.

## Modified Files

| File | Changes |
|------|---------|
| `src/TextControl.cpp` | Revise Enter behavior (insert row only at last row), add up/down cross-cell navigation, smart column memory |
| `src/MarkdownTextItem.cpp` | `toMarkdown()` delegates to `TableSerializer` for table frames |
| `src/SceneCoordinator.cpp` | Stop creating `TableBlockItem`; run `TableConverter` post-load |
| `src/MarkdownHighlighter.cpp` | Add "skip if inside table frame" guard in `highlightBlock()` |
| `include/markoff/Editor.h` | Add table signals + operation slots |
| `src/Editor.cpp` | Implement table signals/slots, context menu for tables |
| `libs/markoff-parser/…/MarkdownSplitter.cpp` | Stop splitting at table boundaries (tables stay in `Text` segments) |
| `CMakeLists.txt` | Add new source files |

## Testing Strategy

| Test | What it verifies |
|------|-----------------|
| `tst_table_serializer` | Round-trip: pipe text → `QTextTable` → pipe text. Column width computation. Alignment markers. |
| `tst_table_converter` | Auto-detection on load. Reparse reconciliation. "Once a table, always a table" invariant. |
| `tst_table_layout` | `blockBoundingRect()` returns correct geometry for table blocks. Column width distribution. |
| `tst_table_navigation` | Tab/Shift+Tab/Enter/Escape/Arrow behavior. Smart cursor entry. Row insertion at table edge. |
| `tst_table_operations` | Insert/delete row/column via API slots. Undo grouping. Guard rails (can't delete last row/col). |
| `tst_table_integration` | End-to-end: load markdown with table, edit cells, serialize, verify output. |

## Risks

1. **Layout engine complexity.** The `blockBoundingRect()` graft touches the
   most performance-critical path in the editor. Thorough testing and
   profiling needed.

2. **Block iteration audit.** Every site that iterates document blocks must
   handle table frames. Missing one causes silent data loss or rendering
   artifacts. Grep for `doc->begin()`, `block.next()`, `QTextBlock` iteration.

3. **Reparse stability.** The reconciliation between parser output and live
   `QTextTable`s must be bulletproof. An off-by-one in position matching
   could cause spurious reconversions or missed conversions.

4. **Undo edge case.** Undoing a table creation reverts to pipe text, but the
   next reparse reconverts. This is a minor UX wrinkle — acceptable for v1.

## References

- Archived design: `docs/archive/2026-04-01-table-embedding-design.md`
- Archived attempts: `docs/archive/TODO-tables.md`
- Rich element strategy: `docs/rich-element-strategy.md`
- Qt harvest source: `/home/clinton/src/qtbase/src/gui/text/qtextdocumentlayout.cpp`
- Obsidian table implementation: per-cell CodeMirror editors in thick widget
- Advanced Tables plugin: auto-formatting, smart cursor, formula engine (scope (c) reference)
