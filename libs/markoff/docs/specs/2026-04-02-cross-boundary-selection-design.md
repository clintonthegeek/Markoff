# Cross-Boundary Selection Design

> **Status: IMPLEMENTED** — SelectionManager, SelectableItem, and
> SelectionScene all shipped. Retained for design rationale.

Companion to `2026-04-02-graphicsview-editor-design.md`.
Supersedes `2026-04-02-cross-boundary-selection-research.md` (archived).

## Problem

In a QGraphicsScene with multiple independent text items and non-text
items (tables, code blocks, images), Qt's implicit mouse grab locks
all drag events to the initially-pressed item. There is no built-in
mechanism for selection across item boundaries.

## Approach

A `SelectionManager` class that lives on the scene and takes over
mouse event handling when a drag crosses an item boundary. It uses
three confirmed-safe Qt mechanisms:

1. **`ungrabMouse()`** on the grabber item to break the implicit
   grab (`qgraphicsscene.cpp:959`). After ungrab, the scene's base
   `mouseMoveEvent()` returns immediately for buttons-pressed moves
   (`qgraphicsscene.cpp:3982-3984`), so the override must handle
   cross-boundary moves without calling the base class.

2. **`setTextCursor()`** with programmatic selections on text items
   (`TextControl.cpp:104`). Replaces the cursor including selection
   state, triggers repaint of selection highlight, emits
   `selectionChanged()`.

