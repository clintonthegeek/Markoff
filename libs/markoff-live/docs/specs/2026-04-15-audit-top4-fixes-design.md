# Audit Top-4 Fixes — Design

> **Status:** Approved 2026-04-15.
>
> **Scope:** Markoff-internal. Fixes four public API bugs identified in
> `docs/AUDIT-2026-04-15.md` Part III items 1–4.
>
> **References:**
> - `docs/AUDIT-2026-04-15.md` — code audit that identified these issues
> - `src/SceneCoordinator.h` — item list owner
> - `src/Editor.cpp` — public API implementations
> - `src/SelectionManager.h` — cross-boundary selection

---

## Problem Statement

Editor's public API methods assume the focused `MarkdownTextItem` IS the
document. In reality, the document is distributed across multiple items
managed by `SceneCoordinator`. Four public methods are broken:

1. **`selectAll()`** delegates to `focusedTextItem()->textControl()->selectAll()`,
   selecting within one QTextDocument instead of across all items.
2. **`cut()`** removes selected text from text items but leaves fully-selected
   block items (tables, images) visible in the scene.
3. **`goToLine(int line)`** navigates within the focused item's QTextDocument.
   Line 50 of the global document may be in a completely different item.
4. **`cursorLine()` / `cursorColumn()`** return block number + 1 within the
   focused item, not the global source line/column.

## Design

### Foundation: Coordinate Mapping in SceneCoordinator

All four fixes need the ability to map between global source coordinates
and (item, local position) pairs. Two new public methods on
`SceneCoordinator`:

```cpp
struct GlobalPosition {
    int line;    // 1-based source line in flat markdown
    int column;  // 1-based column within that line
};

GlobalPosition globalPositionOf(const MarkdownTextItem *item,
                                 int localBlockNumber,
                                 int columnInBlock) const;

struct ItemPosition {
    MarkdownTextItem *item = nullptr;
    int localBlockNumber = 0;
    int columnInBlock = 0;
};

ItemPosition itemAtGlobalLine(int globalLine) const;
```

**`globalPositionOf`** walks `m_items` in order. For each item before the
target, counts its source lines:
- Text items: number of QTextBlocks (each block is one source line), but
  blocks containing U+FFFC with multi-line `RawProperty` expand to
  multiple source lines — same expansion logic as `allMarkdown()`.
- Block items: newline count in `toMarkdown()`.
- Inter-item separators: 1 newline between text–text, 2 newlines when
  either neighbor is a block item (matching `toMarkdown()`).

Sums give the line offset. Add `localBlockNumber` and the 1-based
adjustment.

**`itemAtGlobalLine`** performs the inverse: same walk, stop when
cumulative line count reaches `globalLine`, return the item and remaining
lines as local block number.

**Shared helper:** Extract `interItemNewlines(prevIsText, currIsText) → int`
used by both `toMarkdown()` and the mapping methods to guarantee they
agree on separator counts.

**Complexity:** O(n) in item count per call. Item lists are typically
tens to low hundreds of entries — same cost as `repositionItems()` which
already runs on every reparse.

### Fix 1: `selectAll()`

Replace the current implementation:
```cpp
void Editor::selectAll() {
    if (auto *ti = focusedTextItem()) ti->textControl()->selectAll();
}
```

With delegation to `SelectionManager`, matching what the context menu
already does:
```cpp
void Editor::selectAll() {
    auto *mgr = m_scene->selectionManager();
    QKeyEvent e(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    mgr->handleKeyPress(&e);
}
```

`SelectionManager::handleKeyPress` already handles Ctrl+A correctly:
sets anchor to first item position 0, current to last item's end,
enters `CrossBoundary` mode, and calls `applySelection()`.

### Fix 2: `cut()`

After the existing copy + text removal logic, add block item removal:

```cpp
void Editor::cut()
{
    copy();

    // Remove text selections (existing logic)
    for (auto *item : m_coordinator->items()) {
        if (item->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(item);
            QTextCursor c = ti->textControl()->textCursor();
            if (c.hasSelection())
                c.removeSelectedText();
        }
    }

    // NEW: Remove fully-selected block items
    bool structureChanged = false;
    auto &items = m_coordinator->mutableItems();
    for (int i = items.size() - 1; i >= 0; --i) {
        if (!items[i]->isTextItem() && items[i]->isFullySelected()) {
            m_scene->removeItem(items[i]->asGraphicsItem());
            delete items[i]->asGraphicsItem();
            items.removeAt(i);
            structureChanged = true;
        }
    }

    m_scene->selectionManager()->clearSelection();

    if (structureChanged) {
        m_coordinator->repositionItems();
        m_scene->setSelectableItems(m_coordinator->items());
    }
}
```

