# E4 dogfood fixture

Open with:

`./build-dev/bin/markoff-live-app libs/markoff-live/tests/fixtures/tables_dogfood.md`

Walk this file top-to-bottom against the checklist in
`docs/handoff/2026-05-22-e4-dogfood-request.md`. Each section below
matches one checklist group. The intro paragraph and trailing paragraph
of every section exist on purpose — boundary tests need an adjacent
block to land in.

paragraph above the first table so the top-of-table boundary tests have somewhere to come from.

## Section 1 — Rendering: alignment, header, inline formats

| Left                    | Center        | Right             |
|:------------------------|:-------------:|------------------:|
| short                   | mid           | end               |
| longer text on the left | centred       | numbers 42        |
| **bold cell**           | *italic cell* | `code cell`       |
| see [[Some Page]]       | [ext](https://example.com) | mixed **bold** + *em* |

Verify:

- No `|` separators or alignment-row glyphs visible.
- "Left" column hugs the left edge; "Center" is centred; "Right" hugs the right.
- Header row is visually distinct (weight + background slot).
- `**bold**` renders bold; `*italic*` renders italic; `` `code` `` renders code; `[[Some Page]]` is styled as a wikilink; `[ext](url)` is styled as a link.

## Section 2 — Cell editing + typing perf + save round-trip

Click into "longer text on the left" above and type. Each keystroke should feel sub-50 ms; other cells should not flicker or rebind. Save (Ctrl+S), close, reopen — the table round-trips byte-identical to your edits.

## Section 3 — Navigation (Tab, Shift+Tab, arrows, Esc)

paragraph above the navigation table so Shift+Tab at (0,0) and Up at row 0 have an exit target.

| One | Two | Three | Four |
|-----|-----|-------|------|
| 1.1 | 1.2 | 1.3   | 1.4  |
| 2.1 | 2.2 | 2.3   | 2.4  |
| 3.1 | 3.2 | 3.3   | 3.4  |

paragraph below the navigation table so Tab at (lastR, lastC) and Down at the last row have an exit target.

Verify:

- Tab walks 1.1 → 1.2 → 1.3 → 1.4 → 2.1 (wraps to next row).
- Shift+Tab reverses.
- Tab at 3.4 exits the table to the paragraph below; Shift+Tab at 1.1 exits to the paragraph above.
- Left at qtPos 0 of "2.1" lands at the end of "1.4"; Right at the end of "1.4" lands at qtPos 0 of "2.1".
- Up from "2.2" lands in "1.2" (column preserved); Down from "2.2" lands in "3.2".
- Up from "1.2" exits the table upward; Down from "3.2" exits the table downward.
- Esc inside any cell exits cell editing; a selection ring appears around the whole table (BlockSelected).
- From BlockSelected, Up/Down moves to the adjacent paragraph above/below.

## Section 4 — Selection (within-cell, cross-cell, cross-boundary)

paragraph above the selection table — landing target for Shift+Left at (0,0).

| A  | B  | C  |
|----|----|----|
| aa | bb | cc |
| dd | ee | ff |

paragraph below the selection table — landing target for Shift+Right at (lastR, lastC).

Verify:

- Click into "ee" mid-cell; Shift+Left / Shift+Right grows the selection inside the cell only.
- With caret at the right edge of "ee", Shift+Right extends the active end into "ff" — per-cell highlight grows in both cells.
- Shift+Down from "bb" extends the selection into "ee" (crosses cell rows).
- Caret at qtPos 0 of "aa"; Shift+Left extends backward into the paragraph above (cross-boundary).
- Caret at the end of "ff"; Shift+Right extends forward into the paragraph below.

## Section 5 — Block-level delete cascade

paragraph above the delete-cascade table.

| Solo | Cell |
|------|------|
| only | row  |

paragraph below the delete-cascade table.

Verify:

- Caret at qtPos 0 of "only"; press Backspace. The table enters BlockSelected (no char delete, no merge into the paragraph above). Press Backspace again: the whole table is deleted.
- Undo (Ctrl+Z) until the table is back. Caret at the end of "row"; press Delete. Same: enter BlockSelected, second Delete removes the table.

## Section 6 — Links inside cells

| Kind                | Sample                                |
|---------------------|---------------------------------------|
| wikilink            | [[Some Page]]                         |
| wikilink with alias | [[Some Page\|click here]]             |
| wikilink to heading | [[Some Page#Section]]                 |
| external link       | [Qt Documentation](https://doc.qt.io) |

Verify:

- Ctrl+click "Some Page": `LinkService::linkActivated(page="Some Page")`.
- Ctrl+click "click here": activates "Some Page" (alias resolved).
- Ctrl+click "Section": activates "Some Page#Section".
- Ctrl+click "Qt Documentation": opens the external URL.
- Ctrl-hover any wikilink in a cell: cursor flips to PointingHandCursor; LinkService receives hover.

## Section 7 — Cross-mode round-trip (Live ↔ Source)

Reuse any table above.

- Switch to Source view. Tables appear as pipe text.
- In Source, add a word to a cell. Switch back to Live — the edit is present in the grid.
- In Source, delete the alignment row (`|----|----|`) of a table. Switch back to Live — that table demotes to plain paragraphs (no graphical grid).

## Section 8 — Clipboard (within-cell + whole-table)

Use Section 3's nav table.

- Click into "2.2"; select the text "2.2"; Ctrl+C. Move to "3.3", select "3.3", Ctrl+V — "3.3" becomes "2.2".
- Select a cell value, Ctrl+X cuts it; Ctrl+V pastes back.
- Press Esc to enter BlockSelected on the table; Ctrl+C — the whole pipe-table source goes to the clipboard. Paste into a separate text editor to verify the full markdown text (including alignment row).

## Section 9 — Wrap + column-width (already confirmed; sanity check only)

| Short | Description                                                                                              | Code     |
|-------|----------------------------------------------------------------------------------------------------------|----------|
| a     | A long prose cell that should wrap to multiple lines whenever the viewport gives the column less width than the cell's full single-line measurement. | `single` |
| b     | medium-length sibling.                                                                                   | `short`  |
| c     | tiny.                                                                                                    | `x`      |

Verify:

- "Description" column wraps; "Short" and "Code" stay narrow.
- No horizontal overflow / horizontal scrollbar.
- Row heights grow to fit wrapped lines (no horizontal "seams" between short and tall cells in the same row).

trailing paragraph so Section 9 has a below-block boundary for any final arrow-exit / Shift+arrow / Down checks.