3. **`setTextInteractionFlags(Qt::NoTextInteraction)`** on the
   originating text item during cross-boundary drag (the "Gutenberg
   trick"). Cleanly disarms the item's mouse handling so it cannot
   interfere. Restored on release.

The SelectionManager is a plain QObject owned by the scene. It does
not subclass QGraphicsScene — the scene subclass delegates mouse
events to it.

## Item Contract: SelectableItem

Every item in the scene implements this interface so the
SelectionManager can operate on heterogeneous items uniformly:

```cpp
class SelectableItem {
public:
    virtual ~SelectableItem() = default;

    // The underlying QGraphicsItem
    virtual QGraphicsItem *asGraphicsItem() = 0;

    // Text items return true; tables, images, code blocks return false
    virtual bool isTextItem() const = 0;

    // --- Text item operations ---

    // Hit-test scene coords → char position (-1 if miss)
    virtual int hitTest(const QPointF &scenePos) const { return -1; }

    // Set a selection range programmatically (anchor, cursor)
    virtual void setSelection(int anchorPos, int cursorPos) {}

    // Clear any active selection
    virtual void clearSelection() {}

    // Get selected text as raw markdown
    virtual QString selectedMarkdown() const { return {}; }

    // Get all text as raw markdown
    virtual QString allMarkdown() const { return {}; }

    // --- Non-text item operations ---

    // Set/get fully-selected state (item paints blue overlay)
    virtual void setFullySelected(bool selected) {}
    virtual bool isFullySelected() const { return false; }

    // --- Common ---

    // Serialize to markdown (all items must implement)
    virtual QString toMarkdown() const = 0;
};
```

The SelectionManager only talks to items through this interface.
It never needs to know whether it's dealing with a table, image,
or code block — just "text item" vs "non-text item."

MarkdownTextItem implements `hitTest`, `setSelection`,
`selectedMarkdown`, `allMarkdown`, and `toMarkdown` (identical to
`allMarkdown` for text items).

Non-text BlockItem subclasses implement `setFullySelected` and
`toMarkdown`. Their `toMarkdown()` returns the original syntax:
pipe tables, fenced code blocks, `$$...$$`, `![alt](path)`, etc.

## State Model

```cpp
enum class SelectionMode { None, WithinItem, CrossBoundary };

struct SelectionState {
    SelectionMode mode = SelectionMode::None;

    // Anchor (where drag started)
    SelectableItem *anchorItem = nullptr;
    int anchorTextPos = -1;

    // Current (where cursor is now)
    SelectableItem *currentItem = nullptr;
    int currentTextPos = -1;

    // Cached ordered item list (populated on entering CrossBoundary)
    QList<SelectableItem *> orderedItems;
};
```

Three modes:

- **None**: No active drag. All mouse events pass through to Qt.
- **WithinItem**: Dragging inside the originating item. Qt's native
  grab handles selection. SelectionManager just watches.
- **CrossBoundary**: Drag has left the originating item. Grab
  released. SelectionManager owns all selection state.

Transitions:

```
None → WithinItem:           mouse press on any item
WithinItem → CrossBoundary:  mouse move exits originating item
WithinItem → None:           mouse release (normal Qt handling)
CrossBoundary → None:        mouse release (we commit and clean up)
```

The `orderedItems` list is populated once on entering CrossBoundary
mode by querying the scene coordinator for items sorted by Y
position. This avoids repeated scene queries during drag.

## Mouse Event Flow

The scene subclass overrides three methods and delegates to the
SelectionManager.

### mousePressEvent

Record anchor, let Qt handle normally.

1. Hit-test `scenePos` to find which SelectableItem was pressed.
2. If text item: `hitTest(scenePos)` → `anchorTextPos`.
3. Store anchor, set `mode = WithinItem`.
4. Call base class. Qt establishes implicit grab; text cursor is set.

### mouseMoveEvent

The critical override.

```
If mode == None:
    call base class, return

If mode == WithinItem:
    if scenePos is inside anchorItem's boundingRect:
        call base class (normal within-item selection), return
    else TRANSITION to CrossBoundary:
        1. anchorItem->asGraphicsItem()->ungrabMouse()
        2. if anchorItem is text:
               setTextInteractionFlags(Qt::NoTextInteraction)
        3. populate orderedItems from scene coordinator
        4. mode = CrossBoundary
        5. fall through to cross-boundary handling

If mode == CrossBoundary:
    1. find which item scenePos is over (walk orderedItems by Y)
    2. if text item: currentTextPos = currentItem->hitTest(scenePos)
    3. call applySelection()
    4. do NOT call base class
```

### mouseReleaseEvent

```
If mode == CrossBoundary:
    1. if anchorItem is text:
           restore setTextInteractionFlags(Qt::TextEditorInteraction)
    2. mode = None
    3. do NOT call base class
    return

Otherwise:
    mode = None
    call base class
```

The happy path (within-item selection) stays entirely on Qt's native
code path. The SelectionManager only takes control when the drag
actually crosses a boundary.

## Selection Application Algorithm

`applySelection()` is called on every mouse move in CrossBoundary
mode. It walks the ordered items and sets each one's selection state.

```
anchorIdx = orderedItems.indexOf(anchorItem)
currentIdx = orderedItems.indexOf(currentItem)
forward = currentIdx >= anchorIdx
lo = min(anchorIdx, currentIdx)
hi = max(anchorIdx, currentIdx)

for i in 0..orderedItems.size():
    item = orderedItems[i]

    if i < lo or i > hi:
        // Outside selection range
        if item.isTextItem(): item.clearSelection()
        else: item.setFullySelected(false)

    else if i == anchorIdx and i == currentIdx:
        // Same item (user dragged back into anchor item)
        if item.isTextItem(): item.setSelection(anchorTextPos, currentTextPos)
        else: item.setFullySelected(true)

    else if i == anchorIdx:
        // First item — partial from anchor to edge
        if item.isTextItem():
            if forward: item.setSelection(anchorTextPos, END)
            else:        item.setSelection(anchorTextPos, 0)
        else: item.setFullySelected(true)

    else if i == currentIdx:
        // Last item — partial from edge to cursor
        if item.isTextItem():
            if forward: item.setSelection(0, currentTextPos)
            else:        item.setSelection(END, currentTextPos)
        else: item.setFullySelected(true)

    else:
        // Middle item — fully selected
        if item.isTextItem(): item.setSelection(0, END)
        else: item.setFullySelected(true)
```

`END` means `document()->characterCount() - 1`.

Non-text items at the anchor or current position are always fully
selected since they have no character-level granularity.

Visual result:
- Text items show native Qt selection highlights (via QTextCursor
  with anchor/position, rendered through `getPaintContext()`)
- Non-text items paint a semi-transparent blue overlay
  (`QColor(51, 153, 255, 80)`) when `fullySelected` is true

## Clipboard Serialization

Ctrl+C delegates to `SelectionManager::createMimeData()`:

```cpp
QMimeData *SelectionManager::createMimeData() const
{
    auto *data = new QMimeData;
    data->setText(serializeAsMarkdown());
    // Future: data->setHtml(serializeAsHtml());
    return data;
}
```

`serializeAsMarkdown()` walks selected items in order:

```
lo = min(anchorIdx, currentIdx)
hi = max(anchorIdx, currentIdx)

for i in lo..hi:
    item = orderedItems[i]

    if i == anchorIdx or i == currentIdx:
        // Partial selection at edges
        if item.isTextItem(): result += item.selectedMarkdown()
        else: result += item.toMarkdown()
    else:
        // Fully selected middle item
        if item.isTextItem(): result += item.allMarkdown()
        else: result += item.toMarkdown()

clipboard->setMimeData(data)
```

Since each MarkdownTextItem holds raw markdown (the highlighter
applies visual formatting but does not alter the underlying text),
`selectedMarkdown()` returns `QTextCursor::selectedText()` directly.
No rendered-to-source position mapping is needed.

For non-text items, `toMarkdown()` serializes to the original syntax:
pipe tables, fenced code blocks, `$$...$$`, `![alt](path)`, etc.

The clipboard always contains valid markdown.

**Ctrl+X (Cut)**: Same serialization, then delete. For text items,
`QTextCursor::removeSelectedText()`. For fully-selected non-text
items, remove from scene and adjust the coordinator. If the cut
removes a non-text item between two adjacent text items, merge
them.

**Future formats**: When HTML/RTF export is needed, add
`serializeAsHtml()` alongside `serializeAsMarkdown()` and set both
MIME types on the QMimeData. Add `toHtml()` to the SelectableItem
interface at that point.

## Edge Cases

### Shift+Click Across Items

If `mode == None` and Shift is held during press, check if there's
an existing anchor from a previous within-item selection. If so,
skip WithinItem mode and jump straight to CrossBoundary using the
existing anchor and the new click position as current.

### Ctrl+A (Select All)

Scene `keyPressEvent` catches Ctrl+A. Sets `anchorItem` = first
item, `anchorTextPos` = 0, `currentItem` = last item,
`currentTextPos` = END. Calls `applySelection()`. Mode =
CrossBoundary.

### Shift+Arrow at Text Item Boundary

Text items emit `cursorAtBoundary(direction)` when Shift+Arrow
can't move further (QTextCursor::movePosition returns false while
Shift is held). The scene catches this signal and extends the
cross-boundary selection into the adjacent item — same
`applySelection()` logic, driven by keyboard instead of mouse.

