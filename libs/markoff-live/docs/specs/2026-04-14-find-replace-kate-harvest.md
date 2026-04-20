# Find/Replace — Kate Harvest Notes

> **Purpose:** Reference document for future spec/plan authors. Summarises what Kate has, what's reusable, and what markoff-specific adaptation is needed.

## Kate Architecture

All search code lives in `~/src/kde/src/ktexteditor/src/search/`. Six core files, all compact.

### Core Search Algorithms

**`KatePlainTextSearch`** (~125 lines)
- Files: `kateplaintextsearch.h`, `kateplaintextsearch.cpp`
- Line-by-line `QString::indexOf()` / `lastIndexOf()`
- Handles single-line and multi-line patterns
- Case sensitivity via `Qt::CaseSensitivity`
- Forward and backward search
- Returns `KTextEditor::Range(line, col, line, col + len)`

**`KateRegExpSearch`** (~500 lines)
- Files: `kateregexpsearch.h`, `kateregexpsearch.cpp`
- Wraps `QRegularExpression` with `UseUnicodePropertiesOption`
- Returns `QList<Range>` — one per capture group
- Replacement supports:
  - `\1`, `\2` — capture group references
  - `\U`, `\u`, `\L`, `\l` — case conversion
  - `\#N#` — replacement counter with padding
- Special: rewrites `\s` to `[ \t]` to prevent cross-line matching unless explicit

**`KateMatch`** (~78 lines)
- Files: `katematch.h`, `katematch.cpp`
- Coordinator: picks plain text or regex search based on options
- Owns the search result range
- `replace()` method applies replacement text with capture group expansion

### Search Bar UI

**`KateSearchBar`** (~1050 lines + 2 `.ui` files)
- Files: `katesearchbar.h`, `katesearchbar.cpp`
- Derives from `KateViewBarWidget` (docked at bottom of view)
- Two modes toggled at runtime:

**Incremental mode** (`searchbarincremental.ui`, 172 lines):
- Find field (editable QComboBox)
- Next/Previous buttons
- Match case checkbox
- Status label ("Not found", "Reached bottom", etc.)
- Real-time as-you-type: searches from initial cursor pos, wraps around

**Power mode** (`searchbarpower.ui`, 345 lines):
- Find + Replace fields
- Mode selector: Plain Text / Whole Words / Escape Sequences / Regex
- Case sensitivity, in-selection toggles
- Replace, Replace All, Find All buttons
- Cancel button (QStackedWidget swap during long operations)

### Match Highlighting

Uses `KTextEditor::MovingRange` with `KTextEditor::Attribute`:

```cpp
auto *highlight = doc->newMovingRange(range, DoNotExpand);
highlight->setView(m_view);          // per-view
highlight->setZDepth(-10000.0);      // below text
highlight->setAttribute(highlightMatchAttribute);
m_hlRanges.append(highlight);
```

Two attributes: one for find matches (typically yellow), one for replacements. Hard cap of **65536 highlights** to prevent perf degradation.

### Replace-All Atomicity

All replacements wrapped in a single undo group:

```cpp
if (m_matchCounter == 0)
    documentPrivate->editStart();     // beginEditBlock equivalent

// ... all replacements happen here ...

documentPrivate->editEnd();           // endEditBlock equivalent
doc->undoManager()->undoSafePoint();  // prevent merge with user edits
```

### Time-Slicing for Large Documents

Replace-all processes 50k lines per chunk, then yields to the event loop:

```cpp
if (numLinesSearched >= 50000) {
    QTimer::singleShot(0, this, &KateSearchBar::findOrReplaceAll);
    return;  // continue in next event loop iteration
}
```

Shows Cancel button during long operations.

### Search Wrapping

Manual wrap with user prompt (power mode) or automatic wrap (incremental mode). Visual feedback: "Reached bottom, continued from top" via `showSearchWrappedHint()`.

### Search Options

```cpp
enum SearchMode {
    MODE_PLAIN_TEXT       = 0,
    MODE_WHOLE_WORDS      = 1,
    MODE_ESCAPE_SEQUENCES = 2,  // \n, \t substitution
    MODE_REGEX            = 3
};
```

Maps to `KTextEditor::SearchOptions` flags: `Default`, `CaseInsensitive`, `Backwards`, `WholeWords`, `Regex`, `EscapeSequences`.

## Markoff Adaptation Notes

### What's different

1. **Multi-item document:** Markoff splits markdown into multiple `MarkdownTextItem` objects. Search must cross item boundaries. Markoff already has `Editor::findText()` iterating `textItemsInSearchOrder()` — this is the integration point.

2. **No `KTextEditor::Document` interface:** Kate's search algorithms take a document interface with `line(int)` and `lines()`. Options:
   - Adapter pattern: wrap markoff's item list in a document-like interface
   - Direct approach: iterate items, search within each, handle cross-boundary matches

3. **Highlight rendering:** Kate uses `MovingRange`. Markoff would use `QTextEdit::ExtraSelection` on each `TextControl`, or paint overlays in the `MarkdownTextItem::paint()` method.

### Existing markoff search infrastructure

```cpp
// Editor.h — already has:
bool findText(const QString &find, QTextDocument::FindFlags flags);
bool findAndReplace(const QString &find, const QString &replace,
                    QTextDocument::FindFlags flags);

// Uses textItemsInSearchOrder() to iterate across items
```

This provides the cross-item iteration. What's missing:
- Replace-all
- Incremental (as-you-type) highlighting
- Match count display
- Search bar UI
- Regex support

### Suggested approach

1. **Search bar widget:** Adapt Kate's `.ui` files for a `QWidget` docked at the bottom of the `Editor` (QGraphicsView). Start with incremental mode only.

2. **Highlight matches:** Use `QTextCharFormat` with background color, applied via `QTextCursor::mergeCharFormat()` or `TextControl::setExtraSelections()`. Clear on search bar close. Cap at 65536 per Kate.

3. **Replace-all:** Wrap in `beginEditBlock()`/`endEditBlock()` per item. For cross-item atomicity, use a compound undo command or accept per-item undo granularity.

4. **Regex support:** `QRegularExpression` directly — no need for Kate's wrapper unless capture group replacement is needed. Kate's `KateRegExpSearch` is worth stealing if `\U\1` style replacements are desired.

5. **Time-slicing:** Probably unnecessary initially — markdown documents are rarely 50k+ lines. Add if needed.

### Reusable code (ranked by value)

| File | Lines | Value | Notes |
|------|-------|-------|-------|
| `searchbarincremental.ui` | 172 | High | Layout directly usable |
| `searchbarpower.ui` | 345 | High | Layout directly usable |
| `kateplaintextsearch.cpp` | 125 | Medium | Simple, but markoff's existing `QTextDocument::find()` may suffice |
| `kateregexpsearch.cpp` | 500 | Medium | Worth stealing for capture group replacement |
| `katesearchbar.cpp` | 1050 | Low | Too coupled to KTextEditor, but good reference for behavior |

### Tests to reference

- `~/src/kde/src/ktexteditor/autotests/src/searchbar_test.h`
- `~/src/kde/src/ktexteditor/autotests/src/searchbar_test.cpp`

### Open questions for spec

- Start with incremental-only or both modes?
- Regex support in v1 or deferred?
- How to handle matches spanning item boundaries (e.g., multi-line search pattern)?
- Search scope: current item only vs. full document vs. selection?
- Integration with code folding — auto-unfold when match is in folded region?
- Keyboard shortcuts: Ctrl+F (find), Ctrl+H (replace), F3/Shift+F3 (next/prev)?
- Persist search history across sessions?
