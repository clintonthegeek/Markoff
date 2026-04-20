# Markoff: Atomic Blocks and Table Editing

## Overview

Several markdown block types share a common interaction pattern: they render as
a single graphical unit in the document, capture input when focused, and
serialize changes back to the underlying markdown. We call these **atomic
blocks**. Tables are the most complex example, so this document uses tables as
the primary case study while establishing the general pattern.

---

## The Atomic Block Pattern

### What Makes a Block "Atomic"

An atomic block is a region of markdown that:

1. **Renders as a graphical widget**, not as raw text lines
2. **Behaves as a single unit** in the document flow — backspace from outside
   selects the whole block, second backspace deletes it
3. **Captures input** when the cursor is inside — keyboard, mouse, and context
   menus are handled by the block, not the editor
4. **Serializes back to markdown** — edits inside the block modify the
   underlying raw markdown text in the `QTextDocument`

### Atomic Block Types

| Block Type | Raw Markdown | Rendered As | Internal Editing |
|-----------|-------------|------------|-----------------|
| **Table** | Pipe-delimited rows | Interactive grid with cell editing | Cell navigation, row/column ops |
| **Code block** | Fenced ` ``` ` region | Syntax-highlighted box with chrome | Plain text editing with language awareness |
| **Callout** | `> [!type]` blockquote | Colored box with icon | Rich text body, type/fold toggles |
| **Math (display)** | `$$ ... $$` | Rendered LaTeX equation | LaTeX source editing on click |
| **Mermaid diagram** | ` ```mermaid ``` ` | Rendered SVG diagram | Source editing on click |
| **Embedded note** | `![[note]]` | Rendered note preview | Not directly editable (navigate on click) |
| **Frontmatter** | `--- yaml ---` | Properties table / hidden | Key-value editing UI |
| **Image** | `![alt](url)` | Rendered image with sizing | Resize handles, alt-text editing |

### Layout-Affecting State: The Critical Constraint

A lesson from Obsidian's CodeMirror 6 architecture: block-level rendering
decisions (atomic block heights, widget replacements) **must affect the
document layout**, not just the paint pass. In CM6 terms, block widgets must
come from State Fields (computed on every transaction), not View Plugins
(viewport-only).

For Markoff, this means: atomic block heights must be reported by
`MarkoffDocumentLayout::blockBoundingRect()` on every document change and
cursor movement — not computed lazily during `paintEvent()`. If we defer
height calculation to paint time, scrollbar ranges will be wrong, scroll
position jumps will occur, and `QTextCursor` hit testing will misplace clicks.

**Concrete consequence:** When the parser detects an atomic block (e.g., a
table pattern is completed), the following must happen synchronously:

1. `MarkoffDocumentLayout::documentChanged()` is called
2. The layout computes the atomic block's rendered height
3. `blockBoundingRect()` returns the rendered height for that block
4. The scrollbar range and viewport are updated
5. THEN paint happens with correct geometry

This is NOT optional. Deferring layout to paint causes:
- Scrollbar flicker (range changes after paint)
- Click misalignment (hit test uses stale heights)
- Selection rectangles in wrong positions
- Viewport jumps when scrolling past atomic blocks

### Lifecycle

```
                    ┌──────────────────────────────┐
                    │     Raw Markdown Text         │
                    │  (in QTextDocument)           │
                    └──────────┬───────────────────┘
                               │
                    ┌──────────▼───────────────────┐
                    │     Parser Detection          │
                    │  MD4C callback or             │
                    │  pattern match on text change │
                    └──────────┬───────────────────┘
                               │
                    ┌──────────▼───────────────────┐
                    │  Atomic Block Created         │
                    │  - Registered in block map    │
                    │  - Source range recorded      │
                    │  - Rendered height computed   │
                    └──────────┬───────────────────┘
                               │
              ┌────────────────┴────────────────┐
              │                                 │
   ┌──────────▼──────────┐          ┌──────────▼──────────┐
   │  Cursor Outside     │          │  Cursor Inside       │
   │  → Rendered view    │          │  → Interactive edit   │
   │  → Click enters     │          │  → Captures input    │
   │  → Bksp selects     │          │  → Serializes to md  │
   └─────────────────────┘          └──────────────────────┘
