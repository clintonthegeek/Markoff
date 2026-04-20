# Markoff: Editor Architecture Survey

## Overview

This document examines how existing WYSIWYG and hybrid markdown editors work,
what Qt6 provides for building one, and the architectural approaches available
to Markoff.

---

## How Existing Editors Work

### The Two Fundamental Approaches

Every WYSIWYG markdown editor falls into one of two camps:

**Camp A: "Document is plain text, rendering is decoration"**
(CodeMirror 6, Obsidian Live Preview, Zettlr)

The document model is always the raw markdown string. Visual formatting is
achieved by overlaying decorations (styled ranges, widgets, replacements) on
top of the plain text. The cursor always operates on the underlying text.
When the cursor enters a decorated region, decorations are removed to reveal
raw syntax.

**Camp B: "Document is structured, markdown is I/O format"**
(ProseMirror, TipTap, Milkdown, Typora)

The document model is a typed tree of nodes (headings, paragraphs, lists).
Markdown is parsed into this tree on load and serialized back on save. The
user edits the tree, never seeing raw markdown unless they switch to a source
view. All editing operations are schema-constrained.

Both approaches work. The key question is which maps better onto Qt6.

---

### Obsidian's Approach (Camp A — CodeMirror 6)

Obsidian uses CodeMirror 6 with custom extensions for Live Preview. The
architecture:

1. **Document model:** Flat string split by lines, stored in a persistent
   tree for efficient edits at arbitrary positions.
2. **State management:** Immutable `EditorState` objects. All changes go
   through transactions (atomic state transitions). Enables undo/redo,
   collaborative editing, and plugin coordination.
3. **Decoration types:**
   - *Mark:* Style ranges (bold, italic, heading size)
   - *Widget:* Insert DOM elements at a position (math renderings, checkboxes)
   - *Replace:* Hide a range and optionally show a widget instead (images, diagrams)
   - *Line:* Style entire lines (blockquote indentation, callout background)
4. **Cursor-driven transitions:** A `ViewPlugin` tracks cursor position.
   When the cursor enters a decorated range, that range's decorations are
   removed, revealing raw markdown. When the cursor leaves, decorations
   are re-applied.
5. **Extension composition:** Extensions are combined via "facets" —
   composable configuration values that merge without conflicts.

**Key insight for Markoff:** This approach keeps the source of truth as plain
text. There is no bidirectional synchronization problem — the document IS the
markdown. Rendering is purely a view concern.

### Typora's Approach (Camp B — Hidden Source)

Typora is closed-source but its behavior reveals the architecture:

1. The document is represented as HTML internally (Chromium DOM).
2. Markdown syntax is hidden except at the cursor's current element.
3. Bidirectional mapping between markdown and HTML is maintained.
4. Typing in a formatted region modifies the underlying markdown.

**Key insight:** True Camp B WYSIWYG for markdown is extremely hard because
markdown is ambiguous in ways structured formats are not. `_foo_` is emphasis,
but `_foo _bar_` depends on context. Round-trip fidelity (markdown → rendered →
edited → markdown) is a perennial source of bugs.

### Mark Text's Approach (Camp B — Custom "Muya" Engine)

Mark Text built a custom engine called Muya:

1. Block-based document model (not using ProseMirror or CodeMirror).
2. Parses markdown into blocks with CommonMark + GFM.
3. Transforms bidirectionally between markdown and DOM representation.
4. Event listeners handle input and update both models.

**Key insight:** It IS possible to build a custom WYSIWYG engine from scratch.
Muya proves this. But Mark Text is Electron-based — the DOM provides a rich
rendering surface that Qt does not have natively.

---

## What Qt6 Provides

### QTextDocument: The Foundation

Qt's rich text model. A tree of:
- **Frames** (QTextFrame) — containers
- **Blocks** (QTextBlock) — paragraphs, headings, list items
- **Fragments** (QTextFragment) — runs of text with uniform formatting
- **Tables** (QTextTable) — grid of cells
- **Lists** (QTextList) — grouped list items

Each block has a `QTextBlockFormat` (alignment, spacing, margins) and each
fragment has a `QTextCharFormat` (font, color, weight, etc.).

**Markdown support (since Qt 5.14):**
- `setMarkdown()` — parse markdown, populate document (CommonMark + GitHub dialect)
- `toMarkdown()` — serialize back

**Limitations for our purposes:**
- Limited markdown dialect (no Obsidian extensions)
- Round-trip lossy (some formatting lost in markdown ↔ rich text conversion)
- No callouts, no wikilinks, no embeds, no math, no mermaid
- `QTextBlockFormat` has limited styling (no `border-radius`, limited `max-width`)
- No custom block types (everything is a paragraph, heading, list, or table)

