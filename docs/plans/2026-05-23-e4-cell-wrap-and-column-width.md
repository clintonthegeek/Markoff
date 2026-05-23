# E4 follow-up — Cell wrap + smart column width — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> or superpowers:subagent-driven-development to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port Penelope's `distributeColumnsAuto` to `TableEditBinding`;
have `TableDelegate.qml` consume the computed widths and let `TextEdit`
wrap inside them. The dogfood-request's two open styling rows (cells
don't line-wrap, ragged column widths) flip to ticks. Phase H tag
`v0.7.0-e4` can then proceed.

**Architecture:** Measurement + distribution in C++ on `TableEditBinding`
via `QFontMetricsF`. QML consumes a `QVariantList<qreal>` of per-column
widths and applies them as `Layout.preferredWidth` on each cell, with
`wrapMode: WrapAtWordBoundaryOrAnywhere` on the `TextEdit`. `TextEdit`'s
own `QTextLayout` does the line-breaking; `GridLayout`'s per-row
max-implicitHeight gives row equalisation for free.

**Tech Stack:** Qt 6.8+ (Quick, Quick.Controls, Gui, Test), C++20,
CMake 3.19+, existing `TableEditBinding`, `LiveListModelBinding::theme`,
`Markoff::Theme` slot accessors.

**Spec:** `docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md`.
Read §3 (algorithm) before A2, §5 (falsifiable invariants) before C2/C3.
Cite section numbers in commit messages where relevant.

**Reference implementation:** `~/dev/Penelope/src/engine.cpp` —
`measureColumnMetrics`, `distributeColumnsAuto`. The C++ port is
algorithmically one-for-one.

**Conventions:**
- Build: `cmake --build build-dev --target <target> -j 8` (never bare `-j`).
- Tests: `scripts/run-tests.sh --bin <test_binary>` (defaults to
  `QT_QPA_PLATFORM=offscreen`).
- SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on every new file.
- `tr()` for user-visible strings (none expected in this plan).
- TDD: failing test → implementation → green → commit. Frequent commits.
- Falsifiability proofs (INVARIANTS.md invariant 4): for the load-bearing
  invariant tests, the implementation commit is preceded by a stub-then-revert
  proving the test fails when the relevant code is broken.

**Plan-time resolutions** (defer-from-spec decisions baked here):
- New C++ helpers (`cellMinWidth`, `cellMaxWidth`, the distribute function)
  live in an anonymous namespace inside `TableEditBinding.cpp`, **not** as
  separate headers, until/unless a second consumer appears. Keeps the
  measurement surface internal.
- `kMinColumnWidth = 60.0` (matches the retired `Layout.minimumWidth: 60`).
  Defined as `static constexpr` near the helpers.
- Cell padding constant: read from `TableEditBinding` rather than
  hard-coded to 4 — the QML side uses `anchors.margins: 4`, but the C++
  side should accept it as a parameter so a future theme-driven padding
  doesn't break the metric. First pass: `TableEditBinding::cellPadding()`
  returns `4.0`.
- `fontMetricsRevision` (spec §3.4): land **only if** the integration
  test in C3 shows widths don't re-flow on theme zoom. Skip otherwise;
  mark as a follow-up in `docs/queue.md` if the check passes today.
- The new fixture `tables_wrap.md` co-locates with `tables_basic.md` in
  `libs/markoff-live/tests/fixtures/`.

---

## Phase A — C++ measurement + distribution

### Task A1: `TableEditBinding` width-metric helpers

