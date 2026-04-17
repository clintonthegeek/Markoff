# Markoff Architecture

Two Qt6/C++ libraries for Obsidian-flavored markdown:

**MarkoffParser** (`libs/markoff-parser/`) — standalone parser wrapping
tree-sitter-markdown with Obsidian extensions. Depends only on Qt6::Core
and tree-sitter. Provides a typed query API for headings, links, wikilinks,
tags, footnotes, and word count.

**MarkoffEditor** (`libs/markoff/`) — live-preview editor widget. Depends
on MarkoffParser plus Qt6::Widgets, KF6SyntaxHighlighting, and JKQTMathText.
Always operates in live preview mode. Read-only display is the same widget
with editing disabled via `setReadOnly(true)`.

Neither library depends on Corbomite. Application-level features (vault
navigation, completion popups, session management) remain in the host.

---

## Design Principles

1. **Library-first.** Markoff exports a clean widget API. Consumers create
   an `Editor`, call `setPlainText()`, connect signals. No vault paths,
   no note models, no file I/O.

2. **Markdown is the document.** The source of truth is always flat markdown
   text. Scene items, formatted documents, and rendered images are derived
   views that can be rebuilt at any time.

3. **GPL harvest.** We fork Qt's GPL source directly when we need control
   beyond the public API. The TextControl fork gives us full ownership of
   the text editing state machine without private header dependencies.

4. **Heterogeneous blocks.** A markdown document contains text, tables,
   code blocks, math, images, diagrams. These are fundamentally different
   visual objects. The architecture represents them as separate scene items,
   not as regions within a single QTextDocument.

---

## System Diagram

```
                         +---------------------------+
                         |       Host Application    |
                         |  (Corbomite, test-app)    |
                         +---------------------------+
                                     |
                         setPlainText, setTheme, signals
                                     |
                         +---------------------------+
                         |    Editor : QGraphicsView  |  <-- Public API
                         +---------------------------+
                                     |
                    +----------------+----------------+
                    |                                 |
          +---------+----------+          +----------+---------+
          |   SelectionScene   |          |  SceneCoordinator  |
          |  (QGraphicsScene)  |          |     (QObject)      |
          +---------+----------+          +----------+---------+
                    |                                 |
          +---------+----------+        +-------------+----------+
          | SelectionManager   |        |  TreeSitterParser      |
          |  cross-boundary    |        |  MarkdownSplitter      |
          |  mouse/keyboard    |        |  item create/destroy   |
          +--------------------+        |  repositioning         |
                                        +-----------+------------+
                                                    |
                    +-------------------------------+-----+
                    |                 |                    |
          +---------+-------+  +-----+--------+  +-------+--------+
          | MarkdownTextItem|  | TableBlockItem|  | (future items) |
          | (SelectableItem)|  | (SelectableItem) | ImageItem, etc |
          +--------+--------+  +-----+--------+  +----------------+
                   |                  |
          +--------+--------+        |
          |   TextControl   |   custom QPainter
          |  (Qt fork)      |   rendering
          +--------+--------+
                   |
          +--------+--------+
          |  QTextDocument  |
          |  + Highlighter  |
          |  + MathObject   |
          +--------+--------+
                   |
          +--------+--------+
          |  MathRenderer   |
          |  (JKQTMathText) |
          +-----------------+
```

---

## Layers

### Layer 1: Public API

**MarkoffParser** — five headers in `include/markoff-parser/`:

| Header | Type | Purpose |
|--------|------|---------|
| `Document.h` | Model | Parsed markdown with query API |
| `TreeSitterParser.h` | Parser | tree-sitter wrapper, span map, block boundaries |
| `SourceSpan.h` | Data | Formatting span struct, UTF-8 offset utilities |
| `MarkdownSplitter.h` | Utility | Split markdown at block boundaries |
| `TableHandler.h` | Utility | Pipe table parsing and serialization |

**MarkoffEditor** — seven headers in `include/markoff/`:

| Header | Type | Purpose |
|--------|------|---------|
| `Editor.h` | Widget | Main editor widget (QGraphicsView) |
| `Theme.h` | Config | Colors, fonts, element formats |
| `EditorSettings.h` | Config | Tab size, line numbers, wrapping |
| `ResourceProvider.h` | Interface | Image/link/embed resolution |
| `SearchBar.h` | Widget | Embedded find/replace bar |
| `LinkRenderer.h` | Signal hub | Typed link/tag activation and hover emission |
| `FoldingTypes.h` | Data | Heading path computation and fold region keys |

