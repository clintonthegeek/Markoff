# QGraphicsView-Based Editor Architecture

> **Status: IMPLEMENTED** — Core architecture shipped. Retained for design
> rationale. Superseded specs moved to `archive/`.

Supersedes the QTextTable-in-QTextDocument approach from
`2026-04-01-table-embedding-design.md` (archived).

## Problem

Embedding non-text elements (tables, images, math, mermaid diagrams)
inside a single QTextDocument causes intractable conflicts:

- The syntax highlighter formats table cell blocks, making text
  invisible or applying wrong styles
- The revert/convert cycle (QTextTable <-> pipe markdown) runs on
  every reparse, growing the document and causing scroll instability
- Click/cursor handling inside QTextTable conflicts with the text
  control's expectations
- Undo across table creation boundaries crashes (dangling QTextTable
  pointers)
- QPlainTextDocumentLayout can't handle variable-height blocks;
  QTextDocumentLayout works but fights the "markdown is the document"
  principle

These are not implementation bugs — they are architectural mismatches.
QTextDocument was designed for a single homogeneous rich text stream.
We need a document model that supports heterogeneous block types.

## Solution

Split the document into multiple independent text regions separated
by non-text items, all managed by a QGraphicsScene:

```
QGraphicsView (the editor widget)
  +-- QGraphicsScene
        +-- MarkdownTextItem #1  (editable text: lines 1-140)
        +-- TableItem #1         (interactive table)
        +-- MarkdownTextItem #2  (editable text: lines 153-155)
        +-- TableItem #2         (interactive table)
        +-- MarkdownTextItem #3  (editable text: lines 158-197)
```

Each `MarkdownTextItem` wraps a QWidgetTextControl (our existing
forked TextControl) with its own QTextDocument. Each document contains
only plain markdown text — no QTextTable objects, no frames, no rich
text. The highlighter works perfectly because it only sees markdown.

Tables are separate QGraphicsItem subclasses that paint themselves
and handle their own input. They are NOT part of any QTextDocument.

The QGraphicsView provides pixel-based smooth scrolling, viewport
culling (only visible items are painted/laid out), and Z-ordering —
all for free.

## Architecture

### Scene Items

**MarkdownTextItem** (QGraphicsTextItem subclass)
- Wraps our forked TextControl + QTextDocument
- Contains a contiguous region of raw markdown text
- Full editing: cursor, selection, IME, undo, clipboard
- Highlighter (MarkdownHighlighter) runs independently on each
  document — no cross-contamination
- Rendered height = documentLayout()->documentSize().height()
- Emits signals when content changes (for reparse, serialization)

**TableItem** (QGraphicsItem subclass)
- Custom QPainter rendering: grid lines, cell text, headers, chrome
- Custom cursor management: tracks active cell, cursor position
  within cell text, blinking caret, text selection per cell
- Cell text editing via per-cell QTextDocument + QTextLayout (for
  IME, font metrics, word wrap) — NOT a QGraphicsProxyWidget
- Owns its data model: headers, rows, columns, alignments
- Tab/Shift+Tab/Enter/Escape navigation between cells
- Row/column operations via context menu and hover handles
- Serializes to/from pipe-delimited markdown
- Does NOT live in any QTextDocument
- Height = computed from row count and cell content

**CodeBlockItem** (QGraphicsItem subclass)
- Paints syntax-highlighted code with language label, line numbers
- Handles text editing internally (monospace font, tab = spaces)
- Serializes to/from fenced code block markdown
- KSyntaxHighlighting for language-specific coloring

**ImageItem** (QGraphicsItem subclass)
- Renders image from vault path or URL
- Click to enter source mode (edit alt text, path)
- Resize handles
- Serializes to `![alt](path)` or `![[wikilink]]`

**MathItem** (QGraphicsItem subclass)
- Renders LaTeX via JKQTMathText
- Click to enter source editing (monospace LaTeX input)
- Serializes to `$$...$$` block

**MermaidItem** (QGraphicsItem subclass)
- Renders SVG diagram
- Click to enter source editing
- Serializes to ` ```mermaid ... ``` `

**EmbedItem** (QGraphicsItem subclass)
- Renders note preview
- Click to navigate to embedded note
- Serializes to `![[note]]`

