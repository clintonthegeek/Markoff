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
          | MarkdownTextItem|  | ImageBlockItem|  | (future items) |
          | (SelectableItem)|  | (SelectableItem) | Mermaid, etc.  |
          +--------+--------+  +-----+--------+  +----------------+
                   |
          +--------+--------+
          |   TextControl   |
          |   (Qt fork)     |
          +--------+--------+
                   |
          +--------+--------+
          |  QTextDocument  |
          |  + Highlighter  |
          |  + MathObject   |
          |  + CheckboxObj  |
          |  + QTextTable * |
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
- Registers MathTextObject + CheckboxTextObject for inline glyph
  rendering (U+FFFC object replacement characters)
- Hosts `QTextTable` frames inline — pipe-table markdown is converted
  into a real `QTextTable` at load time via `TableConverter`, providing
  native cell editing with cursor/undo/selection
- Detects and paints DecoratedRanges (code block backgrounds, callout
  borders, blockquote indicators, horizontal rules)
- Emits `cursorAtBoundary(Qt::Edge)` for focus transfer between items

**BlockItem** (QGraphicsObject + SelectableItem) base class:
- Paints selection overlay when fully selected
- Subclasses: **ImageBlockItem** (read-only image rendering via
  ResourceProvider). Tables used to have their own `TableBlockItem`
  subclass; they are now embedded inside MarkdownTextItem — see the
  block-type decision framework below for why.

### Block type decision framework

When adding a new visual element (embed, Mermaid, custom diagram, ...),
pick the host based on the element's interactive content:

| Content shape | Host | Examples |
|---|---|---|
| Fundamentally non-textual (raster, canvas, SVG) | separate `BlockItem` subclass | `ImageBlockItem`, future `MermaidItem` |
| Rich-text frame with interior cursors/selection/undo | `QTextFrame` / `QTextTable` **inside** `MarkdownTextItem` | Tables (v1) |
| Opaque single-glyph substitution for inline source | `QTextObjectInterface` on `MarkdownTextItem`'s document | Inline math (`$x$`), display math (`$$...$$`), task checkboxes |
| Visual decoration around otherwise-normal text | painter pass in `MarkdownTextItem` via `DecoratedRange` | Code block background, callout borders, blockquote bars, horizontal rule |

**Rule of thumb**: default to the lightest option that can hold the
content. Only promote to a separate `BlockItem` when the content is
genuinely not text — i.e., you would never want a cursor inside it.

This is why tables moved out of `TableBlockItem` and into
`MarkdownTextItem`: cells have interior text, and `QTextTable` gives us
native cell cursors, selection, and undo without any cross-boundary
protocol. It also unifies the editing surface — Tab/Shift+Tab navigate
cells via the same TextControl that drives paragraph typing.

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
4. **Cursor reveal** (click-only): When the user **mouse-clicks** on a
   math glyph, `updateReveal()` expands the U+FFFC back to raw LaTeX
   source so the math can be edited. Arrow-key navigation just steps
   past the 1-char U+FFFC naturally — no reveal fires. On cursor exit
   (e.g., click elsewhere, or a subsequent reparse), the source
   collapses back to a glyph.

   This click-only design replaced an earlier arrow-key-triggered
   reveal that had ~300 LOC of reentrancy guards and deferred
   flag-clearing. The current mechanism is small enough that the
   per-glyph / per-item simplification debated in earlier drafts is no
   longer a pressing question. Guards that remain: `m_inSubstitution`
   (prevents recursive substitution passes), `m_inCursorUpdate`
   (prevents reveal from re-entering during cursor-snap), and
   `m_snappingCursor` (suppresses reveal while snap-past-delimiter is
   repositioning the caret).

---

## Theme System

`Markoff::Theme` is a two-part value: a `QHash<Element, QTextCharFormat>`
that drives highlighter-applied formatting (57 element types covering
every markdown construct), and a `PaintColors` sub-struct that drives
every surface a `QTextCharFormat` can't express — custom-painted glyphs,
ExtraSelection overlays, rounded backdrops, callout accent bars, etc.
Together those two pieces cover every color in the editor.

