# Working on Tables — Developer Guide

**Audience:** Any agent or developer modifying table-related code in Markoff.
Read this before touching `SceneCoordinator`, `MarkdownTextItem`, `TableConverter`,
`TableSerializer`, `MarkdownHighlighter`, or `TextControl` in any table-related context.

## How Tables Work

Tables live as `QTextTable` frames inside `MarkdownTextItem`'s `QTextDocument`.
They are NOT separate scene items. The pipe-delimited markdown is a serialization
format only — the user never sees or edits it directly.

### The Pipeline

```
File on disk (pipe markdown)
  ↓ Editor::setPlainText()
  ↓ SceneCoordinator::loadMarkdown()
  ↓ MarkdownSplitter::split()          — tables stay in Text segments
  ↓ createTextItem(seg.text)           — document has raw pipe text
  ↓ stripInlineSubstitutions()         — remove math glyphs (source form)
  ↓ TableConverter::convert()          — pipe text → QTextTable frames
  ↓ buildHighlightingSource()          — document-aligned string for parser
  ↓ tree-sitter parse → setSpanMap()   — spans already in doc coordinates
  ↓ refreshInlineSubstitutions()       — re-apply math glyphs
  ↓ rehighlight()                      — apply spans to display
```

### How Highlighting Works Around Tables

The key insight: **both load and reparse use the same simple approach.**

`buildHighlightingSource()` creates a string with the same character count
as the `QTextDocument`. Non-table blocks are copied at their exact document
positions. Table cell blocks are left as newlines (the fill character from
initialization). This produces a string that:

1. Has all non-table text at its true document position
2. Has neutral content where tables are (the highlighter skips table blocks)
3. Can be parsed directly by tree-sitter — spans come out in document
   coordinates with no remapping needed

The highlighter's frame guard returns early for blocks inside `QTextTable`
frames, so any spans tree-sitter generates for the table-region newlines
are harmless — they never reach rendering.

This is the same approach for both `loadMarkdown()` and the non-structural
`reparse()` path. There is no offset mapping, no anchor search, no
coordinate remapping. One path, one set of coordinates.

### Historical note: the offset mapping era

An earlier version of this pipeline used a more complex approach for the
reparse path: serialize tables back to pipe text via `allMarkdown()`, parse
that with tree-sitter (producing spans in pipe-text coordinates), then
remap every span back to document coordinates using an anchor-based
empirical measurement system. This was ~120 lines of offset computation
involving table region detection, overlapping region merging, anchor block
searching, cumulative shift tracking, and per-span adjustment.

The complex path existed because of a believed limitation: that tree-sitter
failed to detect `$...$` math expressions after large whitespace runs
(which `buildHighlightingSource()` produces where tables are). Empirical
testing in April 2026 disproved this — tree-sitter handles math after
whitespace correctly. The entire offset mapping pipeline was removed in
favor of the unified `buildHighlightingSource()` approach, eliminating
a major source of fragility and the class of bugs described in invariant 5.

### The Source Position Span Cache

`MarkdownTextItem` caches span positions in `m_sourcePositionSpans` so that
`refreshInlineSubstitutions()` can restore source-form spans after stripping.
**This cache must be invalidated** (via `invalidateSourcePositionSpans()`)
whenever you externally set a new span map with different offsets. If you
don't, the cache overwrites your corrected spans on the next substitution
refresh.

This was the cause of a multi-hour debugging session. Don't repeat it.

## Invariants — Do Not Break These

### 1. Once a table, always a table.

Once pipe text is converted to a `QTextTable`, it stays that way. The reparse
cycle recognizes existing tables via `reconcile()` and never re-converts them.
The document's `QTextTable` frames are the source of truth — not the regions
that tree-sitter detects in serialized pipe text.

### 2. Signals must be blocked during the strip/rebuild cycle.

`adjustSpanOffsets()` is connected to `QTextDocument::contentsChange`. Every
document mutation fires it. If signals are not blocked during the table
conversion and span rebuild, `adjustSpanOffsets` fires for each mutation AND
`applyInlineSubstitutions` also adjusts spans manually — double adjustment.

Always wrap the cycle in:
```cpp
const bool blocked = doc->blockSignals(true);
// ... strip, convert, set span map, refresh substitutions ...
doc->blockSignals(blocked);
hl->rehighlight(); // after unblocking
```

### 3. Strip inline substitutions before table conversion.

`createTextItem()` applies math/checkbox substitutions (U+FFFC), which change
character positions. The table converter uses offsets from the raw segment text.
If substitutions are present, those offsets point to wrong positions.

Always `stripInlineSubstitutions()` before `convert()`.

### 4. The highlighter skips table blocks.

`MarkdownHighlighter::highlightBlock()` has a frame guard (lines 310-320) that
returns early for any block inside a `QTextTable` frame. This prevents the
span-based formatter from applying markdown formatting to table cell content
(which would hide text via delimiter hiding, apply wrong styles, etc.).

Do not remove this guard. If you want formatting in table cells (scope (c)),
you need a separate per-cell formatting path.

### 5. reconcile() never creates tables — it only syncs records.

`TableConverter::reconcile()` synchronizes `m_records` with the document's
existing `QTextTable` frames. It must NEVER call `convert()`. The document's
frames are the source of truth; if tree-sitter's parse of the serialized pipe
text produces a different number of regions than document tables, that is a
parsing artifact — not a reason to modify the document.

Table creation is handled exclusively by:
- `TableConverter::convert()` during `loadMarkdown()` (initial load)
- `TableConverter::convert()` in the structural-change reparse path

### 6. allMarkdown() uses the frame iterator, not block iteration.

