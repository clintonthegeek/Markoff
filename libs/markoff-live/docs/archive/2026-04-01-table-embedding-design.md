# Table Embedding via QTextTable Harvest

Supersedes: `2026-04-01-tables-design.md`

## Problem

Six markdown block types need to render with geometry unrelated to
their source text: tables, images, display math, mermaid diagrams,
embedded notes, and frontmatter. Tables are the hardest because they
require interactive cell editing inside the rendered form.

Two approaches have been tried and failed:

1. **QTextTable insertion (commit 80aca66):** Replaced pipe text with
   QTextTable in the QTextDocument. Failed because the forked
   QPlainTextDocumentLayout does not support QTextFrame/QTextTable —
   it assumes every element is a simple text paragraph.

2. **QTableWidget overlay (commit f205617):** Created QTableWidget
   instances as children of the viewport, manually positioned over
   hidden pipe text. Failed because scroll sync, click passthrough,
   height calculation, and widget lifecycle are all intractable when
   managing overlay widgets by hand.

## Key Insight

The forked PlainTextDocumentLayout is the bottleneck, not the
QTextDocument or TextControl. QTextTable works fine as a document
model object — it just needs a layout engine that knows how to
handle it. Since we fork Qt GPL source directly, we can harvest the
table layout code from QTextDocumentLayout and graft it into our
existing layout. No need to replace the whole layout engine.

## Approach

Graft QTextTable layout support into the existing forked
PlainTextDocumentLayout. The layout remains line-based and fast for
normal text blocks (95%+ of any document). Only table regions use
the richer layout code harvested from QTextDocumentLayout.

QTextTable objects replace pipe text in the QTextDocument during
live preview. Cell editing, cursor navigation, selection, IME, and
undo all work natively because QTextTable cells contain real
QTextDocument content. Obsidian-style chrome (grid lines, handles,
buttons, context menu) is painted as decorations in paintEvent.

## Architecture

### Document Model

In live preview mode, the QTextDocument contains:
- Raw markdown text for all non-table content
- QTextTable objects at table positions (pipe text removed)

On save or source mode switch, QTextTable objects are serialized
back to pipe-delimited markdown. The document becomes flat text.

### Layout Engine (grafted from QTextDocumentLayout)

The forked PlainTextDocumentLayout gains table awareness. During
block iteration, it distinguishes three cases:

1. **Normal block** (not in a table frame) — existing fast path,
   unchanged
2. **First block of a QTextTable frame** — delegate to harvested
   `layoutTable()` code, report the table's full pixel height
3. **Subsequent blocks inside a table frame** — skip, already
   handled by table layout

This three-way check is added to:
- `documentChanged()` — layout computation
- `blockBoundingRect()` — height queries
- `draw()` — rendering
- `hitTest()` — click-to-position mapping
- Scrollbar value calculations

Detecting whether a block belongs to a table is cheap:
`QTextCursor(block).currentTable()` returns non-null if the block
is inside a QTextTable frame.

### Table Navigation (already in TextControl fork)

The forked TextControl (from QWidgetTextControl) already contains:
- `gotoNextTableCell()` — Tab moves to next cell, inserts row at end
- `gotoPreviousTableCell()` — Shift+Tab moves to previous cell
- `selectedTableCells()` — column/row selection
- Table-aware deletion and clipboard

This code is currently dormant because the layout never produced
tables. Enabling it requires no new code — just a working
QTextTable in the document with a layout that handles it.

### Paint Path

In `paintEvent`, when rendering table blocks:

1. Paint table background and grid lines
2. Delegate cell content painting to harvested `drawTableCell()`
3. Paint Obsidian chrome on top:
   - Column header handles (on hover, above each column)
   - Row handles (on hover, left of each row)
   - `+` button at right edge (append column)
   - `+` button at bottom edge (append row)
   - Column resize drag handles
   - Alignment indicators

### Serialization

`TableHandler::serializeToMarkdown()` already exists and works.
Called:
- On file save (walk document, find QTextTable objects, serialize)
- On source mode switch (revert QTextTables to pipe text in the document)
- Before tree-sitter reparse (serialize so parser sees raw markdown)

The serializer is deterministic: same cell contents and alignments
produce identical pipe text. Round-trip fidelity is testable:
`parse(serialize(parse(input))) == parse(input)`.

## Table Lifecycle

### Creation Trigger

Everything is plain text until the separator row is completed:

```
| a header | another header|     <- plain text, highlighted
|-----|------                     <- still plain text, incomplete
```

The user types the final `|` that completes the separator row.
At that keystroke, synchronously:

1. Detect: header line + separator line form a valid table
2. Parse: extract column count, header text, alignments
3. Delete: remove all pipe text from the document
4. Insert: QTextTable with parsed headers
5. Add: one empty data row below the headers
6. Place: cursor in first cell of the data row
7. Layout: table renders immediately, no flicker

### Editing

Cell editing is native QTextDocument editing. The user types in a
cell, the text appears, undo works, IME works. On every cell change,
the table's markdown serialization is updated (for save readiness).

Navigation:

| Action | Behavior |
|--------|----------|
| Tab | Next cell (left-to-right, top-to-bottom) |
| Shift+Tab | Previous cell |
| Tab from last cell | Insert new row, cursor to first cell |
| Enter | Insert new row below, cursor to new row |
| Escape | Move cursor out of table (below) |
| Arrow keys | Navigate cells at boundaries |

### Row and Column Operations (via context menu and handles)

Column operations (column header handle):
- Sort ascending/descending
- Add column before/after
- Move column left/right
- Align left/center/right
- Duplicate column
- Delete column