```
Theme
├── formats[Element]  → QTextCharFormat, applied by MarkdownHighlighter
├── textFont, codeFont
└── paint             → PaintColors, read at paint time by graphics items
    ├── codeBlockBg / codeBlockBorder / codeBlockLanguageLabel
    ├── searchMatchBg / searchCurrentMatchBg      (ExtraSelection backgrounds)
    ├── checkboxCheckedFill / checkboxCheckMark /
    │   checkboxUncheckedOutline                  (CheckboxTextObject glyph)
    ├── imagePlaceholderBg / imagePlaceholderBorder /
    │   imagePlaceholderText                      (ImageBlockItem fallback)
    ├── blockSelectionOverlay                     (BlockItem "fully selected")
    └── calloutAccents{type→color} + calloutDefault
```

### Propagation

`Editor::setTheme(theme)` stores the theme and pushes it to
`SceneCoordinator::setTheme(theme)`. The coordinator stores the active
theme in `m_theme` (so freshly-created items inherit it on first paint)
and, for each existing item, dispatches:

- Text items → `MarkdownTextItem::setTheme()` which fans out to the
  item's `MarkdownHighlighter` (rehighlight with new `formats`) and to
  its `CheckboxTextObject` (glyph colors), then re-runs
  `detectDecoratedRanges()` so callout accents pick up the new palette.
- `BlockItem` subclasses (ImageBlockItem, etc.) → virtual
  `BlockItem::setTheme()` which handles `blockSelectionOverlay` in the
  base class; subclasses override to pull their own `paint` fields.

### Resolving callout colors

`Theme::calloutColor("warning")` lowercases the type and looks it up in
`paint.calloutAccents`, returning `paint.calloutDefault` when missing.
Obsidian-compatible type names (`note`/`info`/`todo`,
`abstract`/`summary`/`tldr`, `tip`/`hint`/`important`, `success`/`check`/
`done`, `question`/`help`/`faq`, `warning`/`caution`/`attention`,
`failure`/`fail`/`missing`, `danger`/`error`/`bug`, `example`, `quote`/
`cite`) are populated by `defaultLight()` / `defaultDark()`.

### Factory methods

- `Theme::defaultLight()` / `Theme::defaultDark()` — built-in palettes.
- `Theme::fromSchemeFile(path)` — QOwnNotes-style INI format. The INI
  describes `formats` only; the paint colors are inherited from
  `defaultLight()` so host apps never receive a partially-initialized
  Theme. A host that wants custom paint colors should assign them on
  the returned `Theme::paint` before handing it to `Editor::setTheme`.

**Open question**: extend `fromSchemeFile()` to read KDE color schemes
(Breeze, etc.) for native desktop integration.

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
| MarkdownTextItem | Shipped | Full editing, highlighting, math, checkboxes, decorated ranges, embedded QTextTable frames |
| ImageBlockItem | Shipped (read-only) | Rendered image via ResourceProvider, missing-image fallback. Rescales on viewport resize via `setMaxWidth`. Placeholder colors driven by `Theme::paint`. |

## Embedded rich elements (inside MarkdownTextItem)

| Element | Mechanism | Status |
|---------|-----------|--------|
| Inline math `$x$` | `MathTextObject` (QTextObjectInterface), U+FFFC with `RawProperty` for round-trip | Shipped, cursor-click reveals source for editing |
| Display math `$$...$$` | Same, `DisplayProperty = true` | Shipped |
| Pipe tables | `QTextTable` frame, converted at load via `TableConverter`, serialized via `TableSerializer` | **Shipped — v1 editable.** Cell editing, Tab/Shift+Tab navigation, context menu for insert/delete row/column, column alignment preserved through serialize/reparse. |
| Task checkboxes `- [ ]` / `- [x]` | `CheckboxTextObject` (QTextObjectInterface), click-to-toggle in `MarkdownTextItem::mousePressEvent` | Shipped graphic; `Editor::toggleCheckbox()` action is broken against the substitution layer (tracked in TODO) |

## Future block item types

