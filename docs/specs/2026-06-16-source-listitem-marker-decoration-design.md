# Source-view list-item marker decoration — design (queue #8.3)

**Date:** 2026-06-16. **Scope:** `markoff-source` (`Markoff::Source::Editor`)
only. Does not touch `markoff-styled` or `markoff-live`.

## Problem

`markoff-source` seeds and edits its `QPlainTextEdit` from
`MarkoffDocument::widgetFlatView()`. For every `BlockKind` except
`ListItem`, `widgetFlatView()`'s per-block text already contains the raw
markdown marker (`# `, `> `, fence lines, …) because the tree-sitter
harvest keeps those bytes inside the block's content range. `ListItem` is
the one exception: `harvestListItem` narrows the parser byte range to
**after** the marker before the buffer is ever populated (see
`markoff-core/CLAUDE.md` "Block buffer convention" +
`MarkoffDocument::materializeBlocksFromParsedDoc` comment on
`isSetext`/ListItem, `MarkoffDocument.cpp` ~line 2047). So a `- foo` list
item's `blockText()` is `"foo"` — the marker was never in the buffer to
begin with, unlike the other kinds where it's `blockText()` content that
KSyntaxHighlighting merely colors.

Source view's whole purpose is "raw markdown visible," so `foo` instead
of `- foo` is a real gap. `serializeForSave()` already knows how to
reconstruct the marker (`markerForListItem` in `MarkoffDocument.cpp`) —
the gap is that this reconstruction never reaches the live-edited
`QPlainTextEdit`.

## Decision (per docs/INVARIANTS.md #2 discipline, applied to this seam)

**Decoration wins, not content.** The list marker is rendered as a
paint-time decoration over reserved `QTextBlockFormat` left-margin space,
**not** inserted as literal `QTextDocument` text. `SourceTextDocumentBinding`,
`Detail::findBlockAtSepByte`, `applyFlatEdit`, and every other byte-math
consumer of `widgetFlatView()` are **unchanged** — the flat view the
binding edits against stays marker-free, exactly as it is today.

**Why not the content-injection option (queue's option "a"/"b" content
variant).** Splicing the marker into the actual edited text requires the
shared `Detail::findBlockAtSepByte` (consumed by both `markoff-source`
*and* `markoff-styled` via `SourceTextDocumentBinding`) to become aware of
a per-block prefix-skip, i.e. a second per-kind "view width" alongside the
existing separator-width parameter. `docs/queue.md`'s Discipline Log
already carries **three** prior incidents in exactly this bug class ("one
flat-text view changed separator/prefix width, a sibling byte-walk
didn't": the 2026-05-27 `SEP_LEN` underflow, the 2026-05-30
`setHeadingLevel` `SEP_LEN=2` staleness, and the 2026-06-09
`SourceFindAdapter` `globalChar += 2` drift). Reproducing that shape for a
purely cosmetic gap is a bad trade. Decoration-only keeps the change
additive to rendering and leaves the editing seam's byte math untouched.

**Trade-off, accepted and documented:** the marker is not selectable or
copyable as text (it isn't in the `QTextDocument`). If a future dogfood
pass wants "select-all copies raw markdown including markers," that is
new scope — tracked as a v0.2 follow-up, not silently implied by this fix.

## Shape

- New `MarkoffDocument::listItemDisplayMarker(BlockId) const` (public,
  alongside `widgetFlatView()`): returns the indent+marker+`" "` bytes for
  a `ListItem` block (reusing the exact reconstruction rules
  `serializeForSave()` already uses via `markerForListItem`), empty
  `QByteArray` for every other kind. Single source of truth — no
  duplicated marker logic between save and display.
- `Source::Editor` gains `applyListItemMarkerDecorations()`, run from the
  same `d2DocumentChanged` hook as `applyParagraphMargins()` (mirrors the
  existing pattern at `Editor.cpp:451`). It walks `qdoc->begin()..end()`
  zipped against `doc->iterateBlocks()` (1:1 per WP unification — one
  `QTextBlock` per model block), and for each `ListItem` block: sets
  `QTextBlockFormat::setLeftMargin()` wide enough for the marker text
  (via `QFontMetrics`) and records the marker string in a per-block side
  table (keyed by `QTextBlock` block number, rebuilt each call — no
  persistent cache to go stale).
- `Detail::InnerEditor::paintEvent` override: call
  `QPlainTextEdit::paintEvent` first (text renders in the margin-adjusted
  position, leaving the reserved gap blank), then draw each visible
  block's marker string into that gap — same visible-block iteration
  `Gutter::paintEvent` already uses (`firstVisibleBlock()` +
  `blockBoundingGeometry`/`blockBoundingRect` walk), just painted into the
  viewport instead of the side gutter.

## Falsifiable test

`tst_source_widget_listitem_marker.cpp`, driven through the real
`Markoff::Source::Editor` widget (production callsite — not a direct
`MarkoffDocument` API call, per invariant #5): load `"- foo\n"`, assert
`editor->listItemMarkerFor(row) == "- "` (a thin test-only accessor
exposing the side table) and that the block's `leftMargin() > 0`. Proven
failing pre-fix (no decoration wired up), passing post-fix.
`plainTextEdit()->toPlainText()` is asserted to stay `"foo"` (unchanged
edit-seam text) both before and after, pinning the "decoration, not
content" contract so a future change can't silently flip it back to
content-injection without this test failing first.