The editor API hides all scene internals. Consumers never see
SelectionScene, SceneCoordinator, MarkdownTextItem, or any other
implementation class. Read-only mode is `Editor::setReadOnly(true)` —
no separate reading view widget.

### Layer 2: Scene Architecture

**Editor** (QGraphicsView) owns a SelectionScene and a SceneCoordinator.

**SelectionScene** (QGraphicsScene) contains the ordered list of scene items
and owns the SelectionManager. It intercepts mouse/keyboard events and
delegates cross-boundary selection logic to the SelectionManager.

**SceneCoordinator** (QObject) orchestrates the scene:
- Parses markdown with TreeSitterParser
- Splits at block boundaries with MarkdownSplitter
- Creates/destroys MarkdownTextItem and BlockItem instances
- Positions items vertically with spacing
- Debounces reparse on text changes (150ms timer)
- Propagates theme, font, and width changes to all items

**SelectionManager** (QObject) handles selection across item boundaries:
- Three modes: None, WithinItem, CrossBoundary
- Uses `ungrabMouse()` to break Qt's implicit grab on boundary crossing
- Applies selection via the SelectableItem interface
- Serializes selected markdown for clipboard

### Layer 3: Scene Items

All scene items implement **SelectableItem** — a pure interface for
cross-boundary selection. Text items provide `hitTest`, `setSelection`,
`selectedMarkdown`. Non-text items provide `setFullySelected`, `toMarkdown`.

**MarkdownTextItem** (QGraphicsObject + SelectableItem):
- Wraps TextControl (forked QWidgetTextControl) + QTextDocument
- Each item has its own MarkdownHighlighter instance
- Registers MathTextObject for inline LaTeX glyph rendering
- Detects and paints DecoratedRanges (code block backgrounds, callout
  borders, blockquote indicators)
- Emits `cursorAtBoundary(Qt::Edge)` for focus transfer between items

**BlockItem** (QGraphicsObject + SelectableItem) base class:
- Paints selection overlay when fully selected
- Subclasses: TableBlockItem (read-only table rendering)

### Layer 4: Text Editing

**TextControl** — forked from Qt's QWidgetTextControl. Provides the
complete text editing state machine: cursor, selection, undo/redo,
clipboard, IME, drag-and-drop. Operates on a standard QTextDocument.

The fork replaced Qt's pimpl macros with plain nested structs, broke
the QWidgetPrivate inheritance chain, and uses only QAbstractScrollArea's
public API. No private Qt headers required.

### Layer 5: Parsing

**TreeSitterParser** — wraps the tree-sitter C API with vendored
tree-sitter-markdown grammars (with wiki-link and tag extensions). Produces:
- Flat span map (QList<SourceSpan>) for the highlighter
- Block boundaries for the splitter (tables, fenced code blocks)
- UTF-8/char offset mapping

**DocumentBuilder** — MD4C-based SAX parser that builds the Document AST.
Currently powers the reading view (Renderer) and Document query API.
*Planned for removal* once Renderer migrates to tree-sitter.

**MarkdownSplitter** — uses TreeSitterParser to find block boundaries and
split markdown into segments (Text, Table, FencedCodeBlock). Each segment
becomes a scene item.

### Layer 6: Rendering

**MarkdownHighlighter** (QSyntaxHighlighter) — driven by the tree-sitter
span map. Applies Theme formats to text ranges. Handles:
- All inline formatting (bold, italic, code, math, links, etc.)
- Block-level state (code blocks, frontmatter, blockquotes)
- Cursor-aware delimiter visibility (hides `**`, `*`, etc. away from cursor)
- Code block syntax highlighting via KSyntaxHighlighting

**MathTextObject** (QTextObjectInterface) — registered with QTextDocument
to render inline math as custom glyphs (U+FFFC replacement character).

**CheckboxTextObject** (QTextObjectInterface) — registered alongside
MathTextObject to render task list checkboxes (`[ ]` / `[x]`) as
graphical icons. Click-to-toggle handled by
`MarkdownTextItem::mousePressEvent()`. Shares the `RawProperty` round-trip
mechanism with MathTextObject.

**MathRenderer** — stateless utility that renders LaTeX to QImage via
JKQTMathText. Process-wide thread-safe cache keyed by
(latex, displayMode, fontSize, devicePixelRatio). Images supersampled at
3x DPI.

**Renderer** — walks the Document AST and produces a QTextDocument with
HTML content for the ReadingView. Currently uses MD4C-based Document AST.

---

## Data Flow: Keystroke to Pixel

