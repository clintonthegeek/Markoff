# Cross-block selection spike — findings

**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Spike location:** `.spike/cross-block-selection/`
**Context:** Pre-design derisking for the Live Render walking skeleton (`docs/specs/2026-04-29-live-render-design.md`, drafted alongside).

The legacy `markoff-live` editor accumulated a class of bugs that all traced to the same architecture: many text-edit components glued together with custom focus/selection routines + a forked `QWidgetTextControl`. Before committing to a delegate-per-AST-block architecture in QML — which structurally also has multiple text-edit components — we built a focused spike to answer one question:

> Can we render a multi-block document as separate `TextEdit` delegates *and* still deliver native-feeling cross-block selection — drag, copy, off-screen tracking — without forking Qt internals or accumulating bandages?

The spike answered: **yes**, with one new C++ abstraction (`SelectionModel`) of ~90 lines, a `MouseArea` overlay that drives it, and per-delegate `Connections` handlers that follow the model. *No fork. No reentrancy hell. No `setFocusProxy`.* The behavior matches a native text editor end-to-end: in-block selection, cross-block selection, mouse drag past every window edge, lateral column tracking through the gaps between delegates.

This document captures every nuance we hit, in detail. Future us will read this before starting the production `LiveSelectionModel`.

---

## 1. Architecture (what works)

```
ApplicationWindow
└── ColumnLayout
    ├── header bar (status + Copy + Clear buttons)
    └── ListView
        ├── delegate: TextEdit (readOnly: true; selectByMouse: false)
        │     └── Connections { target: selModel; onSelectionChanged ... }
        └── MouseArea (anchors.fill: parent; preventStealing: true)
              └── handles all mouse press / drag / release; calls SelectionModel.begin/extend
```

Three load-bearing pieces:

1. **`SelectionModel` (C++)** — owns `(anchorBlock, anchorOffset, activeBlock, activeOffset)` and emits `selectionChanged`. Provides `rangeForBlock(blockIndex)` returning `(start, end)` or `(-1, -1)` for "no selection in this block". Provides `collectSelectedText(blockTexts)` for clipboard assembly.

2. **`MouseArea` over the ListView** — owns *all* left-button input. The delegate's `TextEdit.selectByMouse = false`; the MouseArea handles press / drag / release, hit-tests via a careful `hit(mouseX, mouseY)` function, and calls `selModel.begin(...)` / `selModel.extend(...)`.

3. **Per-delegate `Connections` to the model** — when `selModel.selectionChanged` fires, the delegate computes its slice of the global selection via `selModel.rangeForBlock(index)` and applies it via `textEdit.select(start, end)` (or `textEdit.deselect()`). TextEdit renders highlight natively.

