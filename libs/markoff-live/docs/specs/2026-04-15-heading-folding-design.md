# Heading Folding — Design

> **Status:** IMPLEMENTED. Plan: `../plans/2026-04-15-heading-folding.md`.
>
> **Scope:** Markoff-internal. Host integration (where fold state lives
> on disk) is addressed separately in
> [`../2026-04-15-heading-folding-host-integration.md`](../2026-04-15-heading-folding-host-integration.md).
>
> **References:**
> - Kate harvest notes: [`2026-04-14-code-folding-kate-harvest.md`](./2026-04-14-code-folding-kate-harvest.md)
> - Parent user-facing requirements: `docs/CORBOMITE_SPECIFICATION.md` §6.5 (in parent repo; pinned behaviour summarized below)
> - Obsidian audit signals: `docs/obsidian-audit/01-markoff-gaps.md` §Rendering (in parent repo; pinned behaviour summarized below)

## Summary

Add collapsable headings to the markoff editor. v1 folds only ATX headings
(H1–H6). The underlying `FoldingModel` is designed generically so later
plans can add fold regions for fenced code blocks, lists, and block quotes
without refactoring.

The visible affordance is a dedicated left gutter column containing a
triangle arrow per heading. Clicking toggles; Ctrl+Click folds/unfolds
every heading at the same level. Folded headings hide all items between
them and the next heading at ≤ their own level. Nothing else is painted
— no inline ellipsis, no placeholder row. The next visible item slides up
directly under the folded heading.

Fold state is keyed by **heading hierarchy path** (e.g.
`["Intro", "Goals"]`) and serialized to JSON. Markoff exposes the JSON
via `Editor::serializeFoldState()`; where it's persisted is the host
app's decision.

## User-facing behaviour (from parent §6.5)

Pinned here so this spec is self-contained:

- Each H1–H6 heading gets a collapse/expand arrow in the gutter.
- Clicking the arrow hides all content under the heading until the next
  heading of equal or higher level.
- `Ctrl+Click` on the fold arrow folds/unfolds all headings at that level.
- User-facing commands: Fold All, Unfold All, Toggle Fold at cursor.
- A global setting `foldHeadings = true` controls whether the gutter
  arrows are shown at all. (Host wires this; markoff exposes
  `Editor::setGutterVisible(bool)`.)

Obsidian parity additions (from audit):

- Navigating to `[[Note#Subheading]]` auto-unfolds ancestor headings so
  the target is visible.
- Find matches inside folded regions auto-unfold the enclosing heading
  chain to reveal the match.
- Both auto-unfold cases emit `foldsAutoExpanded(paths)` so the host
  can show UX if desired (e.g., "3 sections expanded to show match").

## Architecture

Three cooperating pieces inside `libs/markoff/`:

### 1. `FoldingModel` (`src/FoldingModel.h/.cpp`)

Owns the set of folded heading paths. Pure data — no widget dependencies.

**State:**

- `QSet<QStringList>` of currently-folded paths.
- `QList<HeadingEntry>` cache, rebuilt on each reparse. `HeadingEntry`
  augments the existing `Markoff::HeadingInfo` with a hierarchy path:
  - `QStringList path` — full hierarchy path
  - `Markoff::HeadingInfo info` — reuses the existing struct (text,
    level, source offset, item index)

**Signals:**

- `void foldStateChanged()` — folded-path set changed.
- The existing `Editor::headingsChanged(QList<HeadingInfo>)` signal
  drives `FoldingModel::reconcile()`; `FoldingModel` itself does not
  own a separate signal for this.

**API (internal, used by `Editor` facade):**

```cpp
void fold(const QStringList &path);
void unfold(const QStringList &path);
void toggle(const QStringList &path);
bool isFolded(const QStringList &path) const;

QList<QStringList> allPaths() const;
QList<QStringList> foldedPaths() const;
QList<HeadingEntry> headings() const;

void foldAll();
void unfoldAll();
void foldAllAtLevel(int level);
void unfoldAllAtLevel(int level);
void foldLevel(int n);      // every Hn where n >= n
void unfoldLevel(int n);

QJsonObject serialize() const;
void restore(const QJsonObject &);

// Called from the reparse pipeline (SceneCoordinator or Editor).
void reconcile(const QList<HeadingEntry> &newHeadings);
```

