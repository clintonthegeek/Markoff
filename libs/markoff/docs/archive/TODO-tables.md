# Tables — Current Status and Next Steps

## What Exists

- `TableHandler` class: detects pipe tables, parses to `ParsedTable`, serializes back
- `TableWidget` (QTableWidget subclass): created from ParsedTable, styled for editor
- `Editor::Private::embeddedWidgets`: list of widgets with block ranges
- `createEmbeddedWidgets()`: detects tables, creates TableWidgets
- `repositionEmbeddedWidgets()`: attempts to position over pipe text
- Table blocks detected as `DecoratedRange::Table`, text made transparent

## Known Bugs

1. **Scroll offset wrong** — tables appear too high, overlapping previous
   content. The block-height-sum approach for computing scroll offset
   doesn't match the editor's actual scroll position.

2. **Pipe text still interactive** — transparent text is still selectable
   and clickable. Need to either remove it from the document or make
   the table widget consume all mouse events in its region.

3. **QTableWidget has own scrollbar** — need `setVerticalScrollBarPolicy(
   Qt::ScrollBarAlwaysOff)` and size the widget to fit all rows.

4. **Height calculation** — widget height should be the table's natural
   height (all rows visible), not the pipe text height.

5. **Reparse recreates widgets** — every text change in the document
   destroys and recreates table widgets. Need to detect when a table
   hasn't changed and preserve the widget.

## Approach for Next Session

Consider: instead of fighting to position a QWidget over hidden text,
could we replace the pipe text blocks with a QTextTable in the document
and use a layout engine that supports it? The `QTextDocumentLayout`
(from QTextEdit, not QPlainTextEdit) handles QTextTable natively.

Alternatively: use a hybrid layout where table regions use the rich
text layout and everything else uses the plain text layout.

Or: accept the embedded widget approach but fix the positioning by
computing it from the paint path (which already knows where blocks are)
rather than from block height sums.
