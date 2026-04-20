# Markoff: Reference Codebase Analysis

## Overview

This document catalogs reusable techniques, patterns, and architectural insights
from two reference codebases:

- **Penelope** (`~/dev/Penelope`) — A markdown reader with PostScript/PDF
  rendering, Knuth-Plass typography, and HarfBuzz text shaping
- **Calligra** (`~/src/kde/src/calligra`) — A full office suite with a custom
  text editing/layout engine built on top of Qt

Both are mature C++/Qt6 codebases that have solved subsets of the problems
Markoff faces. This analysis identifies what to borrow, what to adapt, and
what to skip.

---

## From Penelope

### Architecture Overview

Penelope's pipeline:

```
Markdown text
    ↓ MD4C parser (SAX callbacks)
ContentBuilder (callback handler)
    ↓ builds
Content::Document (semantic tree: Paragraph, Heading, List, Table, CodeBlock...)
    ↓ resolved styles
Layout::Engine
    ↓ produces
Box tree (GlyphBox → LineBox → BlockBox → Pages)
    ↓ rendered by
BoxTreeRenderer → {PdfBoxRenderer, QtBoxRenderer}
    ↓ output
PDF bytes / QPainter screen rendering
```

### Reusable Pattern 1: Content Model (Semantic Intermediate Representation)

**What it is:** A `std::variant`-based tree of typed nodes representing the
semantic structure of the document, decoupled from both the parser and the
renderer.

```cpp
namespace Content {
    using InlineNode = std::variant<
        TextRun, InlineCode, Link, InlineImage, FootnoteRef, SoftBreak, HardBreak
    >;
    
    using BlockNode = std::variant<
        Paragraph, Heading, CodeBlock, BlockQuote, List, Table, HorizontalRule,
        FootnoteSection
    >;
    
    struct Document {
        QList<BlockNode> blocks;
    };
}
```

**Why it matters for Markoff:** This is the document model pattern. The parser
produces this tree; the renderer consumes it. Adding an Obsidian extension means
adding a variant case (e.g., `Callout`, `WikiLink`, `Embed`) — the parser and
renderer each handle it independently.

