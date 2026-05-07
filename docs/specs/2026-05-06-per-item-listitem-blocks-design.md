# Per-Item ListItem Blocks Design

**Date:** 2026-05-06
**Status:** Draft for review
**Replaces:** the "list = one block containing multi-line text" compromise that
landed in D2's `materializeBlocksFromParsedDoc` and propagated through D3.

## Why we're doing this

The D-arc plan said *one block = one semantically atomic unit, with per-block
CRDT buffers, parser-driven kinds.* In `materializeBlocksFromParsedDoc` we hit
a comment that has cost us two days of dogfood pain:

> *"For ListTight/ListLoose, full list source is stored; item-level unwrapping
> is deferred (the v1 parser doesn't expose item byte ranges)."*

That deferral is wrong on its premise — tree-sitter-markdown **does** expose
`list_item` nodes with byte ranges (verified in
`vendor/tree-sitter-markdown/.../node-types.json:401`). We just didn't wire
them. The whole-list-as-one-block compromise then forced a regex-driven marker
parser into `LiveStructuralKeyHandler`, manual renumbering, a multi-trailing-
`\n` strip in the model, vestigial `indentLevel` block attributes that
pretend to be per-item, and a five-API cursor-delivery surface to handle
in-block-multi-line text races.

We delete all of that by fulfilling the original plan: each list item is one
CRDT block.

## Goal

Turn every `list_item` parser node into one `BlockKind::ListItem` block with:
- `indent` (int) — nesting depth from outer `list` ancestors
- `markerKind` (enum) — Dot ("1."), Paren ("1)"), Minus, Plus, Star, TaskUnchecked, TaskChecked
- `markerNumber` (int, 1+) — only meaningful for ordered (`Dot`/`Paren`)
- buffer text — **the item's content only** (no marker, no leading whitespace,
  no item-separator newlines)

Adjacent ListItem blocks render visually as a list. Renumbering is a walk over
consecutive same-indent ordered items in the model, run after every change in
`onD2Changed`.

## Non-goals

- No collaborative editing changes. Per-block CRDT buffers and the per-block
  undo UI stay.
- No interactive-block changes. Math, Image, HR, CodeBlock, Heading,
  Paragraph, Blockquote stay as they are.
- No parser-side rewriting. Tree-sitter-markdown stays vendored as-is. We
  only consume different nodes from it.
- No source-format change. Round-trip `loadFromMarkdown(src)` →
  `serializeToMarkdown()` produces the same bytes (the marker + indent are
  reconstructed from attrs at serialize time).

## Target block representation

For source:
```
1. one
2. two
   - sub a
   - sub b
3. three
```

The current model has one block:
```
ListItem text="1. one\n2. two\n   - sub a\n   - sub b\n3. three" indentLevel=0
```

The target model has five blocks, in `iterateBlocks()` order:
```
ListItem text="one"   indent=0  markerKind=Dot    markerNumber=1
ListItem text="two"   indent=0  markerKind=Dot    markerNumber=2
ListItem text="sub a" indent=1  markerKind=Minus
ListItem text="sub b" indent=1  markerKind=Minus
ListItem text="three" indent=0  markerKind=Dot    markerNumber=3
```

The `text` is the item's content **without** the marker. Marker presentation
is the delegate's responsibility (or `LiveBlockModel` synthesizes a display
prefix from attrs).

## Marker rendering: where does the "1." come from?

Two choices, with a recommendation:

**A. Delegate prepends from attrs.** `ListItemDelegate.qml` reads
`model.markerKind` + `model.markerNumber` + `model.indent` and renders the
marker as a non-editable label to the left of the TextEdit. The TextEdit
holds only the content text. **Recommended.** This is what makes Tab,
renumbering, and indent visualization clean — the marker is not in the
editing buffer.

**B. Buffer text includes the marker.** `text="1. one"` like today, but per
item. Source-faithful in the buffer, but every Enter handler re-parses the
marker out, the cursor model is awkward (qtPos=0 sits before "1"), and Tab
has to manipulate leading whitespace.

I'm going with **A** in this spec. Buffer is content-only; marker is
display-only-from-attrs; serialization reconstructs `"<indent><marker> <text>"`.

## Parser-side changes

`libs/markoff-parser/src/TreeSitterParser.cpp`:

1. **`TopLevelBlock::Kind`** — add `ListItem`. Keep `ListTight`/`ListLoose`
   only if anything else uses them (probably retire — grep says foundation's
   `mapTopLevelKind` is the only consumer and switches on them).