Row operations (row handle):
- Add row above/below
- Move row up/down
- Duplicate row
- Delete row

Edge buttons:
- `+` at right edge: append column
- `+` at bottom edge: append row

### Destruction

- Backspace from line after table: select entire table (highlight)
- Backspace again: delete table, cursor at deletion point
- Source mode: table serialized to pipe text, editable as raw text
- Switching back to live preview: pipe text reconverted to QTextTable

### Save

Walk the document. For each QTextTable:
1. Read cell contents and alignments
2. Compute column widths (pad to widest cell)
3. Generate header row, separator row, data rows
4. Replace the QTextTable with the pipe text in a copy for writing

The file on disk always contains pipe-delimited markdown, never
QTextTable serialization artifacts.

## What to Harvest from Qt (GPL Source)

### Table Layout (~500 lines from qtextdocumentlayout.cpp)

- `layoutTable()` — column width computation, cell positioning,
  row height calculation
- `drawTableCell()` — cell content rendering
- `drawTableCellBorder()` — border painting

Strip: CSS box model, border-collapse, nested frames, percentage
widths. Keep: fixed/content-based column sizing, simple grid lines,
cell padding.

### QTextTable Data Model (use as-is, ~1,300 lines)

QTextTable's public API is sufficient:
- `insertRows()`, `removeRows()`, `appendRows()`
- `insertColumns()`, `removeColumns()`, `appendColumns()`
- `cellAt(row, col)`, `rows()`, `columns()`
- `QTextTableCell::firstCursorPosition()` / `lastCursorPosition()`
- `resize(rows, cols)`

Fork only if custom per-cell data is needed beyond what
QTextTableCellFormat provides. Start with the public API.

### Table Navigation (already forked, ~40 lines in TextControl)

- `gotoNextTableCell()` — Tab navigation with auto-row-insert
- `gotoPreviousTableCell()` — Shift+Tab navigation

Already in the codebase. Just needs a working QTextTable.

## What Changes in Existing Code

### PlainTextDocumentLayout (graft target)

Add table-aware branches in ~5-6 methods. Non-table code paths
completely unchanged. Performance: zero regression for documents
without tables.

### Editor::paintEvent

Add table chrome painting when rendering table blocks: grid lines,
hover handles, edge buttons, alignment indicators. Same pattern as
existing decorated range painting for code blocks and callouts.

### Editor::Private::reparseDocument

Table conversion after span map application, same timing as commit
80aca66. Sequence: tree-sitter parse -> span map -> highlighter ->
table conversion.

### TableHandler

Keep: `detectTables()`, `serializeToMarkdown()`, parsing utilities.
Replace: `convertToQTextTable()` with updated version.
Delete: `TableWidget` (QTableWidget subclass), all embedded widget
machinery.

### TextControl

Enable dormant table cell navigation. May need minor edits to
integrate with Obsidian-specific behavior (Enter = new row,
Escape = exit table).

## What Gets Deleted

- `TableWidget` class (QTableWidget subclass)
- `EmbeddedWidget` struct and list
- `createEmbeddedWidgets()`
- `repositionEmbeddedWidgets()`
- `clearEmbeddedWidgets()`
- `AtomicBlock` base class and all subclasses (already slated)
- `CodeAtomicBlock`, `CalloutAtomicBlock` (already slated)

## What's Unchanged

- Decorated ranges (code blocks, callouts, blockquotes)
- Non-table text layout (fast line-based path)
- TextControl input handling, IME, clipboard, undo
- Tree-sitter parsing, span map
- Highlighter
- ReadingView, Renderer (reading mode is a separate pipeline)

## Generalization to Other Block Types

The table-aware branch in the layout engine establishes the
"replaced block" pattern: detect a frame/object, delegate to
custom layout, report custom height. The same pattern handles
future block types:

| Block Type | Rendered As | Interactive? | Layout Approach |
|------------|-------------|-------------|-----------------|
| Table | QTextTable grid | Yes — cell editing | Harvested layoutTable() |
| Image | QPixmap | No — click to source | renderedHeight + custom paint |
| Display math | JKQTMathText output | No — click to source | renderedHeight + custom paint |
| Mermaid | QSvgRenderer output | No — click to source | renderedHeight + custom paint |
| Embedded note | Rendered note content | No — click to navigate | renderedHeight + custom paint |
| Frontmatter | Properties UI | Yes — key/value editing | TBD (may use QTextTable) |

Tables are the hardest case. Every other block type is simpler
because it is either read-only or uses the same QTextTable
machinery (frontmatter).

## Risks

### Grafting layoutTable() may pull dependencies

QTextDocumentLayout's internal types and helper functions may be
entangled with `layoutTable()`. Mitigation: extract incrementally,
replacing internal types with public Qt API equivalents. The core
algorithm (compute column widths, position cells) is portable.

### Tree-sitter reparse with tables present

Reparsing requires serializing tables back to pipes first, then
re-parsing, then reconverting. This is a serialize-parse-convert
round trip on every text change. Mitigation: only reparse on
changes outside table regions. Changes inside tables (cell edits)
update the QTextTable directly without triggering a full reparse.

### Source position mapping

Span map byte offsets are computed from raw markdown, before table
conversion. After conversion, those offsets are invalid for table
regions. Mitigation: table regions are excluded from span map
application (the table's visual formatting comes from the QTextTable
and chrome painting, not from the highlighter).

### Undo across table creation boundary

Creating a table (pipe text -> QTextTable) is a document edit.
Undoing past the creation point must restore the pipe text. This
should work naturally if the conversion is wrapped in a
QTextDocument edit block. Verify in testing.