**Generic design for future block types.** The `QStringList` path type
is aliased as `FoldRegionKey`. Later block types can add their own
keying scheme (e.g., list-item ordinal path `[1,2,3]` serialized as
`"list:1.2.3"`). The `QSet<FoldRegionKey>` container is reusable; the
block-type-specific logic lives in the AST-to-key conversion, not in
`FoldingModel` itself.

### 2. `FoldGutter` (`src/FoldGutter.h/.cpp`)

A `QGraphicsObject` parented to the scene, positioned in viewport
coordinates via `SceneCoordinator`'s existing viewport-pinned-item
mechanism. Paints fold arrows at each heading item's Y-coordinate.
Receives mouse events and dispatches them to columns.

**Column architecture.** The gutter holds an ordered list of
`GutterColumn*`:

```cpp
class GutterColumn {
public:
    virtual ~GutterColumn() = default;
    virtual int width() const = 0;
    virtual void paintCell(QPainter *painter,
                           const QRect &cellRect,
                           int itemIndex) = 0;
    virtual bool handleClick(QPoint localPos,
                             int itemIndex,
                             Qt::KeyboardModifiers mods) = 0;
};
```

v1 ships exactly one concrete column: `FoldArrowColumn`.

- `width() = 16` px.
- `paintCell` draws a triangle if `itemIndex` corresponds to a heading:
  rightward (closed) when folded, downward (open) otherwise. No paint
  if the item isn't a heading.
- `handleClick` with no modifiers calls
  `FoldingModel::toggle(path)`. With `Qt::ControlModifier` it calls
  `FoldingModel::foldAllAtLevel(level)` (or `unfoldAllAtLevel` if all
  siblings at that level are already folded).

`LineNumberColumn` is **not** implemented in this plan. Its absence is
intentional — the `GutterColumn` interface is the proof that v2 can
add line numbers without touching `FoldGutter` internals.

**Triangle painting.** Adapted from
`~/src/kde/src/ktexteditor/src/view/kateviewhelpers.cpp:2194–2226`
(the `paintTriangle` logic in `KateIconBorder`, ~50 lines). Uses
`Theme` colors instead of `KateRendererConfig`.

**Coordinate mapping.** `FoldGutter` calls
`SceneCoordinator::itemIndexAt(y)` to resolve a click Y to an item
index, then consults `FoldingModel::headings()` to determine whether
the item is a heading and its level. No duplicated layout math.

**Gutter width / visibility.** `FoldGutter::width()` returns
`sum(column->width())` plus a 2 px separator. `Editor::setGutterVisible(bool)`
controls the whole gutter (default visible).

### 3. `SceneCoordinator` integration

`SceneCoordinator` subscribes to `FoldingModel::foldStateChanged()`.
On change:

1. For each item in the scene, compute its enclosing heading path (cached
   — only recomputed when `headingsChanged()` fires).
2. If any prefix of that path is in the folded set, call
   `item->setVisible(false)`. Otherwise `setVisible(true)`.
3. Call existing relayout logic so Y-positions update.

Items are never removed from the scene — just hidden. Item identity is
preserved across fold/unfold cycles. Existing selection, cursor, and
decoration logic continues to work against the hidden items; if the
cursor was inside a now-folded region, it moves to the enclosing
heading's end (matching Kate).

## Public `Editor` API

Added to `include/markoff/Editor.h`. All paths are `QStringList`.

**Query:**

```cpp
QList<QStringList> headingPaths() const;
bool isFolded(const QStringList &path) const;
QList<QStringList> foldedPaths() const;
```

**Mutate (individual):**

```cpp
void fold(const QStringList &path);
void unfold(const QStringList &path);
void toggleFold(const QStringList &path);
void toggleFoldAtCursor();
```

**Mutate (bulk):**

