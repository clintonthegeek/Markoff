# Markoff: Options and Tradeoffs

## Overview

This document synthesizes the findings from the problem definition (01),
parser survey (02), editor architecture survey (03), and reference codebase
analysis (04) into concrete architectural options for Markoff.

---

## The Three Viable Architectures

After eliminating approaches that are clearly wrong (Electron, QWebEngine,
building entirely from scratch with no Qt text infrastructure), three viable
architectures remain. Each represents a different answer to the fundamental
question: **where does the markdown live?**

---

### Option A: QPlainTextEdit + Overlay ("Markdown is the document")

**Core idea:** The raw markdown string lives in a `QPlainTextEdit`. A parallel
AST tracks the document structure. The editor paints rendered content over the
plain text for blocks away from the cursor ("live preview overlay"). Source
mode is just the plain text with syntax highlighting. Reading mode is a
separate rendering widget.

**Parser:** MD4C → custom AST builder (Penelope pattern)

**Component architecture:**

```
┌─────────────────────────────────────────────────────┐
│                    MarkoffEditor                     │
│  ┌──────────────────────────────────────────┐       │
│  │           QPlainTextEdit                  │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  QTextDocument (raw markdown)    │    │       │
│  │  │  + QTextBlockUserData per block  │    │       │
│  │  └──────────────────────────────────┘    │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  QSyntaxHighlighter              │    │       │
│  │  │  (markdown syntax coloring)      │    │       │
│  │  └──────────────────────────────────┘    │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  Overlay Painter                  │    │       │
│  │  │  (renders AST blocks over text)   │    │       │
│  │  └──────────────────────────────────┘    │       │
│  └──────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────┐       │
│  │           MarkoffDocument                 │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  AST (variant-based tree)        │    │       │
│  │  │  + source positions per node     │    │       │
│  │  │  + rendered height per block     │    │       │
│  │  └──────────────────────────────────┘    │       │
│  └──────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────┐       │
│  │           MarkoffRenderer                 │       │
│  │  (AST → QPainter for any context)        │       │
│  └──────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────┘
```

**Mode switching:**
- **Source mode:** QPlainTextEdit renders normally. QSyntaxHighlighter active.
  Overlay disabled.
- **Live preview:** Overlay active. Blocks near cursor show raw text (highlighting).
  Blocks far from cursor show rendered output (overlay painter).
- **Reading mode:** Separate `MarkoffReadingView` widget. Uses same
  `MarkoffRenderer` to paint the AST. No editing.

**Data flow:**
```
User types → QPlainTextEdit updates QTextDocument → contentsChanged signal
→ Re-parse affected blocks (MD4C) → Update AST → Invalidate block render cache
→ Re-compute block heights → Request viewport repaint
→ Overlay painter renders affected blocks
```

**Strengths:**
- No bidirectional sync — markdown IS the document, always
- QPlainTextEdit handles all text editing (cursor, selection, undo, clipboard,
  IME, accessibility, find/replace, drag-drop, multiple cursors)
- Source mode is free (just disable overlay)
- High performance (QPlainTextEdit uses line-by-line scrolling)
- Incremental by nature (only dirty blocks re-parsed and re-rendered)
- Matches Obsidian's conceptual architecture (CodeMirror = plain text + decorations)

**Weaknesses:**
- Block height management: rendered blocks (images, diagrams, tables) need more
  vertical space than their raw text. Requires custom `QPlainTextDocumentLayout`
  subclass. **This is the hardest engineering problem in this option.**
- Overlay precision: rendered content must align exactly with the scroll position
  and block positions of the underlying QPlainTextEdit
- Table rendering: a markdown table is N lines of pipe-delimited text but renders
  as a grid. The geometry mismatch is awkward.
- Copy/paste: selecting rendered content should copy markdown, not rendered text.
  Requires intercepting clipboard operations.

**Risk level:** Medium. The block height problem is bounded and has known
solutions (custom layout, margin tricks, placeholder lines). Everything else
is straightforward.

---

### Option B: QTextEdit + AST Synchronization ("Rich text is the document")

**Core idea:** The rendered view is the primary editing surface, backed by
`QTextEdit` and its `QTextDocument`. Markdown is parsed into the
`QTextDocument`'s rich text model on load. Edits happen in the rich text view.
A custom serializer converts the `QTextDocument` back to markdown on save and
on every change (for incremental sync).

**Parser:** MD4C or cmark → custom AST → QTextDocument population