### QTextEdit: The Default Rich Text Widget

Subclass of `QAbstractScrollArea` that renders a `QTextDocument`.

**What it provides:**
- Rich text rendering (bold, italic, headings, lists, tables, images)
- Cursor management and selection
- Undo/redo
- Copy/paste (rich text and plain text)
- Drag and drop
- Find and replace
- Spell checking integration

**What it lacks:**
- Custom block rendering (callouts, code blocks with headers, math)
- Live preview / WYSIWYG markdown editing
- Source position tracking
- Decoration/widget overlay system (no CodeMirror-style decorations)
- Performance on large documents (pixel-exact layout for entire document)

### QPlainTextEdit: The Fast Alternative

Optimized for plain text. Uses line-by-line scrolling instead of pixel-exact
layout, enabling excellent performance on large documents.

**What it provides:**
- `QSyntaxHighlighter` support (the basis for markdown highlighting)
- Line numbers, current line highlighting
- Fast scrolling and rendering for large files
- `QTextBlockUserData` — attach custom data to each line

**What it lacks:**
- No rich text rendering (no images, no variable font sizes, no tables)
- No inline widgets
- Cannot display rendered markdown

### QAbstractTextDocumentLayout

Custom layout engine for `QTextDocument`. You subclass this and override:
- `documentChanged()` — called when blocks change, do layout here
- `draw()` — paint the document
- `hitTest()` — map screen coordinates to document positions
- `blockBoundingRect()` — return the rect for a block
- `layoutInlineObject()` — position custom inline objects

`QPlainTextDocumentLayout` is the built-in simplified implementation
(line-by-line, no pixel-exact heights).

**This is the key extension point.** By implementing a custom layout, you can
control exactly how each block is positioned and painted, while still using
`QTextDocument` as the document model and `QTextCursor` for editing.

### QTextObjectInterface

Allows custom inline objects within `QTextDocument`. You implement:
- `intrinsicSize()` — report the object's size
- `drawObject()` — paint the object

Objects are placed using a special character (U+FFFC) in the text stream and
a custom `QTextCharFormat` property identifying the object type.

**Use cases:** Inline math renderings, callout icons, checkbox widgets,
embedded images with custom chrome.

### QGraphicsView / QGraphicsScene

The canvas framework. Arbitrary 2D items painted with `QPainter`.

**Relevant capabilities:**
- `QGraphicsProxyWidget` — embed QWidgets inside the scene
- `QGraphicsTextItem` — text items with `QTextDocument` rendering
- Custom items with full paint control
- Pan, zoom, selection

**For the editor:** Could serve as the editing surface instead of
`QTextEdit`/`QPlainTextEdit`. Calligra uses this approach — text shapes
in a graphics scene. Provides maximum rendering flexibility but requires
implementing cursor, selection, input handling, and scrolling from scratch.

### QSyntaxHighlighter

Base class for syntax highlighting on `QTextDocument`. Processes blocks
one at a time via `highlightBlock()`. Can apply `QTextCharFormat` to
text ranges. Works with both `QTextEdit` and `QPlainTextEdit`.

Already used by `qmarkdowntextedit` and `KSyntaxHighlighting`.

---

## Architectural Approaches for Markoff

### Approach 1: Extended QTextEdit (Camp B Lite)

**Concept:** Use `QTextEdit` as the editing surface. Parse markdown → populate
`QTextDocument` with rich formatting. When the user edits, serialize back to
markdown. Use `QTextObjectInterface` for custom inline objects (math, images).
Use `QAbstractTextDocumentLayout` for custom block rendering (callouts,
code blocks).

**How it works:**
1. Parse markdown → build AST → populate `QTextDocument` with custom
   block/char formats and inline objects
2. User edits the rich text view
3. On change, serialize `QTextDocument` → markdown (custom serializer,
   not `toMarkdown()`)
4. Cursor near raw syntax → show syntax (decoration toggle)

**Pros:**
- `QTextEdit` provides cursor, selection, undo/redo, input handling, clipboard
- `QTextDocument` provides a structured document model
- `QTextObjectInterface` supports inline objects
- Least work for basic text editing affordances

**Cons:**
- Bidirectional sync (markdown ↔ QTextDocument) is the central difficulty
- Round-trip fidelity is hard — markdown → rich text loses information
  that markdown → markdown does not
- `QTextEdit` fighting — its default behaviors assume rich text editing, not
  markdown editing. You spend a lot of time suppressing unwanted behaviors.