```cpp
void foldAll();
void unfoldAll();
void foldAllAtLevel(int level);
void unfoldAllAtLevel(int level);
void foldLevel(int n);
void unfoldLevel(int n);
```

**Persistence:**

```cpp
QJsonObject serializeFoldState() const;
void restoreFoldState(const QJsonObject &);
```

**Signals:**

```cpp
void foldStateChanged();
void foldsAutoExpanded(const QList<QStringList> &paths);
// Existing `headingsChanged(QList<HeadingInfo>)` signal continues to
// emit post-reparse; no new signal added.
```

Invalid inputs (unknown path, malformed JSON) are no-ops with a
`qCWarning` entry. Library contract: best effort, never crash.

## Path encoding

A heading's path is the ordered list of ancestor heading texts ending
in its own text. Computation walks the AST:

1. For each `atx_heading` node, record its level N and trimmed text.
2. The path's prefix is the sequence of most-recent headings at each
   level < N (skipping levels that don't have one).
3. Example: `# A` → `["A"]`; `## B` (after A) → `["A", "B"]`;
   `## C` (still under A) → `["A", "C"]`; `# D` (new top) → `["D"]`.

**Text normalization.** Heading text is extracted as plain text with
inline markdown stripped — `## **Goals**` and `## Goals` produce the
same path. This matters because toggling bold on a heading would
otherwise drop its fold.

**Duplicate siblings.** Two `## Goals` under the same parent share a
path. Disambiguated deterministically with a 1-based occurrence
suffix only when needed:

```
["Intro", "Goals"]
["Intro", "Goals#2"]
["Intro", "Goals#3"]
```

The `#N` suffix is only emitted for the 2nd+ occurrences; first
occurrence never gets a suffix. Users rarely write duplicate siblings
in practice.

## Serialization format

```json
{
  "version": 1,
  "folds": [
    ["Intro", "Goals"],
    ["Reference", "API", "Query"]
  ]
}
```

`version` is reserved for future additions. When later plans add
fold regions for other block types, they add sibling keys alongside
`folds`, e.g., `"codeFolds": [...]`, `"listFolds": [...]`. Unknown
keys in loaded JSON are ignored with a warning.

## Edit reconciliation

Hooked into the existing tree-sitter debounced reparse — **no new
timer**, same trigger as rehighlight.

On each reparse completion (`Editor::headingsChanged` fires):

1. Rebuild `FoldingModel`'s heading cache from the new
   `QList<HeadingInfo>`, augmenting each entry with its computed path.
2. Compute the new path set (`QSet<QStringList>` of all paths now
   present).
3. Intersect with the currently-folded set. Folded paths that still
   exist remain folded. Folded paths that no longer exist are dropped.
4. If the folded set changed, emit `foldStateChanged()`.

**Stability properties:**

- Editing body text: no heading paths change → no folds affected.
- Typing inside a heading: during the debounce window, the fold stays
  (the old path is still in the cache). When debounce fires, the old
  path is gone → fold drops. A single predictable transition per edit
  session, not per keystroke.
- Rename / promote / demote: the path changes by definition → fold
  drops. Acceptable cost for a one-metaphor design.
- Insert new heading: unrelated folds untouched.
- Undo/redo: triggers reparse → flows through the same reconcile.

## Auto-unfold

### On navigation

`Editor::scrollToHeading(const HeadingInfo&)` (existing signature)
resolves the target's path via `FoldingModel`, then:

1. For each prefix `path[0..i]`, check `isFolded(prefix)`.
2. If folded, call `unfold(prefix)`.
3. Collect all prefixes that were unfolded.
4. If the list is non-empty, emit
   `foldsAutoExpanded(unfoldedPrefixes)` before scrolling.
5. Then scroll to the target item.

### On find

`Editor::findText()` (existing) receives a match. Before moving the
selection/cursor:

1. Locate the enclosing heading item for the match.
2. Walk that heading's path from the root, unfolding any folded
   prefix.
3. If any prefix was unfolded, emit
   `foldsAutoExpanded(unfoldedPrefixes)` before the match is
   highlighted.

Both paths share the same `unfoldAncestors(path)` helper internally.

## Testing

Five test binaries in `libs/markoff/tests/`, registered in
`tests/CMakeLists.txt` following the pattern of
`tst_search_bar`. All run with `QT_QPA_PLATFORM=offscreen`.

### `tst_folding_model`

Unit tests on `FoldingModel`:

- Path computation from a sequence of headings (simple + nested +
  skipped levels).
- Text normalization: `## **Goals**` and `## Goals` produce same path.
- Duplicate-sibling disambiguation: `#N` suffix deterministic and
  applied only to 2nd+ occurrences.
- `fold` / `unfold` / `toggle` / `isFolded` basic state.
- `foldAll` / `unfoldAll` / `foldAllAtLevel(3)` / `foldLevel(3)`
  behaviour.
- `serialize()` / `restore()` round-trip preserves fold set.
- `restore()` with malformed JSON is a no-op + warning.
- Signals: `foldStateChanged` fires exactly once per mutation that
  actually changes state.

### `tst_folding_reconcile`

Reconciliation on reparse:

- Rename a folded heading → fold drops.
- Promote folded H2 to H1 → fold drops (path changes).
- Insert unrelated heading → fold preserved.
- Edit body text under a folded heading → fold preserved.
- Insert duplicate-sibling heading → existing sibling's path
  unchanged; new one gets `#2` suffix.
- Debounce ordering: fold set not thrashed while reparse pending.

### `tst_folding_integration`

End-to-end via `Editor` API using an in-memory document:

- Fold an H2 → items between it and the next H≤2 become invisible.
- Unfold → items re-shown in original order.
- Nested fold: fold an H2, then fold its parent H1. Unfold the H1.
  Items under the H2 remain hidden (H2 stays in the folded set —
  fold entries are independent). Unfold the H2 next; all items
  re-shown.
- `scrollToHeading(["A", "B"])` with both folded auto-unfolds and
  emits `foldsAutoExpanded(["A"], ["A","B"])`.
- `findText` landing in a folded region auto-unfolds + signals.
- `toggleFoldAtCursor` on cursor inside a folded region operates on
  the enclosing heading.

### `tst_fold_gutter`

Visual + interaction tests:

- Click at a heading's Y toggles its fold.
- Ctrl+Click triggers `foldAllAtLevel`.
- Click on a non-heading row is a no-op.
- Triangle paint state reflects `isFolded` (rightward vs downward).
- Gutter width = column widths sum + 2 px.
- `setGutterVisible(false)` hides the gutter and reclaims its width.

### `tst_fold_persistence`

- `serialize → JSON string → parse → restore` round-trip.
- Restore with a path that no longer exists after content changed:
  silently dropped, `foldedPaths()` omits it.
- Restore with `version: 2` JSON containing extra keys: `folds` still
  applied; extra keys ignored with warning.
- Restore with missing `folds` key: no folds set, no crash.

## Non-goals for v1

- Line numbers in the gutter (architecture supports; separate plan).
- Fold regions for fenced code blocks, lists, or block quotes
  (`FoldingModel` is generic; separate plans will add them).
- Animated fold/unfold (v1 is instant).
- Mouse-hover preview of folded content (future plan).
- Per-heading persistent fold flags in markdown source (Obsidian's
  `+`/`-` callout folding — not applicable to headings anyway).
- Wire-compatibility with Obsidian's fold JSON schema. We use our
  own path-based schema; Obsidian's line-number-based schema has the
  known-bug documented in the audit.

## Open questions

None blocking the implementation plan. The following will be decided
during implementation and noted in the plan:

- Exact triangle geometry (Kate's is 7×7 inscribed in 16×16 with
  theme-aware color — port verbatim unless visual review says otherwise).
- Whether the gutter is a scrolling peer of the content (moves with
  scrollbar) or pinned to viewport (content scrolls past it). Kate
  pins to viewport; Obsidian pins to viewport; same here unless
  `SceneCoordinator` makes that awkward.