**Component architecture:**

```
┌─────────────────────────────────────────────────────┐
│                    MarkoffEditor                     │
│  ┌──────────────────────────────────────────┐       │
│  │           QTextEdit                       │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  QTextDocument (rich text)       │    │       │
│  │  │  + QTextObjectInterface          │    │       │
│  │  │  + custom block formats          │    │       │
│  │  └──────────────────────────────────┘    │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  QAbstractTextDocumentLayout     │    │       │
│  │  │  (custom layout for callouts,    │    │       │
│  │  │   code blocks, etc.)             │    │       │
│  │  └──────────────────────────────────┘    │       │
│  └──────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────┐       │
│  │     Markdown ↔ QTextDocument Sync         │       │
│  │  ┌─────────────┐  ┌──────────────────┐   │       │
│  │  │ MD → QTD    │  │ QTD → MD          │   │       │
│  │  │ (populate)  │  │ (serialize)       │   │       │
│  │  └─────────────┘  └──────────────────┘   │       │
│  └──────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────┘
```

**Mode switching:**
- **Reading mode:** Same QTextEdit, set read-only
- **Editing mode:** QTextEdit writable, full rich text editing
- **Source mode:** Switch to QPlainTextEdit showing raw markdown
- **Live preview:** Same as editing mode (always WYSIWYG), with
  cursor-position-aware syntax reveal

**Data flow:**
```
User types → QTextEdit updates QTextDocument → contentsChanged signal
→ Serialize changed blocks to markdown → Update internal markdown buffer
→ (Optional: re-parse to validate round-trip fidelity)
```

**Strengths:**
- True WYSIWYG — the user always sees rendered content
- QTextEdit handles rich text editing naturally (format toggling, block
  splitting, etc.)
- QTextDocument supports images, tables, and inline objects natively
- No overlay precision issues — the rendered content IS the editing surface
- QAbstractTextDocumentLayout gives full control over block rendering

**Weaknesses:**
- **Bidirectional synchronization is the central difficulty.** Converting
  markdown → QTextDocument is well-understood. Converting QTextDocument →
  markdown (preserving the exact original markdown) is extremely hard.
  - Indentation whitespace gets lost
  - List markers (`-` vs `*` vs `+`) are normalized
  - Emphasis delimiters (`_` vs `*`) lose their original form
  - Reference links become inline links
  - Blank line patterns change
  - Frontmatter can be corrupted
- **Round-trip fidelity failures cause data loss.** If the user opens a file,
  makes a small edit, and saves, the ENTIRE file gets reserialized through the
  QTextDocument → Markdown path. Any fidelity bug silently corrupts the file.
- `QTextEdit` fighting: it has default behaviors for rich text (auto-formatting,
  paste as rich text, etc.) that fight markdown semantics
- Custom block rendering via `QAbstractTextDocumentLayout` is complex, poorly
  documented, and has limited examples in the wild
- Performance: `QTextEdit` uses pixel-exact layout for the entire document,
  slower than `QPlainTextEdit` on large files

**Risk level:** High. Round-trip fidelity is an unsolved problem in the markdown
editor space. Typora, which has years of engineering behind it, still has
round-trip bugs. Every markdown ↔ rich text editor project eventually hits
this wall.

---

### Option C: Hybrid Widget ("Both live, composited")

**Core idea:** Build a custom widget (on `QAbstractScrollArea`) that manages
both a raw text model and a rendered model. The widget renders block-by-block,
choosing between raw and rendered presentation based on cursor proximity. The
custom widget handles text input, cursor, and selection itself, but delegates
line-level text layout to `QTextLayout`.

This is a middle ground between Option A (which reuses QPlainTextEdit
wholesale) and building from scratch.

**Parser:** MD4C → custom AST builder

**Component architecture:**

```
┌─────────────────────────────────────────────────────┐
│              MarkoffEditor (QAbstractScrollArea)      │
│  ┌──────────────────────────────────────────┐       │
│  │           MarkoffDocument                 │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  Raw markdown string             │    │       │
│  │  │  + AST with source positions     │    │       │
│  │  │  + Block array (laid out blocks) │    │       │
│  │  └──────────────────────────────────┘    │       │
│  └──────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────┐       │
│  │           MarkoffViewport                 │       │
│  │  For each visible block:                  │       │
│  │  ┌──────────────────────────────────┐    │       │
│  │  │  If cursor in block:              │    │       │
│  │  │    → QTextLayout (raw text lines) │    │       │
│  │  │  Else:                            │    │       │
│  │  │    → Renderer (AST → QPainter)   │    │       │
│  │  └──────────────────────────────────┘    │       │
│  └──────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────┐       │
│  │           Input Handler                   │       │
│  │  Key events → modify raw markdown         │       │
│  │  → re-parse affected blocks → re-layout   │       │
│  └──────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────┘
```

