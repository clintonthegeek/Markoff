# Cross-Boundary Selection Research

Companion to `2026-04-02-graphicsview-editor-design.md`.

## Key Finding: Calligra Doesn't Solve Our Problem

Calligra Words uses one QTextDocument shared across multiple visual
shapes. Text flows between shapes via KWTextFrameSet. Selection is
just a QTextCursor range within that single document — it naturally
spans shape boundaries because shapes are viewports into the same
document. Tables in Calligra are QTextTable objects inline in the
document flow, same approach we're abandoning.

Our case is fundamentally different: each text region is a separate
QTextDocument, and tables are not in any QTextDocument. We need
genuine cross-item selection.

## The Mouse Grab Problem

From Qt source (`qgraphicsscene.cpp` lines 1315-1434):

Once a QGraphicsTextItem receives a mouse press, it holds an
implicit mouse grab for the entire drag. The scene sends ALL
subsequent mouse move events to that same item, regardless of
where the cursor is. Cross-boundary drag cannot work through
normal event routing.

**Solution**: Override scene mouse events. When the drag exits the
original grabber's bounding rect, call `ungrabMouse()` on the
grabber and take over selection management at the scene level.

## Architecture: SelectionManager on the Scene

### State Model

```cpp
struct SelectionAnchor {
    QGraphicsItem *item = nullptr;
    int textPosition = -1;    // char pos in text item, -1 for non-text
    QPointF scenePos;
};

struct ItemSelectionState {
    QGraphicsItem *item = nullptr;
    bool fullySelected = false;
    int textSelStart = -1;    // -1 = from beginning
    int textSelEnd = -1;      // -1 = to end
};
```

### Mouse Event Flow

1. **Press**: Record anchor (item + text position). Let Qt handle
   normally (sets up grab, text cursor).

2. **Move**: Check if cursor left the grabber's bounding rect.
   If yes: enter cross-boundary mode.
   - Call `mouseGrabberItem()->ungrabMouse()` to release the grab
   - Compute which items are between anchor and current position
   - For each item: set selection state (partial for first/last,
     full for middle items)

3. **Release**: Commit selection. Copy state for clipboard use.

### Cross-Boundary Selection Algorithm

Given anchor and endpoint scene positions:

```
ordered = all items sorted by Y position
anchorIdx = index of anchor item
endIdx = index of endpoint item
forward = endIdx >= anchorIdx

for i in range(min, max):
    if i == first:
        text item: select from anchor to end (forward) or start to anchor (backward)
        non-text: fully selected
    elif i == last:
        text item: select from start to endpoint (forward) or endpoint to end (backward)
        non-text: fully selected
    else:
        fully selected (middle item)
```

### Visual Rendering

- **Text items**: Use `QGraphicsTextItem::setTextCursor()` with a
  selection range. Qt paints the selection highlight natively.

- **Non-text items** (tables, images, code blocks): Paint a semi-
  transparent blue overlay (`QColor(51, 153, 255, 80)`) when
  `fullySelected` flag is set.

### Clipboard (Ctrl+C)

Intercept in `QGraphicsScene::keyPressEvent`:

```
for each selected item in document order:
    if text item:
        if fully selected: append sourceMarkdown()
        else: map rendered positions back to source markdown,
              extract substring
    if table: append toMarkdown() (pipe text)
    if code block: append toMarkdown() (fenced block)
    if image: append toMarkdown() (![alt](path))
```

Put concatenated result on clipboard as text/plain. The clipboard
always contains raw markdown, never rendered text.

### Rendered-to-Source Position Mapping

The hardest sub-problem. When the user selects rendered text, the
character positions are in the rendered QTextDocument (where `**`
markers are hidden, `[text](url)` shows only "text", etc.). We
need to map these back to source markdown positions.

Each MarkdownTextItem stores its source markdown and a mapping
table built during rendering:

```
(sourceStart, sourceEnd, renderedStart, renderedEnd)
```

For delimiter-hidden spans (bold markers, link syntax), the rendered
range is shorter than the source range. The mapping table allows
converting any rendered character position to a source position.

The existing `SourceSpan` system in the codebase already tracks
these relationships — it has `charOffset`/`charLength` for rendered
positions and `utf8Offset`/`utf8Length` for source positions. This
mapping can be reused.

## Edge Cases

### Shift+Click

Keep existing anchor, update endpoint, enter cross-boundary mode.
Works naturally with the algorithm above.

### Ctrl+A (Select All)

Mark all items as fully selected. Set anchor to first item position
0, endpoint to last item end.

### Keyboard Selection (Shift+Arrow Across Boundaries)

Text items emit `selectionBoundaryReached(item, direction)` when
Shift+arrow hits the document boundary (cursor.movePosition returns
false while shift is held). The scene extends the cross-boundary
selection into the adjacent item.

### Triple-Click

Handled within a single text item (selects paragraph). If user then
Shift+clicks elsewhere, cross-boundary selection extends from the
triple-click range.

## Prior Art: Daino Notes Block Editor

The closest prior art. Uses a ListView where each block is a
delegate. Their cross-block selection:

- Two global indices (selectionStartIndex, selectionEndIndex) track
  which blocks are in the selection range
- Each delegate independently evaluates its selection state
- For clipboard: converts rendered HTML back to markdown

Our approach is similar but uses QGraphicsScene items instead of
ListView delegates, and we maintain source-to-rendered position
mapping instead of HTML-to-markdown conversion.

## Rubber-Band Selection: Not Viable

QGraphicsView's rubber-band selection provides item-level selection
only (whole items selected or not). It conflicts with text
interaction ("you can't select text with the mouse — a rubber band
will be drawn in the background instead"). It gives no positional
information within text items. Not usable for our case.

However, `QGraphicsScene::items(QPainterPath)` can be used to
determine which items intersect a selection region — useful as a
helper for the SelectionManager.