**What to adapt:**
- Add source position tracking (Penelope's `SourceRange: {startLine, endLine}`)
  to every node — essential for cursor mapping in the editor
- Add Obsidian-specific node types (Callout, WikiLink, Embed, MathBlock, etc.)
- Make the tree mutable for editor operations (insert, delete, split, merge blocks)
- Consider `std::variant` vs. class hierarchy — variant is great for pattern
  matching with `std::visit`, but class hierarchy is easier to extend with
  virtual dispatch

**Key files:**
- `~/dev/Penelope/src/model/contentmodel.h` — Type definitions
- `~/dev/Penelope/src/model/contentbuilder.h/cpp` — MD4C → Content::Document
  (1,027 lines, the pattern we'd replicate)

---

### Reusable Pattern 2: ContentBuilder (Parser → Document Model Bridge)

**What it is:** A class that receives MD4C SAX callbacks and constructs the
Content::Document tree. It maintains a stack of open blocks/spans and assembles
the tree as callbacks fire.

**How it works:**
```
MD4C fires enter_block(HEADING, level=2)
    → ContentBuilder pushes Heading{level:2} onto block stack
MD4C fires enter_span(EMPHASIS)
    → ContentBuilder pushes italic format onto char format stack
MD4C fires text("Hello")
    → ContentBuilder creates TextRun with current format, appends to current block
MD4C fires leave_span(EMPHASIS)
    → ContentBuilder pops char format stack
MD4C fires leave_block(HEADING)
    → ContentBuilder pops block stack, appends completed Heading to document
```

**Key technique:** Format stack. Inline formatting (bold, italic, code, links)
is tracked as a stack of `QTextCharFormat` that gets pushed on `enter_span` and
popped on `leave_span`. Nested formatting works naturally.

**What to adapt for Markoff:**
- Extend the callback handler for Obsidian extensions (callouts from
  blockquotes, wikilinks from links, highlights from text, etc.)
- Track source byte offsets (MD4C provides them) and store in each node
- Build the tree in a way that supports incremental updates (e.g., record
  which blocks came from which line ranges so we can re-parse just those lines)

**Key files:**
- `~/dev/Penelope/src/model/contentbuilder.cpp` — The builder pattern (1,027 lines)
- `~/dev/Penelope/src/markdown/documentbuilder.cpp` — Older QTextDocument path (1,094 lines)

---

### Reusable Pattern 3: BoxTreeRenderer (Multi-Backend Rendering)

**What it is:** An abstract base class that traverses the layout tree and calls
virtual primitives for each rendered element. Subclasses implement the
primitives for different output targets.

```cpp
class BoxTreeRenderer {
protected:
    // Primitives — override these for each backend
    virtual void drawRect(const QRectF &rect, const QColor &fill, ...) = 0;
    virtual void drawGlyphs(FontFace *face, qreal fontSize, const GlyphRenderInfo &, ...) = 0;
    virtual void drawImage(const QRectF &destRect, const QImage &image) = 0;
    
    // Shared traversal — walks the box tree and calls primitives
    void renderBlockBox(const BlockBox &block);
    void renderLineBox(const LineBox &line);
    void renderGlyphBox(const GlyphBox &glyph);
};
```

**Why it matters for Markoff:** We need to render markdown content in multiple
contexts: editor overlay, reading mode widget, canvas cards, and eventually
print/PDF. A multi-backend renderer with shared traversal and pluggable
primitives prevents duplication.

**What to adapt:**
- Simplify — Penelope's box tree is overkill for screen rendering. We don't need
  glyph-level positioning for the editor. But the pattern of "traverse AST, call
  virtual paint methods" is right.
- Our backends: `QPainter` (for editor overlay and reading mode), `QTextDocument`
  (for canvas cards, using the existing RenderedDocument interface), and eventually
  PDF/print.

**Key files:**
- `~/dev/Penelope/src/render/boxtreerenderer.h/cpp` — Abstract base
- `~/dev/Penelope/src/pdf/pdfboxrenderer.h/cpp` — PDF backend (758 lines)

---

### Reusable Pattern 4: Style Resolution

**What it is:** A style system with named paragraph styles (Heading1, BodyText,
CodeBlock) and character styles (Bold, Italic, InlineCode), each with a parent
chain for inheritance.

```cpp
ParagraphStyle resolved = styleManager->resolvedParagraphStyle("Heading2");
// Inherits from "BodyText" parent, applies Heading2 overrides
```

**Why it matters for Markoff:** Markdown elements have consistent styling that
should be configurable (themes). A style manager maps AST node types to
resolved styles, making theme changes trivial.

**What to adapt:**
- Much simpler than Penelope's full ODF style model. We need:
  - Heading styles (H1-H6): font size, weight, color, spacing
  - Body text: font, size, line height
  - Code block: font family, background, border
  - Callout: background color, border color, icon (per type)
  - Quote: left border, indent, italic
- Integrate with KDE's color schemes (Breeze Dark, etc.) and Obsidian's
  CSS class system

**Key files:**
- `~/dev/Penelope/src/style/stylemanager.h`
- `~/dev/Penelope/src/style/paragraphstyle.h`
- `~/dev/Penelope/src/style/characterstyle.h`

---

### Reusable Pattern 5: Source Mapping

**What it is:** Every rendered element carries a `SourceMapEntry` linking it back
to the markdown source line range and screen rectangle.

```cpp
struct SourceMapEntry {
    int pageNumber;
    QRectF rect;        // Screen-local coordinates
    int startLine;      // 1-based markdown source line
    int endLine;
};
```

**Why it matters for Markoff:** Bidirectional source mapping is critical for:
- Cursor positioning: click in rendered view → cursor in source
- Scroll sync: scroll in rendered view → scroll in source (and vice versa)
- Outline navigation: click heading in outline → scroll to heading in view
- Backlink highlighting: show which rendered block a backlink refers to

**What to adapt:**
- Make it byte-offset-based (not just line-based) for inline cursor mapping
- Store in the AST nodes, not in a separate list — the AST IS the source map
- Add column/offset tracking for inline elements

**Key files:**
- `~/dev/Penelope/src/canvas/documentview.cpp` — Source map usage (1,772 lines)

---

### Reusable Pattern 6: Render Cache

**What it is:** An LRU cache of rendered output (rasterized page images) with
a generation counter to prevent stale frames during document reload.

```cpp
struct RenderCache {
    struct Page {
        int generation;
        QImage raster;
    };
    QHash<int, Page> pages;
};
```

**Why it matters for Markoff:** In the editor, we render blocks to paint in the
overlay. Caching rendered blocks avoids re-rendering unchanged content on every
paint event.

**What to adapt:**
- Cache per-block, not per-page
- Invalidate on: content change, style change, width change, cursor proximity
  change (block transitions between rendered/raw mode)
- Use the AST's content hash as cache key

---

### Techniques to Skip from Penelope

- **Knuth-Plass line breaking:** Overkill for a screen editor. Qt's built-in
  `QTextLayout` or `QPainter::drawText()` with word wrap is sufficient. Worth
  porting only if we add print/PDF export.
- **HarfBuzz text shaping:** Same — Qt handles text shaping for screen rendering.
  Only needed for precise PDF glyph positioning.
- **Font subsetting:** PDF-specific.
- **Pagination:** Not needed for the editor or reading mode.
- **PostScript output:** Obviously not needed.
- **Footnote page-bottom placement:** Obsidian renders footnotes at document
  bottom, not page bottom. Simpler.

---

## From Calligra

### Architecture Overview

Calligra's text editing stack:

```
QTextDocument (document model, extended with metadata)
    ↑ wrapped by
KoTextDocument (resource attachment: styles, inline objects, etc.)
    ↑ edited by
KoTextEditor (wraps QTextCursor, unified undo/redo)
    ↑ laid out by
KoTextDocumentLayout → KoTextLayoutArea → KoTextLayoutRootArea
    ↑ displayed by
TextShape (QGraphicsItem in KoCanvasBase)
```

### Reusable Pattern 1: KoTextDocument (Document Wrapper)

**What it is:** A thin wrapper around `QTextDocument` that attaches additional
resources (style managers, inline object managers, change tracking) without
subclassing `QTextDocument`.

**Technique:** Uses `QTextDocument::addResource(url, type, data)` to store
typed pointers as document resources, retrievable by URL.

**Why it matters for Markoff:** If we build on `QPlainTextEdit` (Approach 3),
we need to attach markdown-specific data to the `QTextDocument`. Calligra's
wrapper pattern avoids subclassing and keeps the data cleanly associated.

**What to adapt:**
- Attach our AST to the document
- Attach block metadata (rendered height, cache, AST node pointer) per block
- Attach the render engine / style manager

---

### Reusable Pattern 2: KoTextEditor (Cursor Wrapper)

**What it is:** A wrapper around `QTextCursor` that routes all editing operations
through a command system for unified undo/redo.

```cpp
class KoTextEditor {
public:
    void bold();           // Toggle bold on selection
    void insertText(const QString &text);
    void insertTable(int rows, int cols);
    
    void addCommand(KUndo2Command *cmd);
    void beginEditBlock();
    void endEditBlock();
    
private:
    QTextCursor m_cursor;   // Private — only accessible to commands
};
```

**Why it matters for Markoff:** We need markdown-aware editing operations:
- "Toggle bold" means wrapping selection in `**...**`, not applying a char format
- "Insert heading" means prepending `## ` to the line, not changing block format
- "Insert link" means wrapping selection in `[...](url)`

All of these should be undoable as single operations.

**What to adapt:**
- Build a `MarkoffEditor` that wraps either `QPlainTextEdit`'s cursor or our
  own cursor abstraction
- Implement markdown-specific editing commands (ToggleBold, InsertHeading,
  InsertLink, ToggleList, etc.)
- Each command knows how to modify the raw markdown text and can undo itself
- Group related text changes (e.g., wrapping selection in `**...**` is one undo
  unit, not two separate inserts)

---

### Reusable Pattern 3: QTextBlockUserData for Block Metadata

**What it is:** `QTextBlockUserData` is a Qt mechanism for attaching custom data
to each `QTextBlock`. Calligra's `KoTextBlockData` stores:
- List counter data (width, text, position)
- Border data (paragraph borders, reference counted)
- Paint strategies (custom rendering rules)
- Markup ranges (spell check, grammar errors)

**Why it matters for Markoff:** In the QPlainTextEdit + Overlay approach, we
need to know for each text block:
- What AST node type it represents (heading, paragraph, code block line, etc.)
- What its rendered height is (for layout)
- Whether it's currently in "raw" or "rendered" mode
- Cached rendered output (QImage or paint commands)
- Source position data (byte range in the full markdown)

**What to adapt:**
```cpp
struct MarkoffBlockData : QTextBlockUserData {
    ASTNode *astNode;          // Pointer into the AST
    int renderedHeight;         // Height when rendered (vs raw text height)
    bool isRendered;            // Currently showing rendered or raw?
    QImage cachedRender;        // Cached rendered output
    int sourceOffset;           // Byte offset in markdown string
    int sourceLength;           // Byte length of this block's source
};
```

---

### Reusable Pattern 4: Incremental Layout

**What it is:** Calligra's `KoTextDocumentLayout` only re-lays-out blocks that
have changed (marked "dirty"). It uses `documentChanged()` signals from
`QTextDocument` to know which blocks were modified, then re-computes layout
for only those blocks.

**How it works:**
1. `QTextDocument` emits `contentsChanged(position, charsRemoved, charsAdded)`
2. Layout marks affected blocks as dirty
3. Layout runs asynchronously (via `QTimer::singleShot`)
4. Only dirty blocks and their successors (if heights changed) are re-laid-out
5. Rendering updates only the affected viewport region

**Why it matters for Markoff:** For keystroke-level performance, we cannot
re-parse and re-render the entire document on every edit. Incremental layout
is essential.

**What to adapt:**
- On text change: re-parse only the affected block(s) using the primary parser
- Compare old and new AST nodes — if structurally identical (same node type,
  same children), skip re-rendering
- If rendered height changes, re-layout all subsequent blocks (shift their
  y-positions)
- Use `QPlainTextEdit`'s `blockCountChanged` and `contentsChanged` signals
  as triggers

---

### Reusable Pattern 5: Custom Inline Objects (KoInlineObject)

**What it is:** Calligra registers custom "inline objects" that can appear
within text flow — bookmarks, anchors, footnote references, variables. Each
object:
- Occupies a placeholder character (U+FFFC) in the text stream
- Reports its size via `intrinsicSize()`
- Paints itself via `paint(QPainter*, QRectF, ...)`
- Updates its position when the layout changes

**Why it matters for Markoff:** In the editor, we need inline objects for:
- Math expressions rendered inline: `$E = mc^2$` → rendered equation
- Checkboxes: `- [x]` → actual checkbox widget
- Inline images: `![alt](url)` → actual image (small)
- Tags: `#tag` → styled pill/chip

**What to adapt:**
- Use `QTextObjectInterface` (Qt's native mechanism, which Calligra extends)
- Register object types for each inline element
- The overlay approach may be simpler: instead of `QTextObjectInterface`,
  just track inline positions and paint over them during the overlay pass

---

### Reusable Pattern 6: Visitor Pattern for Selection

**What it is:** `KoTextVisitor` traverses complex selections (spanning tables,
frames, nested structures) with a single visitor pattern, handling all the
edge cases of partial selection.

**Why it matters for Markoff:** When the user selects text across multiple
markdown blocks (e.g., from a heading through a code block to a paragraph),
we need to correctly:
- Copy the selected markdown (not rendered HTML)
- Apply formatting to the selection (if in WYSIWYG mode)
- Handle deletion (merge blocks at selection boundaries)

**What to adapt:**
- Build a selection visitor that walks the AST between two source positions
- Each node type handles its own copy/delete semantics
- Heading deletion merges with the following paragraph
- Code block partial selection preserves the fence markers

---

### Techniques to Skip from Calligra

- **ODF serialization:** Not needed. Our format is markdown.
- **Change tracking / revisions:** Not needed initially. Git provides this.
- **Multi-shape text flow:** Not needed. Our text flows in a single column.
- **Tab stops:** Markdown doesn't use tab stops for alignment.
- **Drop caps:** Not a markdown feature.
- **Column layout:** Not needed initially.
- **Sections / master pages:** Not needed.
- **Full OOXML/ODF style model:** Way too complex. Markdown styles are simple.

---

## Synthesis: What to Borrow

### From Penelope — borrow heavily:

| Pattern | Adapt How | Priority |
|---------|-----------|----------|
| Content model (variant AST) | Add source positions, Obsidian nodes, mutability | Critical |
| ContentBuilder (SAX → AST) | Extend for Obsidian extensions | Critical |
| Multi-backend renderer | Simplify to QPainter + QTextDocument | High |
| Style resolution | Simplify to markdown-appropriate styles | Medium |
| Source mapping | Make byte-offset-based, embed in AST | High |
| Render cache | Per-block instead of per-page | Medium |

### From Calligra — borrow selectively:

| Pattern | Adapt How | Priority |
|---------|-----------|----------|
| QTextBlockUserData for metadata | Store AST node, rendered height, cache | Critical |
| Incremental layout | React to contentsChanged, re-parse affected blocks | Critical |
| Editor wrapper (KoTextEditor) | Markdown-aware editing commands with undo | High |
| Document wrapper | Attach AST and render engine to QTextDocument | Medium |
| Inline objects | QTextObjectInterface for math, images, checkboxes | Medium |
| Selection visitor | AST-aware copy/delete across blocks | Low (later) |

### From neither — build fresh:

| Component | Why |
|-----------|-----|
| Live preview mode switching | Novel requirement, no reference |
| Block height management in QPlainTextEdit | Specific to Approach 3 |
| Cursor navigation through rendered blocks | Specific to Approach 3 |
| Obsidian extension parsing | No reference handles this |
| Callout / embed / wikilink rendering | Obsidian-specific |
| Mode system (source/preview/reading) | Obsidian-specific |

---

## Dependency Inventory

Libraries already available in Corbomite that Markoff can reuse:

| Library | Currently Used For | Markoff Would Use For |
|---------|-------------------|----------------------|
| KSyntaxHighlighting | Code blocks in reading mode | Code blocks everywhere + source mode highlighting |
| JKQTMathText | LaTeX math in reading mode | LaTeX math in all modes |
| mmdr (Rust FFI) | Mermaid diagrams in reading mode | Mermaid diagrams in all modes |
| MD4C | (not yet, Penelope uses it) | Primary markdown parser |

Libraries to potentially add:

| Library | License | Purpose |
|---------|---------|---------|
| yaml-cpp | MIT | YAML frontmatter parsing |
| tree-sitter + tree-sitter-markdown | MIT | Incremental editor parsing (phase 2) |