```
Keystroke
  |
  v
Editor::keyPressEvent
  |
  v
QGraphicsView -> SelectionScene -> MarkdownTextItem::keyPressEvent
  |
  v
TextControl::processEvent -> QTextDocument mutation
  |
  +---> MarkdownTextItem::onCursorPositionChanged
  |       |
  |       +---> snapCursorPastDelimiters (skip hidden syntax)
  |       +---> updateMathReveal (expand/collapse math regions)
  |       +---> MarkdownHighlighter::setCursorPosition
  |
  +---> SceneCoordinator::onItemTextChanged
          |
          +---> [150ms debounce]
          |
          v
        SceneCoordinator::reparse
          |
          +---> TreeSitterParser::parse (full AST rebuild)
          +---> MarkdownSplitter::split (find new block boundaries)
          +---> Rebuild scene if boundaries changed
          +---> MarkdownHighlighter::setSpanMap (new formatting data)
          +---> MarkdownTextItem::refreshMathSubstitution
                  |
                  +---> stripMathSubstitution (remove U+FFFC glyphs)
                  +---> applyMathSubstitution (insert fresh glyphs)
```

Between full reparses, `MarkdownHighlighter::adjustSpanOffsets()`
incrementally shifts span byte offsets as the user types, keeping
highlighting approximately correct without waiting for the debounce.

---

## Math Rendering Pipeline

Inline math (`$x^2$`) and display math (`$$\int_0^1 f(x) dx$$`) follow
the same pipeline:

1. **Detection**: TreeSitterParser marks spans with `math` or `mathDisplay`
2. **Substitution**: `applyMathSubstitution()` replaces each math source
   range with a single U+FFFC character carrying format properties:
   - `SourceProperty`: the LaTeX source (`x^2`)
   - `DisplayProperty`: inline vs display mode
   - `RawProperty`: original delimited form (`$x^2$`)
3. **Rendering**: Qt's layout engine calls `MathTextObject::intrinsicSize()`
   and `drawObject()` for each U+FFFC. These delegate to
   `MathRenderer::render()`, which compiles the LaTeX and returns a cached
   QImage.
4. **Cursor reveal**: When the cursor enters a math region,
   `updateMathReveal()` expands the U+FFFC back to raw LaTeX so the user
   can edit it. On cursor exit, it re-substitutes the glyph.

**Open question**: The reveal/collapse mechanism is the most complex
subsystem in the codebase (~300 lines with reentrancy guards and deferred
flag clearing). Should we explore a simpler model — e.g., always showing
source in the focused text item and only substituting in unfocused items?
This would trade per-glyph reveal for per-item reveal, dramatically
simplifying the state machine at the cost of slightly different UX from
Obsidian.

---

## Theme System

The Theme struct contains a `QHash<Element, QTextCharFormat>` with 57
element types covering all markdown constructs, plus base text and code
fonts.

Themes propagate through: `Editor::setTheme()` -> `SceneCoordinator::setTheme()`
-> each item's `MarkdownHighlighter::setTheme()` -> immediate rehighlight.

Factory methods: `Theme::defaultLight()`, `Theme::defaultDark()`,
`Theme::fromSchemeFile()` (QOwnNotes INI format).

**Open question**: Should the Theme system absorb the hardcoded visual
constants currently scattered across rendering code (table grid colors,
code block backgrounds, selection overlay, callout type colors)? This
would make themes fully control all visual output but adds ~20 new
Element values or a separate decoration color map.

**Open question**: Should `Theme::fromSchemeFile()` be extended to read
KDE color schemes (Breeze, etc.) for native desktop integration?

---

## Read-Only Mode

The editor has a single display mode: live preview. `setReadOnly(true)`
disables text editing while preserving the visual presentation.
`Qt::TextBrowserInteraction` flags are set on all text items, allowing
link clicking and text selection but not input.

Non-text block items (TableBlockItem) may still allow non-destructive
display adjustments in read-only mode — such as column width resizing —
that affect only visual presentation and do not modify the underlying
markdown.

---

## Current Block Item Types

| Type | Status | Capabilities |
|------|--------|-------------|
| MarkdownTextItem | Shipped | Full editing, highlighting, math, checkboxes, decorated ranges |
| TableBlockItem | Shipped (read-only) | Custom-painted table grid, column alignment |
| ImageBlockItem | Shipped (read-only) | Rendered image via ResourceProvider, missing-image fallback |

## Planned Block Item Types