```

---

## Qt Infrastructure for Atomic Blocks

### QTextTable — Already an Atomic Block

Qt's `QTextTable` is, conceptually, already an atomic block inside
`QTextDocument`. It occupies a region of the document, has its own internal
structure (rows, columns, cells), and the text control already has special
handling for it (Tab navigation, selection, cursor placement).

What Qt provides:

```
QTextTable (qtexttable.h — 102 lines, qtexttable.cpp — 1,305 lines)
├── Data model
│   ├── rows(), columns()
│   ├── cellAt(row, col) → QTextTableCell
│   ├── insertRows(), insertColumns()
│   ├── removeRows(), removeColumns()
│   ├── appendRows(), appendColumns()
│   ├── mergeCells(), splitCell()
│   └── resize(rows, cols)
│
├── Cell access
│   ├── QTextTableCell::firstCursorPosition()
│   ├── QTextTableCell::lastCursorPosition()
│   ├── QTextTableCell::row(), column()
│   ├── QTextTableCell::rowSpan(), columnSpan()
│   └── QTextTableCell::format() / setFormat()
│
└── Navigation
    ├── rowStart(), rowEnd()
    └── cellAt(QTextCursor) — find which cell contains cursor
```

```
QTextDocumentLayout::layoutTable() (~300 lines in qtextdocumentlayout.cpp)
├── Column width calculation (min/max constraints)
├── Column width distribution (proportional, percentage, fixed)
├── Cell padding and borders
├── Border collapse
├── Per-cell text layout with word wrapping
└── Cell position computation

QTextDocumentLayout::drawTableCell() + drawTableCellBorder()
├── Cell content rendering
├── Border rendering (per-edge, with border-collapse)
├── Clipping
└── Cursor rendering within cells