**Files:**
- Edit: `libs/markoff-live/src/TableEditBinding.cpp` (anonymous namespace)
- New: `libs/markoff-live/tests/tst_live_render_table_layout_metrics.cpp`
- Edit: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests.** Friend or expose via a test-only
      Q_INVOKABLE wrapper. Slots:
      - `cellMinWidth_emptyString_returnsKMinColumnWidth` —
        `cellMinWidth("", font, padding=4) == 60.0`.
      - `cellMinWidth_singleWord_returnsWordAdvancePlusPadding` —
        compute expected via `QFontMetricsF::horizontalAdvance("Hello") + 8`,
        compare against the helper, floored at 60.
      - `cellMinWidth_multipleWords_returnsLongestWordWidth` —
        `"short verylongword x"` → returns `horizontalAdvance("verylongword") + 8`.
      - `cellMaxWidth_returnsFullStringWidth` —
        `cellMaxWidth("the quick brown fox", font, 4)` ==
        `horizontalAdvance("the quick brown fox") + 8`.

- [ ] **Step 2: Implement** `cellMinWidth(text, font, padding)` and
      `cellMaxWidth(text, font, padding)` in the anonymous namespace.
      `cellMinWidth`: split on `QRegularExpression("\\s+")`, take max of
      per-token `horizontalAdvance`, add `2*padding`, floor at
      `kMinColumnWidth`. `cellMaxWidth`: single `horizontalAdvance` call,
      add `2*padding`.

- [ ] **Step 3:** Tests pass.

- [ ] **Commit:** `feat(live): TableEditBinding cell-width metric helpers (E4 wrap A1)`

### Task A2: `distributeColumnsAuto` port from Penelope

**Files:**
- Edit: `libs/markoff-live/src/TableEditBinding.cpp`
- Edit: `libs/markoff-live/tests/tst_live_render_table_layout_metrics.cpp`

- [ ] **Step 1: Write failing tests.** Four slots driving the three
      Penelope branches plus the floor case:
      - `auto_totalMaxLeqAvail_distributesSurplusEvenly` —
        cols with `min=50,max=100` × 3, `avail=400` → each gets
        `100 + (400-300)/3 = 133.33`.
      - `auto_totalMinGeqAvail_scalesMinsProportionally` —
        cols with `min=100` × 3, `avail=150` → each gets `100 * 150/300 = 50`.
      - `auto_proportionalBranch_distributesBetweenMinAndMax` —
        cols `[{min:50,max:200},{min:50,max:100}]`, `avail=225`.
        `totalMin=100, totalMax=300, W=125, D=200`.
        col 0: `50 + 150*125/200 = 143.75`.
        col 1: `50 + 50*125/200 = 81.25`.
      - `auto_sumsToAvailWidth` — for each branch's input, assert
        `sum(widths) == avail` within 1e-6.

- [ ] **Step 2: Falsifiability proof commit pair.** Stub the proportional
      branch to return `metrics[i].minWidth` only (drop the proportional
      term). Run the test — slot 3 fails (cols too narrow), slot 4 fails
      (sum < avail). Revert. Commit message:
      `test(live): falsifiability proof — distributeColumnsAuto proportional branch (E4 wrap A2-proof)`

- [ ] **Step 3: Implement** `distributeColumnsAuto(metrics, availWidth)`
      verbatim from Penelope per spec §3.3. Anonymous namespace.

- [ ] **Step 4:** Tests pass; proof commit lives in history above the
      implementation commit.

- [ ] **Commit:** `feat(live): TableEditBinding distributeColumnsAuto (E4 wrap A2)`

### Task A3: `computeColumnWidths` Q_INVOKABLE

**Files:**
- Edit: `libs/markoff-live/include/markoff/live/TableEditBinding.h`
- Edit: `libs/markoff-live/src/TableEditBinding.cpp`
- Edit: `libs/markoff-live/tests/tst_live_render_table_layout_metrics.cpp`

- [ ] **Step 1: Write a failing test.** Build a `ParsedTable`-shaped
      `QVariantMap` (headers + body QVariantLists); call
      `binding.computeColumnWidths(headers, body, availWidth)`; assert
      the returned list has `numCols` elements and matches the expected
      Penelope output for known content. Use a real `Markoff::Theme`
      instance for the font (default light); fixture content known.

