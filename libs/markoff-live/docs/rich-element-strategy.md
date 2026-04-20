# Rich Element Rendering Strategy

How MarkoffEditor renders markdown elements that go beyond plain styled
text. Establishes the standard approaches, when to use each, and the
implementation path for every remaining rich element.

---

## The Problem

Markdown contains elements that need more than text styling: math
equations rendered as glyphs, code blocks with syntax highlighting
and backgrounds, callout boxes with colored borders, tables with grid
layouts, images as pixel rectangles, checkboxes that toggle on click.

We've iterated through several approaches (QTextTable embedding,
QWidget overlays, pixmap-based atomic blocks — all archived). The
current codebase has three working approaches. This document
standardizes them as the definitive patterns and maps every element
to the right one.

---

## The Three Patterns

### Pattern A: Decorated Range

**What it is:** Custom painting (backgrounds, borders, accent bars,
graphical lines) drawn *behind* the editable text in
`MarkdownTextItem::paintDecoratedRanges()`. The text itself is
untouched — still editable, cursor works, undo works. The highlighter
handles text formatting (colors, fonts, hiding delimiters); the
decorated range adds visual chrome around it.

**Current implementation:**
- `DecoratedRange` struct stores type, block range, metadata (language,
  callout type/color)
- `detectDecoratedRanges()` scans the document for code fences and
  callout markers
- `paintDecoratedRanges()` draws per-type chrome before the text is
  painted

**What it paints today:**
- Fenced code blocks: gray rounded-rect background, thin border,
  language label in top-right corner
- Callouts (`> [!note]`): faint tinted background in callout color,
  solid 4px left accent bar

**Properties:**
- Text remains fully editable inside the range
- Cursor navigation is unaffected (transparent to cursor)
- Undo/redo work normally
- No span map coupling (parallel to spans, not dependent on them)
- Very cheap: just metadata + a `drawRoundedRect` per range
- Cannot replace text with different visual (can only add chrome
  around existing characters)

**When to use:** The source text IS the displayed text (possibly with
delimiters hidden by the highlighter), and we just need visual
grouping — backgrounds, borders, accent bars, divider lines.

### Pattern B: Inline Object Substitution (U+FFFC)

**What it is:** A contiguous range of source text is replaced with a
single Unicode Object Replacement Character (U+FFFC). Qt's layout
engine calls our `QTextObjectInterface` implementation to size and
paint a custom glyph in place of that character. The source text is
preserved in QTextCharFormat properties and can be revealed for
editing.

**Current implementation:**
- `MathTextObject` implements `QTextObjectInterface` with
  `intrinsicSize()` and `drawObject()` for LaTeX rendering
- `MathRenderer` converts LaTeX to cached QImage via JKQTMathText
- `applyMathSubstitution()` finds math spans and replaces source text
  with U+FFFC carrying format properties (SourceProperty, RawProperty,
  DisplayProperty)
- `stripMathSubstitution()` reverses the process (U+FFFC → source)
- `updateMathReveal()` expands a single glyph back to source when the
  cursor clicks on it; re-collapses when cursor leaves

**Properties:**
- Source text is hidden but preserved in format properties
- Cursor treats U+FFFC as a single character (arrow keys step past)
- Click-to-reveal allows editing the source inline
- Undo/redo work (document tracks substitution as edits)
- Creates span map staleness (substituted positions don't match
  source positions — managed by stripping before reparse)
- Moderate complexity: the reveal/collapse state machine is the most
  complex subsystem in the codebase (~300 lines)

**When to use:** A short inline source range should be replaced with
a rendered visual object — a glyph, icon, or small image. The object
is not interactive beyond click-to-reveal.

### Pattern C: Scene-Level BlockItem

**What it is:** The markdown document is split at block boundaries by
`MarkdownSplitter`. Each segment becomes either a `MarkdownTextItem`
(editable text) or a `BlockItem` subclass (custom QGraphicsObject
with its own rendering). Block items participate in cross-boundary
selection via the `SelectableItem` interface.

**Current implementation:**
- `BlockItem` base class provides selection overlay painting and the
  `SelectableItem` non-text-item interface