**Strengths:**
- Maximum control over rendering and interaction
- No block height hacks — the custom widget owns the layout entirely
- No overlay alignment issues — there's only one rendering pass
- Natural live preview — just a per-block rendering decision
- Source of truth is always markdown (no sync problem)

**Weaknesses:**
- Must implement text input handling (including IME, compose sequences,
  dead keys, input methods for CJK/Arabic/etc.)
- Must implement cursor rendering and navigation
- Must implement selection (mouse drag, shift-click, shift-arrow, word/line
  selection, rectangular selection)
- Must implement clipboard (copy, cut, paste format detection)
- Must implement undo/redo with semantic grouping
- Must implement scrolling (smooth, snap-to-line, page up/down)
- Must implement accessibility (screen reader, high contrast)
- Must implement find and replace
- Must implement drag and drop
- Must implement multiple cursors (Obsidian supports this)

Each of these is substantial. Together they represent a multi-month effort
even for an experienced Qt developer.

**Risk level:** Very High. The scope is enormous. Every one of those "must
implement" items has edge cases that take weeks to get right (try implementing
correct double-click word selection with Unicode word boundaries, or correct
IME handling for Japanese input).

---

## Tradeoff Matrix

| Criterion | Option A (QPlainTextEdit+Overlay) | Option B (QTextEdit+Sync) | Option C (Custom Widget) |
|-----------|----------------------------------|--------------------------|-------------------------|
| **Time to MVP** | ~2-3 months | ~2-3 months | ~6-12 months |
| **Time to polished** | ~6 months | ~12+ months (round-trip bugs) | ~18+ months |
| **Live Preview quality** | Good (overlay) | Natural (always WYSIWYG) | Excellent |
| **Source mode** | Free | Needs separate widget | Must implement |
| **Round-trip fidelity** | Perfect (markdown is the document) | Fragile (serialization) | Perfect |
| **Text editing** | Free (QPlainTextEdit) | Free (QTextEdit) | Must implement |
| **Custom rendering** | Medium (overlay) | Hard (QAbstractTextDocumentLayout) | Full control |
| **Large file perf** | Excellent | Poor | Depends on implementation |
| **Block height mgmt** | Hard | N/A | N/A |
| **Accessibility** | Free (QPlainTextEdit) | Free (QTextEdit) | Must implement |
| **Maintenance burden** | Low | High (sync bugs) | Very High |
| **Upgrade path** | → Option C if needed | Dead end | Final form |

---

## Parser Strategy Options

Orthogonal to the editor architecture, we need a parsing strategy:

### Strategy 1: MD4C + Custom AST Builder

- MD4C for CommonMark/GFM parsing (SAX callbacks)
- Our `MarkoffDocumentBuilder` constructs a variant-based AST
- Obsidian extensions handled in the builder (two-layer parsing)
- Wikilinks and math handled by MD4C flags for free

**Pros:** Proven pattern (Penelope). Fastest parser. Smallest dependency.
Built-in wikilinks and math.

**Cons:** No AST from parser — we build everything. No plugin API for
clean extension separation.

### Strategy 2: cmark + Post-Processing

- cmark for CommonMark parsing (AST output)
- Walk AST and transform for Obsidian extensions
- Callouts: inspect blockquote first lines
- Math: scan for `$...$` in text nodes (or add as extension)
- Wikilinks: must handle ourselves

**Pros:** Full AST from parser. Best CommonMark compliance. Active maintenance.

**Cons:** No GFM support (tables, task lists) without cmark-gfm. No wikilinks
or math without extensions. Post-processing AST for callouts/embeds is fragile.

### Strategy 3: cmark-gfm Fork + Custom Extensions

- Fork cmark-gfm, rebase extension API onto cmark 0.31.x
- Write custom extensions for Obsidian syntax using the plugin API
- Each extension produces custom AST node types

**Pros:** Cleanest architecture. Extensions participate in parsing loop.
Custom node types in the AST.

**Cons:** Must maintain a fork. Extension API is complex and under-documented.
Rebasing onto new cmark versions is nontrivial.