- [ ] **Step 2: Implement** `Q_INVOKABLE QVariantList computeColumnWidths(const QVariantList &headers, const QVariantList &body, qreal availWidth) const;`.
      - Resolve the cell-default font from `binding->theme()->font(Theme::TextDefault)`
        (or whatever the slot accessor is — verify; fall back to `QGuiApplication::font()`
        if no theme).
      - Resolve the header font: same as default but with `font.setBold(true)`.
      - Build `metrics` per column: aggregate `cellMinWidth`/`cellMaxWidth`
        across header row (bold font) + body rows (default font).
      - Call `distributeColumnsAuto(metrics, availWidth)`.
      - Convert to `QVariantList<qreal>` and return.
      - Empty headers or `numCols == 0` → return empty list (spec §7 risk 4).
      - `availWidth <= 0` → return empty list (spec §7 risk 3 — second
        fire is the source of truth).

- [ ] **Step 3:** Test passes.

- [ ] **Commit:** `feat(live): TableEditBinding::computeColumnWidths Q_INVOKABLE (E4 wrap A3)`

---

## Phase B — QML application

### Task B1: Bind `columnWidths` in `TableDelegate.qml`

**Files:**
- Edit: `libs/markoff-live/qml/delegates/TableDelegate.qml`

- [ ] **Step 1:** Add a root property on the delegate:
      ```qml
      property var columnWidths: (tableEditBinding && root.parsedTable && root.parsedTable.parseOk)
          ? tableEditBinding.computeColumnWidths(
                root.parsedTable.headers,
                root.parsedTable.body,
                cellGrid.width)
          : []
      ```
      Place near the existing `parsedTable` property binding so a future
      reader sees them together.

- [ ] **Step 2: Smoke check** with `markoff-live-app fixtures/tables_basic.md`
      — observe nothing visually changes yet (B2 wires the consumption
      side). Open the log; the binding fires without errors.

- [ ] **Commit:** `feat(live): TableDelegate computes columnWidths (E4 wrap B1)`

### Task B2: Apply widths + flip `wrapMode` (the user-visible change)

**Files:**
- Edit: `libs/markoff-live/qml/delegates/TableDelegate.qml`
  (currently `Layout.fillWidth: true` at line 400,
  `Layout.minimumWidth: 60` at line 401, `wrapMode: TextEdit.NoWrap`
  at line 420).

- [ ] **Step 1: Falsifiability proof commit pair.** Before applying the
      real change, stub the cell to use a hard-coded `Layout.preferredWidth: 60`
      regardless of column. Run `markoff-live-app fixtures/tables_basic.md`;
      visually confirm the table renders as 60px-wide columns (broken).
      Revert. Commit message:
      `test(live): falsifiability proof — per-column width binding (E4 wrap B2-proof)`
      *(This proof is visual/manual; the automated counterpart lands in C3 slot 1.)*

- [ ] **Step 2: Apply** the spec §4 diff to the per-cell `Rectangle`:
      - `Layout.fillWidth: false` (was `true`).
      - `Layout.preferredWidth: (cellRect.c < root.columnWidths.length) ? root.columnWidths[cellRect.c] : 60`.
      - Delete `Layout.minimumWidth: 60` (the C++ floor handles it).

- [ ] **Step 3: Apply** to the per-cell `TextEdit`:
      - `wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere` (was `NoWrap`).

- [ ] **Step 4: Smoke check** with `markoff-live-app` on the existing
      `fixtures/tables_basic.md` and on a real document with mixed-width
      tables (the dogfood doc). Observe: prose columns wrap; label
      columns stay narrow; no horizontal overflow; row heights grow to
      fit wrapped content.

- [ ] **Commit:** `feat(live): TableDelegate consumes columnWidths + wraps cells (E4 wrap B2)`

### Task B3: Theme-zoom re-evaluation (conditional)

**Files:**
- Possibly edit: `libs/markoff-live/include/markoff/live/Theme.h`,
  `libs/markoff-live/src/Theme.cpp`,
  `libs/markoff-live/qml/delegates/TableDelegate.qml`.