This requires exposing a `mutableItems()` accessor or a
`removeItemAt(int)` method on `SceneCoordinator`. The latter is
preferable — it encapsulates the scene removal + list update:

```cpp
void SceneCoordinator::removeBlockItem(int index);
```

After removal, `repositionItems()` fixes layout and
`setSelectableItems()` updates the selection manager's item list.

### Fix 3: `goToLine(int globalLine)`

Replace the current item-local implementation with:

```cpp
void Editor::goToLine(int line)
{
    if (!m_coordinator) return;

    auto pos = m_coordinator->itemAtGlobalLine(line);
    if (!pos.item) return;

    pos.item->setFocus();
    QTextCursor cursor(pos.item->document());
    QTextBlock block = pos.item->document()->findBlockByNumber(pos.localBlockNumber);
    if (block.isValid()) {
        cursor.setPosition(block.position());
    } else {
        cursor.movePosition(QTextCursor::End);
    }
    pos.item->textControl()->setTextCursor(cursor);
    ensureFocusedCursorVisible();
}
```

### Fix 4: `cursorLine()` / `cursorColumn()`

Replace both methods:

```cpp
int Editor::cursorLine() const
{
    auto *ti = focusedTextItem();
    if (!ti || !m_coordinator) return 1;

    auto gp = m_coordinator->globalPositionOf(
        ti,
        ti->textControl()->textCursor().blockNumber(),
        ti->textControl()->textCursor().positionInBlock());
    return gp.line;
}
```

`cursorColumn()` requires no change — column is always relative to the
current line, so `positionInBlock() + 1` is correct regardless of which
item the cursor is in. The existing implementation is fine.

## Source Line Counting

The line-counting logic must handle U+FFFC inline objects that expand
to multi-line source (e.g., display math `$$\n...\n$$`). The same
expansion logic already exists in `allMarkdown()` and
`ensureHeadingMap()`. Rather than duplicating it a third time, extract
a static helper:

```cpp
static int sourceLineCount(const MarkdownTextItem *item);
```

This walks the item's QTextBlocks, expanding U+FFFC fragments via
`RawProperty`, and returns the total source line count. Used by
`globalPositionOf`, `itemAtGlobalLine`, and optionally by
`ensureHeadingMap` to replace its inline copy.

## Testing

### New test file: `tests/tst_global_coordinates.cpp`

Tests for the coordinate mapping and all four fixed methods:

**Coordinate mapping:**
- Single text item: `globalPositionOf` returns 1-based line matching block number
- Two text items: second item's line 1 maps to global line (first item's line count + 2)
- Text + block + text: block item's separator newlines counted correctly
- Round-trip: `itemAtGlobalLine(globalPositionOf(item, block, col).line)` returns the same item and block

**selectAll (fix 1):**
- After `selectAll()`, `SelectionManager::mode()` is `CrossBoundary`
- After `selectAll()` + `copy()`, clipboard contains full document text

**cut (fix 2):**
- Cut with a fully-selected StubBlockItem removes it from the scene
- `coordinator->items().size()` decreases by 1
- `toPlainText()` no longer contains the block item's markdown

**goToLine (fix 3):**
- `goToLine(1)` focuses the first text item, cursor at block 0
- `goToLine(N)` where N is in the second text item: focuses correct item, cursor at correct block
- `goToLine(999)` on a short document: cursor at end of last item (clamp, don't crash)

**cursorLine / cursorColumn (fix 4):**
- Cursor in first item, first line: `cursorLine() == 1`
- Cursor in second text item after a 5-line first item: `cursorLine() == 7` (5 lines + 1 separator + 1)
- `cursorColumn()` matches `positionInBlock() + 1` regardless of item

## Files Changed

| File | Change |
|------|--------|
| `src/SceneCoordinator.h` | Add `GlobalPosition`, `ItemPosition` structs; `globalPositionOf()`, `itemAtGlobalLine()`, `removeBlockItem()` methods; `sourceLineCount()` static helper |
| `src/SceneCoordinator.cpp` | Implement new methods; extract `interItemNewlines()` helper; refactor `toMarkdown()` to use it |
| `src/Editor.cpp` | Rewrite `selectAll()`, `cut()`, `goToLine()`, `cursorLine()`, `cursorColumn()` |
| `tests/tst_global_coordinates.cpp` | New test file (~12 test functions) |
| `tests/CMakeLists.txt` | Register new test executable |

## Out of Scope

- `scrollToHeading()` already works correctly (it converts `sourceOffset`
  to line number via UTF-8 byte counting, independent of items).
- `cursorScreenRect()` returns viewport coordinates, which are correct
  regardless of global line numbering.
- Performance optimization (cumulative cache) — not needed at current
  document sizes; can be added later if profiling shows a bottleneck.
