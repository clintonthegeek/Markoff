# E4 follow-up — Cell line-wrapping + smart column width (2026-05-23)

**Scope:** Table cells in Live wrap their content within a sensibly-sized column.
First-pass column widths (`Layout.fillWidth: true` + `Layout.minimumWidth: 60`
from plan B2 step 1) are replaced by a content-aware distribution. Cells use
`TextEdit.WrapAtWordBoundaryOrAnywhere` instead of `NoWrap`.

**Status:** Follow-up spec — `docs/plans/2026-05-22-e4-tables.md` §Risks #5
explicitly deferred this as a styling adjustment. Surfaced as an open dogfood
finding in `docs/handoff/2026-05-22-e4-dogfood-request.md` (2026-05-23).
Phase H tag `v0.7.0-e4` should land *after* this spec's implementation —
visually broken tables are not the shipping state.

**Reference implementation:** `~/dev/Penelope/src/engine.cpp` —
`Engine::layoutTable`, `measureColumnMetrics`, `distributeColumnsAuto`,
`distributeColumnsOptimal`. Penelope is the user's other Qt6 program; it
already solved this for its print/preview engine. This spec ports the
**Auto** algorithm verbatim. The **Optimal** algorithm is recorded as a
deferred follow-up (it has an architectural cost in Live that Penelope
doesn't pay — see §6).

---

## 1. Goal

For any pipe table rendered in Live:

1. The table consumes the full delegate width (no horizontal overflow).
2. Each column's width responds to its content: a column of short labels stays
   narrow; a column of prose takes the remaining width.
3. When the natural maximum widths exceed available space, the columns that
   most benefit from wrapping shrink first.
4. Long cells wrap at word boundaries (and within long unbreakable runs as a
   last resort) rather than overflowing or forcing horizontal scroll.
5. Row heights grow to fit the tallest wrapped cell in the row.
6. The algorithm matches `distributeColumnsAuto` from Penelope (CSS `table-layout: auto`
   in spirit): proportional distribution between `totalMin` and `totalMax`.

Out of scope (deferred): per-column resize handles; user-overridable widths;
the Penelope `Optimal` greedy-narrowing algorithm (see §6); per-span text-style
contribution to width metrics beyond the cell-default font.

---

## 2. Architecture

Three pieces, each at the natural authority for its data:

```
┌─────────────────────────────────────────────────────────────┐
│ TableEditBinding (C++)         — measurement + distribution │
│   - Q_INVOKABLE QVariantList computeColumnWidths(           │
│         headers, body, availWidth)                          │
│   - Internally: QFontMetricsF over the cell-default font    │
│     (TextDefault Theme slot)                                │
│   - Implements distributeColumnsAuto from Penelope          │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼ (QVariantList<qreal>, length == numCols)
┌─────────────────────────────────────────────────────────────┐
│ TableDelegate.qml (QML)        — apply + wrap               │
│   - Property: columnWidths = tableEditBinding               │
│         .computeColumnWidths(parsedTable.headers,           │
│                              parsedTable.body,              │
│                              cellGrid.width)                │
│   - Per-cell: Layout.preferredWidth: columnWidths[c]        │
│               Layout.fillWidth: false                       │
│   - Per-cell TextEdit: wrapMode: WrapAtWordBoundaryOrAnywhere│
│   - Row heights fall out of GridLayout's per-row max        │
│     (cellEdit.implicitHeight + 2*padding)                   │
└─────────────────────────────────────────────────────────────┘
```

The C++ side measures and decides. The QML side applies and lets `TextEdit`
do its own line-breaking inside the assigned column width. We **never** ask
the QML side to break lines and feed heights back — that's the architectural
boundary that keeps Auto cheap (and the boundary that makes Optimal expensive,
§6).

---

## 3. The Auto algorithm (ported from Penelope)

### 3.1 Per-cell metrics

For each cell text, compute two widths using `QFontMetricsF` over the
**cell-default font** (the TextDefault slot, body cells; bold-modified for the
header row):

- **`maxWidth`** = `fm.horizontalAdvance(cellText)` — the single-line width if
  the cell didn't wrap.
- **`minWidth`** = `max(fm.horizontalAdvance(token) for token in tokenize(cellText))`
  — the widest unbreakable run. `tokenize` splits on whitespace; CJK / glyphs
  inside the longest token are not further broken (matches the spirit of
  `WrapAtWordBoundaryOrAnywhere`'s "or anywhere" fallback being a last resort).

Add `2 * cellPadding` (current value: 4px from `anchors.margins: 4`) to both.

Apply a hard floor `kMinColumnWidth = 60.0` (preserves the B2 minimum for
empty / very-short cells).

### 3.2 Per-column aggregation

For each column `c`:
```
metrics[c].minWidth = max(cell.minWidth for cell in column c)
metrics[c].maxWidth = max(cell.maxWidth for cell in column c)
metrics[c].minWidth = max(metrics[c].minWidth, kMinColumnWidth)
metrics[c].maxWidth = max(metrics[c].maxWidth, metrics[c].minWidth)
```

### 3.3 Distribution (verbatim port of `distributeColumnsAuto`)

Given `metrics[]` and `availWidth`:

```cpp
qreal totalMin = sum(metrics[i].minWidth);
qreal totalMax = sum(metrics[i].maxWidth);

if (totalMax <= availWidth) {
    // Everything fits without wrapping. Use maxes; distribute surplus evenly.
    qreal surplus = availWidth - totalMax;
    for (int i = 0; i < n; ++i)
        widths[i] = metrics[i].maxWidth + surplus / n;
}
else if (totalMin >= availWidth) {
    // Available width can't hold even the unbreakable runs.
    // Scale mins down proportionally (cells will wrap aggressively).
    for (int i = 0; i < n; ++i)
        widths[i] = metrics[i].minWidth * (availWidth / totalMin);
}
else {
    // The interesting case. Proportional distribution between min and max:
    // W = budget above totalMin; D = total stretch available.
    qreal W = availWidth - totalMin;
    qreal D = totalMax - totalMin;
    for (int i = 0; i < n; ++i)
        widths[i] = metrics[i].minWidth
                  + (metrics[i].maxWidth - metrics[i].minWidth) * W / D;
}
```

This is the same code in `~/dev/Penelope/src/engine.cpp` —
the spec ports the formula and the three branches one-for-one.

### 3.4 Re-compute triggers

`columnWidths` re-evaluates when any of:

- `parsedTable` changes (cell content shifted, structural row/col change).
- `cellGrid.width` changes (delegate width changes when the ListView resizes).
- The Theme's body / header font metrics change (font-size zoom, theme swap).

The first two fall out of QML bindings naturally. Theme-font changes already
re-fire `themeColorFor` bindings via the existing E2.6 anchor pattern; piggyback
a `fontMetricsRevision: liveBinding.theme.metricsRevision` property on
`TableDelegate.qml` to force re-evaluation (if no such revision counter exists,
add one — single integer, increments when font slots change).

---

## 4. QML application

In `libs/markoff-live/qml/delegates/TableDelegate.qml` (current widths at
TableDelegate.qml:400–402, wrap at TableDelegate.qml:420):

```qml
property var columnWidths: tableEditBinding && root.parsedTable.parseOk
    ? tableEditBinding.computeColumnWidths(
          root.parsedTable.headers,
          root.parsedTable.body,
          cellGrid.width)
    : []

// per-cell Rectangle:
Layout.fillWidth: false                         // was: true
Layout.preferredWidth: (cellRect.c < root.columnWidths.length)
                      ? root.columnWidths[cellRect.c]
                      : 60
// Layout.minimumWidth: 60   ← retire; the metric step floors at 60

// per-cell TextEdit:
wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere   // was: NoWrap

// Row height (already correct in the current code):
implicitHeight: cellEdit.implicitHeight + 8
// GridLayout's per-row max-implicitHeight gives row-equalisation for free —
// no explicit "equalise cell heights" pass (Penelope needed one because it
// laid out cells absolutely; GridLayout handles it for us).
```

Note: the existing `Layout.minimumWidth: 60` is retired; the C++ side already
floors per-column min at 60, so the GridLayout-side floor is redundant and
would fight the computed proportional width when `availWidth` is small.

---

## 5. Falsifiable invariants

A new test file `libs/markoff-live/tests/tst_live_render_table_layout.cpp`
drives the realistic-input harness against `fixtures/tables_basic.md` plus a
new `fixtures/tables_wrap.md` containing one wide-prose column. Slots:

1. **wide-widget-fits-without-wrap** — set viewport width comfortably wider than
   the table's `totalMax`; assert every cell's `lineCount == 1`.
2. **narrow-widget-wraps-prose-column** — set viewport width such that
   `totalMin < availWidth < totalMax`; assert the prose column wrapped
   (`lineCount > 1`) and the short-label columns did not.
3. **very-narrow-widget-honors-min-floor** — set viewport width below
   `totalMin`; assert each column ≥ `kMinColumnWidth - 1px` (proportional
   scale-down still respects the floor *after* metric aggregation) and that
   the table does not overflow the viewport horizontally
   (`cellGrid.width == viewport.width`).
4. **no-horizontal-overflow** — across the full sweep of viewport widths from
   `kMinColumnWidth * numCols` upward, the sum of column widths matches
   `cellGrid.width` to within 1px of rounding error.
5. **row-height-grows-with-wrap** — assert the row containing a wrapped cell
   has `implicitHeight > singleLineRow.implicitHeight`.

**Falsifiability proofs** (per INVARIANTS.md invariant 4): for each load-bearing
test, the implementation commit is preceded by a stub-then-revert proving the
test fails when the relevant code is broken:

- Stub `computeColumnWidths` to return equal widths → slot 2 fails (short
  columns get the same width as the prose column).
- Stub the `WrapAtWordBoundaryOrAnywhere` to `NoWrap` → slot 2 fails
  (`lineCount == 1`).
- Stub the `totalMin >= availWidth` branch to share evenly → slot 3 fails
  (columns drop below `kMinColumnWidth`).

---

## 6. Deferred: the `Optimal` algorithm

Penelope offers a second algorithm `distributeColumnsOptimal`: start at max
widths, then greedily narrow the column whose narrowing causes the least
height increase, until the total fits. This produces aesthetically tighter
tables (a long prose column narrows to e.g. 3 lines instead of 5) at the cost
of N iterations of "measure column height at trial width X."

In Penelope this is cheap: the engine owns `breakIntoLines`, so a trial
measurement is a pure function call. In Markoff Live, line-breaking is owned
by `TextEdit`'s underlying `QTextLayout` — we'd need a hidden-from-scene
`QTextLayout`-based measurer on the C++ side
(`TableEditBinding::estimateCellHeight(text, fontStyle, width)`). Doable, but
non-trivial; warrants its own follow-up spec once dogfood says Auto isn't
enough.

If we add Optimal later: gate it behind a `MarkoffSettings` switch
(`tableLayoutAlgorithm: Auto | Optimal`), matching Penelope's pattern. The
spec for that follow-up should cite this section.

---

## 7. Risks

1. **Inline-format width contribution.** Bold / italic / code-font runs are
   slightly wider than the default font for the same text. First pass measures
   with cell-default font only — likely fine since the metrics differ by
   <10% and the Auto algorithm is already proportional. If dogfood says a
   column of monospace `inline code` consistently wraps when it shouldn't,
   the follow-up is to measure each `SourceSpan` against its kind-specific
   font and sum. Note in the implementation commit so the next agent can
   trace the deferral.

2. **`computeColumnWidths` re-evaluation thrash.** The binding fires on
   every parsedTable change. For a 20×5 table that's 100 `horizontalAdvance`
   calls per fire — sub-millisecond. Should not need debouncing; verify
   under the typing-perf bench (`tst_live_render_table_typing_perf`) before
   declaring done. If it does show up, debounce via the same pattern as
   `scheduleD2Changed`.

3. **`cellGrid.width` initial-zero.** During delegate construction
   `cellGrid.width` is briefly 0; the binding fires once with `availWidth: 0`
   and again with the real width. The `totalMin >= availWidth` branch already
   handles this (returns scaled mins → near-zero widths → harmless, replaced
   immediately by the second fire). Don't add a guard; the second fire is
   the source of truth.

4. **Empty table (0 columns).** `computeColumnWidths` returns an empty list;
   the QML side already short-circuits via `parsedTable.parseOk`. Confirm
   the empty-table path in a test slot to lock that behaviour.

---

## 8. Implementation outline (sketch — plan lives in `docs/plans/`)

A1. Add `TableEditBinding::computeColumnWidths` (header + impl + CMake) with
    measurement helpers `cellMinWidth` / `cellMaxWidth` exposed for testing.

A2. Unit test `tst_live_render_table_layout_metrics` against the C++ API
    directly — known content → known widths.

A3. Wire `TableDelegate.qml` to consume `computeColumnWidths`; retire
    `Layout.fillWidth: true` + `Layout.minimumWidth: 60`; flip
    `wrapMode` to `WrapAtWordBoundaryOrAnywhere`.

A4. Add `tst_live_render_table_layout.cpp` — the five falsifiable slots
    from §5, each with its stub-then-revert proof commit.

A5. Add fixture `tables_wrap.md`.

A6. Update the E4 dogfood request: tick the two "Open" rows in the findings
    table once dogfood confirms; remove the wrap/width caveat from the
    "Pending" column.

A7. Re-baseline `tst_live_render_table_typing_perf` if the new binding
    measurably moves it. (Expect <5% impact; document if otherwise.)

A8. Tag `v0.7.0-e4` once Phase H checklist passes including the new
    wrap/width behaviour.

---

## 9. Acceptance

Spec is implemented when:

- `tst_live_render_table_layout` and `tst_live_render_table_layout_metrics`
  pass.
- The dogfood-request checklist's two pending styling rows resolve to ticks.
- The `markoff-live-app` walkthrough on a real document with mixed-width
  tables produces no horizontal scroll and no visually-broken column sizing.
- The follow-up note for Optimal lives in this spec's §6, ready for a future
  spec to cite.