- `TableBlockItem` renders a read-only pipe table as a grid
- `MarkdownSplitter` uses `TreeSitterParser::findBlockBoundaries()`
  to identify split points
- `SceneCoordinator::loadMarkdown()` creates items per segment
- `SceneCoordinator::reparse()` detects structure changes and
  rebuilds the scene when block boundaries shift

**Properties:**
- The visual output has no character-level correspondence to source
- Full control over rendering (custom `paint()`)
- Participates in cross-boundary selection (fully-selected overlay)
- Serializes back to markdown via `toMarkdown()`
- Not editable as text (no QTextDocument, no cursor, no IME)
- Heaviest approach: requires parser, splitter, coordinator changes
  for each new type

**When to use:** The visual representation is geometrically
incompatible with the source text — grids, pixel rectangles, embedded
previews, diagrams. The source text cannot meaningfully be displayed
as styled characters.

---

## Decision Framework

```
Is the element just styled text (colors, fonts, weight)?
  YES → Highlighter span. Done.
  NO ↓

Is the source text the displayed text, just with chrome around it?
  YES → Pattern A (Decorated Range). Done.
  NO ↓

Is the element inline (within a paragraph, short range)?
  YES → Pattern B (U+FFFC Substitution). Done.
  NO ↓

Is the visual output geometrically incompatible with source text?
  YES → Pattern C (Scene-Level BlockItem). Done.
  NO → Pattern A or B, case by case.
```

---

## Element-by-Element Classification

### Already Implemented

| Element | Source | Visual | Pattern | Status |
|---------|--------|--------|---------|--------|
| Bold, italic, strikethrough | `**text**`, `*text*`, `~~text~~` | Styled text, hidden delimiters | Highlighter | Done |
| Inline code | `` `code` `` | Monospace + background tint | Highlighter | Done |
| Links | `[text](url)` | Colored text, hidden URL | Highlighter | Done |
| Wiki links | `[[note]]` | Colored text, hidden brackets | Highlighter | Done |
| Tags | `#tag` | Colored text | Highlighter | Done |
| Headings | `# Title` | Scaled bold, hidden hash | Highlighter | Done |
| List markers | `- item`, `1. item` | Colored marker | Highlighter | Done |
| Footnote refs | `[^1]` | Superscript number, hidden `^` | Highlighter | Done |
| Frontmatter | `---\nyaml\n---` | Muted color | Highlighter | Done |
| Inline math | `$x^2$` | Rendered LaTeX glyph | **U+FFFC** | Done |
| Display math | `$$\int f(x)$$` | Rendered LaTeX glyph | **U+FFFC** | Done |
| Fenced code block | ` ```lang ... ``` ` | Gray background, syntax colors, language label | **Decorated Range** | Done |
| Callout | `> [!note] Title` | Tinted background, accent bar, styled title | **Decorated Range** | Done |
| Pipe table | `| a | b |` | Custom grid rendering | **BlockItem** | Done (read-only) |

### Remaining — Straightforward

| Element | Source | Visual | Pattern | Rationale |
|---------|--------|--------|---------|-----------|
| Horizontal rule | `---` or `***` or `___` | Graphical line across width | **Decorated Range** | Source text is already hidden by highlighter; paint a line over it |
| Blockquote border | `> text` | Left accent bar | **Decorated Range** | Same pattern as callout accent bar, just without background tint |
| Task checkbox | `- [ ]` / `- [x]` / `- [>]` | Checkbox icon | **U+FFFC** | Short inline range → rendered icon. Click toggles state. |
| Inline image (rare) | `![alt](url)` mid-paragraph | Thumbnail | **U+FFFC** | Short inline range → rendered image. Click to reveal source. |

### Remaining — Needs Design Work

| Element | Source | Visual | Pattern | Open Questions |
|---------|--------|--------|---------|----------------|
| Block image | `![alt](url)` on own line | Full-width rendered image | **BlockItem** | Resize handles? Lazy loading? Missing-image placeholder? |
| Embed | `![[note]]` | Embedded note preview | **BlockItem** | Recursion depth? Editing the embedded content? Interaction? |
| Editable table | `| a | b |` | Grid with editable cells | **BlockItem** | Per-cell QTextDocument? Tab navigation? Row/column ops? |
| Mermaid diagram | ` ```mermaid ... ``` ` | Rendered SVG | **BlockItem** | External renderer? Async? Click-to-edit? |