| Type | Purpose | Key Questions |
|------|---------|---------------|
| Editable table | Cell editing, row/column ops | Per-cell QTextDocument + QTextLayout, or simpler model? Context menu vs hover handles? |
| CodeBlockItem | Syntax-highlighted code editing | Separate item or just a decorated MarkdownTextItem with enhanced rendering? |
| MermaidItem | SVG diagram rendering | External renderer dependency? Async rendering? |
| EmbedItem | Embedded note preview | Recursive rendering depth limit? |

**Design note**: Not all visual elements need to be separate BlockItem
types. Code blocks and display math work well as decorated regions within
MarkdownTextItem. The split-into-separate-item approach is necessary when
the visual representation has fundamentally different geometry from the
source text (tables, images). For code blocks, the source text IS the
visual text — just with different styling.

---

## Parsing

Tree-sitter-markdown is the sole parser. It lives in MarkoffParser and
serves both the editor (span map, block boundaries) and the Document
query API (headings, links, tags) via separate CST traversals:

- `TreeSitterParser::buildSpanMap()` — flat formatting ranges for the highlighter
- `TreeSitterParser::buildDocumentQueries()` — structured HeadingInfo/LinkInfo/TagInfo
- `TreeSitterParser::findBlockBoundaries()` — table/code block split points

**Open question**: tree-sitter supports incremental parsing via
`ts_tree_edit()`. The current implementation does a full reparse on every
debounced keystroke. At what document size does this become a bottleneck?
Should incremental parsing be prioritized, or is 150ms debounce + full
reparse fast enough for typical note sizes (< 10,000 lines)?

---

## Obsidian Grammar Extensions

The vendored tree-sitter-markdown grammar supports wiki links and tags
via compile-time flags. Several Obsidian-flavored constructs are NOT
yet in the grammar:

- `==highlighted text==` — handled by the vendored grammar's `highlight` node
- `%%comment text%%` — handled by the vendored grammar's `obsidian_comment` node
- `![[embed]]` — embed prefix on wikilinks
- `^block-id` — block references
- `> [!type]` — callout blocks (partially handled by DecoratedRange
  detection in MarkdownTextItem)

**Open question**: Fork the vendored grammar to add these nodes, or
handle them as a post-processing pass over the tree-sitter CST? Forking
gives clean AST nodes but requires maintaining a grammar fork and
regenerating with the tree-sitter CLI (Node.js toolchain). Post-processing
is simpler but repeats the pattern we're trying to eliminate.

---

## Cross-Boundary Selection

The SelectionManager handles selection spanning multiple scene items:

1. **Mouse press** records anchor item + char position
2. **Mouse drag within item** is handled natively by Qt (WithinItem mode)
3. **Mouse drag across boundary** breaks the grab, enters CrossBoundary mode
4. **applySelection()** walks ordered items, setting partial selection on
   edge items and full selection on middle items
5. **Ctrl+C** serializes selected markdown from each item in order
6. **Ctrl+A** selects all items
7. **Escape** clears cross-boundary selection

Keyboard selection (Shift+Arrow at item boundary) extends cross-boundary
selection by detecting `cursorAtBoundary` signals and programmatically
extending into adjacent items.

---

## What's Not Built Yet

Roughly ordered by dependency and value:

1. **Incremental tree-sitter parsing** — `ts_tree_edit()` for keystroke-level
   performance on large documents
2. **Incremental rehighlight** — only rehighlight blocks whose spans changed
3. **Theme-driven visual constants** — move hardcoded colors/layout values
   into Theme/EditorSettings
4. **Editable tables** — the biggest missing interactive feature
5. **Horizontal rules as graphical lines** — not just styled `---`
6. **Blockquote left border** — visual indicator beyond color
7. **Obsidian grammar fork** — native AST nodes for embeds, block refs
8. **Round-trip fidelity** — blank lines lost in serialization
9. **QAction-based shortcuts** — replace hardcoded keyPressEvent checks
10. **Highlighter test suite** — tst_highlighter.cpp
11. **TextControl test suite** — the 2000-line Qt fork has zero direct tests
12. **Performance benchmarks** — large document stress tests
13. **Accessibility** — screen reader, keyboard-only navigation

---

## Dependencies

**MarkoffParser:**

| Dependency | Version | Purpose |
|-----------|---------|---------|
| Qt6::Core | 6.8+ | QString, QList, QByteArray |
| tree-sitter | system | Parser framework (via pkg-config) |
| tree-sitter-markdown | vendored | Markdown + inline grammars (EXTENSION_WIKI_LINK, EXTENSION_TAGS) |

**MarkoffEditor** (in addition to MarkoffParser):