### Strategy 4: md4qt

- Use md4qt (KDE library) as the primary parser
- Qt6-native types, AST output, LaTeX math support
- Add Obsidian extensions via contribution or fork

**Pros:** Most Qt-native. KDE ecosystem alignment. AST + Qt types.

**Cons:** Small community. No Obsidian extensions. Risk of abandonment.

### Parser Recommendation

**Strategy 1 (MD4C + Custom AST Builder)** for phase 1, with the door open
to Strategy 3 (cmark-gfm fork) for the long term.

Rationale:
- MD4C is proven in our ecosystem (Penelope's ContentBuilder is the exact
  pattern we need)
- Built-in wikilinks and math reduce initial scope significantly
- The custom AST builder is something we need regardless of parser choice —
  no parser produces Obsidian-aware AST nodes natively
- If we later need AST manipulation features (e.g., for programmatic document
  transformation), we can migrate to cmark without changing the builder's
  output format

---

## Rendering Strategy Options

### Strategy 1: AST → QPainter (Direct)

Walk the AST and paint directly to QPainter for each block. No intermediate
QTextDocument.

**Pros:** Maximum control. No QTextDocument limitations. Can render anything.
**Cons:** Must handle line breaking, text layout, font metrics ourselves.
Can use `QTextLayout` for line-level layout within blocks.

### Strategy 2: AST → QTextDocument → QPainter

Build a QTextDocument from the AST, then paint it with
`QTextDocument::drawContents()`.

**Pros:** Qt handles line breaking, text layout, inline objects. Familiar API.
Compatible with existing RenderedDocument interface (canvas cards).
**Cons:** QTextDocument limitations (no callout rendering, limited styling).
Must use QTextObjectInterface for custom elements.

### Strategy 3: Hybrid

Use QTextDocument for simple blocks (paragraphs, headings, lists) and direct
QPainter for complex blocks (callouts, code blocks, math, diagrams, embeds).

**Pros:** Best of both — leverage Qt for the common case, custom rendering
for the special cases.
**Cons:** Two rendering paths to maintain. Complexity at the boundary.

### Rendering Recommendation

**Strategy 3 (Hybrid)** is the pragmatic choice. QTextDocument handles 80%
of markdown blocks well (paragraphs, headings, basic lists, inline formatting).
Custom QPainter rendering handles the Obsidian-specific blocks that QTextDocument
cannot express.

This also maintains backward compatibility with the existing
`MarkdownRenderEngine` / `RenderedDocument` interface — the canvas card path
can continue to use `QTextDocument` via `RenderedDocument::toQTextDocument()`,
while the editor overlay uses direct `QPainter`.

---

## Recommended Architecture

**UPDATE:** The recommended architecture has changed from Option A to
**Option C (Custom Widget via Qt Source Harvest)** with **MD4C + Custom AST
Builder** parsing and **Hybrid Rendering**. See `06-qt-source-harvest.md` for
the full rationale. The phase structure below remains valid — only the editor
widget approach changes (forked Qt internals instead of QPlainTextEdit overlay).

### Phase 1: Foundation (Parsing + Document Model + Reading Mode)

Build the core without touching the editor:

1. **MarkoffDocument** — The AST. Variant-based tree with source positions,
   Obsidian node types, block metadata. Built by `MarkoffDocumentBuilder`
   from MD4C callbacks.
2. **MarkoffRenderer** — AST → QPainter rendering with support for all block
   types. Hybrid strategy (QTextDocument for simple blocks, direct paint for
   complex ones).
3. **MarkoffReadingView** — `QAbstractScrollArea` subclass that renders a
   `MarkoffDocument` in reading mode. Replaces current `NotePreviewWidget` /
   `QTextBrowser` path.
4. **MarkdownRenderEngine implementation** — Implement the existing
   `MarkdownRenderEngine` interface using the new parser and renderer.
   Canvas cards work immediately.
5. **Test suite** — Unit tests for parsing (each Obsidian extension), rendering
   (golden file tests comparing output), and the engine interface.

**Deliverable:** Reading mode and canvas cards use the new engine. The existing
`qmarkdowntextedit` editor continues to work unchanged for editing.

### Phase 2: Editor Integration (Live Preview)

Build the editor overlay:

6. **MarkoffEditorWidget** — Subclass of `QPlainTextEdit` with:
   - Custom `QPlainTextDocumentLayout` for block height management
   - Overlay painter that renders AST blocks over the plain text
   - Cursor-aware mode switching (raw vs. rendered per block)
   - Markdown-aware editing commands (ToggleBold, InsertLink, etc.)