- [ ] **Step 1: Check.** After B2 lands, change the theme's body font
      size at runtime (use whatever mechanism E2.6 wired — likely a
      slot setter on `Markoff::Theme` exposed via `LiveListModelBinding`).
      Observe: do `columnWidths` re-evaluate? If yes, **skip Steps 2–4**;
      log to `docs/queue.md` as a closed item ("font-metrics re-flow
      verified working without revision counter") and proceed to C1.

- [ ] **Step 2 (conditional):** Add `metricsRevision : int` Q_PROPERTY
      to `Markoff::Theme` with NOTIFY. Increment in every font-slot
      setter.

- [ ] **Step 3 (conditional):** In `TableDelegate.qml`, anchor the
      `columnWidths` binding to the revision:
      ```qml
      property int _metricsRevision: (root.liveBinding && root.liveBinding.theme)
          ? root.liveBinding.theme.metricsRevision : 0
      property var columnWidths: {
          void _metricsRevision  // dependency anchor
          return ...
      }
      ```

- [ ] **Step 4 (conditional):** Re-test the zoom; widths re-flow.

- [ ] **Commit (conditional):** `feat(live): Theme.metricsRevision + TableDelegate re-eval on zoom (E4 wrap B3)`

---

## Phase C — Falsifiable invariants on the realistic-input harness

### Task C1: `fixtures/tables_wrap.md`

**Files:**
- New: `libs/markoff-live/tests/fixtures/tables_wrap.md`

- [ ] **Step 1:** Author a fixture with:
      - One paragraph before.
      - A 3-column table: `Label | Description | Code` — `Description`
        contains one row with ~200 chars of prose (forces wrap in any
        non-absurd viewport), other rows short; `Code` contains
        `` `single` `` and `` `also short` ``; `Label` is always 5–10
        chars.
      - One paragraph after.

- [ ] **Commit:** Bundled with C2 (single commit for the phase since the
      fixture only matters once C2 consumes it).

### Task C2: `tst_live_render_table_layout` (realistic-input integration)

**Files:**
- New: `libs/markoff-live/tests/tst_live_render_table_layout.cpp`
- Edit: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write failing slots** — the five slots from spec §5:
      1. `wide_widget_fits_without_wrap` — viewport width >> `totalMax`;
         every cell `lineCount == 1`.
      2. `narrow_widget_wraps_prose_column` — `totalMin < avail < totalMax`;
         prose column `lineCount > 1`; short-label column `lineCount == 1`.
      3. `very_narrow_widget_honors_min_floor` — viewport < `totalMin`;
         each column ≥ 59px (1px rounding tolerance); table doesn't
         overflow viewport horizontally (`cellGrid.width == viewport.width`).
      4. `no_horizontal_overflow` — sweep viewport widths from
         `kMinColumnWidth * numCols` upward in 50px increments;
         `sum(columnWidths) == cellGrid.width` to within 1px.
      5. `row_height_grows_with_wrap` — row containing the wrapped prose
         cell has `implicitHeight > singleLineRow.implicitHeight`.

- [ ] **Step 2: Falsifiability proofs** (already proven for the C++ side
      in A2; this step adds the QML-reach proofs):
      - Stub `TableDelegate.qml`'s `columnWidths` to equal widths
        (`numCols` entries of `cellGrid.width / numCols`). Run slot 2 —
        fails (short-label column gets the same width as the prose column).
        Revert.
      - Stub `wrapMode` back to `NoWrap`. Run slot 2 — fails
        (`lineCount == 1` everywhere). Revert.
      - These proofs commit as a pair before the test itself, per
        INVARIANTS.md invariant 4. Commit message:
        `test(live): falsifiability proof — column widths + wrap reach QML (E4 wrap C2-proof)`.

- [ ] **Step 3:** All 5 slots pass against the implementation from B2.
      Test count grows by 5 (and the fixture lands).

- [ ] **Commit:** `test(live): tst_live_render_table_layout — wrap + column-width invariants (E4 wrap C2)`

---

## Phase D — Verify + close out

### Task D1: Typing-perf bench re-baseline

**Files:**
- Possibly edit: `libs/markoff-live/tests/tst_live_render_table_typing_perf.cpp`.

- [ ] **Step 1:** Run `scripts/run-tests.sh --bin tst_live_render_table_typing_perf`.
      Compare the per-keystroke latency against the prior baseline
      (commit `5c67777`).

- [ ] **Step 2:** If unchanged (within 5% noise), no action needed.
      If regressed >5%, profile `computeColumnWidths` — most likely
      culprit is over-eager re-evaluation on `cellGrid.width` thrash.
      Mitigation per spec §7 risk 2: debounce via the
      `scheduleD2Changed` pattern.

- [ ] **Commit (conditional):** `perf(live): debounce computeColumnWidths re-evaluation (E4 wrap D1)`

### Task D2: Update dogfood request

**Files:**
- Edit: `docs/handoff/2026-05-22-e4-dogfood-request.md`.

- [ ] **Step 1:** In the "Findings so far" table, change the two "Open"
      rows for wrap + column-width from "Pending" to a resolved entry
      with the commit hash from B2.

- [ ] **Step 2:** In the "Rendering" checklist, add one line:
      `- [ ] Cells with long content wrap; column widths reflect content (not all equal, not ragged).`

- [ ] **Commit:** `docs(handoff): tick wrap + column-width in E4 dogfood request (E4 wrap D2)`

### Task D3: Tag

- [ ] **Step 1:** Re-run the full E4 dogfood checklist on a real document.
      All boxes tick.

- [ ] **Step 2:** `git tag v0.7.0-e4` at the dogfood-confirmed commit.

- [ ] **Step 3:** Update `docs/e-arc/e-arc-status.md` phase-board row for
      E4 → `complete`.

- [ ] **Commit:** `docs(e-arc): E4 complete; v0.7.0-e4 tagged (E4 wrap D3)`

---

## Risks and unknowns

1. **Spec §7 risks already enumerated.** Not duplicated here; re-read
   before A3 (inline-format width contribution) and D1 (re-eval thrash).

2. **`Markoff::Theme` font-slot accessor.** Spec assumes `theme->font(Theme::TextDefault)`
   or similar exists. If it doesn't (the E2.6 wiring is colour-focused,
   not font-focused), A3 Step 2 adds the accessor — small detour, but
   call it out in the commit. If a font accessor is genuinely absent,
   first-pass fallback is `QGuiApplication::font()`; that's adequate for
   the Auto algorithm (proportional distribution is robust to ~10%
   font-size error), and the proper slot accessor follows up.

3. **`Layout.preferredWidth` vs `GridLayout.columnCount` interaction.**
   `GridLayout` may still try to fill remaining width if our
   `Layout.preferredWidth` sums to less than `cellGrid.width`. The
   algorithm guarantees `sum(columnWidths) == cellGrid.width` in the
   `totalMax <= avail` and `totalMin >= avail` branches, but the
   proportional branch sums to exactly `avail` only by construction —
   verify the floating-point sum lands within rounding error of
   `cellGrid.width`. If `GridLayout` stretches the last column to
   absorb the rounding gap, that's acceptable; document and move on.

4. **Empty cells in the header row.** A header like `| | foo |` has an
   empty cell — its `cellMinWidth` is `kMinColumnWidth` via the floor;
   `cellMaxWidth` is also `kMinColumnWidth` (since `2*padding < 60`).
   The metric aggregation correctly takes the max across rows, so a
   non-empty body cell in the same column drives the width. Verify in
   C2 slot 1.

---

## Estimated commits

Roughly 8–10 commits across A1–D3, plus 2–3 falsifiability-proof commit
pairs. Each task targets one commit unless stated. Phase B3 may be
zero commits if the conditional check passes.