- Custom block rendering via `QAbstractTextDocumentLayout` is complex and
  poorly documented
- Live preview (cursor-aware decoration toggle) requires tracking which blocks
  are "near" the cursor and re-rendering them on cursor movement

**Assessment:** Feasible for reading mode and basic editing. Painful for live
preview. KDE's Marknote went this route and hit limitations.

---

### Approach 2: Custom Widget on QAbstractScrollArea (Camp A Native)

**Concept:** Build a custom widget from scratch on `QAbstractScrollArea`. The
document model is the raw markdown string (or our AST with source positions).
Rendering is done by painting directly to the viewport with `QPainter`. The
widget manages its own cursor, selection, and input handling.

This is the Qt-native equivalent of CodeMirror 6's architecture.

**How it works:**
1. Parse markdown → build AST with source positions
2. Layout: walk AST, compute block positions (y-offsets, heights), compute
   line breaks within blocks
3. Paint: iterate visible blocks, paint rendered content for blocks away
   from cursor, paint raw text for blocks near cursor
4. Input: handle key events, modify the markdown string, re-parse affected
   blocks, re-layout and re-paint
5. Cursor: map pixel position ↔ markdown byte offset using source positions

**Pros:**
- Source of truth is always markdown — no synchronization problem
- Full control over rendering — callouts, math, diagrams, anything
- Live preview is natural — just change what you paint per block based on
  cursor distance
- Incremental rendering — only re-paint dirty blocks
- No `QTextEdit` fighting

**Cons:**
- Enormous implementation effort. Must implement from scratch:
  - Text input handling (including IME/compose sequences)
  - Cursor rendering and navigation (left, right, up, down, home, end,
    word movement, line wrapping)
  - Selection (mouse drag, shift-click, shift-arrow, double-click word,
    triple-click line)
  - Clipboard (copy, cut, paste with format detection)
  - Undo/redo (semantic grouping, merge adjacent typing)
  - Scrolling (smooth scroll, scroll-to-cursor, page up/down)
  - Accessibility (screen reader, high contrast, keyboard navigation)
  - Find and replace
  - Drag and drop
  - Multiple cursors
  - Bidi text support
  - Line wrapping / text layout
- Reimplementing all of this correctly and robustly is a multi-year effort

**Assessment:** Maximum flexibility, maximum effort. This is what Calligra did
(though they still used `QTextDocument` internally and `QTextCursor` for
editing operations). The critical question is whether we can shortcut the
text handling by using `QTextLayout` for line breaking and glyph positioning
while owning the rest.

---

### Approach 3: Hybrid — QPlainTextEdit + Overlay Rendering (Camp A Pragmatic)

**Concept:** Use `QPlainTextEdit` as the editing surface (it handles text input,
cursor, selection, undo/redo, scrolling). Overlay rendered content on top of the
plain text using viewport painting. When the cursor is in a block, show raw
markdown (the plain text underneath). When the cursor is elsewhere, paint the
rendered version over the plain text.

This is the approach used by several Qt-based code editors that need inline
annotations (error squiggles, inline values, etc.).

**How it works:**
1. `QPlainTextEdit` holds the raw markdown as its document
2. `QSyntaxHighlighter` provides base syntax coloring
3. On paint, for each visible block:
   - If cursor is in or near this block: let `QPlainTextEdit` render normally
     (showing raw markdown with syntax highlighting)
   - If cursor is far: paint the rendered version (from AST → `QPainter`)
     over the text area, hiding the raw text
4. Block heights must be managed: rendered blocks may be taller than raw text
   (images, math, diagrams). Use `QTextBlockFormat::setLineCount()` or
   custom block heights to allocate space.
5. Inline widgets (math, images) painted during the overlay pass

**Pros:**
- `QPlainTextEdit` handles all text editing affordances (cursor, selection,
  undo/redo, clipboard, IME, find/replace, accessibility)
- Source of truth is always the plain text — no sync problem
- Incremental: only re-render blocks whose markdown changed
- Live preview is the natural mode — just toggle the overlay per block
- `QSyntaxHighlighter` provides source-mode highlighting for free
- Performance: `QPlainTextEdit` uses line-by-line scrolling, excellent
  for large documents

**Cons:**
- Block height management is tricky. A rendered image block takes more vertical
  space than the raw `![alt](url)` text. `QPlainTextEdit` assumes uniform line
  heights — we'd need to fake extra lines or subclass the layout.
- Overlay rendering must align precisely with the text widget's scrolling
- Tables in WYSIWYG mode are hard (a table in markdown is N lines of text,
  but rendered is a grid — different geometry)
