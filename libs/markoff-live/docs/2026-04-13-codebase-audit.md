# Markoff Codebase Audit — 2026-04-13

Comprehensive review of the markoff widget library measuring current
implementation against the goal: a first-class Qt-style text entry widget
which seamlessly and beautifully presents live markdown for editing.

---

## Executive Summary

Markoff is ~11,000 lines of C++20/Qt6 across 43 source files, implementing
a QGraphicsView-based markdown editor with split text regions, tree-sitter
parsing, AST-driven syntax highlighting, inline LaTeX math rendering, and
cross-boundary selection. The architecture is sound and the public API is
clean. The main concerns are: (1) significant documentation drift from the
rapid evolution through phases 1-3, (2) hardcoded visual constants outside
the theme system, (3) a few code duplication patterns, and (4) the dual
parser (MD4C + tree-sitter) that should be collapsed.

---

## The Good

### Architecture

The QGraphicsView pivot was the right call. The split-scene model —
where each markdown block boundary spawns a separate MarkdownTextItem or
BlockItem within a QGraphicsScene — solves the fundamental problem that
QTextDocument can't host heterogeneous block types. This enables:

- Tables as custom-painted QGraphicsObjects alongside editable text
- Per-item QTextDocument + MarkdownHighlighter (no cross-block state leaks)
- Cross-boundary selection via SelectionManager without fighting Qt's
  native selection model
- Clean Source/LivePreview mode switching by rebuilding the scene

### Public API

Editor.h is a genuinely clean widget API. It inherits QGraphicsView but
exposes none of the scene internals. Consumers get:

- Content: `setPlainText`, `toPlainText`, `document()`
- Config: `setTheme`, `setEditorSettings`, `setResourceProvider`
- Editing: `toggleBold`, `insertTable`, `toggleCheckbox`, etc.
- Signals: `textChanged`, `cursorPositionChanged`, `headingsChanged`,
  `wikiLinkTrigger`, `tagTrigger`

No SelectionScene, SceneCoordinator, or MarkdownTextItem leaks through.
The `Document` query API (`headings()`, `links()`, `tags()`, `wordCount()`)
is also well-encapsulated behind an opaque pimpl.

### Math Rendering

The MathTextObject/MathRenderer split is exemplary:

- MathTextObject implements QTextObjectInterface for inline U+FFFC glyphs
- MathRenderer is a stateless utility with a process-wide thread-safe cache
- Images are supersampled at 3x DPI for crisp display
- Cursor-enter reveals source LaTeX; cursor-exit re-substitutes the glyph

### Code Hygiene

- No dead code, no commented-out blocks, no stale includes
- Parent ownership is correct throughout (no memory leaks)
- Signal/slot discipline is proper — no direct calls where signals belong
- `std::unique_ptr` used correctly for pimpl and owned objects
- Clean test suite with 12 test executables covering core functionality

### TextControl Fork

The fork from Qt's QWidgetTextControl is well-managed. The pimpl macros
(`Q_D`, `Q_DECLARE_PRIVATE`) were replaced with plain nested structs,
the `QWidgetPrivate` inheritance chain was broken cleanly, and only
public `QAbstractScrollArea` API is used. This is maintainable.

---

## The Bad

### 1. Hardcoded Visual Constants Outside the Theme

Multiple rendering classes contain hardcoded colors and layout values that
bypass the theme system entirely:

**Colors not in Theme:**
- `BlockItem.cpp:24` — selection overlay `QColor(51, 153, 255, 80)`
- `TableBlockItem.cpp:94,134,142,146` — header bg, grid lines (5 colors)
- `MarkdownTextItem.cpp:758,761,768` — code block bg, border, label color
- `MarkdownHighlighter.cpp:245` — fallback code syntax color `QColor(0x33, 0x33, 0x33)`
- `DecoratedRange.cpp:10-38` — 20+ callout type colors

**Layout magic numbers:**
- `SceneCoordinator.cpp:78-81` — `m_spacing = 8.0`, `m_leftMargin = 16.0`,
  `m_topMargin = 12.0`, `m_itemWidth = 600.0`