### Escape

Clears all selection across all items, mode = None.

### Click Without Shift

Any new press without Shift clears the cross-boundary selection
and starts fresh.

### Drag Over Empty Space

If scenePos is below all items or in margins, `currentItem` stays
as the nearest item (last item for below, first for above).
Selection extends to the nearest edge. No crash.

### Drag Reversal

User drags down past several items, then drags back up past the
anchor. The forward/backward flag is recomputed on every move.
`applySelection()` clears items that are no longer in range and
updates partial selections on the new edges.

## Codebase Fitness

### What We Reuse

| Component | Status | Notes |
|-----------|--------|-------|
| `TextControl` | **Ready** | Already forked from Qt. `setTextCursor()`, `hitTest()`, `getPaintContext()` all confirmed working. Used inside MarkdownTextItem. |
| `TextControlPrivate` | **Ready** | Selection state (cursor anchor/position), repaint optimization (`repaintOldAndNewSelection`), focus handling all present. |
| `MarkdownHighlighter` | **Ready** | Runs independently per QTextDocument. No changes needed — each MarkdownTextItem gets its own highlighter instance. |
| `TreeSitterParser` | **Ready** | Parses markdown, builds span map. Used by the scene coordinator for splitting, not by SelectionManager. |
| `SourceSpan` | **Ready** | Formatting spans with char offsets. Used by highlighter, not directly by selection. |
| `TableHandler::detectTables` | **Reuse parsing** | Table detection and `parseRow`/`parseAlignment` reused by TableItem. The `convertToQTextTable` and `serializeToMarkdown` methods are not needed (TableItem owns its own data model and serialization). |
| `DecoratedRange` | **Adapt** | Currently tracks block ranges in a single QTextDocument. In the new architecture, each MarkdownTextItem detects its own decorated ranges within its local document. The struct itself is unchanged. |

