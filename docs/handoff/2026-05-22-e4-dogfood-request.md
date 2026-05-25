# E4 — dogfood request (2026-05-22)

**Scope:** Pipe tables render as a graphical grid in Live; cells are
individually editable; navigation follows Word/Obsidian conventions;
inline formatting + links work inside cells; the alignment row and
`|` separators are never visible in Live.

**Plan:** [`docs/plans/2026-05-22-e4-tables.md`](../plans/2026-05-22-e4-tables.md) Phase H.
**Spec:** [`docs/specs/2026-05-22-e4-tables-design.md`](../specs/2026-05-22-e4-tables-design.md).

## How to run

```bash
./build-dev/bin/markoff-live-app libs/markoff-live/tests/fixtures/tables_dogfood.md
```

The `tables_dogfood.md` fixture is organized 1-to-1 with the checklist
below — each section in the fixture has the minimum markdown that
exercises one checklist group, plus the adjacent paragraphs that
boundary tests need. The older `tables_basic.md` and `tables_wrap.md`
fixtures are still wired into the integration tests; either works for
ad-hoc poking.

## Checklist

### Rendering

- [ ] Pipe table renders as a graphical grid; no `|` separators visible.
- [ ] Alignment row (`|---|:---:|---:|`) is not visible.
- [ ] Column alignment (left / center / right) respects the alignment markers.
- [ ] Header row is visually distinct (bold + background slot).
- [ ] Cells with `**bold**` render bold; `*italic*` renders italic.
- [ ] Cells with `[[Page]]` render styled as a wikilink; `[text](url)` styled as a link.
- [x] Cells with long content wrap; column widths reflect content (short-label columns stay narrow; prose columns take the remaining width; no horizontal overflow). *(Dogfood-confirmed 2026-05-23.)*

### Cell editing

- [ ] Click into a cell; type — character lands in that cell, other cells unchanged.
- [ ] Typing feels fluid (sub-50ms per keystroke). *(Resolved 2026-05-23, `5c67777` — parser inline-trigger fast-path.)*
- [ ] Save and reload: table is byte-identical to the edits.

### Navigation

- [ ] Tab: forward through the row; wraps to next row; exits table at the end.
- [ ] Shift+Tab: backward through the row; wraps; exits at the start.
- [ ] Left at cell-start moves to previous cell end; Right at cell-end moves to next cell start.
- [ ] Up / Down across rows preserves the column.
- [ ] Up at top row exits table to previous block; Down at bottom row exits to next block.
- [ ] Esc: exits cell editing; whole-table selection ring appears.
- [ ] From BlockSelected: Up/Down moves to an adjacent block.

### Selection

- [ ] Inside a cell (not at an edge), Shift+Left / Shift+Right extends the selection within the cell.
- [ ] At a cell edge, Shift+arrow advances the active end into the adjacent cell; per-cell highlight grows. *(Resolved 2026-05-23, `9d5235f` — cross-cell Shift+arrow extension.)*
- [ ] Shift+Up / Shift+Down crosses cell rows.
- [ ] Shift+Left at (0, 0) qtPos 0 / Shift+Right at (lastR, lastC) cell-end extends across the table boundary into surrounding paragraphs.

### Block-level delete cascade

- [ ] Backspace at (0, 0) qtPos 0: enters BlockSelected. Second Backspace: table deleted.
- [ ] Delete at (lastR, lastC) cell-end: enters BlockSelected. Second Delete: table deleted.

### Links inside cells

- [ ] Ctrl+click on `[[Page]]` activates the wikilink (LinkService receives activation, page="Page").
- [ ] Ctrl+click on `[click](https://example.com)` opens the URL.
- [ ] Ctrl-hover over a wikilink in a cell flips the cursor to PointingHandCursor; LinkService receives hover.

### Cross-mode round-trip

- [ ] Switch to Source view: table appears as pipe text. Edits there round-trip back to Live grid on switch.
- [ ] In Source view, delete the alignment row; switch back to Live: the table is plain-text paragraphs (no graphical grid).

### Clipboard / Ctrl-chords

- [ ] Ctrl+C, Ctrl+X, Ctrl+V work inside a cell (within-cell text).
- [ ] Ctrl+C on a BlockSelected table copies the whole pipe-table source.

## Findings so far

| Date | Finding | Resolution |
|------|---------|------------|
| 2026-05-23 | Typing in a cell laggy (~hundreds of ms) after F1's parser fix | `5c67777` — parser inline-trigger fast-path |
| 2026-05-23 | Shift+arrow at cell edges didn't extend into adjacent cells | `9d5235f` — cross-cell Shift+arrow wired |
| 2026-05-23 | Cells don't line-wrap (`wrapMode: NoWrap` from B2); long cells overflow visually | `acc76c6` — TableDelegate `wrapMode: WrapAtWordBoundaryOrAnywhere`; plan `docs/plans/2026-05-23-e4-cell-wrap-and-column-width.md` |
| 2026-05-23 | Column widths don't adapt to content sensibly; ragged sizing | `acc76c6` — `TableEditBinding::computeColumnWidths` ports Penelope's `distributeColumnsAuto`; spec `docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md` |
| 2026-05-23 | After the wrap landed, short cells exposed horizontal "seams" — the GridLayout's background colour leaked above/below a short cell whose row had a taller wrapped sibling | `6a0865a` — `Layout.fillHeight: true` on the cell Rectangle so it stretches to the row height. Dogfood-confirmed: seams gone, table feels usable. |

## Out of scope for E4 (do not regress; do not test)

- Column resizing by drag — not planned.
- Adding/removing rows/columns via UI gestures — not planned (edit the pipe source in Source view).
- Table-of-contents-style navigation — outside E-arc.

## On pass

Tag `v0.7.0-e4` at the dogfood-confirmed commit. Update
`docs/e-arc/e-arc-status.md` phase board row for E4 → `complete`.

## On fail

Append specific bugs to `docs/handoff/2026-05-22-e4-dogfood-findings.md`
with repro steps. Hold the tag until either:
- The bug is fixed and the affected row in the checklist above passes; or
- The bug is explicitly downgraded to a follow-up commit (styling-only,
  per plan §Risks #5) and logged in `docs/queue.md`.
