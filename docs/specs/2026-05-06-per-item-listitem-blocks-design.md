# Per-Item ListItem Blocks Design

**Date:** 2026-05-06
**Status:** Approved (open questions resolved 2026-05-06; see §"Open questions" at end).
**Relationship to D3:** This spec is **not a redesign of D3** — it is the
corrective spec that fulfills D3 §1 premise 6 ("Each list item is a separate
`BlockKind::ListItem` block in IdList"). The D3 *spec* always required
per-item granularity; the D3 *implementation* compromised in
`materializeBlocksFromParsedDoc` ("item-level unwrapping is deferred").
That deferral is the root of the dogfood bug class around lists. This spec
removes the deferral and brings the implementation back in line with D3.

**Position in the D arc:** D3-correction. After this lands, D3 is complete
in spirit and substance (not just in checkbox count). D4 / D5 are unchanged.

**Read first:**
1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D arc orientation
2. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items
3. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — D3 binding
   spec; §1 premise 6 and §7 (`AttrNames`) are the load-bearing parts this
   spec fulfills
4. `docs/specs/2026-05-04-d2-foundation-reshape-design.md` §6 — parser
   surface contract (we add one extra parser surface here, see §"Parser-side")

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

Turn every `list_item` parser node into one `BlockKind::ListItem` block with
these attributes (all stored in the per-block `BlockAttrsMap`, AttrNames declared
in `markoff-foundation/AttrNames.h`):

| AttrName | Type | Meaning | Set on |
|---|---|---|---|
| `IndentLevel` | int (0+) | Nesting depth from outer `list` ancestors | every ListItem |
| `MarkerStyle` | QString | Marker shape: `"dot"` / `"paren"` / `"minus"` / `"plus"` / `"star"` / `"task"` | every ListItem |
| `MarkerNumber` | int (1+) | Sequence number; **only set for `MarkerStyle ∈ {"dot", "paren"}`** | ordered ListItems |
| `Checked` | bool | Task-list checkbox state; **only set for `MarkerStyle == "task"`** | task-list ListItems |
| `LooseRun` | bool | Whether this item belongs to a loose list (blank lines between items) | every ListItem |

Buffer text is **the item's content only** — no marker, no leading indent
whitespace, no trailing newlines. The buffer holds what the user actually
edits as text within the item. Marker and indent are presentation +
serialization concerns reconstructed from attrs.

Adjacent ListItem blocks render visually as a list. Renumbering is a
caller-driven helper invoked from each structural handler that affects
ordered-marker sequencing (see §"Renumbering" below).

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
ListItem text="one"   IndentLevel=0  MarkerStyle="dot"   MarkerNumber=1  LooseRun=false
ListItem text="two"   IndentLevel=0  MarkerStyle="dot"   MarkerNumber=2  LooseRun=false
ListItem text="sub a" IndentLevel=1  MarkerStyle="minus"                 LooseRun=false
ListItem text="sub b" IndentLevel=1  MarkerStyle="minus"                 LooseRun=false
ListItem text="three" IndentLevel=0  MarkerStyle="dot"   MarkerNumber=3  LooseRun=false
```

For the same source as a *loose* list (blank lines between items):
```
1. one

2. two
```
Both items get `LooseRun=true`. Serialization emits the blank lines back.

The `text` is the item's content **without** the marker. Marker presentation
is the delegate's responsibility — `ListItemDelegate.qml` reads `MarkerStyle`
+ `MarkerNumber` + `Checked` from `model.attrs` and renders a non-editable
marker label to the left of the TextEdit. Indent is rendered as left padding.

## Marker storage and rendering

Marker lives in attrs, **not** in the buffer text. Buffer is content-only.
The `MarkerStyle` + `MarkerNumber` encoding (two attrs, not one mashed
QString like `"1."`) is chosen so renumbering is a single int LWW edit per
item, not a string parse-and-rewrite.

`ListItemDelegate.qml` renders the marker as a non-editable `Text` element
to the left of the TextEdit, populated from `model.attrs`:

| `MarkerStyle` | Rendered prefix |
|---|---|
| `"dot"` | `"<MarkerNumber>."` (e.g., `"3."`) |
| `"paren"` | `"<MarkerNumber>)"` |
| `"minus"` | `"-"` |
| `"plus"` | `"+"` |
| `"star"` | `"*"` |
| `"task"` | clickable checkbox; `"[ ]"` when `Checked=false`, `"[x]"` when `true` |

The marker label sits inside the TextEdit's `leftPadding`. `leftPadding =
8 + IndentLevel * indentWidth` puts the marker at the right indent level.
The TextEdit holds the content text only; cursor positions are relative
to content (qtPos=0 is at the start of "one", not before "1").

For task lists, click on the checkbox toggles `Checked` via
`d2SetBlockAttr` (one attr edit, picked up by the next `onD2Changed` cycle).
This is a clean stress-test of the per-item architecture: if the toggle
doesn't show up correctly, something is wrong with the attr-edit path.

Serialization (`serializeForSave`) reconstructs the source line from attrs:
`"<indent_spaces><marker> <text>"`, joined with `\n` between items, with an
extra `\n\n` between items in the same `LooseRun=true` run.

## Renumbering

**Caller-driven**, in a shared helper `Cmd::renumberRunStartingAt(doc,
blockId, transaction)`. Each structural handler that may have changed
ordered-marker sequencing calls this helper inside its own
`UndoLog::Transaction`. The helper:

1. Walks forward and backward from `blockId` to find the contiguous run of
   `ListItem` blocks at the same `IndentLevel` with the same
   `MarkerStyle ∈ {"dot", "paren"}` (one run = one ordered list visually).
2. Assigns `MarkerNumber = (firstItem.MarkerNumber + offset)` for each
   item in the run.
3. Issues `d2SetBlockAttr(blockId, MarkerNumber, n, transaction)` for each
   item where the new number differs from the stored value.

Callers:
- `ListItem` Enter handler (insert-after, insert-before, mid-split)
- `Cmd::backspaceMerge` and `Cmd::deleteMerge` (when the merged-out block
  was a ListItem)
- Tab / Shift-Tab handlers (indent change re-runs items into different
  contiguous runs)
- Kind-transition (Paragraph → ListItem promotion seeds a new run; ListItem
  → Paragraph demotion may re-seed the surviving run)

**Why caller-driven, not post-applyOps in `onD2Changed`.** D2 §4.2
specifies one `UndoEntry(actionId, targets[])` per user action.
Caller-driven puts the renumber ops into the *same* transaction as the
originating action, so undo is one step. A post-applyOps pass would create
a second `UndoEntry`, leaving an undo to "1. one\n2. two\n3. \n3. three"
intermediate state that requires a second undo to actually revert.

**Known gap (out of scope for this spec).** Selection-spanning-multiple-
blocks-then-Delete goes through the QML TextEdit's native handling →
`LiveEditBinding::onContentsChange`, not through a structural handler. With
per-item blocks, multi-block selection is a separate refactor (selection
model needs to handle cross-block ranges). When that lands, the cross-
block-delete path will need to call `Cmd::renumberRunStartingAt` too.

## Parser-side changes

`libs/markoff-parser/src/TreeSitterParser.cpp`:

1. **`TopLevelBlock::Kind`** — add `ListItem`. Retire `ListTight` and
   `ListLoose` (their only consumer is foundation's `mapTopLevelKind`,
   which switches over after this change).
2. **`classifyTopLevelKind`** — `"list_item"` → `Kind::ListItem`. `"list"`
   nodes are *not* emitted as TopLevelBlocks anymore — the walker
   traverses into them but only emits their `list_item` children.
3. **`collectTopLevelBlocks`** — when the walker hits a `list` node,
   recurse into its `list_item` children. Track an `indentDepth` counter
   incremented per nested `list`. Each `list_item` emits one
   `TopLevelBlock`. Also track `looseRun` (true if the parent `list` node's
   tree-sitter type indicates loose; tree-sitter-markdown emits
   distinct types for this).
4. **`TopLevelBlock` fields** — add:
   - `int indentDepth` (0-based; from nested-`list` ancestor count)
   - `QString markerStyle` (one of the strings from §"Goal":
     `"dot"`/`"paren"`/`"minus"`/`"plus"`/`"star"`/`"task"`)
   - `int markerNumber` (only meaningful when `markerStyle ∈ {"dot",
     "paren"}`)
   - `bool checked` (only meaningful when `markerStyle == "task"`; from
     `task_list_marker_checked` vs `task_list_marker_unchecked`)
   - `bool looseRun` (true if parent list is loose)
   - The existing `byteStart` / `byteEnd` are now the item's **content**
     range, not the whole item including marker and trailing newline.
     Compute by: `content_start = first non-marker, non-block_continuation
     child's start_byte`, `content_end = last non-marker child's end_byte`,
     then strip trailing `\n`s.
5. **Test:** `tst_parser_list_items` (new) — fixtures: mixed ordered+
   unordered, nested 2-deep, task-list with checked + unchecked + extended,
   tight + loose. Verify one `TopLevelBlock` per `list_item` with correct
   `indentDepth`/`markerStyle`/`markerNumber`/`checked`/`looseRun`.

**Note re: D4 (parser scope reduction).** D4 deletes `ParsePool` and
`IncrementalParseSession` — surviving parser surface is `Document::fromMarkdown`
+ `inlineSpansFor`. This spec doesn't change that contract; we're just
*emitting more information* through the existing `Document::fromMarkdown`
load-time call. D4 stays unchanged.

## Foundation-side changes

`libs/markoff-foundation/src/MarkoffDocument.cpp`:

1. **`mapTopLevelKind`** — `Kind::ListItem` → `BlockKind::ListItem`. Remove
   the `ListTight`/`ListLoose` cases.
2. **`materializeBlocksFromParsedDoc`** — for `tb.kind == ListItem`, set
   block attrs:
   - `IndentLevel` ← `tb.indentDepth`
   - `MarkerStyle` ← `tb.markerStyle`
   - `MarkerNumber` ← `tb.markerNumber` (only when style is "dot"/"paren")
   - `Checked` ← `tb.checked` (only when style is "task")
   - `LooseRun` ← `tb.looseRun`
   The buffer content is `bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart)`
   (now content-only per parser fix). **Delete** the comment about
   "item-level unwrapping is deferred."
3. **`AttrNames.h`** — keep existing `IndentLevel`, `MarkerStyle`, `Checked`
   (already declared). Add `MarkerNumber`, `LooseRun`. Do **not** rename
   `IndentLevel` to `Indent` — the original name reads naturally under
   per-item granularity (the rename was prompted by the wrong "block ≠
   item" mental model).
4. **Serialization** (`serializeForSave` and any other emit-markdown path):
   when a contiguous run of ListItem blocks is encountered, emit each as:
   `"<' ' * IndentLevel * 2><marker> <buffer text>"` joined by `\n`,
   with extra `\n` between items if `LooseRun=true`. Marker text:
   - `MarkerStyle="dot"` → `"<MarkerNumber>."`
   - `MarkerStyle="paren"` → `"<MarkerNumber>)"`
   - `MarkerStyle="minus"`/`"plus"`/`"star"` → `"-"`/`"+"`/`"*"`
   - `MarkerStyle="task"` + `Checked=false` → `"- [ ]"`
   - `MarkerStyle="task"` + `Checked=true` → `"- [x]"`
5. **`Cmd::D2.cpp`** — add helpers (caller-driven renumber depends on these
   being callable from the structural handlers):
   - `BlockId insertListItemAfter(MarkoffDocument&, BlockId currentItem, UndoLog::Transaction&)`
     — copies `IndentLevel`, `MarkerStyle`, `LooseRun`; sets
     `MarkerNumber = current + 1` when ordered. Returns new BlockId.
   - `BlockId insertListItemBefore(MarkoffDocument&, BlockId currentItem, UndoLog::Transaction&)`
     — analogous; sets `MarkerNumber = current` (caller renumbers afterward).
   - `void renumberRunStartingAt(MarkoffDocument&, BlockId anyItemInRun, UndoLog::Transaction&)`
     — see §"Renumbering" above.
6. **Test:** `tst_d2_list_roundtrip` (new) — load → iterate blocks →
   assert per-item structure (kind, attrs, content) → serialize → assert
   source identity (byte-equal) for: tight ordered, tight unordered,
   loose ordered, mixed-marker (some `1.` and some `1)` in same source),
   nested 2-deep, task-list (checked + unchecked + extended).
7. **Test:** `tst_d2_list_renumber` (new) — exercise `renumberRunStartingAt`:
   insert mid-run, delete mid-run, indent change splits a run into two,
   ordered + unordered mix in the same indent doesn't get cross-run
   renumbered.

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

- **Enter at end of a non-empty item** →
  `Cmd::insertListItemAfter(doc, blockId, t)` then
  `Cmd::renumberRunStartingAt(doc, blockId, t)` in the same transaction.
  Cursor: `requestTextCaretAtNewRow(blockIndex+1, 0)`.
- **Enter mid-item** → split in one transaction: truncate current item's
  buffer at byteOff, `insertListItemAfter`, set new item's buffer = the
  suffix, then `renumberRunStartingAt`. Cursor at start of new row.
- **Enter at start of non-empty item** → `insertListItemBefore` with same
  `MarkerStyle`/`IndentLevel`/`LooseRun`; `renumberRunStartingAt`. Cursor
  stays at original row.
- **Enter on empty item** → if `IndentLevel > 0`: decrement IndentLevel
  attr (item joins outer run; `renumberRunStartingAt` on the outer run).
  Otherwise: change kind to Paragraph (clears all list attrs, leaves
  empty buffer); old run renumbers.
- **Tab** → `IndentLevel` attr += 1 (cap at 6); `renumberRunStartingAt`
  on both the old run (now missing this item) and the new (now containing
  this item).
- **Shift-Tab** → `IndentLevel` attr -= 1, clamp at 0; renumber both runs.
  If at 0 and Shift-Tab again, no-op (not list-exit; that's Enter-on-empty).
- **Backspace at qtPos=0** → if `IndentLevel > 0`: decrement (same as
  Shift-Tab). Otherwise: merge with previous block via
  `Cmd::backspaceMerge`. If previous was ListItem in same run, run
  shrinks; renumber. If previous was Paragraph, demote this to Paragraph
  before merging; renumber the surviving run.
- **Delete at qtPos=length** → merge next block in via
  `Cmd::deleteMerge`. If next was ListItem in same run, run shrinks;
  renumber.

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

### `qml/delegates/ListItemDelegate.qml`

- **Render** marker as a non-editable `Text` element (or, for task items, a
  clickable checkbox) to the left of the TextEdit, populated from
  `model.attrs` per the table in §"Marker storage and rendering".
- The TextEdit shows `model.text` (content only). Cursor positions are
  relative to content — qtPos=0 is at the start of the item's content.
- **Indent** rendering uses `leftPadding: 8 + model.attrs.IndentLevel *
  indentWidth`. Marker label sits inside the padding.
- **Task-list checkbox** click handler calls
  `binding.document.d2SetBlockAttr(model.blockAnchor, "checked", !checked)`
  inside a fresh transaction.
- Structural keys still go through `tryHandle` like today; the handler is
  much shorter (no regex marker parsing).

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

**Per-block undo with structural changes.** Resolved in §"Renumbering"
above: caller-driven, all renumber attr-edits go into the originating
action's `UndoLog::Transaction`. One user action = one `UndoEntry` =
one undo step.

## What gets deleted

Rough LOC estimate (positive = deletion):

- `LiveStructuralKeyHandler.cpp` ListItem Enter/Backspace/Delete: **+100 LOC**
  (regex, multi-line scan, renumber loop, atLineStart branch, marker
  reconstruction)
- `LiveListModelBinding.cpp` `while raw.endsWith('\n')`: **+5 LOC**
- `LiveCursorState.cpp` `requestTextCaretAtRow` immediate-resolve path,
  `resolvePendingForRow`: **+30 LOC**
- `KindTransition.cpp` ListItem regex on multi-line text: **+5 LOC** (kept
  for paragraph-→-list promotion; just simpler against single-line text)
- `tst_live_render_structural` integration tests built around the multi-
  line-block assumption: **+80 LOC** (replaced by simpler per-item tests)

Rough LOC additions:

- Parser `collectTopLevelBlocks` recursion + marker harvest: **-50 LOC**
- Foundation `materializeBlocksFromParsedDoc` attr population: **-15 LOC**
- Foundation `serializeForSave` list reconstruction (incl. task lists): **-40 LOC**
- `Cmd::insertListItemAfter` / `Before` / `renumberRunStartingAt`: **-50 LOC**
- `AttrNames.h` two new attrs (`MarkerNumber`, `LooseRun`): **-4 LOC**
- `LiveBlockModel` new roles (MarkerStyleRole, MarkerNumberRole,
  IndentLevelRole, CheckedRole, LooseRunRole): **-25 LOC**
- `ListItemDelegate.qml` marker label + indent + task-list checkbox: **-50 LOC**
- New tests (`tst_parser_list_items`, `tst_d2_list_roundtrip`,
  `tst_d2_list_renumber`): **-150 LOC**

**Net: ~-50 LOC overall** (slight net add because of the new tests, which
are *value*, not bloat). Dramatic complexity reduction in the hot path
(`LiveStructuralKeyHandler` ListItem section: ~-100 LOC of regex + scan
gone). The dogfood bug class — phantom newlines, no-renumber-on-insert
(fixed band-aid-style today, replaced cleanly here), no-renumber-on-delete,
cursor delivery race for in-block multi-line edits, broken nested-list
Tab — **stops existing as a category**, not "is fixed."

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

## Decisions (open questions resolved 2026-05-06)

1. **Marker storage:** attribute, two-attr encoding (`MarkerStyle` QString
   + `MarkerNumber` int). Renumbering = one int LWW edit per item, not
   string parse-and-rewrite. Spec's pre-existing `MarkerStyle` declaration
   stays; `MarkerNumber` is new.
2. **Loose lists:** per-item `LooseRun` bool attr, parser-set at load.
   Preserved through edits; serialization emits blank lines between items
   in loose runs.
3. **Renumber location:** caller-driven via `Cmd::renumberRunStartingAt`,
   called from each structural handler within the originating
   `UndoLog::Transaction`. Forced by D2 §4.2's one-UndoEntry-per-action
   undo model — a post-applyOps renumber would require two undos to
   revert one user action.
4. **`IndentLevel` naming:** keep. The rename to `Indent` was prompted by
   the wrong "block ≠ item" mental model; under per-item granularity, the
   original name reads naturally.
5. **Task lists:** fully in scope. Same shape as regular ListItem
   (parser already classifies via `task_list_marker_*` nodes); adds
   `MarkerStyle="task"` + `Checked` bool + clickable-checkbox rendering
   in `ListItemDelegate.qml`. ~25 extra LOC; included now to avoid a
   second rework pass.

## Future / follow-up

- **Multi-block selection + delete.** Out of scope for this spec. With
  per-item blocks, selecting across items means the selection model needs
  cross-block range support. When that lands, the cross-block-delete path
  must call `Cmd::renumberRunStartingAt` to keep ordered runs sequential.
- **Loose ↔ tight conversion gesture.** No UI for it in this spec. Lists
  preserve their parser-emitted loose/tight status through edits.
  A future "convert this list to loose" command would set `LooseRun=true`
  on every item in the run.
- **`MoveAfter` for drag-to-reorder.** Per the collabtext scope-line item
  1, no `moveAfter` in collabtext v1; reorder decomposes to remove +
  insert. Acceptable.