| Type | Expected host (per framework above) | Notes |
|------|-------------------------------------|-------|
| MermaidItem | Separate `BlockItem` (diagram is non-textual) | External renderer, likely async — consider cache keyed by source text + theme |
| EmbedItem (`![[note]]`) | Host depends on editability: opaque navigate-to-source → `QTextObjectInterface`; interactive live-preview → `QTextFrame` subclass; full recursive editor → separate `BlockItem` with nested `Editor` | Grammar support needs `![[...]]` node (see Obsidian extensions below) |
| HorizontalRuleItem | Currently handled as `DecoratedRange::HorizontalRule` painter pass | Only promote to a separate item if we need drag-resize or richer styling |
| CodeBlockItem | Stay in `MarkdownTextItem` — source text IS the visual text, just styled | Handled today as `DecoratedRange::CodeBlock` + KSyntaxHighlighting |

---

## Parsing

Tree-sitter-markdown is the sole parser. It lives in MarkoffParser and
serves both the editor (span map, block boundaries) and the Document
query API (headings, links, tags) via separate CST traversals:

- `TreeSitterParser::buildSpanMap()` — flat formatting ranges for the highlighter
- `TreeSitterParser::buildDocumentQueries()` — structured HeadingInfo/LinkInfo/TagInfo
- `TreeSitterParser::findBlockBoundaries()` — table/code block split points

### Known issue: redundant parses per reparse cycle

A single debounced reparse currently drives these tree-sitter parses of
the full document:

1. `SceneCoordinator::reparse()` calls `MarkdownSplitter::split()` on the
   full source via the shared `m_parser`.
2. Per text segment, `m_parser` is reused to parse each segment's text
   (per-item parses — not redundant, each segment is a distinct input).
   `detectTableRegions()` reads `findBlockBoundaries()` off that same
   per-segment parser state.
3. `ensureHeadingMap()` reuses the full-document heading list captured
   by `captureFullDocumentParse()` at the top of `reparse()` /
   `loadMarkdown()` — **no longer parses independently**. This removed
   one full-document parse per fold/outline query.
4. `onDocumentReparsed()` then calls `Document::fromMarkdown(toPlainText())`
   to rebuild the `Document` model for headings/links/tags signals —
   still a second full-source parse. `Document::fromMarkdown` strips
   frontmatter + footnote definitions before parsing, so collapsing
   this with the editor's `m_parser` would require teaching the
   editor pipeline about those strips too. Not done yet.

**Open question**: tree-sitter supports incremental parsing via
`ts_tree_edit()`. The current implementation does a full reparse on every
debounced keystroke. At what document size does this become a bottleneck?
Should incremental parsing be prioritized alongside the parse-path
unification above, or deferred until the single-parser refactor lands?

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

See `TODO.md` for the canonical backlog. The architecturally-significant
gaps, roughly ordered by dependency and value:

1. **Parse-path unification** — collapse the 2–4 independent tree-sitter
   parses per reparse cycle (SceneCoordinator + detectTableRegions +
   ensureHeadingMap + Document::fromMarkdown) into one shared AST. Also
   retires the MD4C-backed `DocumentBuilder` path so Obsidian grammar
   extensions (embeds, block refs) only need to be taught one parser.
2. **Incremental tree-sitter parsing** — `ts_tree_edit()` for keystroke-level
   performance on large documents. Land after (1).
3. **Cross-item undo coordination** — each `MarkdownTextItem` has its own
   undo stack today, so multi-item edits (e.g., selectAll + toggleBold)
   cannot be atomically undone.
4. **Incremental rehighlight** — targeted `rehighlightBlock()` calls
   instead of full-document rehighlights on reparse.
5. **Theme-driven visual constants** — code block bg/border, search
   highlight yellow/orange, callout colors, `TableStyle` struct currently
   hardcoded.
6. **Highlighter + SceneCoordinator + TextControl test suites** — large
   untested surfaces. TextControl (2,572-line Qt fork) is the biggest risk.
7. **Performance benchmarks** — no harness exists for large-document
   keystroke-latency or reparse-time measurement.
8. **Obsidian grammar additions** — `![[embed]]` and `^block-id` still
   need grammar nodes; `==highlight==` and `%%comment%%` already work.
9. **Accessibility** — zero `QAccessibleInterface` coverage on scene
   items; a screen reader sees opaque shapes.