QWidgetTextControl (qwidgettextcontrol.cpp)
├── gotoNextTableCell() — Tab moves to next cell
│   └── Auto-inserts new row when Tab past last cell
├── gotoPreviousTableCell() — Shift+Tab moves to previous cell
├── selectedTableCells() — column/row selection
└── Table-aware deletion and clipboard
```

### QTextObject + QTextFrame — The Containment Model

`QTextTable` inherits from `QTextFrame`, which is a container inside
`QTextDocument`. Frames are nested: the root frame contains all content, and
tables are child frames with their own internal structure.

This containment model is how Qt handles "a thing inside the document that
is more than plain text." We can use the same model for other atomic blocks:

- **Code blocks** → a `QTextFrame` wrapping the code content
- **Callouts** → a `QTextFrame` wrapping the callout body
- **Math blocks** → a `QTextObject` (single position, custom rendering)

Or we can implement atomic blocks WITHOUT using QTextFrame, by tracking them
as metadata in our AST and rendering them in our paint pass. Both approaches
work; the QTextFrame approach integrates more deeply with Qt's cursor and
selection model.

---

## Table Editing: The Full Specification

Based on observed Obsidian behavior:

### Table Creation

**Trigger:** The user types a valid markdown table header + separator:

```
| Column A | Column B |
|----------|----------|
```

**Detection:** When the user completes the separator line (types the closing
`|`), the parser detects the table pattern.

**Transition:** The raw text is replaced with a `QTextTable` in the document:
1. Record the source text range (start/end positions in markdown)
2. Parse the header row to determine column count and header text
3. Create `QTextTable` with the correct dimensions
4. Populate header cells with the header text
5. Place cursor in the first data cell
6. The raw markdown lines become invisible — the table widget renders instead

**Round-trip:** The `QTextTable` is the editing surface. On every cell edit,
the underlying markdown text is regenerated from the table contents and written
back to the source range. The user never sees the raw pipes again (unless they
switch to source mode).

### Cell Editing

| Action | Behavior |
|--------|----------|
| Click on cell | Place cursor in cell, cell becomes editable |
| Type in cell | Text appears in cell, markdown regenerated |
| Tab | Move to next cell (left→right, top→bottom) |
| Shift+Tab | Move to previous cell |
| Tab from last cell | Insert new row, move cursor to first cell of new row |
| Enter | Insert new row below current, move cursor to new row |
| Escape | Move cursor out of table (below) |
| Arrow keys | Navigate between cells (up/down/left/right at cell boundaries) |

**Markdown inside cells:** Cells support inline markdown: bold, italic, code,
links, wikilinks. The cell editor must parse and render inline markdown within
each cell.

### Row and Column Operations

**Column operations (via column header handle):**

The column header handle appears when the mouse hovers near the top edge of a
column. Clicking it selects the entire column. Right-clicking or clicking a
menu icon shows the column context menu:

| Action | Implementation |
|--------|---------------|
| Sort by column (A→Z) | Read all cells in column, sort rows, rewrite table |
| Sort by column (Z→A) | Same, reverse sort |
| Add column before | `table->insertColumns(col, 1)`, regenerate markdown |
| Add column after | `table->insertColumns(col + 1, 1)`, regenerate markdown |
| Move column left | Swap column data with column-1, regenerate |
| Move column right | Swap column data with column+1, regenerate |
| Align left/center/right | Update separator line alignment markers (`---`, `:---:`, `---:`) |
| Duplicate column | Insert column, copy all cell contents |
| Delete column | `table->removeColumns(col, 1)`, regenerate markdown |

**Row operations (via row handle):**

The row handle appears when the mouse hovers near the left edge of a row.
Clicking it selects the entire row. Operations:

| Action | Implementation |
|--------|---------------|
| Add row above | `table->insertRows(row, 1)` |
| Add row below | `table->insertRows(row + 1, 1)` |
| Move row up | Swap row data |
| Move row down | Swap row data |
| Duplicate row | Insert row, copy cell contents |
| Delete row | `table->removeRows(row, 1)` |

**Edge buttons:**

| Widget | Position | Action |
|--------|----------|--------|
| `+` at right edge | Right of the focused row | `table->appendColumns(1)` |
| `+` at bottom edge | Below the last row | `table->appendRows(1)` |

### Table-Level Interactions

| Action | Behavior |
|--------|----------|
| Backspace from line below table | Select entire table (visual highlight) |
| Backspace again (table selected) | Delete entire table, cursor at deletion point |
| Click outside table | Deselect table, cursor at click point |
| Drag column border | Resize column width, regenerate markdown |
| Select text across table boundary | Not supported — selection stops at table edge |
| Copy table (Ctrl+C with table selected) | Copy as markdown pipe table |
| Paste table (markdown pipes) | Parse and create QTextTable |
| Cut table | Copy as markdown, delete table |

### Markdown Regeneration

Every edit to the table must regenerate the markdown source. The regenerator:

1. Reads all cell contents from the `QTextTable`
2. Computes column widths (pad to widest cell content per column, or minimum)
3. Generates the header row: `| Header1 | Header2 |`
4. Generates the separator row with alignment: `|---------|:--------:|`
5. Generates each data row: `| Cell1   | Cell2    |`
6. Replaces the original source range in the `QTextDocument`'s underlying text

The regeneration is wrapped in a `QTextDocument` edit block so it appears as
a single undo step.

```cpp
// Pseudocode for markdown regeneration
QString MarkoffTableBlock::toMarkdown() const
{
    // Compute column widths
    QList<int> widths = computeColumnWidths();
    
    QString md;
    
    // Header row
    md += QLatin1Char('|');
    for (int c = 0; c < columns(); ++c) {
        md += QLatin1Char(' ');
        md += cellText(0, c).leftJustified(widths[c]);
        md += QStringLiteral(" |");
    }
    md += QLatin1Char('\n');
    
    // Separator row
    md += QLatin1Char('|');
    for (int c = 0; c < columns(); ++c) {
        QString sep(widths[c] + 2, QLatin1Char('-'));
        // Apply alignment
        if (alignment(c) == Qt::AlignCenter)
            sep[0] = sep[sep.size()-1] = QLatin1Char(':');
        else if (alignment(c) == Qt::AlignRight)
            sep[sep.size()-1] = QLatin1Char(':');
        md += sep + QLatin1Char('|');
    }
    md += QLatin1Char('\n');
    
    // Data rows
    for (int r = 1; r < rows(); ++r) {
        md += QLatin1Char('|');
        for (int c = 0; c < columns(); ++c) {
            md += QLatin1Char(' ');
            md += cellText(r, c).leftJustified(widths[c]);
            md += QStringLiteral(" |");
        }
        md += QLatin1Char('\n');
    }
    
    return md;
}
```

---

## Implementation Architecture

### MarkoffAtomicBlock — Base Class

```cpp
class MarkoffAtomicBlock
{
public:
    virtual ~MarkoffAtomicBlock() = default;
    