- `Editor.cpp:108` — `viewport()->width() - 32` (magic 32px margin)
- `Editor.cpp:45` — `verticalScrollBar()->setSingleStep(20)`
- `MarkdownTextItem.cpp:32` — `m_document->setDocumentMargin(8)`
- `TableBlockItem.cpp:38` — `m_cellPadding = 8.0`
- `SceneCoordinator.cpp:25` — reparse debounce `150ms`

The theme system (Element enum with 57 values, QHash<Element, QTextCharFormat>)
is comprehensive and well-designed. These outliers should join it.

### 2. Dual Parser Redundancy

MD4C and tree-sitter coexist but serve overlapping roles:

- **Tree-sitter** drives the editor: block boundary detection, span map,
  syntax highlighting, delimiter show/hide
- **MD4C** drives the reading view: DocumentBuilder → Document AST → Renderer
- **DocumentBuilder's Layer 2** post-processing (callouts, highlights,
  comments, tags) duplicates work tree-sitter could handle natively

The TODO already flags this (`TODO.md:76-79`), but the dual path means
every new markdown feature must be implemented twice. The Renderer should
be migrated to walk the tree-sitter CST, after which MD4C, DocumentBuilder,
DocumentBuilder_p.h, SourceSpan.cpp, and SourceSpan.h can be deleted.

### 3. Code Duplication Patterns

**findChild highlighter lookup (5 sites):**
```cpp
auto *hl = qobject_cast<MarkdownHighlighter *>(
    textItem->document()->findChild<QSyntaxHighlighter *>());
```
This pattern appears in `SceneCoordinator.cpp` (lines 149, 403),
`MarkdownTextItem.cpp` (lines 259, 469, 486, 618). The findChild call
is fragile (assumes single highlighter per document) and should be a
direct pointer stored on MarkdownTextItem.

**Math delimiter parsing (2 sites):**
`MarkdownTextItem.cpp` lines 306-315 and 519-527 both decode `$`/`$$`
delimiters identically. Extract to a shared helper.

**DocumentBuilder pattern splitting (2 sites):**
`DocumentBuilder.cpp` lines 319-325 and 368-411 share nearly identical
before/matched/after splitting logic for `==...==` and `%%...%%`.

### 4. MarkdownHighlighter Mode Enum Duplication

Both `Editor.h` and `MarkdownHighlighter.h` define identical `Mode` enums:
```cpp
enum class Mode { Source, LivePreview };
```
No shared definition. If one changes, the other silently diverges.

### 5. Inconsistent Ownership Semantics

`ResourceProvider*` is stored as a raw non-owning pointer in three places
(Editor, SceneCoordinator, Renderer) with no documentation of the lifetime
contract. While this follows Qt convention, the fact that it's stored
without any null checks at usage sites is fragile. Similarly,
`SelectionManager::createMimeData()` returns an owning `QMimeData*` that
the caller must delete — should return `std::unique_ptr<QMimeData>`.

---

## The Ugly

### 1. Documentation Has Drifted Significantly

The documentation describes a phased implementation plan that no longer
matches reality. The code has evolved rapidly through phases 1-3
simultaneously, but the docs still describe them as future work:

**Phase 1 spec** (`specs/2026-04-01-markoff-phase1-design.md`) describes
Editor.h as "forked from QPlainTextEdit" with `Editor_p.h`. Reality:
Editor now inherits QGraphicsView. `Editor_p.h` doesn't exist. The file
structure table lists files that don't exist (`Editor_p.h`) and misses
files that do (SelectionManager, SceneCoordinator, MathTextObject,
MarkdownSplitter, BlockItem hierarchy, etc.).

**Phase 2 spec** (`specs/2026-04-01-markoff-phase2-design.md`) describes
live preview as a future feature built on QPlainTextEdit with
`MarkoffBlockData` and variable block heights. Reality: live preview is
implemented via QGraphicsView scene reconstruction in SceneCoordinator.
The QPlainTextEdit approach was superseded by the QGraphicsView design.

**Phase 3 spec** (`specs/2026-04-01-tree-sitter-migration.md`) says
"what gets added: TreeSitterParser.h/cpp" and "rewritten MarkdownHighlighter."
Both of these already exist and are in use. The spec claims MD4C will be
deleted, but MD4C is still present (powering Renderer).