- Some interactions are awkward: clicking on a rendered image should... what?
  Move the cursor to the `![alt](url)` line? Open the image? Both?
- Cursor movement through rendered blocks: arrow-down should skip from the
  last line above the block to the first line below it if the block is rendered
  as a widget (not as N lines of text)

**Assessment:** The pragmatic sweet spot. 80% of the editing UX for 20% of
the implementation effort compared to Approach 2. The block height problem
is the main engineering challenge.

---

### Approach 4: Dual Widget — QPlainTextEdit (source/edit) + Custom Renderer (preview)

**Concept:** Two separate widgets sharing one document. The editor widget is
`QPlainTextEdit` with markdown syntax highlighting. The preview widget is a
custom-rendered view (using `QPainter` on a `QWidget` or `QAbstractScrollArea`).
In "live preview" mode, both are visible; in source mode, only the editor; in
reading mode, only the preview.

NOT the same as a "split view" — in live preview mode, the two widgets are
overlaid or interlocked, with the preview replacing the editor block-by-block
except where the cursor is.

**How it works:**
1. `QPlainTextEdit` is the editing surface (always present, sometimes hidden)
2. Preview renderer draws rendered blocks into a separate viewport
3. In live preview mode, blocks near the cursor show the `QPlainTextEdit`;
   blocks far from the cursor show the preview renderer
4. Scroll positions are synchronized

**Pros:**
- Clean separation of editing and rendering concerns
- Each widget can be optimized for its purpose
- Source mode = just show the editor
- Reading mode = just show the renderer
- Live preview = composite the two

**Cons:**
- Synchronization between two widgets is complex (scroll position, cursor
  position, block heights must match)
- The "interlocking" display is effectively the same problem as Approach 3's
  overlay, just with more moving parts
- Two viewports means potential for visual glitches during transitions

**Assessment:** Over-engineered compared to Approach 3. The separation is
conceptually clean but the interlocking display negates the simplicity benefit.

---

### Approach 5: QGraphicsView-Based Editor (Calligra Approach)

**Concept:** Use `QGraphicsView` / `QGraphicsScene` as the editing surface.
Each block (paragraph, heading, code block, callout, etc.) is a
`QGraphicsItem` that handles its own rendering and input. The scene manages
layout, scrolling, and item coordination.

**How it works:**
1. Parse markdown → AST
2. For each AST block, create a `QGraphicsItem`:
   - `ParagraphItem` — handles text editing for its block
   - `HeadingItem` — larger text, outline integration
   - `CodeBlockItem` — syntax-highlighted, language label
   - `CalloutItem` — colored box with icon
   - `MathItem` — LaTeX rendering
   - `ImageItem` — image display with sizing
3. Scene manages vertical stacking of items
4. Each item owns a `QTextDocument` fragment for its inline text
5. Cursor moves between items as the user navigates

**Pros:**
- Maximum rendering flexibility — each item type paints however it wants
- Natural model for "blocks with different rendering" — each block IS a
  different item type
- `QGraphicsProxyWidget` can embed full QWidgets for complex cases
- Pan/zoom comes free (useful for future features)
- Canvas cards already use this architecture

**Cons:**
- `QGraphicsView` is designed for 2D graphics, not text editing
- Cursor navigation between items is complex — must handle up/down arrow
  crossing item boundaries, selection spanning multiple items
- Text editing within items requires `QGraphicsTextItem` or
  `QGraphicsProxyWidget` with `QTextEdit` — nested editing is awkward
- Performance: each item is a separate paint call, no shared text layout
- Scroll behavior is different from text editors (viewport-based vs
  line-based)
- Accessibility is poor — screen readers don't understand graphics items

