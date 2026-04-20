# Markoff: Problem Definition

## What Is Markoff?

Markoff is Corbomite's bespoke markdown editing and rendering library — a unified
Qt6/C++ engine that handles parsing, layout, and interactive editing of
Obsidian-flavored markdown. It replaces the current patchwork of regex-based
renderers, third-party editor widgets, and ad-hoc canvas rendering code with a
single, coherent system.

The name "markoff" is a working title for the library target `Corbomite::Markoff`.

---

## Why We Need It

### The Current Patchwork

Corbomite currently has **four separate systems** touching markdown:

| System | What It Does | How | Problems |
|--------|-------------|-----|----------|
| `MarkdownRenderer` (libs/core) | Reading mode HTML | 1000+ lines of regex → HTML string → `QTextBrowser` | Fragile regexes. Nested emphasis fails. Reference links unsupported. Complex list nesting breaks. Tables lack alignment. Setext headings missing. |
| `qmarkdowntextedit` (git submodule) | Source-mode editor | Forked `QPlainTextEdit` with syntax highlighting | No WYSIWYG. No live preview. Not ours — upstream changes are painful to merge. Obsidian extensions bolted on. |
| `TextCardItem::paint()` (libs/canvas) | Canvas text card rendering | Separate, even more minimal regex hack → `QTextDocument` → `QPainter` | Duplicated logic. No feature parity with reading mode. |
| `MarkdownHighlighter` (qmarkdowntextedit) | Editor syntax coloring | `QSyntaxHighlighter` subclass with Obsidian extension states | Coupled to the editor widget. Cannot be reused. |

Each of these was adequate to bootstrap the application. None of them is adequate
for the long term. Every time we add an Obsidian feature (callouts, math,
mermaid, embeds), we touch two or three of these systems with different, diverging
implementations.

### What Obsidian Actually Requires

Obsidian is not a "markdown viewer." It is a **markdown-native IDE** with three
tightly coupled display modes that share a unified experience:

1. **Source mode** — raw markdown with syntax highlighting
2. **Live Preview** — WYSIWYG hybrid where markdown syntax hides when the cursor
   leaves a region and reappears when the cursor enters it
3. **Reading mode** — fully rendered, non-editable view

All three modes operate on the **same document model** and must support the
**same feature set**. In Obsidian, switching between modes is instantaneous and
preserves scroll position and cursor location. This is the bar we must clear.

### The Obsidian Feature Surface

Beyond standard CommonMark + GitHub Flavored Markdown, Obsidian defines a
substantial extension surface:

**Linking & Embedding:**
- Wikilinks: `[[Note]]`, `[[Note|Display]]`, `[[Note#Heading]]`, `[[Note#^block-id]]`
- Embeds: `![[note]]`, `![[image.png]]`, `![[note#heading]]`, `![[image.png|300]]`
- Block references: `^block-id` (defining), `[[Note#^block-id]]` (referencing)

**Rich Formatting:**
- Callouts: `> [!type]` with 13+ types, foldable (`+`/`-`), nested, custom titles
- Highlights: `==text==`
- Comments: `%%hidden text%%`
- Math: `$inline$`, `$$display$$` (LaTeX)
- Mermaid diagrams in fenced code blocks
- Task lists with custom states: `[/]`, `[-]`, `[?]`, `[!]`, `[>]`

**Metadata:**
- YAML frontmatter (properties): `tags`, `aliases`, `cssclasses`, custom fields
- Tags: `#tag`, `#nested/tag` (hierarchical)
- Footnotes: `[^1]` and inline `^[content]`

**Editing UX:**
- Folding (headings, indented content)
- Multiple cursors
- Auto-pairing of brackets, quotes, markdown delimiters (`$`, `=`, `~`, `%%`)
- Vim mode
- Drag-and-drop for files, images, links
- Suggestion/autocomplete system triggered by `[[` (links), `#` (tags),
  `/` (slash commands) — with fuzzy matching against the metadata cache
- Smart paste: HTML → markdown conversion, image save + embed, URL wrapping
- Inline footnotes `^[text]` (note: Obsidian only supports these in Reading
  View, not in the editor — we can do better)

Each of these must work correctly in all three modes, with consistent behavior
and appearance.

---

## The Core Problems

### Problem 1: No AST

The current `MarkdownRenderer` converts markdown to HTML using regular
expressions. There is no intermediate representation — no abstract syntax tree,
no document model. This means:

- **No structural awareness.** The renderer cannot answer "what heading is the
  cursor inside?" or "what are the children of this list item?"
- **No incremental updates.** Any change requires re-rendering the entire document.
- **No round-trip fidelity.** Markdown → HTML is lossy; there is no HTML → Markdown path.
- **No cursor mapping.** We cannot map a cursor position in the rendered output
  back to a byte offset in the source markdown.
- **Extension fragility.** Each new Obsidian extension is another regex bolted onto
  a 1000-line function.

An AST is the foundation everything else depends on. Without it, live preview,
embeds, block references, and outline/backlink integration are all ad-hoc hacks.

### Problem 2: No Unified Document Model

The editor widget owns a `QTextDocument`. The reading mode owns a different
`QTextDocument` (or just HTML in a `QTextBrowser`). The canvas owns yet another
`QTextDocument` per card. These are unrelated objects with no shared structure.