    // Source range in the markdown text
    int sourceStart() const;
    int sourceEnd() const;
    void setSourceRange(int start, int end);
    
    // Rendering
    virtual QSizeF size(qreal availableWidth) const = 0;
    virtual void paint(QPainter *painter, const QRectF &rect,
                       const MarkoffStyle &style) const = 0;
    
    // Interaction
    virtual bool handleKeyPress(QKeyEvent *event) = 0;
    virtual bool handleMousePress(QMouseEvent *event, const QPointF &pos) = 0;
    virtual bool handleMouseMove(QMouseEvent *event, const QPointF &pos) = 0;
    virtual bool handleContextMenu(QContextMenuEvent *event, const QPointF &pos) = 0;
    
    // Cursor management
    virtual bool containsCursor(int documentPosition) const = 0;
    virtual QRectF cursorRect() const = 0;
    
    // Serialization
    virtual QString toMarkdown() const = 0;
    
    // Focus
    virtual void enterBlock(int documentPosition) = 0;
    virtual void leaveBlock() = 0;
    
Q_SIGNALS:
    void contentsChanged();          // Block content was modified
    void markdownChanged(const QString &md);  // Regenerated markdown
    void cursorPositionChanged();
    void requestDelete();            // Block wants to be deleted
};
```

### MarkoffTableBlock — Table Implementation

```cpp
class MarkoffTableBlock : public MarkoffAtomicBlock
{
public:
    // Construction from parsed markdown
    static std::unique_ptr<MarkoffTableBlock> fromMarkdown(
        const QString &markdown, int sourceStart);
    
    // Table structure
    int rows() const;
    int columns() const;
    QString cellText(int row, int col) const;
    void setCellText(int row, int col, const QString &text);
    Qt::Alignment columnAlignment(int col) const;
    void setColumnAlignment(int col, Qt::Alignment align);
    
    // Row/column operations
    void insertRow(int before);
    void appendRow();
    void removeRow(int row);
    void insertColumn(int before);
    void appendColumn();
    void removeColumn(int col);
    void moveRow(int from, int to);
    void moveColumn(int from, int to);
    void duplicateRow(int row);
    void duplicateColumn(int col);
    void sortByColumn(int col, Qt::SortOrder order);
    
    // MarkoffAtomicBlock interface
    QSizeF size(qreal availableWidth) const override;
    void paint(QPainter *painter, const QRectF &rect,
               const MarkoffStyle &style) const override;
    bool handleKeyPress(QKeyEvent *event) override;
    bool handleMousePress(QMouseEvent *event, const QPointF &pos) override;
    bool handleMouseMove(QMouseEvent *event, const QPointF &pos) override;
    bool handleContextMenu(QContextMenuEvent *event, const QPointF &pos) override;
    QString toMarkdown() const override;
    
private:
    // Internal state
    struct Cell {
        QString text;
        // Inline markdown formatting (parsed lazily)
    };
    
    QList<QList<Cell>> m_cells;       // [row][col]
    QList<Qt::Alignment> m_alignments;
    QList<qreal> m_columnWidths;      // Computed layout
    QList<qreal> m_rowHeights;
    
    // Editing state
    int m_editRow = -1;               // Currently edited cell
    int m_editCol = -1;
    int m_cursorPos = 0;              // Cursor position within cell text
    int m_selectionStart = -1;
    
    // Hover state
    int m_hoverRow = -1;              // Row under mouse
    int m_hoverCol = -1;              // Column under mouse
    bool m_showColumnHandle = false;
    bool m_showRowHandle = false;
    bool m_showAddColumnButton = false;
    bool m_showAddRowButton = false;
    
    // Layout
    void computeLayout(qreal availableWidth);
    QRectF cellRect(int row, int col) const;
    QPair<int,int> cellAt(const QPointF &pos) const;
    