**Assessment:** Interesting for a document layout tool (Calligra's use case)
but over-engineered for a markdown editor. The block-item model is appealing
but the text editing story within items is the weak point.

---

## Approach Comparison

| | Effort | Live Preview | Rendering Flex | Text Editing | Performance |
|---|---|---|---|---|---|
| **1: Extended QTextEdit** | Medium | Hard | Medium | Free | Medium |
| **2: Custom from scratch** | Very High | Natural | Maximum | Must build | Depends |
| **3: QPlainTextEdit + Overlay** | Medium | Natural | High | Free | High |
| **4: Dual Widget** | High | Complex | High | Free | High |
| **5: QGraphicsView** | High | Natural | Maximum | Hard | Medium |

---

## Recommendation

**UPDATE:** The original recommendation was Approach 3 (QPlainTextEdit +
Overlay). This has been superseded by a variant of **Approach 2 (Custom Widget)**
that eliminates the "from scratch" objection by forking Qt's own GPL source
code. See `06-qt-source-harvest.md` for full details.

The revised approach forks `QPlainTextEdit` + `QWidgetTextControl` (~7,800
lines of GPL code) into our own `MarkoffEditor` + `MarkoffTextControl`, then
modifies the internals to add live preview rendering, variable block heights,
and markdown-aware input handling. This gives us:

- All text editing affordances (cursor, selection, IME, clipboard, undo/redo,
  accessibility, scrolling) from day one — taken directly from Qt
- Full control over internals — no fighting the public API
- No block height management hacks — we modify the layout code directly
- No overlay alignment issues — rendering is in the widget's own paint path

The original Approach 3 rationale (text editing is solved, live preview is
natural, source mode is free, rendering flexibility is high, incremental by
design) still applies — but now from inside the widget rather than on top of it.

---

## Reading Mode: A Separate Rendering Pipeline

A key finding from Obsidian's internals: **Live Preview and Reading View are
two completely separate rendering pipelines.** Obsidian uses CodeMirror 6 +
Lezer for Live Preview and remark/unified + Prism for Reading View. They even
use different syntax highlighting libraries, which causes subtle styling
inconsistencies between modes.

For Markoff, reading mode does not use the editor widget at all. It is a
separate `QWidget` subclass that:

1. Receives a parsed AST
2. Lays out blocks using `QPainter` metrics
3. Paints rendered content directly
4. Handles link clicks, scroll, selection (for copy), and find

This is essentially a simplified version of Penelope's `DocumentView` (minus
pagination) and the existing `RenderedDocument` / `MarkdownRenderEngine`
interface already spec'd for canvas cards.

**Unlike Obsidian, we can do better on consistency.** Both our Live Preview
(editor block rendering) and Reading View consume the same AST and use the
same `MarkoffRenderer`. The rendering code is shared — both pipelines call
the same AST → `QPainter` functions. The only difference is context: the
editor renderer paints blocks inline with the text widget, while the reading
view renderer paints blocks into a standalone scroll area. This gives us
visual consistency that Obsidian lacks.

---

## Key Technical Challenges

### 1. Block Height Management in QPlainTextEdit

When a markdown image `![alt](url)` is rendered as an actual image in the
overlay, it takes (say) 200px instead of the 15px of the raw text line. The
`QPlainTextEdit` must allocate 200px of vertical space for that block.

**Approach:** Subclass `QPlainTextDocumentLayout` and override
`blockBoundingRect()` to return the rendered height for blocks that are in
"preview" mode (cursor far away). When the cursor enters the block, snap back
to the text height.

### 2. Cursor Navigation Through Rendered Blocks

When the user presses Down Arrow and the next block is a rendered image, the
cursor should jump past the image to the next text block. When they press Up
Arrow from below the image, same thing in reverse.

**Approach:** Override `QPlainTextEdit::keyPressEvent()` for arrow keys. Check
if the target block is rendered; if so, skip to the next/previous text block.

### 3. Click-to-Edit on Rendered Blocks

When the user clicks on a rendered callout, the overlay should disappear and
the cursor should be placed in the raw markdown at the clicked position.

**Approach:** In `mousePressEvent()`, check if the click is in a rendered block.
If so, map the click position to the source text offset (using source position
data from the AST), place the cursor there, and trigger a re-paint that shows
the raw text.

### 4. Inline Widget Rendering

LaTeX math, checkboxes, and inline images need to render within a line of text,
not as full-block replacements.

**Approach:** Use `QTextObjectInterface` to register custom inline objects.
These participate in `QPlainTextEdit`'s text layout and are sized/positioned
by the layout engine. The overlay paints over them when in rendered mode.

Alternatively, for the overlay approach: track the x-position of inline
objects within the line and paint them at the correct offset during overlay
rendering. This avoids `QTextObjectInterface` entirely but requires manual
inline layout.

### 5. Table Rendering

Markdown tables are N lines of pipe-delimited text. Rendered, they're a grid.
The geometry is fundamentally different.

**Approach:** Treat the entire table as a single rendered block. When the
cursor enters any line of the table's source text, show all lines as raw text.
When the cursor is elsewhere, render the table as a grid in the overlay,
allocating the full grid height in the block layout.

### 6. Scroll Position Preservation

When switching between source mode and live preview, or when a rendered block
changes height, the viewport should not jump.

**Approach:** Track the "anchor block" — the topmost visible block and its
pixel offset within the viewport. After re-layout, scroll to restore the
anchor block to the same position.