---

## Generalizing the U+FFFC System

The current math substitution code is tightly coupled to LaTeX. To
support checkboxes (and potentially inline images), we generalize it
into an **inline object substitution system** that handles multiple
object types through shared machinery.

### Current State: Math-Only

```
MathTextObject (QTextObjectInterface)
  TypeId = QTextFormat::UserObject + 1
  Properties: SourceProperty, RawProperty, DisplayProperty

MarkdownTextItem methods:
  applyMathSubstitution()   — find math spans, replace with U+FFFC
  stripMathSubstitution()   — reverse: U+FFFC → source text
  refreshMathSubstitution() — strip then re-apply
  updateMathReveal()        — click-to-expand, cursor-leave-to-collapse
```

### Target State: Multi-Type

```
InlineObjectType enum:
  Math        — rendered LaTeX glyph
  Checkbox    — checkbox icon (checked/unchecked/forwarded)
  InlineImage — thumbnail (future)

Each type gets its own QTextObjectInterface subclass:
  MathTextObject      — TypeId = UserObject + 1 (unchanged)
  CheckboxTextObject  — TypeId = UserObject + 2
  InlineImageObject   — TypeId = UserObject + 3 (future)

Shared properties (on QTextCharFormat):
  RawProperty   — original delimited source for round-trip
  SourceProperty — content without delimiters

Type-specific properties:
  Math: DisplayProperty (bool, $$ vs $)
  Checkbox: CheckedProperty (enum: unchecked/checked/forwarded)
  InlineImage: UrlProperty, AltTextProperty (future)

MarkdownTextItem methods (renamed for generality):
  applyInlineSubstitutions()  — find ALL inline object spans, replace
  stripInlineSubstitutions()  — reverse ALL
  refreshInlineSubstitutions() — strip then re-apply
  updateReveal()              — generalized reveal/collapse

Each type registers its handler once in the constructor:
  doc->documentLayout()->registerHandler(MathTextObject::TypeId, m_mathObject);
  doc->documentLayout()->registerHandler(CheckboxTextObject::TypeId, m_checkboxObject);
```

### Key Difference: Checkboxes Don't Need Reveal

Math uses click-to-reveal because the user needs to edit the LaTeX
source. Checkboxes use click-to-toggle: clicking the glyph flips the
checked state in the underlying source text without expanding to
show `[ ]` / `[x]`.

```
Math click flow:
  click on glyph → expand U+FFFC to "$x^2$" → user edits → cursor leaves → re-collapse

Checkbox click flow:
  click on glyph → read CheckedProperty → toggle in source text → resubstitute
```

This means `updateReveal()` needs a type dispatch: math objects get
reveal/collapse behavior, checkbox objects get toggle behavior.
Alternatively, keep reveal as math-only and handle checkbox toggle
in `mousePressEvent()` before the reveal logic runs.

### Substitution Span Detection

Currently `applyMathSubstitution()` finds math spans by scanning the
highlighter's span map for `span.math || span.mathDisplay`. The
generalized version scans for multiple span types:

```
For each span in highlighter.spans():
  if span.math || span.mathDisplay → substitute as Math
  if span.isListMarker && text matches [ ]/[x]/[>] → substitute as Checkbox
```

The highlighter already identifies list markers via `span.isListMarker`.
We need to distinguish task list markers (which have `[ ]` content)
from regular list markers (which are `-` or `1.`). Tree-sitter
provides `task_list_marker_checked`, `task_list_marker_unchecked`, and
`task_list_marker_extended` node types — these should map to a new
span flag (`isTaskMarker`) to make detection trivial.

---

## Generalizing Decorated Ranges

The current decorated range system detects code blocks and callouts
via regex/text scanning in `detectDecoratedRanges()`. To support
horizontal rules and blockquote borders, we extend it.

### Current Types

```cpp
enum Type { CodeBlock, Callout, Blockquote, Table };
```

