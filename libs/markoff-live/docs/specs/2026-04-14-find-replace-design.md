# Find/Replace Search Bar — Design Spec

**Date:** 2026-04-14
**Status:** Approved
**Scope:** markoff library (embedded in Editor widget)
**Deferred:** Regex, whole-word matching, search-in-selection, escape sequences

## Goal

Add an embedded find/replace bar to the markoff `Editor` widget with incremental (as-you-type) match highlighting, next/prev navigation, match count display ("3 of 17"), and replace/replace-all — all accessible via standard keyboard shortcuts.

## Architecture

### Layout

The `Editor` class (QGraphicsView) wraps itself and a `SearchBar` widget in a `QVBoxLayout` inside a container `QWidget`. The container becomes the widget returned to consumers. The `SearchBar` sits below the viewport, hidden by default. This is transparent to consumers — the `Editor` public API is unchanged.

```
┌─────────────────────────────┐
│  Editor viewport            │
│  (QGraphicsView)            │
│                             │
├─────────────────────────────┤
│  SearchBar (hidden default) │
│  [Find field] [<] [>] 3/17 │
│  [Replace field] [R] [All] │ ← toggled by Ctrl+H
└─────────────────────────────┘
```

### Components

**`SearchBar`** (`QWidget`) — Two-row bar:

- **Find row:** `QLineEdit` (find field) + Prev/Next `QToolButton`s + match case toggle (`QToolButton`, checkable) + match count `QLabel` ("3 of 17" or "No results") + Close `QToolButton`.
- **Replace row:** `QLineEdit` (replace field) + Replace `QToolButton` + Replace All `QToolButton`. Hidden by default, shown when Ctrl+H opens the bar or user clicks a toggle.

**`Editor`** — gains:
- Internal `QVBoxLayout` + container `QWidget` composition
- Shortcut wiring (Ctrl+F, Ctrl+H, Escape, F3, Shift+F3)
- `highlightAllMatches()` method to push `ExtraSelection`s to all text items
- `clearSearchHighlights()` method

## Match Highlighting

Uses `TextControl::setExtraSelections()` which is already fully implemented.

On every keystroke in the find field (incremental search):

1. Iterate all text items via `SceneCoordinator::items()`
2. For each `MarkdownTextItem`, run `QTextDocument::find()` in a loop to collect all match cursors
3. Build a `QList<QTextEdit::ExtraSelection>` with a highlight format (yellow background, or theme-appropriate)
4. Call `textControl()->setExtraSelections(selections)` on each item
5. Track total match count across all items
6. Track current match index (the match at/after cursor in the focused item)
7. Update the "N of M" label
8. Cap at 65536 total highlights to prevent performance issues

The current match (where the cursor is) gets a distinct highlight color (e.g., orange) to distinguish it from other matches (yellow).

On search bar close: call `clearSearchHighlights()` which sets empty extra selections on all items.

## Navigation

**Find Next / Find Prev:** Reuse `Editor::findText(text, flags)` which already:
- Searches across all text items in order
- Starts from cursor position in focused item
- Wraps around the document
- Sets cursor and focus on the matched item

After each find, recompute the current match index by counting matches before the cursor position across all items. Update the "N of M" label.

## Replace

**Replace one:** Reuse `Editor::replaceText()` — replaces current selection if it matches, then advances to next match.

**Replace all:** Fix `Editor::replaceAll()` to wrap each item's replacements in `beginEditBlock()`/`endEditBlock()` for atomic undo. After completion:
- Re-run highlight computation (matches will be gone)
- Display "Replaced N occurrences" in the match count label
- Return replacement count

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Ctrl+F | Show find bar (find-only mode), focus find field. If text is selected, populate find field. |
| Ctrl+H | Show find bar (find+replace mode), focus find field. |
| Enter (in find field) | Find next |
| Shift+Enter (in find field) | Find previous |
| F3 | Find next (works even when find bar is closed, using last search term) |
| Shift+F3 | Find previous |
| Enter (in replace field) | Replace current + find next |
| Escape | Close find bar, clear highlights, return focus to editor |

## Highlight Colors

Two `QTextCharFormat`s:

- **Match highlight:** Yellow background (`#FFFF00` or theme's search highlight color). Applied to all matches.
- **Current match highlight:** Orange background (`#FF9632` or theme's active search highlight). Applied to the match at cursor.

Colors should respect the editor's theme/palette. Use `QPalette` colors where possible, with fallback to the above defaults.

## Edge Cases

- **Empty find field:** Clear all highlights, show no count.
- **No matches:** Label shows "No results". No highlights.
- **Document changes while search is active:** Re-run highlight computation on document content changes (`contentsChanged` signal).
- **Find bar opened with selection:** Populate find field with selected text.
- **Single-character search:** Works normally — highlight all occurrences.
- **Very large documents:** 65536 highlight cap. Label shows "65536+ matches" if cap is hit.

## Files

| File | Type | Purpose |
|------|------|---------|
| `include/markoff/SearchBar.h` | New | SearchBar widget header |
| `src/SearchBar.cpp` | New | SearchBar implementation (~250-350 lines) |
| `include/markoff/Editor.h` | Modify | Add search-related methods, SearchBar forward decl |
| `src/Editor.cpp` | Modify | Layout composition, highlight methods, shortcut wiring, replaceAll fix |
| `tests/tst_search_bar.cpp` | New | Tests |
| `tests/CMakeLists.txt` | Modify | Register test |

## Testing

- **Highlight count:** Load document, search for known term, verify match count.
- **Navigation:** Find next advances cursor, find prev goes backward, wrapping works.
- **Replace:** Replace one replaces current match and advances.
- **Replace all + undo:** Replace all on multi-item doc, verify count, single undo restores all.
- **Incremental:** Changing find text updates highlights.
- **Edge cases:** Empty search clears highlights, no-match shows correct label.

## Kate Reference

Detailed harvest notes: `libs/markoff/docs/specs/2026-04-14-find-replace-kate-harvest.md`

Key reusable patterns:
- ExtraSelections for highlighting (same approach)
- 65536 highlight cap
- `editStart()`/`editEnd()` wrapping for replace-all atomicity
- Incremental search from cursor position with wrap