**Phase 1 plan** (`plans/2026-04-01-markoff-phase1.md`) lists a Corbomite
adapter (`MarkoffRenderEngine.h`) that may or may not exist in the main
codebase.

**Cross-boundary selection spec** (`specs/2026-04-02-cross-boundary-selection-design.md`)
and **GraphicsView spec** (`specs/2026-04-02-graphicsview-editor-design.md`)
were created as "future plans" but have been fully implemented. These
should be marked as completed.

The TODO files are the most accurate documentation. The historical specs
are now archaeological artifacts — useful for understanding design
rationale but misleading about current state.

### 2. TODO-tables.md Describes a Dead Approach

`TODO-tables.md` describes embedding `QTableWidget` over invisible pipe
text in a QPlainTextEdit, with scroll offset bugs and reparse widget
recreation issues. This entire approach was superseded by the
QGraphicsView pivot, which uses `TableBlockItem` (a custom
QGraphicsObject). The file should be archived or deleted.

### 3. Math Reveal/Collapse is Complex and Fragile

The cursor-reveal mechanism for inline math in `MarkdownTextItem.cpp`
(lines 335-652) is the most complex subsystem in the codebase. It:

- Strips U+FFFC glyphs and re-inserts raw LaTeX when cursor enters
- Re-substitutes glyphs when cursor leaves
- Snaps cursor past hidden delimiters
- Handles edge cases for display vs inline math
- Uses a reentrancy guard (`m_inMathSubstitution`)
- Uses a deferred event-loop flag clear (`QTimer::singleShot(0, ...)`)
- Acknowledges stale span offsets ("safe but hacky")

This works, but it's the area most likely to produce subtle bugs as new
features are added (e.g., undo across math boundaries, multi-cursor
editing inside math regions).

### 4. QTimer::singleShot(0) Deferred Flag Clearing

`SceneCoordinator.cpp:415-418`:
```cpp
QTimer::singleShot(0, this, [this]() {
    m_inReparse = false;
});
```

This defers the reparse guard clear to the next event loop iteration to
suppress signals from the reparse itself. It works but creates an
invisible temporal coupling: any code that checks `m_inReparse` during
the same event loop tick after reparse completes will see the wrong value.
If a second reparse is requested during this window, it will be incorrectly
blocked.

---

## Test Coverage Assessment

### Well-Covered

- Document parsing (headings, links, wikilinks, tags, footnotes, word count)
- Renderer output (HTML generation from AST)
- Table parsing and serialization
- Cross-boundary selection (SelectionManager state machine)
- Markdown splitting at block boundaries
- Theme construction (light, dark, scheme file)
- Resource provider (path resolution, link existence)
- Editor formatting commands (bold, italic, heading, code, etc.)
- Inline math substitution and reveal

### Not Covered

- Highlighter correctness (no tst_highlighter.cpp)
- SceneCoordinator reparse behavior
- Source/LivePreview mode switching
- Cursor boundary crossing between items
- Auto-scroll during drag selection
- Large document performance (no stress tests)
- Accessibility (screen reader, keyboard-only navigation)
- Editor undo/redo across math substitution boundaries
- ReadingView scrolling and heading navigation

---

## Documentation Inventory and Status

### Current and Accurate

| Document | Status |
|----------|--------|
| `TODO.md` | Current — actively maintained, accurate backlog |
| `01-problem-definition.md` | Evergreen — problem statement still valid |
| `02-parser-survey.md` | Evergreen — research/rationale still useful |
| `03-editor-architecture-survey.md` | Evergreen — survey of approaches |
| `04-reference-codebase-analysis.md` | Evergreen — reference research |
| `05-options-and-tradeoffs.md` | Evergreen — design rationale |
| `06-qt-source-harvest.md` | Evergreen — fork strategy documentation |
| `obsidian-editor-internals.md` | Evergreen — external reference |
| `obsidian-editor-ux-reference.md` | Evergreen — external reference |
| Appendices A-F | Evergreen — external research |

### Obsolete or Misleading