2. **`classifyTopLevelKind`** — `"list_item"` → `Kind::ListItem`. `"list"`
   nodes are *not* emitted as TopLevelBlocks anymore — they're traversed for
   their children only.
3. **`collectTopLevelBlocks`** — when the walker hits a `list` node, recurse
   into its `list_item` children. Track an `indentDepth` counter incremented
   per nested `list`. Each `list_item` gets emitted as one `TopLevelBlock`.
4. **`TopLevelBlock` fields** — add:
   - `int indentDepth` (0-based)
   - `enum class MarkerKind { Dot, Paren, Minus, Plus, Star, TaskUnchecked, TaskChecked }` `markerKind`
   - `int markerNumber` (only set for Dot/Paren)
   - The `byteStart` / `byteEnd` are now the item's **content** range, not
     the whole item including marker and trailing newline. Compute by:
     `content_start = first non-marker child's start_byte`,
     `content_end = last non-marker child's end_byte` (strip trailing `\n`s).
5. **Test:** `tst_parser_list_items` (new) — a fixture markdown with mixed
   ordered, unordered, nested, task-list, and verifies one TopLevelBlock per
   item with correct indent/marker.

## Foundation-side changes

`libs/markoff-foundation/src/MarkoffDocument.cpp`:

1. **`mapTopLevelKind`** — `Kind::ListItem` → `BlockKind::ListItem`. Remove
   the `ListTight`/`ListLoose` cases.
2. **`materializeBlocksFromParsedDoc`** — for `tb.kind == ListItem`, set
   block attrs from `tb.indentDepth`, `tb.markerKind`, `tb.markerNumber`.
   The buffer content is `bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart)`
   (which is now content-only, per parser fix above). **Delete** the comment
   about "item-level unwrapping is deferred."
3. **`AttrNames.h`** — add `MarkerKind`, `MarkerNumber`. Replace
   `IndentLevel` with `Indent` (rename for clarity; `IndentLevel` was
   ambiguous when block ≠ item).
4. **Serialization** (`saveToMarkdown` / wherever we emit markdown) — when a
   ListItem is encountered, prepend `<indent_spaces><marker> ` to the buffer
   text. Marker text from `markerKind` + `markerNumber`:
   - `Dot` → `"<n>."`
   - `Paren` → `"<n>)"`
   - `Minus`/`Plus`/`Star` → `"-"` / `"+"` / `"*"`
   - `TaskUnchecked` → `"- [ ]"`
   - `TaskChecked` → `"- [x]"`
5. **Test:** `tst_d2_list_roundtrip` (new) — load → iterate blocks → assert
   per-item structure → serialize → assert source identity for tight,
   loose, nested, ordered, unordered, mixed, task-list cases.

## Live-render-side changes

### `KindTransition.cpp`

- **Delete** the ListItem detection branch from `inferBlockKind`. Per-item
  blocks come pre-typed from the parser. The only kind transitions that
  remain are *typed paragraph becoming heading/list/quote/etc.* — and
  *that* heuristic stays, but it gets simpler because it now runs against
  one item's text, not multi-line list text.
- The list regex `^[ \t]{0,3}([-*+]|\d+[.)])\s` still fires when a
  Paragraph block's text matches it — that's a Paragraph→ListItem
  promotion. Keep that.

### `LiveListModelBinding.cpp`

- **Delete** the `while (raw.endsWith('\n')) raw.chop(1)` hack. Items have
  no trailing soup; one trailing newline (block delimiter) is stripped per
  the original `if`. Restore the `if` form.
- **Add** a `renumberOrderedLists()` pass after `applyOps`:
  ```
  walk records left-to-right
  track per-(indent, separator) expected-next-number
  on entering a new ordered run, seed expected from first item's markerNumber
  on each ordered item: if markerNumber != expected, queue an attr edit
  apply queued edits in one transaction
  ```
  The pass is idempotent: a fixed-up document re-runs the pass and emits
  zero edits, so `d2DocumentChanged` doesn't recurse.
- **Delete** the structural-row-changed cursor-delivery race plumbing I was
  about to write. With per-item blocks, an Enter creates a *new row*, which
  fires `structuralRowsInserted` — `requestTextCaretAtNewRow` already
  handles this. In-row text edits don't change row identity, so no race.

### `LiveStructuralKeyHandler.cpp`

This file shrinks dramatically. The ListItem section becomes:

- **Enter at end of a non-empty item** → `Cmd::insertListItemAfter(doc, blockId)`.
  Helper inserts a new ListItem block after, copies `indent` + `markerKind`,
  sets `markerNumber = current + 1` for ordered. `requestTextCaretAtNewRow(blockIndex+1, 0)`.