7. **Source mode** — Overlay disabled, `QSyntaxHighlighter` active with
   KSyntaxHighlighting or custom highlighter.
8. **Mode system** — Source / Live Preview / Reading mode switching with
   scroll position preservation.

**Deliverable:** Full three-mode editing replaces `qmarkdowntextedit`.

### Phase 3: Advanced Features

9. **tree-sitter integration** — Incremental parsing for the editor, replacing
   the full-document MD4C re-parse on every edit.
10. **Multiple cursors** — Obsidian supports this.
11. **Vim mode** — Obsidian supports this.
12. **Print/PDF export** — Using the renderer pipeline with pagination.
13. **Themes** — Configurable colors, fonts, spacing via style manager.

---

## Risk Mitigation

### Block Height Management (Primary Risk)

The main technical risk in Option A is making QPlainTextEdit allocate variable
block heights.

**Mitigation:** Build a minimal prototype FIRST. Before committing to the full
architecture, build a `QPlainTextEdit` subclass that:
1. Assigns varying heights to blocks based on custom data
2. Paints custom content in the extra space
3. Handles scrolling correctly

If this prototype works, Option A is viable. If it doesn't (QPlainTextEdit's
internals fight too hard), fall back to Option C with the same AST/renderer
— only the editor widget changes.

**Time to prototype:** ~1 week.

### Round-Trip Fidelity (If We Ever Consider Option B)

If we ever need to pivot to Option B (QTextEdit-based WYSIWYG), the round-trip
fidelity risk can be mitigated by:
- Never serializing the full document through the QTextDocument → markdown path
- Instead, tracking edits as diffs against the original markdown
- Using the AST's source positions to patch edits into the original text
- This preserves all formatting that the user didn't touch

This is complex but avoids the "silent corruption on save" problem.

### Parser Migration (If MD4C Proves Insufficient)

If MD4C's lack of AST or extension API becomes painful, the migration to
cmark-gfm is bounded:
- The `MarkoffDocumentBuilder` interface stays the same — it receives events
  and builds the same AST
- Only the callback wiring changes (MD4C callbacks → cmark AST traversal)
- The renderer and editor are unaffected

---

## Open Questions for Design Phase

These questions should be resolved during brainstorming before implementation:

1. **Block granularity:** Is the "block" always a markdown block (paragraph,
   heading, code block)? Or can it be a group of blocks (e.g., a list is one
   block vs. each list item is a block)?

2. **Cursor behavior in rendered mode:** When the user clicks on a rendered
   heading, should the cursor go to the beginning of the heading text, or to
   the `#` characters? How about clicking on a rendered image?

3. **Inline rendering granularity:** Should inline elements (math, images)
   render immediately (even when cursor is in the same line), or only when
   cursor is in a different block? Obsidian renders inline elements immediately
   unless the cursor is directly adjacent.

4. **Table editing:** In live preview, should tables show as rendered grids
   with cell editing, or as raw pipe-delimited text? Obsidian shows raw text
   when cursor is in the table.

5. **Copy behavior:** Should Ctrl+C from rendered mode copy markdown or
   rendered text? Obsidian copies markdown.

6. **Accessibility priority:** How important is screen reader support for
   the live preview mode specifically? Source mode and reading mode are
   accessible by default (QPlainTextEdit and custom widget respectively).

7. **Canvas card backward compatibility:** Should the new renderer produce
   `RenderedDocument` objects (wrapping QTextDocument) for canvas cards, or
   should canvas cards switch to direct QPainter rendering?

---

## Summary

| Decision | Recommendation | Rationale |
|----------|---------------|-----------|
| **Editor architecture** | Option A: QPlainTextEdit + Overlay | Best effort/reward ratio. Free text editing. Perfect fidelity. |
| **Parser** | MD4C + custom AST builder | Proven pattern. Built-in wikilinks/math. Fastest. |
| **Document model** | Variant-based AST with source positions | Penelope's Content model adapted for Obsidian |
| **Rendering** | Hybrid (QTextDocument for simple, QPainter for complex) | Pragmatic. Compatible with canvas interface. |
| **First phase** | Reading mode + canvas rendering (no editor) | Validates parser and renderer without editor risk |
| **Primary risk** | Block height management in QPlainTextEdit | Prototype early to validate |