What we need is a **single document model** that:
- Is constructed from the AST
- Can be rendered in any context (editor, reader, canvas card, hover preview)
- Tracks source positions (for cursor mapping and incremental updates)
- Supports change notifications (for live preview and backlink updates)

### Problem 3: No WYSIWYG / Live Preview

Obsidian's defining UX is Live Preview: a hybrid mode where rendered and raw
markdown coexist in the same view, with the cursor determining which you see.
This requires:

- **Per-block rendering decisions.** Blocks near the cursor show raw markdown;
  blocks far from the cursor show rendered output.
- **Cursor-aware transitions.** Moving the cursor into a rendered block must
  smoothly transition it to raw mode (and vice versa).
- **Inline widget embedding.** Math, images, callouts, and code blocks must
  render as inline "widgets" within the text flow, not as separate elements.
- **Source position tracking.** The cursor in the rendered view must map back to
  the correct byte offset in the markdown source.

None of this is possible with `QTextBrowser` (read-only) or stock `QPlainTextEdit`
(no rich content). It requires a custom editing surface.

### Problem 4: No Custom Rendering Pipeline

`QTextDocument` and `QTextBrowser` provide basic rich text rendering, but they
cannot handle:

- **Callout boxes** with icons, color coding, and fold toggles
- **Code blocks** with language-labeled headers and copy buttons
- **LaTeX math** rendered inline at arbitrary positions
- **Mermaid diagrams** rendered as SVG within the text flow
- **Embedded notes** rendered as nested document fragments
- **Wikilinks** with vault-aware resolution and unresolved-link styling
- **Canvas cards** where markdown renders inside a bounded, scrollable rectangle

The rendering pipeline needs to be extensible: given an AST node type, dispatch
to a renderer that knows how to paint it into a `QPainter`, position it in the
text flow, and handle interaction events.

### Problem 5: Canvas Integration

Canvas file cards need to render arbitrary vault notes (or subpath sections)
inside `QGraphicsScene`. This requires the render engine to:

- Accept a `RenderProfile` that configures compact rendering (small font, tight
  margins, no images)
- Extract subpath sections (headings or block IDs) from markdown
- Produce output that can be painted by `QGraphicsItem::paint()`
- Support re-rendering on resize

The existing render engine spec (2026-03-31) defines this interface. Markoff
must implement it.

---

## What Markoff Must Be

Markoff is not just a renderer. It is the **core text engine** for Corbomite.
It must provide:

1. **Parsing** — Markdown source → AST with full Obsidian extension support
2. **Document Model** — Structured, observable, source-mapped representation
3. **Layout** — Text flow, line breaking, inline objects, block positioning
4. **Rendering** — Paint to `QPainter` for any context (editor, reader, canvas)
5. **Editing** — Cursor management, selection, input handling, undo/redo
6. **Mode Switching** — Source mode, live preview, reading mode on one document

These six concerns are the sub-problems that the subsequent research documents
will explore approaches for.

---

## Constraints

### Must Have
- GPLv3 compatible (all dependencies)
- Qt6 / KDE Frameworks 6 native
- CommonMark + GFM compliant parsing
- Full Obsidian extension support (see feature surface above)
- Three editing modes (source, live preview, reading)
- Canvas card rendering via the existing `MarkdownRenderEngine` interface
- Incremental re-rendering (keystroke-level performance on large documents)
- Source position tracking (AST ↔ rendered view bidirectional mapping)
- Undo/redo with semantic grouping

### Should Have
- Reuse proven algorithms from Penelope (Knuth-Plass line breaking, HarfBuzz
  text shaping) where they add value
- Reuse architectural patterns from Calligra (KoTextEditor wrapper,
  QTextBlockUserData for block metadata, incremental layout) where applicable
- Extensible block/inline renderer dispatch for future node types
- Print/PDF export path (can come later but architecture should not preclude it)

### Must Not
- Depend on Electron, WebKit, or any web engine
- Require QWebEngineView for any core functionality
- Break the existing `MarkdownRenderEngine` / `RenderedDocument` interface
  (backward compatibility with canvas integration)
- Introduce external process dependencies for basic rendering (mmdr for mermaid
  is acceptable; requiring a running Node.js server is not)

### Open Questions
- Should the editor surface be built on `QTextEdit`/`QPlainTextEdit`, on
  `QAbstractScrollArea` with custom painting, or on `QGraphicsView`?
- Should we use an existing C parser (cmark, MD4C, md4qt) or write our own?
- How much of Penelope's layout engine is worth porting vs. rebuilding?
- Should Live Preview use "decorations" (CodeMirror-style: hide syntax, overlay
  widgets) or "dual documents" (maintain both raw and rendered, switch per-block)?
- Where does the boundary between Markoff and Corbomite's application layer sit?
  (Vault resolution, file I/O, link index — these are app concerns, not library
  concerns, but the library must provide hooks for them.)

---

## Document Roadmap

This problem definition is the first in a series of research documents:

1. **01-problem-definition.md** — This document. What we're solving and why.
2. **02-parser-survey.md** — Evaluation of C/C++ markdown parsers and Obsidian
   extension strategies.
3. **03-editor-architecture-survey.md** — How WYSIWYG/hybrid markdown editors
   work, and what Qt6 provides for building one.
4. **04-reference-codebase-analysis.md** — Techniques and patterns from Penelope
   and Calligra that we can reuse.
5. **05-options-and-tradeoffs.md** — Synthesis: the viable architectural
   approaches for Markoff, with trade-off analysis.