- **Enter mid-item** → split: truncate current item's buffer at byteOff,
  `insertListItemAfter`, set new item's buffer = the suffix, copy marker.
  Cursor at start of new row.
- **Enter at start of non-empty item** → `insertListItemBefore` (or
  `enterAtEnd` of previous block, falling back to head insert) with same
  marker/indent. Cursor stays at original row.
- **Enter on empty item** → if `indent > 0`: decrement indent attr.
  Otherwise: change kind to Paragraph (clears attrs, leaves empty buffer).
- **Tab** → `indent` attr += 1 (cap at 6 or whatever the max is).
- **Shift-Tab** → `indent` attr -= 1, clamp at 0; if 0 and Shift-Tab again,
  no-op (not list-exit; that's Enter-on-empty).
- **Backspace at qtPos=0** → if `indent > 0`: decrement. Otherwise: merge
  with previous block. If previous is ListItem at same/lower indent, merge
  contents and remove this block (`Cmd::backspaceMerge` already does this).
  If previous is Paragraph, demote this to Paragraph then merge.
- **Delete at qtPos=length** → merge next block in (`Cmd::deleteMerge`).

**Deleted from this file:** the marker-prefix regex (`kMarker`), the
ordered-marker regex (`kOrd`), the renumber tail-builder, the `atLineStart`
branch with its renumber-around-the-cursor logic, the multi-line `lineStart`
scan, the `newlineCount`-based "single-vs-multi-item-block" branching, the
`while raw.endsWith` reconstruction. All of it.

Net delta in `LiveStructuralKeyHandler.cpp`: roughly `-160 LOC, +60 LOC` for
the ListItem section. Other sections (Paragraph, Heading, CodeBlock, etc.)
are unchanged.

### `LiveCursorState.cpp`

- **Collapse** to two public APIs:
  `requestTextCaretAtNewRow(int row, int qtPos)` — pure pending, resolves on
   `structuralRowsInserted` covering `row`. (Existing semantics.)
  `requestTextCaretAtAnchor(BlockAnchor anchor, int qtPos)` — pure pending,
   resolves on `structuralRowsInserted` or
   `anchorRenumbered` for that anchor. (Existing semantics, but now the
   only "follow this block" API.)
- **Delete** `requestTextCaretAtRow` (the immediate-resolve variant). It was
  a band-aid for in-block multi-line edits; with per-item blocks it has no
  callers.

### `LiveBlockModel.h/cpp`

- **Add roles:** `MarkerKindRole`, `MarkerNumberRole`, `IndentRole`.
- `BlockRecord` gets corresponding fields, populated from attrs in
  `LiveListModelBinding::onD2Changed`.

### `Cmd::D2.cpp`

- **Add** `insertListItemAfter(MarkoffDocument&, BlockId currentItem)` —
  inserts a new ListItem after, copies `indent`, `markerKind`, sets
  `markerNumber` to +1 for ordered. Returns the new BlockId.
- **Add** `insertListItemBefore(MarkoffDocument&, BlockId currentItem)` —
  inserts a new ListItem before. Cursor stays in current row visually.

### `qml/delegates/ListItemDelegate.qml`

- **Render** the marker as a non-editable `Text` element to the left of the
  TextEdit, populated from `model.markerKind` + `model.markerNumber`.
- The TextEdit shows `model.text` (content only). Cursor positions are
  relative to content — much cleaner.
- **Indent** rendering uses `leftPadding: 8 + model.indent * indentWidth`.
  Marker label sits inside the padding.
- Structural keys still go through `tryHandle` like today, with no
  handler-side regex.

## Tests to update

| Test | Action |
|---|---|
| `tst_live_render_structural::list_item_*` (mine) | Rewrite. New invariants: rowCount changes with structural ops; in-row text doesn't multi-line; marker comes from attrs. |
| `tst_live_render_kind_transition` | Drop the ListItem detection cases (parser handles it). Keep paragraph→list promotion. |
| `tst_live_render_block_model` | Add tests for the new MarkerKind/MarkerNumber/Indent roles. |
| `tst_d2_list_roundtrip` (new) | Foundation roundtrip test. |
| `tst_parser_list_items` (new) | Parser emits one TLB per `list_item`. |

## Migration risks

**Source-faithful round-trip.** The parser→model materialization drops the
marker text and indent whitespace; serialization must reconstruct them. The
roundtrip test needs to cover edge cases: tab vs space indent, ordered
markers with `1.` vs `1)` vs `01.` (CommonMark allows zero-padded but
weirdly), task-list with `[x]` vs `[X]`, blank lines between items
("loose" lists). Loose vs tight is a property of the list as a whole — we
may need a `looseRun` boolean attr on each ListItem to remember whether it
was in a loose list, and serialize blank lines accordingly.

**Existing markdown files.** Anything currently saved or in test fixtures
loads through `loadFromMarkdown` which goes through the parser, so it picks
up the new representation automatically. No migration script needed.

**Per-block undo with structural changes.** Per-block undo today undoes only
buffer edits within that block. After this change, a renumber pass that
fixes 3 markers does 3 attr edits — undo on any one item undoes its number
back to the wrong value. The renumber pass should be marked as
"non-undoable" or grouped with the originating user action via
`UndoLog::Transaction` so the whole renumber undoes/redoes together. Spec
detail: bracket the renumber edits with the user's transaction by passing
the same `Transaction&` from the originating Enter handler.

(Actually — the renumber pass runs in `onD2Changed`, which is *outside* any
caller's transaction. The cleanest fix: detect renumber-needed in the
*caller* (the structural key handler) and queue the marker edits in the
caller's transaction. That makes renumber a *consequence of the user
action*, not a separate model concern. Reconsider where renumber lives:
either caller-driven (in handlers, with a shared helper) or
post-applyOps (uncoupled, but undo-fragile). Open question — favoring
caller-driven for undo coherence.)

**Open question on the renumber location.** Caller-driven means each
structural handler that affects ordered numbering (Enter, Backspace merge,
Delete merge, indent change, kind change to/from ListItem) calls a
`renumberAround(blockId)` helper. Post-applyOps means a single pass after
every model update. Caller-driven is more code but undo-coherent.
Recommend caller-driven; revisit if it gets noisy.

## What gets deleted

Rough LOC estimate (positive = deletion):

- `LiveStructuralKeyHandler.cpp` ListItem Enter/Backspace/Delete: **+100 LOC**
  (regex, multi-line scan, renumber loop, atLineStart branch, marker
  reconstruction)
- `LiveListModelBinding.cpp` `while raw.endsWith('\n')`: **+5 LOC**
- `LiveCursorState.cpp` `requestTextCaretAtRow` immediate-resolve path,
  `resolvePendingForRow`: **+30 LOC**
- `KindTransition.cpp` ListItem regex: **+5 LOC**
- Compound `tst_live_render_structural` integration tests built around the
  multi-line-block assumption: **+80 LOC** (replaced by simpler per-item
  tests)

Rough LOC additions:

- Parser `collectTopLevelBlocks` recursion into list_item: **-40 LOC**
- Foundation marker/indent/markerNumber attr handling + serialize: **-30 LOC**
- `Cmd::insertListItemAfter`/`Before` helpers: **-30 LOC**
- ListItemDelegate marker rendering: **-30 LOC**
- New foundation roundtrip test: **-50 LOC**

**Net: ~+60 LOC deleted overall**, dramatic complexity reduction in the
hottest path (`LiveStructuralKeyHandler`), and the two-day class of dogfood
bugs (phantom newlines, marker race, no-renumber-on-delete, cursor delivery
race for in-block multi-line edits, nested-list Tab broken) **stops
existing**.

## Plan after this spec

If approved:

1. **Day 1 morning** — parser side. Wire `list_item` walking, marker info,
   indent depth. New `tst_parser_list_items`.
2. **Day 1 afternoon** — foundation side. Update `materializeBlocksFromParsedDoc`,
   add attrs, write serializer, new roundtrip test.
3. **Day 2 morning** — live-render side. Rewrite ListItem handlers
   (deletions + new helpers), update model roles, update delegate.
   Implement caller-driven renumber.
4. **Day 2 afternoon** — collapse cursor APIs, update tests, run full suite,
   dogfood.

Half a day per side, full day for the live-render rewrite given the test
churn. Two days total. The result is the feature set we promised:
block-based editing with interactive blocks mixing freely with familiar
text editing, and lists that *are* blocks.

## Open questions for review

1. **Marker as attr vs in buffer text.** Spec recommends attr. Confirm?
2. **Loose lists** — separate `looseRun` attr per item, or a property of
   the run? Implementation detail; either works.
3. **Renumber location** — caller-driven vs post-applyOps. Spec recommends
   caller-driven. Confirm?
4. **`indentLevel` rename to `indent`** — is the rename worth the churn,
   or keep the old name for compat with anything that already references it?
5. **Task lists** — fully in scope here, or punt to a follow-up? Markers
   exist in the parser; indent + content are the same as bullet items;
   `markerKind = TaskChecked/TaskUnchecked` covers it. I'd include it; it's
   <20 extra LOC.