    // Handle positions
    QRectF columnHandleRect(int col) const;
    QRectF rowHandleRect(int row) const;
    QRectF addColumnButtonRect() const;
    QRectF addRowButtonRect() const;
};
```

### Integration with MarkoffTextControl

In our forked text control, atomic blocks are tracked in a registry:

```cpp
class MarkoffTextControlPrivate
{
    // ...existing QWidgetTextControl state...
    
    // Atomic block registry
    QHash<int, std::unique_ptr<MarkoffAtomicBlock>> atomicBlocks;
    // Key = QTextDocument position where block starts
    
    MarkoffAtomicBlock *activeAtomicBlock = nullptr;
    // The block that currently has focus (captures input)
    
    // Lookup
    MarkoffAtomicBlock *atomicBlockAt(int position) const;
    MarkoffAtomicBlock *atomicBlockAt(const QPointF &viewportPos) const;
    
    // Lifecycle
    void createAtomicBlock(int start, int end, /* AST node */);
    void destroyAtomicBlock(int position);
    void syncAtomicBlocks();  // After document change, update block positions
};
```

**Event routing in the forked text control:**

```cpp
void MarkoffTextControl::keyPressEvent(QKeyEvent *e)
{
    Q_D(MarkoffTextControl);
    
    // If an atomic block has focus, delegate to it
    if (d->activeAtomicBlock) {
        if (d->activeAtomicBlock->handleKeyPress(e))
            return;
        // Block didn't handle it — might be Escape to leave, etc.
    }
    
    // Backspace at start of block after atomic block → select the block
    if (e->key() == Qt::Key_Backspace && !d->cursor.hasSelection()) {
        QTextBlock prev = d->cursor.block().previous();
        if (d->cursor.atBlockStart() && d->atomicBlockAt(prev.position())) {
            selectAtomicBlock(prev.position());
            return;
        }
    }
    
    // Backspace with atomic block selected → delete it
    if (e->key() == Qt::Key_Backspace && d->cursor.hasSelection()) {
        auto *block = d->atomicBlockForSelection();
        if (block) {
            d->destroyAtomicBlock(block);
            d->cursor.removeSelectedText();
            return;
        }
    }
    
    // Table auto-creation: detect | header |\n|----|
    if (e->key() == Qt::Key_Bar) {
        // Insert the character first
        QWidgetTextControl::keyPressEvent(e);
        // Check if we just completed a table separator pattern
        if (d->detectTablePattern()) {
            d->createTableFromMarkdown();
        }
        return;
    }
    
    // Default text editing
    QWidgetTextControl::keyPressEvent(e);
}