### What We Build New

| Component | Responsibility |
|-----------|---------------|
| `SelectableItem` | Interface (pure virtual). ~30 lines. |
| `SelectionManager` | State model, mouse event handling, `applySelection()`, `createMimeData()`. ~300 lines. |
| `MarkdownTextItem` | QGraphicsItem wrapping TextControl. Implements SelectableItem for text. Needs `hitTest`, `setSelection`, `selectedMarkdown`, `allMarkdown`. ~200 lines. |
| `BlockItem` | Base class for non-text items. Implements SelectableItem for `setFullySelected`, overlay painting. ~100 lines. |

The SelectionManager is the only genuinely new system. The item
classes are thin wrappers around existing infrastructure
(TextControl for text, custom paint for non-text).

### What We Delete

Nothing from the current codebase is deleted for the
SelectionManager itself. The parent spec
(`graphicsview-editor-design.md`) defines what gets deleted when
the full QGraphicsView migration happens (Editor.cpp,
PlainTextDocumentLayout, QTextTable conversion, etc.). The
SelectionManager is additive.

### Starting Over vs Building On

**We build on the existing codebase.** The core text editing
infrastructure — TextControl, MarkdownHighlighter, TreeSitterParser,
SourceSpan, TableHandler parsing — is sound and directly reusable.
The SelectionManager is a new layer that sits above these components.

The current `Editor.cpp` (QAbstractScrollArea + single
QTextDocument) will be replaced by the QGraphicsView architecture
from the parent spec, but that replacement reuses the same
TextControl, highlighter, and parser. The SelectionManager plugs
into the new scene architecture.

There is nothing in the current codebase that blocks or conflicts
with this design. We add, we don't restart.

## Qt Source Dependencies

These are the Qt internals we rely on, confirmed from
`~/src/qtbase/`:

| Mechanism | Qt Source Location | Risk |
|-----------|-------------------|------|
| Implicit mouse grab | `qgraphicsscene.cpp:1406` | None — public API (`ungrabMouse()` on QGraphicsItem) |
| Grab release | `qgraphicsscene.cpp:959-1001` | None — documented behavior |
| Post-ungrab move swallow | `qgraphicsscene.cpp:3982-3984` | Low — internal, but stable since Qt 4. We don't depend on it; we just don't call base class. |
| `setTextCursor()` | `TextControl.cpp:104` (our fork) | None — we own this code |
| `hitTest()` on document layout | `QAbstractTextDocumentLayout::hitTest()` | None — public API |
| Text interaction flags | `QGraphicsTextItem::setTextInteractionFlags()` | None — public API |

No private Qt headers needed. No `_p.h` includes. The only
"internal" knowledge is understanding that the base class
`mouseMoveEvent` swallows events after ungrab — and we handle that
by simply not calling the base class.
