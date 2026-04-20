# Code Folding — Kate Harvest Notes

> **Purpose:** Reference document for future spec/plan authors. Summarises what Kate has, what's reusable, and what markoff-specific adaptation is needed.

## Kate Architecture (3 layers)

### 1. Fold State — `Kate::TextFolding`

**Files:**
- `~/src/kde/src/ktexteditor/src/buffer/katetextfolding.h`
- `~/src/kde/src/ktexteditor/src/buffer/katetextfolding.cpp`

Self-contained class managing a tree of fold ranges. Key API:

| Method | Purpose |
|--------|---------|
| `newFoldingRange(Range, Flags)` | Register a foldable region |
| `foldRange(id)` | Collapse a range |
| `unfoldRange(id, remove)` | Expand a range |
| `isLineVisible(line, *foldedRangeId)` | O(log n) binary search |
| `lineToVisibleLine(line)` | Buffer line -> visible line |
| `visibleLineToLine(visibleLine)` | Visible line -> buffer line |
| `foldingRangesStartingOnLine(line)` | Get fold markers for gutter |
| `editEnd(startLine, endLine, isLineFoldingStart)` | Re-validate after edits |
| `exportFoldingRanges()` / `importFoldingRanges()` | JSON persistence |

**Data structure:** Nested tree of `FoldingRange` objects, each holding two `TextCursor` pointers (auto-track edits), parent/child links, and flags (`Persistent`, `Folded`). Top-level ranges sorted and non-overlapping. Binary search on `m_foldedFoldingRanges` for visibility.

**Reusability:** High. This class is largely independent of the rest of KTextEditor. The main dependency is `Kate::TextCursor` for position tracking — markoff would substitute `QTextCursor` or plain line numbers.

### 2. Fold Range Computation — `KateBuffer::computeFoldingRangeForStartLine()`

**File:** `~/src/kde/src/ktexteditor/src/document/katebuffer.cpp` (lines 473-619)

Two strategies:

**Token-based** (braces, regions): Reference-counts begin/end markers from syntax highlighting. Searches forward from start line until matching close found.

**Indentation-based** (Python, lists): Scans forward until a non-empty line with indent <= start line's indent.

For markdown:
- **Headings** — token-based. `markdown.xml` uses `beginRegion="H1"` / `endRegion="H1"` etc. H1 folds until next H1, H2 until next H1 or H2.
- **Fenced code blocks** — token-based. `beginRegion="code-block"` / `endRegion="code-block"`.
- **Lists** — indentation-based would work for nested lists.
- **Block quotes** — could use indentation-based.

**Reusability:** The algorithm is reusable. Markoff already has tree-sitter parse data identifying headings, code blocks, etc. — these can generate fold regions directly without going through KSyntaxHighlighting's region system.

### 3. Gutter Rendering — `KateIconBorder`

**File:** `~/src/kde/src/ktexteditor/src/view/kateviewhelpers.cpp` (lines 1753-1799, 2194-2226)

Queries `foldingRangesStartingOnLine()` per visible line. Draws:
- Open triangle (downward) when expanded
- Closed triangle (rightward) when folded

Uses QPainter with antialiasing, color from syntax theme.

**Reusability:** The triangle painting is ~50 lines, directly reusable. The per-line query pattern maps cleanly to markoff's item-based layout.

## Markdown Fold Regions (from KSyntaxHighlighting)

**File:** `~/src/kde/src/syntax-highlighting/data/syntax/markdown.xml`

```xml
<!-- Headers end previous region, begin new one -->
<RegExpr attribute="Header H1" String="^#\s" endRegion="H1" beginRegion="H1"/>
<RegExpr attribute="Header H2" String="^##\s" endRegion="H2" beginRegion="H2"/>

<!-- Code blocks -->
<RegExpr attribute="Fenced Code" String="..." beginRegion="code-block"/>
<RegExpr attribute="Fenced Code" String="..." endRegion="code-block"/>
```

Key insight: Headers use the same ID for `endRegion` and `beginRegion`, creating implicit section boundaries.

## Markoff Adaptation Notes

### What's different

1. **Block model:** Markoff splits the document into multiple `MarkdownTextItem` QGraphicsObjects. Folding hides entire items rather than toggling `QTextBlock::setVisible()`.

2. **Tree-sitter provides structure:** Markoff already has a full parse tree. Fold regions can be derived from tree-sitter node types (headings, fenced_code_block, list, block_quote) rather than syntax highlighting regions.

3. **Item layout:** The `SceneCoordinator` manages item positions. Folding means removing items from the visible layout and inserting a fold placeholder item (showing "# Heading ... (N lines)").

### Suggested approach

1. **FoldingModel** class (adapted from `TextFolding`): Owns the fold state tree. Fed ranges from tree-sitter parse results. Emits signals on fold/unfold.

2. **SceneCoordinator integration**: On fold change, hide affected items (or remove from scene) and insert a fold summary item. On unfold, reverse.

3. **Fold gutter**: Either a dedicated gutter item per text item, or an overlay painted by the `SceneCoordinator`.

4. **Fold range computation**: Walk the tree-sitter AST:
   - `atx_heading` (level N) → fold until next heading of level <= N or EOF
   - `fenced_code_block` → fold the block content
   - `block_quote` → fold entire quote
   - `list` → fold nested items

5. **Persistence**: JSON export/import of fold state, keyed by heading text or line offset.

### Performance

| Operation | Kate | Markoff equivalent |
|-----------|------|--------------------|
| `isLineVisible()` | O(log n) binary search | O(log n) on sorted fold list |
| `foldRange()` | O(1) flag toggle + signal | O(1) flag toggle + item hide |
| Layout update | Repaint visible lines | `SceneCoordinator::relayout()` |

### Open questions for spec

- Should folding persist across editor reloads?
- Fold-all / unfold-all commands?
- Fold level filtering (fold all H2+, keep H1 visible)?
- Interaction with search — auto-unfold when match is in folded region?
- Visual treatment of fold placeholder (just text? clickable? preview on hover?)
