# Tables — Design

## Approach

QTextTable replaces pipe-delimited markdown in the QTextDocument during
live preview. Qt handles cell editing, cursor navigation, selection.
We paint grid chrome (borders, handles, buttons) as decorations.

## Lifecycle

1. Detect pipe table pattern in markdown text
2. Parse: extract headers, alignment, cell contents
3. Replace pipe text with QTextTable in the document
4. Paint grid lines, hover handles, add/remove buttons
5. On file save: serialize QTextTable back to pipe-delimited markdown

## What QTextTable Gives Us

- Cell structure (rows, columns)
- Cursor navigation (Tab, Shift+Tab, arrow keys between cells)
- Cell text editing
- Insert/remove rows and columns
- Cell-level selection
- Auto-insert row on Tab past last cell

## What We Paint

- Grid lines between cells
- Column header handles (hover-activated, above each column)
- Row handles (hover-activated, left of each row)
- Add column button (+ at right edge)
- Add row button (+ at bottom edge)
- Column alignment indicators

## Right-Click Menu

- Sort by column (A→Z, Z→A)
- Add column before/after
- Add row above/below
- Move column left/right
- Move row up/down
- Align left/center/right
- Delete column/row

## Serialization

On save, walk QTextTable cells and regenerate pipe-delimited markdown:
- Compute column widths (pad to widest cell)
- Generate header row, separator row, data rows
- Preserve alignment markers (`:---:` etc.)

## Integration

- Table regions excluded from tree-sitter span map (QTextTable
  changes document structure, invalidating byte offsets)
- detectDecoratedRanges() detects table patterns
- Table conversion happens after span map is built
- QTextTable objects tracked for serialization on save