`Blockquote` and `Table` are defined but not actively used for
painting. Only `CodeBlock` and `Callout` have paint logic.

### Additions

**Horizontal Rule:**
- Detection: block where `text.trimmed()` matches `---`, `***`, or
  `___` (or use the highlighter's `isHorizontalRule` span flag)
- Paint: single horizontal line across the item width, vertically
  centered on the block's layout rect, using Theme's HorizontalRule
  color
- The highlighter already makes the `---` text transparent; the
  decorated range paints the graphical line on top

**Blockquote Border:**
- Detection: blocks where the highlighter has `isBlockquote` spans
  (already tracked by tree-sitter as `block_quote` nodes)
- Paint: thin left accent bar (same as callout but without background
  tint), using Theme's BlockQuote color
- Depth-aware: `span.blockquoteDepth` from the tree-sitter parser
  determines how many nested bars to draw (indent each by ~16px)

### Detection Integration

Currently `detectDecoratedRanges()` uses regex. This should migrate
to using the tree-sitter span map (which is already available from
the highlighter). The span map has `isHorizontalRule`,
`isBlockquoteMarker`, `isBlockquote`, and `blockquoteDepth` flags.
Walking the span map for decorated range detection would:
- Eliminate the regex duplication (tree-sitter already parsed this)
- Handle edge cases the regex misses (e.g., code blocks inside
  blockquotes)
- Be consistent with how the highlighter already identifies these
  constructs

---

## Scene-Level BlockItem Expansion

### Adding a New Block Type: The Checklist

To add a new `BlockItem` subclass (e.g., `ImageBlockItem`):

1. **Parser**: Add a new `BlockBoundary::Type` value in
   `TreeSitterParser::BlockBoundary` enum. Update
   `findBlockBoundaries()` to detect the new block type in the
   tree-sitter CST.

2. **Splitter**: Add a new `MarkdownSegment::Type` value. Update
   `MarkdownSplitter::split()` to emit segments of the new type.

3. **Item class**: Create the new `XxxBlockItem : BlockItem` class.
   Implement `boundingRect()`, `paint()`, `toMarkdown()`. Call
   `paintSelectionOverlay()` at end of `paint()`.

4. **Coordinator**: Update `SceneCoordinator::loadMarkdown()` to
   instantiate the new item type for matching segments. Update
   `reparse()` if the item needs incremental updates (font, theme).

5. **Tests**: Add test cases for the new block type's rendering and
   serialization.

Files touched: `TreeSitterParser.h/cpp`, `MarkdownSplitter.h/cpp`,
new `XxxBlockItem.h/cpp`, `SceneCoordinator.cpp`,
`CMakeLists.txt` (add new source files).

### Current Block Types

| Type | Class | Status | Interactive? |
|------|-------|--------|-------------|
| Pipe table | `TableBlockItem` | Shipped (read-only) | Column width adjustment planned |
| Image | Not yet built | — | Resize handles planned |
| Embed | Not yet built | — | Click-to-navigate planned |
| Mermaid | Not yet built | — | Click-to-edit planned |

### The Editable Table Question

Tables are the hardest remaining block item because they need
cell-level editing. The current `TableBlockItem` is read-only
(custom-painted grid). Making it editable requires:

- Per-cell text editing (QTextDocument per cell, or a single
  QTextDocument with QTextTable, or raw string editing with cursor
  tracking)
- Tab/Shift+Tab navigation between cells
- Enter to add row, column ops via context menu
- Serialization back to pipe-delimited markdown
- Undo/redo across cell edits

This is a substantial feature that warrants its own design spec.
The `TableBlockItem` read-only rendering is a solid foundation to
build on — the cell geometry calculation and pipe-markdown parsing
already work.

---

## Implementation Priority

Ordered by user-visible value and implementation difficulty:

### Tier 1: Low-hanging fruit (extend existing patterns)

1. **Horizontal rule as graphical line** — Add `HorizontalRule` to
   decorated range detection and painting. ~30 lines of code.
   The highlighter already hides the `---` text.

2. **Blockquote left border** — Add `Blockquote` to decorated range
   painting. Reuse the callout accent bar pattern. ~40 lines.

3. **Task checkboxes as icons** — Add `CheckboxTextObject`
   (QTextObjectInterface), register alongside MathTextObject.
   Extend the substitution system to handle checkbox spans.
   Click-to-toggle in `mousePressEvent()`. ~150 lines.

### Tier 2: Moderate effort (new patterns)

4. **Generalize substitution system** — Rename math-specific methods
   to generic inline-substitution methods. Support multiple object
   types through a single strip/apply/refresh cycle. This is
   prerequisite infrastructure for checkboxes and future inline
   objects. ~100 lines of refactoring.

5. **Migrate decorated range detection to span map** — Replace regex
   detection with tree-sitter span-based detection. Eliminates
   duplication and improves robustness. ~80 lines.

### Tier 3: Significant features (new BlockItem types)

6. **ImageBlockItem** — Full-width image rendering with resource
   provider integration. Needs lazy loading, missing-image
   placeholder, and aspect-ratio-preserving layout.

7. **Editable tables** — Cell-level editing in TableBlockItem.
   Needs its own design spec.

8. **Embed preview** — Recursive rendering of embedded notes.
   Needs depth limiting and interaction model design.

### Tier 4: External dependencies

9. **Mermaid diagrams** — Requires a mermaid renderer (CLI tool or
   library). Rendered SVG displayed in a BlockItem with
   click-to-edit-source.

---

## Consistency Principles

1. **The highlighter handles all text formatting.** Colors, fonts,
   weights, delimiter visibility — always via spans. Decorated
   ranges and block items never apply QTextCharFormat.

2. **Decorated ranges handle all block-level chrome.** Backgrounds,
   borders, accent bars, graphical lines — always painted in
   `paintDecoratedRanges()`. The highlighter doesn't paint geometry.

3. **U+FFFC handles all inline visual replacements.** Rendered math,
   checkbox icons, inline thumbnails — always via
   QTextObjectInterface. The highlighter identifies the spans; the
   substitution system replaces them.

4. **Scene-level blocks handle all geometry-incompatible elements.**
   Tables, full images, embeds, diagrams — always as separate
   QGraphicsObject items with their own paint() and toMarkdown().

5. **Everything serializes back to valid markdown.** Every approach
   must support `toMarkdown()` or `allMarkdown()` that produces
   the original source text. U+FFFC glyphs carry their source in
   RawProperty. Block items store their source markdown.

6. **The tree-sitter parser is the single source of truth.** Span
   detection, block boundary detection, and document queries all
   derive from the same parse. No parallel regex detection systems
   (migrate existing regex detection to span-based detection over
   time).

7. **Read-only mode preserves all visual rendering.** When
   `setReadOnly(true)`, all patterns render identically. The only
   difference is that text input is disabled and U+FFFC
   click-to-reveal becomes click-to-follow (for links) or
   click-to-toggle (for checkboxes, which are explicitly allowed
   in read-only mode as a non-destructive interaction).

---

## File Impact Summary

For the Tier 1 + 2 work:

### New Files
- `src/CheckboxTextObject.h/cpp` — QTextObjectInterface for task
  checkboxes

### Modified Files
- `src/MarkdownTextItem.h/cpp` — generalize substitution methods,
  register CheckboxTextObject, handle checkbox click-to-toggle
- `src/DecoratedRange.h/cpp` — add HorizontalRule detection, update
  Blockquote painting
- `src/MarkdownHighlighter.h/cpp` — (minor) may need new span flag
  for task markers if not already distinguishable
- `libs/markoff-parser/src/TreeSitterParser.cpp` — add `isTaskMarker`
  span flag for `task_list_marker_*` nodes
- `libs/markoff-parser/include/markoff-parser/SourceSpan.h` — add
  `isTaskMarker` field

### Unchanged Files
- `src/MathTextObject.h/cpp` — no changes needed
- `src/MathRenderer.h/cpp` — no changes needed
- `src/SceneCoordinator.h/cpp` — no changes for Tier 1/2
- `src/BlockItem.h/cpp` — no changes for Tier 1/2
- `src/TableBlockItem.h/cpp` — no changes for Tier 1/2