| Document | Issue |
|----------|-------|
| `specs/2026-04-01-markoff-phase1-design.md` | Describes QPlainTextEdit architecture; reality is QGraphicsView. File structure table is wrong. |
| `specs/2026-04-01-markoff-phase2-design.md` | Describes live preview on QPlainTextEdit with MarkoffBlockData; superseded by scene reconstruction approach. |
| `specs/2026-04-01-tree-sitter-migration.md` | Describes migration as future; tree-sitter is already integrated. MD4C deletion not yet done. |
| `specs/2026-04-01-table-embedding-design.md` | QTextTable-in-QTextDocument approach; fully superseded by QGraphicsView. |
| `specs/2026-04-01-tables-design.md` | May reference the old embedded widget approach. |
| `specs/2026-04-02-graphicsview-editor-design.md` | Describes the QGraphicsView approach as a design; it's now the implementation. Should be marked "implemented." |
| `specs/2026-04-02-cross-boundary-selection-design.md` | SelectionManager design spec; fully implemented. Should be marked "implemented." |
| `plans/2026-04-01-markoff-phase1.md` | Task list references files that don't exist (Editor_p.h, Corbomite adapter). |
| `TODO-tables.md` | Describes QTableWidget overlay approach; dead code path. |
| `07-atomic-blocks-and-tables.md` | May reference the pre-QGraphicsView atomic block approach. |

### Missing Documentation

- **Current architecture overview** — No document describes the actual
  QGraphicsView/SceneCoordinator/MarkdownTextItem architecture as-built
- **API reference** — Public headers are clean but undocumented beyond code
- **Rendering pipeline diagram** — The keystroke-to-pixel flow is complex
  and spans 6+ classes; a visual would help
- **Math rendering architecture** — The U+FFFC substitution cycle,
  cursor reveal mechanism, and cache strategy are undocumented
- **Testing guide** — How to run tests, what each test file covers, how
  to add new tests

---

## Recommendations (Prioritized)

### High Priority

1. **Write a current architecture document** describing the actual
   QGraphicsView-based design, class relationships, and rendering pipeline.
   This is the single biggest documentation gap.

2. **Migrate Renderer to tree-sitter and delete MD4C.** The dual parser
   is the largest source of unnecessary complexity. Every new markdown
   feature requires double implementation.

3. **Cache the MarkdownHighlighter pointer on MarkdownTextItem** instead
   of 5 redundant `findChild` lookups. Single-line fix, eliminates a
   fragile pattern.

### Medium Priority

4. **Move hardcoded colors into Theme.** BlockItem selection overlay,
   TableBlockItem grid colors, MarkdownTextItem code block background,
   and callout colors should all be Theme-driven.

5. **Extract layout constants** (`m_spacing`, `m_leftMargin`, margins,
   scroll step) into EditorSettings or a LayoutConfig struct.

6. **Mark implemented specs as "Status: Implemented"** with a note at
   the top. Don't delete them — the design rationale is valuable — but
   make it clear they describe a completed design, not a future plan.

7. **Archive TODO-tables.md** (move to an `archive/` directory or add
   a "SUPERSEDED" header).

8. **Add a tst_highlighter.cpp** test covering at least: heading colors,
   bold/italic formats, code block state tracking, delimiter visibility
   near cursor, and math span detection.

### Low Priority

9. **Unify the Mode enum** between Editor and MarkdownHighlighter into
   a single definition (e.g., in a shared types header).

10. **Return `std::unique_ptr<QMimeData>`** from
    `SelectionManager::createMimeData()` to make ownership explicit.

11. **Extract math delimiter parsing helper** from the duplicated sites
    in MarkdownTextItem.

12. **Add performance benchmarks** for large documents (10k+ lines) to
    catch regressions in the reparse/rehighlight cycle.

---

## Codebase Statistics

| Metric | Value |
|--------|-------|
| Total source lines | ~11,100 |
| Source files | 43 (in src/) |
| Public headers | 7 (in include/markoff/) |
| Test files | 12 |
| Documentation files | 32 |
| External dependencies | 5 (Qt6, KF6, tree-sitter, md4c, jkqtmathtext) |
| Largest file | TextControl.cpp (2,550 lines, forked from Qt) |
| Largest original file | Editor.cpp (964 lines) |