Shipped since earlier drafts of this document:
- Heading folding v1 with gutter, per-block visibility, JSON persistence.
- Editable tables v1 (`QTextTable` + `TableConverter` + `TableSerializer`).
- QAction-based shortcuts with `ActionId` registry (Editor::action(id)).
- Audit top-4 fixes for `selectAll` / `cut` / `goToLine` / `cursorLine`.
- Math reveal simplified to click-only expansion (arrow-key reveal removed).
- LinkRenderer typed emission surface for wikilinks and external URLs.
- Round-trip fidelity: byte-for-byte source preservation across
  `toPlainText()`, file save, and select-all + clipboard copy. Blank
  lines render as real cursor-editable `QTextBlock`s (2026-04-17).

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
|   +-- Editor.h                        # QGraphicsView widget + ActionId enum
|   +-- Theme.h                         # Colors, fonts, element formats
|   +-- EditorSettings.h                # Editor behavior config (currently unapplied; tracked in TODO)
|   +-- ResourceProvider.h              # Image/link/embed resolution
|   +-- SearchBar.h                     # Embedded find/replace bar
|   +-- LinkRenderer.h                  # Typed link emission surface
|   +-- FoldingTypes.h                  # Heading path computation (has an include-order wart, tracked in TODO)
+-- src/
|   +-- Editor.cpp                      # God class: scene mgmt, QActions, shortcuts, formatting, search, folding bridge
|   +-- SceneCoordinator.h/cpp          # Scene item management, reparse, coordinate mapping, heading map
|   +-- SelectionScene.h/cpp            # QGraphicsScene + SelectionManager host
|   +-- SelectionManager.h/cpp          # Cross-boundary selection state machine
|   +-- SelectableItem.h                # Item interface (isTextItem, hitTest, selection, markdown)
|   +-- MarkdownTextItem.h/cpp          # Editable text region, hosts rich inline objects + QTextTable frames
|   +-- BlockItem.h/cpp                 # Non-text item base
|   +-- ImageBlockItem.h/cpp            # Read-only image rendering
|   +-- StubBlockItem.h/cpp             # Testing placeholder (only used by tests/demo; lives in prod lib currently — tracked in TODO)
|   +-- TextControl.h/cpp               # Forked from Qt's QWidgetTextControl
|   +-- TextControl_p.h
|   +-- MarkdownHighlighter.h/cpp       # AST-driven syntax highlighting, cursor-aware delimiter hiding
|   +-- MathRenderer.h/cpp              # LaTeX→QImage via JKQTMathText, process-wide cache
|   +-- MathTextObject.h/cpp            # QTextObjectInterface for math glyphs
|   +-- CheckboxTextObject.h/cpp        # QTextObjectInterface for task checkboxes
|   +-- DecoratedRange.h/cpp            # Painter decoration metadata (code blocks, callouts, HR, blockquote bars)
|   +-- TableConverter.h/cpp            # Pipe-text → QTextTable conversion + doc-frame reconciliation
|   +-- TableSerializer.h/cpp           # QTextTable → pretty pipe-text (column widths, alignment markers)
|   +-- TableStyle.h                    # UNUSED struct — awaiting Theme integration (tracked in TODO)
|   +-- FoldingModel.h/cpp              # Heading fold state, path-based identity, reconcile on reparse
|   +-- FoldGutter.h/cpp                # Viewport-pinned gutter item hosting GutterColumns
|   +-- FoldArrowColumn.cpp             # Fold-arrow column implementation
|   +-- GutterColumn.h                  # Abstract gutter column interface
|   +-- SearchBar.cpp                   # Find/replace UI
|   +-- LinkRenderer.cpp                # Typed link emission
|   +-- FoldingTypes.cpp                # Heading path normalization / dedup
|   +-- ResourceProvider.cpp            # Base implementation + default file resolver
|   +-- Theme.cpp                       # Default light/dark factories + QOwnNotes scheme import
|   +-- MarkoffBlockData.h              # DEAD — QTextBlockUserData subclass never instantiated (tracked in TODO)
+-- tests/
+-- app/                                # markoff-testapp + scene-demo
+-- docs/
    +-- plans/                          # Per-feature implementation plans (dated)
    +-- specs/                          # Design specs (dated; implemented ones marked at top)
    +-- archive/                        # Superseded material
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