Critically, **no delegate ever drives the model directly** and **the model is the single source of truth**. Cycle hazards (the legacy's plague) are absent because the data flow is one-way: input → model → delegates.

---

## 2. The traps we hit, in order

### 2.1 `ListView` is a `Flickable` and steals drag gestures

**Symptom:** the entire view bounced elastically like a touch-scrolled list when the user tried to drag-select.

**Root cause:** `ListView` inherits from `Flickable`, which has its own pointer-grab logic that activates after a small drag distance. A child `MouseArea` *anchored inside* a Flickable loses its press to the Flickable's gesture detection.

**Fix:** `MouseArea.preventStealing: true`. This stops the parent Flickable from stealing the press once the MouseArea has accepted it.

```qml
MouseArea {
    anchors.fill: parent
    preventStealing: true   // critical
    acceptedButtons: Qt.LeftButton
    ...
}
```

**Side effect to be aware of:** with `preventStealing: true`, the Flickable's normal "drag to scroll" gesture is suppressed. We rely on the scrollbar / scroll wheel for scrolling. For touch/mobile we'll need to revisit — the Flickable interaction will need to be re-introduced via a more sophisticated gesture arbitration (e.g. a `DragHandler` that only takes precedence after a horizontal-vs-vertical disambiguation). **Not a v0 concern; documented.**

### 2.2 `TextEdit.select(start, end)` silently no-ops on out-of-range `end`

**Symptom:** when the selection spanned three or more paragraphs, the *intermediate* paragraphs (and sometimes the first or last) showed no highlight, even though the model held the correct range.

**Root cause:** the spike originally used `INT32_MAX` as a sentinel meaning "to end of block" — `rangeForBlock` returned `QPoint(start, INT32_MAX)` for a block fully covered by the selection. Per Qt docs and verified empirically, `TextEdit.select(start, end)` *silently does nothing* if `end > text.length`. The selection state simply doesn't update for that delegate.

**Fix:** clamp the sentinel against `textEdit.length` on the QML side before calling `select()`:

```qml
Connections {
    target: selModel
    function onSelectionChanged() {
        const r = selModel.rangeForBlock(textEdit.index)
        if (r.x === -1) {
            textEdit.deselect()
        } else {
            // INT32_MAX from C++ is "to end of block".
            // TextEdit.select silently no-ops if end is out of range, so clamp.
            const end = Math.min(r.y, textEdit.length)
            textEdit.select(r.x, end)
        }
    }
}
```

**Production lesson:** **never trust `select`'s tolerance for sentinel values.** Always clamp on the QML side, in the consumer of `rangeForBlock`. The C++ side can use `INT32_MAX` (or any sentinel ≥ any plausible block length) without problems; the QML side is responsible for constraining it. Document this in the contract: `rangeForBlock`'s return value `y` may exceed the block's length and the consumer **must** clamp.

### 2.3 Backward drags appeared "broken" when really they hit trap 2.2

**Symptom:** dragging from paragraph 5 *upward* to paragraph 1 produced no visible highlight on paragraphs 1-4, even though the header bar's coordinates updated correctly and Ctrl+C copied the right text.

**Root cause:** same as 2.2 — the `INT32_MAX` sentinel was hitting the silent-no-op path. The `SelectionModel.normalized()` function correctly produces forward order regardless of drag direction, so paragraphs 1-3 were getting `(0, INT32_MAX)` and paragraph 4 was getting `(0, INT32_MAX)` (interior + end-of-anchor block).

**Fix:** same as 2.2.

**Production lesson:** *direction of drag* and *order of normalization* are independent. The model produces the same normalized range regardless; the rendering issue was orthogonal. Don't go hunting for asymmetric bugs in the model when the rendering path has a known truncation issue.

### 2.4 Mouse leaving the window stopped extending the selection

**Symptom:** dragging the mouse outside the window's edges during a press caused the selection to *stop tracking*, even though the mouse was still pressed. Native text editors keep extending the selection in this case (with optional auto-scroll).

**Root cause:** `MouseArea` correctly continues to receive `positionChanged` events with out-of-bounds `mouseX` / `mouseY` values when the mouse is grabbed (the underlying Qt mouse-grab semantics work). The bug was *upstream* in our code: my `hit(mouseX, mouseY)` returned `null` for any `mouseY < 0` or `mouseY > height`, and `null` returns prevented `selModel.extend(...)` from being called. The mouse-grab worked; we threw away the events.

**Fix (initial, naïve):** clamp `mouseY` into `[0, height]` before computing `cy`. After clamping, the rest of the hit-test pipeline (itemAt + positionAt) naturally produces "snap to viewport edge with X-tracking."

```js
const clampedX = Math.max(0, Math.min(mouseX, width - 1))
const clampedY = Math.max(0, Math.min(mouseY, height - 1))
const cx = clampedX + listView.contentX
const cy = clampedY + listView.contentY
```

**Production lesson:** **mouse-grab works in QML.** The `MouseArea` does receive out-of-window events while pressed. The platform delivers what we need; we just have to *not* drop the events ourselves. Whenever a hit-test pipeline returns `null`, ask: "is null the right answer here, or did I just give up too early?"

### 2.5 Whitespace below content mapped anchor to the wrong offset

**Symptom:** starting a drag in the empty space *below* the last paragraph (within the ListView's viewport, but past the document content), then dragging upward, caused the *entire* last paragraph to flash highlighted on entry, then "shrink like a top-down forward selection" as the mouse moved up through the paragraph.

**Root cause:** my fallback for "in-spacing-between-delegates" was a two-step probe: first try `itemAt(cx, cy - 8)` (the "above" branch), and if that fails, try `itemAt(cx, cy + 8)` clamped to `contentHeight - 1`. When the press happened way below the last paragraph (large `cy`), the "above" probe at `cy - 8` was still in whitespace, so it failed. The "below" probe at `Math.min(cy + 8, contentHeight - 1)` clamped to content's last pixel — which is *inside* the last paragraph — and returned `{ block: lastIdx, offset: 0 }`. So anchor became (last-block, 0), and any drag became "select from start of last paragraph to current position."

**Fix:** detect "we're in viewport whitespace below content" *explicitly* — `cy >= listView.contentHeight` — and snap to the end of the last block (X-tracked on its bottom line). This case had been conflated with the "in-spacing-between-delegates" fallback, but it's structurally different.

```js
if (cy >= listView.contentHeight) {
    const probe = listView.itemAt(probeX, listView.contentHeight - 1)
    if (probe) {
        const localY = Math.max(0, probe.height - 1)
        return { block: probe.index, offset: probe.positionAt(clampedLocalX(probe, cx), localY) }
    }
    return { block: lastIdx, offset: root.blockTexts[lastIdx].length }
}
```

**Production lesson:** **"in-content-area-but-not-on-an-item" and "past-the-end-of-content" are two different cases.** Conflating them via a generic fallback produces subtle directional bugs. Always handle the "past-the-end" explicitly *before* the spacing fallback.

### 2.6 Lateral mouse movement in gaps did nothing

**Symptom:** when the cursor was in the gap between two delegates (e.g. between paragraph 3 and paragraph 4), moving the mouse sideways had no effect on the selection's column position. The selection stayed pinned at "end of paragraph above" regardless of X.

**Root cause:** my spacing-fallback returned `{ block: above.index, offset: above.text.length }` — fixed to literal end of text, ignoring the X-coordinate.

**Fix:** snap to a *position-on-the-bordering-line* using `positionAt`. For the "above" case, use `positionAt(localX, item.height - 1)` to get the offset on the bottom visual line of the paragraph above, X-tracked. For the "below" case, use `positionAt(localX, 0)` to get the offset on the top visual line of the paragraph below.

We also added mid-gap symmetry: try several `dy` values and pick whichever block's edge is closer (smaller `dy`).

```js
let aboveItem = null, aboveDy = 0
let belowItem = null, belowDy = 0
for (let dy = 4; dy < 64; dy += 4) {
    if (!aboveItem) {
        const a = listView.itemAt(probeX, Math.max(0, cy - dy))
        if (a) { aboveItem = a; aboveDy = dy }
    }
    if (!belowItem) {
        const b = listView.itemAt(probeX, Math.min(listView.contentHeight - 1, cy + dy))
        if (b) { belowItem = b; belowDy = dy }
    }
    if (aboveItem && belowItem) break
}
if (aboveItem && (!belowItem || aboveDy <= belowDy)) {
    const localY = Math.max(0, aboveItem.height - 1)
    return { block: aboveItem.index, offset: aboveItem.positionAt(clampedLocalX(aboveItem, cx), localY) }
}
if (belowItem) {
    return { block: belowItem.index, offset: belowItem.positionAt(clampedLocalX(belowItem, cx), 0) }
}
```

**Production lesson:** **never use `text.length` (literal end) as a fallback in a hit-test.** The hit-test exists to translate cursor coordinates into text positions; if you fall back to a fixed offset, lateral movement stops working. Always compute via `positionAt` if you have an item to call it on.

### 2.7 The right-edge asymmetry — `itemAt` and `positionAt` both fail at margins

**Symptom:** dragging the mouse off the *left* edge of the window worked correctly (selection extended to start-of-line at current Y). Dragging off the *right* edge did *not* work (selection stayed pinned at start-of-line, didn't extend to end-of-line).

**Root cause: a layered Qt API quirk that took two debugging passes to fully understand.**

The delegate is laid out with `x: 12` and `width: ListView.view.width - 24` — i.e. a 12-pixel inset on both sides. So the delegate's *bounding rect* is narrower than the ListView's viewport. Two consequences:

1. **`ListView.itemAt(cx, cy)` returns `null` for any `cx` in the 12px margin** on either side. Hit-testing via `cx` directly fails when the user has dragged outside (because clamping puts `cx` at `width - 1`, which is past the right edge of the item).
2. **`TextEdit.positionAt(localX, localY)` returns `0` for *both* negative `localX` and `localX > item.width`** (i.e. it asymmetrically clamps to start-of-line, *not* end-of-line, on the right side). This is unintuitive but consistent with Qt's "out-of-range returns zero" convention.

The combination produced asymmetric behavior:
- **Left side:** `cx` clamped to 0 → `itemAt(0, cy)` happened to find an item *sometimes* (when the item's bounding rect started at exactly x=12 the test was 12px short, but in practice items rendered with anti-aliasing on adjacent pixels — variable). When it did find one, `positionAt(-12, ...)` returned `0` = start-of-line. The "wrong" answer (zero from out-of-range) coincidentally matched the *correct* expected behavior on the left.
- **Right side:** `cx` clamped to `width - 1` was past the item's right edge → `itemAt` returned `null` → fallback ran but also probed with the same out-of-range `cx` → eventually returned `null` → no model update → selection stayed put. Even when an item *was* found, `positionAt(width-13, ...)` (past item width) returned `0` = start-of-line — the *opposite* of the expected end-of-line.

**Fix (two parts, both required):**

a) **Use a probe X for `itemAt` that's guaranteed inside items' horizontal range.** Compute it once; reuse for every `itemAt` call:

```js
const probeX = listView.width / 2  // any x guaranteed within items
```

Then use `probeX` for hit-testing, but the actual `cx` for column tracking via `positionAt`.

b) **Clamp `localX` into `[0, item.width - 1]` before calling `positionAt`.** This translates "user dragged past the right edge" into "extend to end-of-line" instead of "collapse to start-of-line":

```js
function clampedLocalX(item, contentX) {
    return Math.max(0, Math.min(contentX - item.x, item.width - 1))
}
```

**Production lessons (multiple):**

- **Qt's "out of bounds returns 0" is asymmetric in user-perception but symmetric in implementation.** `positionAt(-X, y)` and `positionAt(width + X, y)` both return 0. On the left, this happens to match user expectation (start-of-line). On the right, it inverts user expectation. Never assume `positionAt` clamps gracefully.

- **`itemAt` is bounds-strict: any pixel outside an item's bounding rect returns null.** If your delegates have horizontal padding, you cannot reliably hit-test with a viewport-coord `x`. Use a separate "probe X" that's known to be inside the items' bounds.

- **The "left works, right doesn't" asymmetry was a strong diagnostic.** If you observe asymmetric behavior on a Cartesian axis, the bug is almost always at a clamp boundary or in a `min`/`max` that has the wrong sign. Don't chase phantom "differential" bugs in the model.

---

## 3. The hit-test pipeline (final, working)

```js
function hit(mouseX, mouseY) {
    const lastIdx = listView.count - 1

    // (a) Out-of-window mouse positions: clamp into the viewport. After
    // clamping, the rest of the pipeline produces "snap to the viewport
    // edge with X-tracking" — the same behavior native text editors
    // give when you drag past the window edge while still pressing.
    const clampedX = Math.max(0, Math.min(mouseX, width - 1))
    const clampedY = Math.max(0, Math.min(mouseY, height - 1))

    const cx = clampedX + listView.contentX
    const cy = clampedY + listView.contentY

    // (b) localX clamped into [0, item.width - 1]. TextEdit.positionAt
    // returns 0 for any out-of-bounds x (both sides), so an unclamped
    // localX past the right edge would collapse to start-of-line rather
    // than extend to end-of-line. Clamping gives symmetric, native behavior.
    function clampedLocalX(item, contentX) {
        return Math.max(0, Math.min(contentX - item.x, item.width - 1))
    }

    // (c) Items have horizontal padding, so itemAt(cx, cy) returns null
    // whenever cx is in the left or right viewport margin. Always probe
    // with an x that's guaranteed inside the items' horizontal range —
    // the actual mouse cx is only used by positionAt (via clampedLocalX)
    // for column tracking.
    const probeX = listView.width / 2

    // (d) Below the document content: snap to last block, X-tracked
    // on its bottom visual line. Handle this BEFORE the spacing
    // fallback, otherwise we conflate "below end of doc" with
    // "between two blocks" and produce subtle directional bugs.
    if (cy >= listView.contentHeight) {
        const probe = listView.itemAt(probeX, listView.contentHeight - 1)
        if (probe) {
            const localY = Math.max(0, probe.height - 1)
            return { block: probe.index, offset: probe.positionAt(clampedLocalX(probe, cx), localY) }
        }
        return { block: lastIdx, offset: root.blockTexts[lastIdx].length }
    }
    if (cy < 0) {
        return { block: 0, offset: 0 }
    }

    // (e) Direct hit. Use probeX for itemAt; cx for positionAt.
    const item = listView.itemAt(probeX, cy)
    if (item) {
        const localY = cy - item.y
        return { block: item.index, offset: item.positionAt(clampedLocalX(item, cx), localY) }
    }

    // (f) In-content but in spacing between delegates: walk up/down a
    // few pixels to find the bordering items. Once found, use
    // positionAt(localX, ...) so lateral mouse movement tracks
    // column-by-column along the bordering visual line. The "above"
    // item's BOTTOM line and the "below" item's TOP line are the two
    // candidates; pick whichever is closer to the actual mouse Y
    // (mid-gap symmetry).
    let aboveItem = null, aboveDy = 0
    let belowItem = null, belowDy = 0
    for (let dy = 4; dy < 64; dy += 4) {
        if (!aboveItem) {
            const a = listView.itemAt(probeX, Math.max(0, cy - dy))
            if (a) { aboveItem = a; aboveDy = dy }
        }
        if (!belowItem) {
            const b = listView.itemAt(probeX, Math.min(listView.contentHeight - 1, cy + dy))
            if (b) { belowItem = b; belowDy = dy }
        }
        if (aboveItem && belowItem) break
    }
    if (aboveItem && (!belowItem || aboveDy <= belowDy)) {
        const localY = Math.max(0, aboveItem.height - 1)
        return { block: aboveItem.index, offset: aboveItem.positionAt(clampedLocalX(aboveItem, cx), localY) }
    }
    if (belowItem) {
        return { block: belowItem.index, offset: belowItem.positionAt(clampedLocalX(belowItem, cx), 0) }
    }
    return null
}
```

**Step-by-step rationale:**

| Step | Purpose | Why it's necessary |
|---|---|---|
| (a) | Clamp mouse coords into viewport | Mouse-grab delivers out-of-window coords; clamp makes them representable as a viewport position. |
| (b) | `clampedLocalX` helper | `positionAt` returns 0 for any out-of-bounds x; clamp converts "off-screen-right" into "end-of-line". |
| (c) | `probeX = listView.width / 2` | Items are horizontally inset; raw `cx` past the inset margin makes `itemAt` return null. Probe at center is always valid. |
| (d) | Below-content branch | Handle "past end of doc" explicitly so we don't conflate with the spacing fallback. |
| (e) | Direct hit on a delegate | Common path. |
| (f) | Spacing fallback | Inter-delegate gaps need column tracking via `positionAt` on the bordering visual line; pick closer of above/below. |

---

## 4. The `SelectionModel` API contract (final)

C++ class. ~90 LOC including comments. Signals on every change.

```cpp
class SelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int anchorBlock  READ anchorBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int anchorOffset READ anchorOffset NOTIFY selectionChanged)
    Q_PROPERTY(int activeBlock  READ activeBlock  NOTIFY selectionChanged)
    Q_PROPERTY(int activeOffset READ activeOffset NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

    Q_INVOKABLE void begin(int block, int offset);          // press
    Q_INVOKABLE void extend(int block, int offset);         // drag
    Q_INVOKABLE void clear();                               // explicit clear

    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;
    Q_INVOKABLE QString collectSelectedText(const QStringList &blockTexts) const;
    Q_INVOKABLE void copySelectionToClipboard(const QStringList &blockTexts) const;
};
```

**Key contract details:**

- `rangeForBlock(blockIndex)` returns:
  - `QPoint(-1, -1)` if no selection touches this block
  - `QPoint(start, end)` for a fully-contained selection in this block
  - `QPoint(start, INT32_MAX)` for "from `start` to end of block" (consumer must clamp)
  - `QPoint(0, INT32_MAX)` for an interior block fully covered by selection (consumer must clamp)
  - `QPoint(0, end)` for "from start of block to `end`"

- `normalized()` (private) produces forward-order `(firstBlock, firstOffset, lastBlock, lastOffset)` regardless of drag direction. This is the single place direction-sensitivity is handled; everything downstream sees forward order.

- `hasSelection()` returns `false` for "anchor and active at same point" (zero-length selection from a click without drag). This is critical for distinguishing "user clicked to position cursor" from "user has highlighted text."

- `copySelectionToClipboard()` lives on the C++ side because QML doesn't have direct access to `QClipboard` and the workarounds (offscreen TextEdit + selectAll + copy) are fragile. The C++ side just calls `QGuiApplication::clipboard()->setText(...)`.

**The QStringList parameter for `collectSelectedText` and `copySelectionToClipboard`:** the spike passes the document's block texts as a `QStringList` from QML. In production, the model will read from a `QAbstractListModel` with the right roles, so this won't need to be passed in.

---

## 5. The per-delegate `Connections` pattern

This is the pattern every text-bearing delegate (paragraph, heading, blockquote, list-item, code-block) will reuse:

```qml
delegate: TextEdit {
    id: textEdit
    required property int index
    required property string blockText  // from model role
    text: blockText
    readOnly: true
    selectByMouse: false   // selection is driven externally
    wrapMode: TextEdit.Wrap

    Connections {
        target: selModel
        function onSelectionChanged() {
            const r = selModel.rangeForBlock(textEdit.index)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                // INT32_MAX from rangeForBlock means "to end of block" — clamp.
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
```

**Why `selectByMouse: false`** is critical: if `selectByMouse` is true, TextEdit installs its own mouse handlers and competes with our top-level MouseArea. The first one to claim a press wins. With multiple delegates in a ListView, the chrome of which TextEdit gets the mouse press is not deterministic from the user's perspective. Disabling per-delegate mouse selection makes the top-level MouseArea the single owner, which is exactly what we need.

**Why `Connections` and not a direct binding:** we need to call `select(start, end)` (an imperative method), not bind a property. Connections fire on the signal and let us run imperative code.

**Don't use `super.copy()` in QML.** JavaScript inside QML doesn't have `super` semantics for QML element method overrides. If you define a `function copy()` on a TextEdit, you shadow the built-in `copy()` and have no way to call the original. We hit this in an early version of the spike and replaced it with C++ clipboard handling.

---

## 6. Coordinate systems — a short reference

Three coordinate systems are in play; getting them mixed up is a primary source of bugs.

| Name | Origin | Used for | How to compute |
|---|---|---|---|
| MouseArea-local | top-left of the MouseArea (= top-left of the ListView in our case) | `mouseX`, `mouseY` from MouseArea events | Reported by Qt directly. |
| ListView content | top-left of the ListView's `contentItem` (the scrollable canvas) | `listView.itemAt(x, y)` and `listView.contentX/Y` | `cx = mouseX + listView.contentX`, `cy = mouseY + listView.contentY` |
| Item-local | top-left of a specific delegate item | `item.positionAt(localX, localY)` | `localX = cx - item.x`, `localY = cy - item.y` |

`contentX` is the horizontal scroll offset of the ListView. With a vertical ListView and no horizontal scrolling, it's always 0. `contentY` is the vertical scroll offset; can grow as the user scrolls.

When mouse leaves the window, `mouseX` / `mouseY` from MouseArea remain in MouseArea-local coords but can be negative or exceed `width` / `height`.

---

## 7. What's still NOT in the spike (deferred to production)

These are *known polish items*, not architectural risks. They live on the editing-spec todo, not as obstacles to the walking skeleton.

1. **Auto-scroll while dragging near viewport edge.** Native text editors scroll the view when the mouse is near (or past) the viewport edge during a drag. Our spike clamps the mouse into the viewport but doesn't scroll. Implementation: a `Timer` that fires while the mouse is outside; it adjusts `listView.contentY` by some delta per tick, then re-runs the hit-test.
2. **Touch / mobile gestures.** `preventStealing: true` disables the Flickable's normal swipe-to-scroll. For touch, we need gesture arbitration: a horizontal-vs-vertical disambiguation that lets the user either swipe-to-scroll (vertical drag without significant horizontal motion) or drag-to-select (horizontal drag).
3. **Selection handles.** Mobile-style draggable handles at the start and end of the selection. Out of scope for read-only walking skeleton.
4. **Keyboard shortcuts beyond Ctrl+C.** Shift+arrow to extend, Cmd/Ctrl+A select-all, etc. Out of scope.
5. **Right-click context menu.** Wired via the KDAB Widget-bridge pattern in production; not in the spike.
6. **Selection persistence across model changes.** When the AST diff updates the model, selection anchored to a removed block should... clear? Snap to nearest? Decide as part of the editing-spec.
7. **Zero-length click visualization.** Clicking without dragging clears the selection but doesn't draw a cursor. Read-only is fine; for editing, we want a blinking cursor at the click point.

---

## 8. Implications for production `LiveSelectionModel`

When the production code is written:

- `LiveSelectionModel` is structurally identical to spike `SelectionModel`. Probably literally renamed and committed verbatim.
- The `hit(mouseX, mouseY)` function (~80 LOC) is the load-bearing piece. Keep it as a function on the QML side rather than promoting to C++ — it depends on QML-only APIs (`listView.itemAt`, `textEdit.positionAt`) and has no useful unit tests in C++ (would all be integration tests against a real ListView + TextEdit).
- The C++ side's testable surface is `SelectionModel` itself: `begin`/`extend`/`clear` correctness, `rangeForBlock` correctness for every position pattern (single-block, multi-block, anchor-after-active, etc.), `collectSelectedText` against fixture text. ~15-20 unit tests cover it.
- The per-delegate `Connections` pattern is reusable across text-bearing delegates *as a literal copy-paste*. Differences between paragraph, heading, blockquote, list-item are all *upstream* (the model row's content, kind, and styling) — the selection wiring is identical.
- Non-text delegates (image, hr, table) need different selection treatment: an image is "selected" or not, a table cell is selected at a finer grain than this model handles, an hr can't really be selected. Out of scope for read-only walking skeleton; the editing-spec resolves this.

---

## 9. Spike code — preserved at `.spike/cross-block-selection/`

The spike code is committed as-is for future reference. To run:

```bash
cd .spike/cross-block-selection
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
./build/spike
```

It opens a window with five paragraphs of lorem ipsum and the selection wiring described in this document. Manual test cases:

1. Click+drag within a paragraph → in-block selection.
2. Click+drag across paragraphs → cross-block selection with mid-paragraph slicing.
3. Drag past every viewport edge → snap-to-edge with column tracking.
4. Drag in the gap between paragraphs → column tracking on the bordering visual line.
5. Drag in whitespace below the last paragraph → snap to end-of-doc, column-tracked.
6. Ctrl+C or "Copy" button → assembled multi-block text on clipboard.
7. "Clear" button → selection clears.

---

## 10. The bet, settled

**Pre-spike question:** is delegate-per-AST-block + custom selection layer fundamentally different from the legacy's multi-`MarkdownTextItem` + forked `TextControl` architecture, or are we just dressing up the same architecture in QML clothing?

**Answer:** different. Six legacy mess-causes are eliminated by the platform change (no `TextControl` fork, no `setFocusProxy`+`sendEvent` recursion, no inline `Q_OBJECT` text objects, no AST-rebuild escape hatch, no scene-out-of-sync re-serialize, no offset-map cursor drift). One legacy-mess-class — custom cross-block selection — re-appears, but in a much simpler form: ~90 lines of `SelectionModel` C++ + ~80 lines of `hit()` JS + ~10 lines of per-delegate `Connections`. It is not a fork. It is not a copy of Qt internals. It is a small abstraction with a clean contract and good test surface.

The bet is on. Proceed with the Live Render walking skeleton design.