void MarkoffTextControl::mousePressEvent(QMouseEvent *e)
{
    Q_D(MarkoffTextControl);
    
    QPointF pos = e->position();
    auto *block = d->atomicBlockAt(pos);
    
    if (block) {
        // Enter the atomic block
        if (d->activeAtomicBlock != block) {
            if (d->activeAtomicBlock)
                d->activeAtomicBlock->leaveBlock();
            d->activeAtomicBlock = block;
            block->enterBlock(/* cursor position */);
        }
        block->handleMousePress(e, pos);
        return;
    }
    
    // Click outside any atomic block — leave current one
    if (d->activeAtomicBlock) {
        d->activeAtomicBlock->leaveBlock();
        d->activeAtomicBlock = nullptr;
    }
    
    // Default click handling
    QWidgetTextControl::mousePressEvent(e);
}
```

**Paint routing:**

```cpp
void MarkoffTextControl::paintContents(QPainter *painter)
{
    Q_D(MarkoffTextControl);
    
    // Paint normal text blocks
    // For each visible block in the document:
    //   if block has an associated atomic block:
    //     atomicBlock->paint(painter, blockRect, style)
    //   else:
    //     paint text normally (raw or rendered based on cursor proximity)
}
```

---

## What We Harvest vs. What We Build

### From Qt (harvest, modify)

| Component | Source | Lines | What We Take |
|-----------|--------|-------|-------------|
| Table data model | `QTextTable` | 1,305 | Row/col insert/remove, cell access, cursor positioning |
| Table layout | `QTextDocumentLayout::layoutTable()` | ~300 | Column width computation, cell positioning |
| Table cell rendering | `QTextDocumentLayout::drawTableCell()` | ~200 | Cell content painting, borders |
| Cell navigation | `QWidgetTextControl::gotoNext/PreviousTableCell()` | ~40 | Tab/Shift+Tab between cells |
| Table selection | `QTextCursor::selectedTableCells()` | ~50 | Column/row selection primitives |

**Total harvested for tables: ~1,900 lines of proven, tested code.**

### What We Build (Obsidian chrome)

| Component | Est. Lines | Description |
|-----------|-----------|-------------|
| Column/row handles | ~200 | Paint handles on hover, detect clicks, trigger selection |
| Context menu | ~150 | QMenu with sort, add, remove, move, align, duplicate actions |
| `+` edge buttons | ~80 | Paint at table edge, click to append row/column |
| Table auto-creation | ~100 | Detect `|---|` pattern, parse header, create QTextTable |
| Markdown regeneration | ~120 | Serialize QTextTable back to pipe-delimited markdown |
| Sort implementation | ~60 | Read column, sort rows, rewrite cells |
| Alignment markers | ~40 | Track `:---:` / `---:` per column, apply to separator |
| Backspace select/delete | ~40 | Atomic block selection and deletion logic |
| Atomic block base class | ~100 | Base interface and registry |
| Resize column drag | ~80 | Mouse drag on column border, update widths |

**Total new code for tables: ~970 lines.** The Obsidian-specific chrome is
less than half the code Qt already gives us for the table fundamentals.

---

## Other Atomic Blocks — Sketches

### Code Block

**Simpler than tables.** A fenced code block is a rectangular area with:
- Language label in the top-right corner
- Copy button
- Syntax-highlighted content (KSyntaxHighlighting)
- Line numbers (optional)

**Internal editing:** Plain text with a monospace font. Enter inserts a newline
within the block. Tab inserts spaces. Backtick is just a character.

**Auto-creation:** When the user types ` ``` ` and hits Enter, create a code
atomic block. When they type the closing ` ``` `, the block is complete.

**Markdown regeneration:** Trivial — wrap content in fences with info string.

### Callout

**Medium complexity.** A callout is:
- A colored box with a left border
- An icon and type label in the header (13 built-in types + aliases + custom)
- Optional custom title
- Optional fold toggle (`+` / `-`)
- Rich text body (supports full markdown inside, including nested callouts)

**Built-in types:** note, abstract/summary/tldr, info, todo, tip/hint/important,
success/check/done, question/help/faq, warning/caution/attention,
failure/fail/missing, danger/error, bug, example, quote/cite.

**Custom callout types:** Obsidian supports user-defined callout types via CSS
variables. Users create custom types by adding CSS:
```css
.callout[data-callout="custom-type"] {
    --callout-color: 200, 100, 50;    /* RGB values */
    --callout-icon: lucide-icon-name; /* or inline SVG */
}
```
Our callout renderer must be driven by a style/config table, not hardcoded
per-type rendering. The 13 built-in types are just default entries in this
table. Users can add more via Corbomite's theme/CSS system.

**Internal editing:** The body is a mini markdown editor — it supports inline
formatting, lists, even nested callouts. The type selector is a dropdown or
clickable label.

**Auto-creation:** When the user types `> [!` and completes the type `]`,
transform the blockquote into a callout.

**Markdown regeneration:** Prefix each line with `> `, first line gets
`[!type] title`, fold marker if applicable.

### Display Math

**Simple interaction model.** A display math block (`$$...$$`) has two states:
- **Rendered:** Shows the LaTeX rendering (via JKQTMathText). Read-only.
- **Editing:** Shows the LaTeX source in a monospace text area.

**Transition:** Click on rendered math → switch to editing. Click outside or
press Escape → re-render and switch back.

**Markdown regeneration:** Wrap content in `$$\n...\n$$`.

---

## QTextFrame vs. Custom Tracking

There are two ways to represent atomic blocks in the document:

### Option 1: Use QTextFrame

Insert a `QTextFrame` into the `QTextDocument` for each atomic block. The frame
occupies the source range. `QTextTable` already works this way.

**Pros:**
- Native cursor navigation respects frame boundaries
- Selection across frame boundaries handled by Qt
- `QTextCursor::currentFrame()` tells you if cursor is in an atomic block
- Undo/redo works with frame content changes

**Cons:**
- Frames change the document structure — the raw markdown text is no longer a
  flat string. Frame content is separate from the surrounding text.
- For non-table blocks, we're shoehorning content into a frame model designed
  for tables and nested documents.
- Harder to serialize back to markdown (must reconstruct flat text from
  frame hierarchy).

### Option 2: Custom Block Metadata

Keep the `QTextDocument` as flat text (raw markdown). Track atomic blocks as
metadata in our AST, keyed by source range. The forked text control routes
events based on cursor position.

**Pros:**
- Source text is always raw markdown — serialization is trivial (it's already
  there)
- No fighting with QTextFrame semantics
- Simpler document model
- Source mode shows the raw text unchanged

**Cons:**
- Must manually handle cursor entry/exit from atomic blocks
- Must manually handle selection at atomic block boundaries
- Must manually handle undo/redo for atomic block edits

### Recommendation: Hybrid

Use `QTextFrame` / `QTextTable` for tables (it's the right abstraction,
already implemented). Use custom metadata for simpler atomic blocks (code,
callout, math, image) where the frame model adds complexity without benefit.

This matches how Qt itself works: tables are frames, but code blocks and
images are not.

---

## Design Decisions

### 1. When Does Table Mode Activate?

**Obsidian behavior:** Table mode activates as soon as the separator line is
complete. The raw markdown is NEVER shown again (even when cursor is inside
the table). Source mode shows raw pipes; live preview and reading mode always
show the grid.

**Our behavior:** Same. Tables are always rendered as grids in live preview
and reading mode. Source mode shows raw pipes. There is no "cursor reveals
raw markdown" for tables — the table is always graphical.

This is different from how we handle inline formatting (where `**bold**`
appears raw near the cursor). Tables are complex enough that raw-mode editing
would be a usability regression.

### 2. How Is Cursor Position Tracked Inside Tables?

The cursor is always at a valid `QTextDocument` position. Within a table, this
means it's inside a cell's text content. The `MarkoffTableBlock` translates
between:
- `QTextDocument` cursor position → (row, col, offset-within-cell)
- (row, col, offset-within-cell) → `QTextDocument` cursor position

This mapping is maintained by `QTextTable::cellAt(position)` and
`QTextTableCell::firstCursorPosition()` — already implemented in Qt.

### 3. How Does Undo/Redo Work?

All table edits go through `QTextCursor` edit blocks:

```cpp
cursor.beginEditBlock();
// ... modify cell contents, insert/remove rows/cols ...
cursor.endEditBlock();
```

This means the entire operation (e.g., "sort by column A") is a single undo
step. Qt's undo stack handles the rest.

### 4. How Is the Table Markdown Kept in Sync?

Two approaches:

**Approach A: Shadow buffer.** The `QTextDocument` contains the raw markdown.
When we enter table mode, we parse the markdown into a `QTextTable` (or our
own table model). Edits happen in the table model. On every edit, we
regenerate the markdown and write it back to the source range.

**Approach B: QTextTable IS the document.** The `QTextDocument` contains the
`QTextTable` directly (not the raw markdown). We serialize to markdown only
on save or when source mode is requested.

**Recommendation: Approach A.** The raw markdown is always in the document.
The table model is a view onto it. This keeps the source of truth simple and
makes source mode trivial (just render the text). The regeneration cost is
negligible for tables (< 1ms for any reasonable table size).

---

## Summary

| Question | Answer |
|----------|--------|
| Can forked QPlainTextEdit handle Obsidian tables? | Yes — Qt's `QTextTable`, layout, and navigation are our foundation |
| How much code do we harvest from Qt? | ~1,900 lines (table data model, layout, rendering, navigation) |
| How much do we write? | ~970 lines (Obsidian chrome: handles, menus, buttons, auto-create, serialization) |
| What's the general pattern? | Atomic blocks: graphical units that capture input and serialize to markdown |
| What other blocks use this pattern? | Code blocks, callouts, display math, mermaid, embeds, frontmatter, images |
| Is this the hardest block type? | Yes. Tables are the most complex. Every other atomic block is simpler. |
| What's the main risk? | Cursor/selection behavior at table boundaries — mitigated by using QTextTable's existing cursor model |