| Dependency | Version | Purpose |
|-----------|---------|---------|
| Qt6::Gui, Qt6::Widgets | 6.8+ | QGraphicsView, QPainter, etc. |
| KF6SyntaxHighlighting | KF6 | Code block syntax coloring |
| JKQTMathText | sibling lib | LaTeX math rendering |

---

## File Map

```
libs/markoff-parser/                    # MarkoffParser library
+-- include/markoff-parser/
|   +-- Document.h                      # Parsed document + query API
|   +-- TreeSitterParser.h              # tree-sitter wrapper
|   +-- SourceSpan.h                    # Span struct + UTF-8 utilities
|   +-- MarkdownSplitter.h             # Block boundary splitting
|   +-- TableHandler.h                  # Pipe table parsing
+-- src/
|   +-- Document.cpp                    # Tree-sitter-based document model
|   +-- TreeSitterParser.cpp            # CST building, span map, document queries
|   +-- SourceSpan.cpp                  # UTF-8 offset mapping
|   +-- MarkdownSplitter.cpp
|   +-- TableHandler.cpp
|   +-- vendor/tree-sitter-markdown/    # Vendored grammar
+-- tests/

libs/markoff/                           # MarkoffEditor library
+-- include/markoff/
|   +-- Editor.h                        # QGraphicsView widget
|   +-- Theme.h                         # Colors, fonts, element formats
|   +-- EditorSettings.h               # Editor behavior config
|   +-- ResourceProvider.h              # Image/link/embed resolution
|   +-- SearchBar.h                     # Embedded find/replace bar
|   +-- LinkRenderer.h                  # Typed link emission surface
|   +-- FoldingTypes.h                  # Heading path computation
+-- src/
|   +-- Editor.cpp
|   +-- SceneCoordinator.h/cpp          # Scene item management, reparse, coordinate mapping
|   +-- SelectionScene.h/cpp            # QGraphicsScene + SelectionManager
|   +-- SelectionManager.h/cpp          # Cross-boundary selection
|   +-- SelectableItem.h               # Item interface
|   +-- MarkdownTextItem.h/cpp          # Editable text region
|   +-- BlockItem.h/cpp                 # Non-text item base
|   +-- TableBlockItem.h/cpp            # Read-only table rendering
|   +-- ImageBlockItem.h/cpp            # Read-only image rendering
|   +-- StubBlockItem.h/cpp             # Testing placeholder
|   +-- TextControl.h/cpp              # Forked from Qt
|   +-- TextControl_p.h
|   +-- MarkdownHighlighter.h/cpp       # AST-driven syntax highlighting
|   +-- MathRenderer.h/cpp             # LaTeX rendering
|   +-- MathTextObject.h/cpp            # QTextObjectInterface for math
|   +-- CheckboxTextObject.h/cpp        # QTextObjectInterface for checkboxes
|   +-- DecoratedRange.h/cpp            # Block decoration ranges
|   +-- FoldingModel.h/cpp              # Heading fold state management
|   +-- FoldGutter.h/cpp                # Viewport-pinned fold UI
|   +-- GutterColumn.h                  # Abstract gutter column + FoldArrowColumn
|   +-- FoldArrowColumn.cpp
|   +-- SearchBar.cpp
|   +-- LinkRenderer.cpp
|   +-- FoldingTypes.cpp
|   +-- ResourceProvider.cpp
|   +-- Theme.cpp
|   +-- MarkoffBlockData.h
+-- tests/
+-- app/
+-- docs/
    +-- archive/
```

---

## Documentation Map

| Document | Purpose |
|----------|---------|
| `architecture.md` | This file — current system description |
| `TODO.md` | Active backlog — the most up-to-date task list |
| `2026-04-13-codebase-audit.md` | Comprehensive code quality audit |
| `01-problem-definition.md` | Why Markoff exists |
| `02-parser-survey.md` | Parser evaluation (MD4C, cmark, tree-sitter) |
| `03-editor-architecture-survey.md` | Editor widget approaches |
| `04-reference-codebase-analysis.md` | Analysis of reference projects |
| `05-options-and-tradeoffs.md` | Design decision rationale |
| `06-qt-source-harvest.md` | Fork strategy for Qt widgets |
| `obsidian-editor-internals.md` | Obsidian's CodeMirror architecture |
| `obsidian-editor-ux-reference.md` | Obsidian UX behavior catalog |
| `specs/` | Design specs (implemented ones marked at top) |
| `plans/` | Implementation plans (implemented ones marked at top) |
| `archive/` | Superseded specs from earlier architectural approaches |
| `appendix-*.md` | External research (vaults, repos, APIs) |