`QTextDocument::begin()` / `block.next()` skips blocks inside `QTextFrame`
children (including `QTextTable`). The `allMarkdown()` method uses
`QTextFrame::iterator` on the root frame, which visits both regular blocks
AND child frames in document order. Tables are serialized via
`TableSerializer::serialize()`.

## Files and Responsibilities

| File | Role | Fragility |
|------|------|-----------|
| `SceneCoordinator.cpp` | Orchestrates load, reparse, table conversion | Medium — the reparse path is now simple |
| `MarkdownTextItem.cpp` | `allMarkdown()`, `buildHighlightingSource()`, inline substitution | Medium — offset-sensitive |
| `TableConverter.cpp` | Pipe text → QTextTable, reparse reconciliation | Low — reconcile is read-only |
| `TableSerializer.cpp` | QTextTable → auto-formatted pipe text | Low — standalone utility |
| `TextControl.cpp` | Tab/Enter/Escape/arrow navigation in tables | Low — isolated key handlers |
| `MarkdownHighlighter.cpp` | Frame skip guard, span application | Medium — the guard is critical |
| `Editor.cpp` | Table signals, operation slots, context menu | Low |

## Testing

Run all markoff tests before committing any table-related change:
```bash
cd build && ctest -R markoff --output-on-failure
```

Key test files:
- `tst_table_serializer.cpp` — round-trip serialization
- `tst_table_converter.cpp` — pipe text → QTextTable conversion, reconcile behavior
- `tst_table_navigation.cpp` — keyboard navigation
- `tst_table_operations.cpp` — insert/delete row/column via API
- `tst_table_integration.cpp` — end-to-end through Editor
- `tst_table_bugs.cpp` — regression tests: remnant text, repeated insert-row-above
- `tst_table_diagnostics.cpp` — structural diagnostics for the showcase

**Manual smoke test** (always do this for visual changes):
```bash
./build/bin/markoff-testapp libs/markoff/tests/showcase.md
```
Check: tables render, text after tables has correct styling, math renders,
headings are styled, no remnant pipe text fragments.

## Common Mistakes

### Don't forget to invalidate the source position span cache.

After setting a new span map via `setSpanMap()` in the reparse path,
call `textItem->invalidateSourcePositionSpans()`. Otherwise
`refreshInlineSubstitutions()` restores the old cached spans.

### Don't let the table converter's endPos eat into adjacent content.

The tree-sitter table boundary may not include trailing newlines. When
expanding to line boundaries, do NOT include the trailing newline — it
serves as a separator between the table and whatever follows. Including
it causes the converter to consume adjacent tables or text.

### Don't run table conversion without stripping substitutions first.

Math glyphs (U+FFFC) are 1 character each, but the source text they replace
can be 50+ characters. If you convert tables while substitutions are present,
the converter's offsets (from the raw segment text) point past the actual
document positions.

### Don't call convert() from reconcile().

`reconcile()` is called during the non-structural reparse path. At that point,
tables already exist as `QTextTable` frames in the document. Calling `convert()`
would use pipe-text coordinates (from `allMarkdown()` or `detectTableRegions()`)
on a document that has a completely different coordinate space. This was the
root cause of a data-loss bug where repeated "Insert Row Above" destroyed
table content and subsequent document sections.

## Vendored Tree-Sitter Grammar

The tree-sitter-markdown grammar is vendored at
`libs/markoff-parser/src/vendor/tree-sitter-markdown/`. We control it fully.

### Grammar fix: GFM delimiter row validation

The upstream tree-sitter-markdown grammar had a bug in its `scanner.c` where
the delimiter row parser accepted all-whitespace cells between pipes as valid
delimiter cells. This violated the GFM spec, which requires each delimiter
cell to contain at least one hyphen (`-`).

The practical impact: when a table had 2+ consecutive empty pipe rows (e.g.
after repeated "Insert Row Above" on the header row), tree-sitter would
interpret one empty row as a "header" and the next as a "delimiter", splitting
a single table into multiple `pipe_table` AST nodes. This is now fixed — the
delimiter row parser returns false for cells containing only whitespace.

If you update the vendored grammar from upstream, verify this fix is preserved
in `tree-sitter-markdown/src/scanner.c` in the `parse_pipe_table()` function's
delimiter row loop: a `|` encountered after only whitespace (before any dashes)
must reject the row, not count the cell.

## Where We Go From Here

### Immediate follow-ups

- **Table visual styling** — The tables currently use Qt's default
  `QTextDocumentLayout` rendering (basic grid). `TableStyle` struct exists
  with theming defaults but isn't wired into rendering yet. Header background,
  grid line colors, cell padding need to be applied via `QTextTableFormat`
  and `QTextTableCellFormat`.

- **Smart cursor entry** — The spec calls for x-position → nearest column
  mapping when arrowing into a table. Currently falls back to first/last cell.

### Scope (b)

- Column alignment controls (UI in context menu + API slots)
- Row/column move operations
- Column resize drag handles
- Auto-format pipe text on every cell edit (live width normalization)
- Header row bold styling, theming

### Scope (c)

- Spreadsheet formulas
- Column sorting
- CSV import/export
- Full inline objects in cells (requires per-cell span map — see below)
- Rectangular paste-into-table

### Architectural notes for scope (c)

Full inline formatting in cells requires the span map to cover table cell
content. The clean approach is per-cell parsing: each cell gets its own
mini span map in cell-local coordinates. This is analogous to Obsidian's
approach (per-cell CodeMirror instances) but in Qt terms. The current
architecture doesn't need to change — per-cell parsing would be a parallel
path added alongside the existing document-level highlighting, not a
replacement for it.