All non-text items share a common base: `BlockItem`. This base
provides the interface for serialization (`toMarkdown()`), height
reporting, selection state, and focus transfer protocol.

### Layout

Items are stacked vertically in the scene. A `DocumentLayout`
coordinator manages positions:

```
y = 0
for each item in order:
    item.setPos(0, y)
    y += item.boundingRect().height() + spacing
scene.setSceneRect(0, 0, viewWidth, y)
```

When an item's height changes (text added/removed, table row
added), the coordinator repositions all items below it and updates
the scene rect. QGraphicsView automatically updates scrollbars.

### Cursor Navigation Between Items

When the cursor hits the boundary of a text item:

1. User presses Down at last line of MarkdownTextItem #1
2. `QTextCursor::movePosition(Down)` returns false (can't move)
3. Our subclass detects this and emits `cursorAtBoundary(Bottom)`
4. The coordinator receives the signal, transfers focus:
   - If next item is a TableItem: focus the table (first cell)
   - If next item is another text item: focus it, cursor at top
5. Similarly for Up at the top of an item, and Escape from a table

### Mode Switching

**Source mode**: All items are removed from the scene. A single
MarkdownTextItem holds the entire raw markdown document. Standard
QPlainTextEdit-like editing.

**Live Preview mode**: The raw markdown is split at table/image/math
boundaries. Each segment becomes a MarkdownTextItem. Tables become
TableItems. The coordinator positions everything.

**Reading mode**: Same scene structure but all items are read-only.
Or: a separate ReadingView widget as today.

### Document Serialization

To save or switch to source mode, walk the scene items in order:

```
QString markdown;
for each item in order:
    if (MarkdownTextItem): markdown += item->toPlainText()
    if (TableItem): markdown += item->toMarkdown()
    if (other): markdown += item->toMarkdown()
```

The result is the original markdown with tables serialized as pipe
text. The file on disk is always flat markdown.

### Parsing and Splitting

On file load or mode switch to live preview:

1. Parse the full markdown with tree-sitter
2. Walk the AST to find ALL block-level non-text boundaries:
   tables, fenced code blocks, display math ($$...$$), mermaid
   blocks, image lines, embed lines
3. Split the markdown at those boundaries into text segments
4. Create MarkdownTextItem for each text segment
5. Create appropriate BlockItem for each non-text block:
   TableItem, CodeBlockItem, MathItem, ImageItem, etc.
6. Position items via the coordinator

On text change within a MarkdownTextItem (debounced):

1. Re-parse that item's markdown with tree-sitter
2. Check if the edit created a new table (user typed separator)
3. If yes: split the text item into two, insert a TableItem between
4. Check if the edit destroyed a table boundary
5. If yes: merge adjacent text items, absorb the table's markdown

### Cross-Boundary Selection

When the user drag-selects across item boundaries:

**State**: A `SelectionManager` on the scene tracks:
- Anchor item + cursor position (where the drag started)
- Current item + cursor position (where the drag is now)
- List of fully-selected items between anchor and current

**Visual rendering**:
- First text item: QTextCursor selection from anchor to end
- Intermediate items: fully highlighted (table gets a blue overlay)
- Last text item: QTextCursor selection from start to current

**Clipboard (Ctrl+C)**:
- Walk selected items in order
- For each text item: get selectedText() or toPlainText() as raw
  markdown
- For each table: call toMarkdown() to get pipe text
- Concatenate and put on clipboard as text/plain

**Implementation**: Scene-level event filter intercepts mouse events
during drag. When the drag crosses an item boundary, the filter
coordinates selection state across items. See the cross-boundary
selection research document for detailed approach.

Cross-boundary selection is a MUST-HAVE, not deferrable. Every
non-text block item (table, code block, image, math, mermaid, embed)
participates in the selection system. A fully selected block item
contributes its markdown serialization to the clipboard.

### What We Keep

- **TextControl** (src/TextControl.cpp): unchanged. Wraps inside
  QGraphicsTextItem instead of being owned by a QAbstractScrollArea.
- **MarkdownHighlighter** (src/MarkdownHighlighter.cpp): unchanged.
  Runs on each text item's QTextDocument independently.
- **TreeSitterParser** (src/TreeSitterParser.cpp): unchanged. Parses
  markdown text, builds span map.
- **Document + Renderer**: unchanged. Reading mode uses the same
  rendering pipeline.
- **TableHandler**: parsing and serialization reused. The conversion
  to QTextTable is deleted — replaced by TableItem.
- **Decorated ranges**: code block/callout backgrounds painted per
  text item, using existing logic.

### What We Delete

- **PlainTextDocumentLayout**: gone. Each text item uses Qt's default
  QTextDocumentLayout.
- **QTextTable conversion/reversion**: gone. Tables are scene items.
- **isTableCellBlock / tableForBlock**: gone. No table cells in any
  QTextDocument.
- **Table layout graft (layoutTable, layoutCellContent, etc.)**: gone.
- **All scroll hacks**: gone. QGraphicsView handles scrolling.
- **Block position cache**: gone. QTextDocumentLayout handles positions.
- **The current Editor.cpp**: replaced entirely. The new editor is a
  QGraphicsView subclass with a scene coordinator.

### What We Build New

| Component | Responsibility | Est. Lines |
|-----------|---------------|-----------|
| `MarkdownTextItem` | Editable text region, wraps TextControl | ~200 |
| `BlockItem` | Base class for non-text items (selection, serialize) | ~100 |
| `TableItem` | Interactive table, custom paint + cursor | ~500 |
| `CodeBlockItem` | Syntax-highlighted code, editing | ~300 |
| `MathItem` | LaTeX rendering, click-to-edit | ~150 |
| `ImageItem` | Image rendering, resize handles | ~150 |
| `SceneCoordinator` | Manages item list, positions, splitting/merging | ~300 |
| `SelectionManager` | Cross-boundary selection + markdown clipboard | ~300 |
| `GraphicsViewEditor` | QGraphicsView subclass, event routing | ~200 |
| `MarkdownSplitter` | Splits markdown at all block boundaries | ~150 |

Total new code: ~2,350 lines. Replaces ~1,500 lines of current
Editor.cpp. Net increase ~850 lines but covers tables, code blocks,
math, images — not just tables.

## GPL Harvest Mindset

We fork Qt GPL source directly — the same approach that gave us
TextControl. We are not limited to Qt's public API:

- **Fork QGraphicsTextItem** (~300 lines in qgraphicsitem.cpp) into
  `MarkdownTextItem`. Modify internals for: cursor boundary
  detection, markdown-aware clipboard, cross-item selection
  coordination, debounced reparse integration.

- **Fork QGraphicsView** (~3,900 lines) into `GraphicsViewEditor`
  if needed for: custom scroll step sizes, scene-level selection
  drag interception, keyboard event routing between items.

- **Reuse QWidgetTextControl** (already forked as TextControl).
  QGraphicsTextItem wraps QWidgetTextControl internally — our fork
  already has all the text editing infrastructure.

The question is never "does Qt's API support this?" — it's "does
Qt's source code contain the machinery we need?" If yes, we take it
and modify it. GPL allows this.

## Risks

### Cross-boundary selection complexity

The hardest part. Drag-selecting across item boundaries requires
coordinating selection state across independent QTextDocuments.
This is a must-have, so the SelectionManager is part of the core
implementation, not a follow-up. The scene-level event filter
approach keeps it contained — items don't need to know about each
other, only the SelectionManager does.

### Text splitting at wrong boundaries

If the tree-sitter parse misidentifies table boundaries, the split
will be wrong. Mitigation: the table pattern is unambiguous (pipe
rows + separator), and we already have TableHandler::detectTables().

### Performance with many items

A document with 50 tables has 51 text items + 50 table items.
QGraphicsScene handles thousands of items efficiently with viewport
culling. Not a concern.

### Undo across splits

When a user types a table separator, the text item splits into two
items + a table. Undo should reverse this. Mitigation: the split
operation is triggered by the debounced reparse, which operates on
the serialized markdown. Undo within the text item reverts the
separator text; the next reparse merges the items back.

## Success Criteria

1. Tables render correctly with cell text visible
2. Clicking in a table cell places cursor there
3. Tab/Shift+Tab navigates between cells
4. Typing in a cell edits the cell
5. Scrolling is smooth (pixel-based, native QGraphicsView)
6. Typing in text regions has <10ms latency
7. Syntax highlighting works correctly (no table cell interference)
8. Ctrl+C from a text region copies raw markdown
9. Source mode shows full raw markdown
10. Save produces identical markdown to what was loaded
    (round-trip fidelity)
