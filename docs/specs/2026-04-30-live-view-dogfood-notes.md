# Live-view dogfood notes

**Date:** 2026-04-30
**Branch:** `exploration/new-foundation`
**Tester:** clinton
**Build:** tip of `exploration/new-foundation` (commit `2ed66d9` + uncommitted `libs/jkqtmathtext` sibling)
**Command:** `./build-dev/bin/markoff-view-qml-app --live docs/specs/2026-04-29-live-render-design.md`

Findings that are NOT already covered by the v0 deferred-scope list in
`docs/specs/2026-04-29-live-render-design.md` §8 or by the spike findings.

---

## F1 — Selection asymmetry on tables

**Observation:** when a drag-selection *begins inside* a table cell, the
selection extends at character / cell granularity within the table —
crossing one cell to the next at cell boundaries. When a drag-selection
*begins outside* the table (e.g. in the paragraph above) and crosses into
the table, the *entire table block* highlights instead of resolving to a
cell-granular offset.

**Why this is a real finding (not the §8 deferral).** The
`docs/specs/2026-04-29-cross-block-selection-spike-findings.md` §C-style
note says only "a table cell is selected at a finer grain than this
model handles". That acknowledges tables-need-special-treatment in the
abstract but does not capture this specific asymmetry: the same
`hit() → positionAt(localX, localY)` pipeline is used in both intra- and
cross-block drag (LiveView.qml — the `onPressed` and `onPositionChanged`
handlers funnel through one `hit()` function), so getting different
resolution depending on the drag's starting block is a property of how
`positionAt` resolves coordinates inside an `MarkdownText`-formatted
table's TextEdit, not of the model.

**Mechanism (confirmed parts).** v0 has no `TableDelegate`. Per
`BlockWalker.cpp:139-140`, every block that isn't image / code-block /
heading / hr defaults to `BlockKind::Paragraph` — including tables. The
table's full markdown source becomes the `text` of a single
`ParagraphDelegate`, whose TextEdit uses `textFormat: TextEdit.MarkdownText`
(`ParagraphDelegate.qml:30`). Qt parses that markdown into its rich-text
document model — the table is laid out as a `QTextTable` with rows and
cells (not HTML; Qt's `QTextDocument` rich-text). `TextEdit.positionAt(x, y)`
resolves against that rich-text geometry.

**Hypothesis for the asymmetry (unverified).** The intra-block case
benefits from the press's local x/y landing on a table-cell text run,
so `positionAt` returns a position inside the underlying source-string
of that cell. The cross-block case enters the table at its top edge,
where local y is in the table's top padding / border region above the
first row, and `positionAt` may return `0` or `text.length` (or a
sentinel-like value) — combined with the existing range builder, that
paints the whole block.

**Why this matters beyond tables.** Any future block kind that renders
as a Qt rich-text container with non-trivial vertical padding (callouts,
embeds, blockquote with rendered children) is at risk of the same
asymmetry — the cross-block-entry edge of the block is the failure mode.
A test fixture for this should exist before TableDelegate / CalloutDelegate
ships.

**Suggested investigation when picking this up.**

1. Reproduce in the test app with a fixture markdown that has a paragraph
   immediately above a small markdown table. Drag from paragraph into a
   middle cell. Log `hit().offset` at the moment cy first crosses into
   the table block.
2. If the offset is 0 or text.length, instrument `positionAt(localX, localY)`
   for the affected delegate; verify what TextEdit's MarkdownText-formatted
   document returns for coords above the first row of an HTML table.
3. Decide whether the fix is in `hit()` (clamp localY into the visible
   text region of the delegate before calling positionAt) or in a
   per-block-kind override of how cross-block-entry resolves.

**Status:** captured for the eventual TableDelegate / cross-block
selection follow-up. Not blocking v0.
