# `markoff-styled` ordered-list continuous numbering

**Date:** 2026-05-29
**Arc:** WP-unification flat-view follow-ups (queue #8.4)
**Scope:** `libs/markoff-styled`
**Related:** v0.1 ListItem rendering (`845fc0f`) put one
  `QTextList` per item — single-item lists, so every ordered
  item renders `1.`. This spec closes the v0.2 follow-up flagged
  in `libs/markoff-styled/CLAUDE.md` ("Single-item lists render
  the marker correctly but ordered items always read `1.`;
  sibling-grouping is a v0.2 follow-up").

## 1. Problem

`applyListItem` creates a fresh `QTextList` per ListItem via
`cursor.createList(lf)` — every item is in its own one-item list.
Qt's `ListDecimal` numbers items by their position in the list, so
every ordered item renders `1.` regardless of how many ordered
items precede it.

The goal is continuous numbering across consecutive same-style
ordered items, with correct resumption across nested-list
transitions (per CommonMark / browser markdown behaviour). Items at
the same depth and marker-style should share one `QTextList`; nested
items should get their own list; an intervening non-list block (or
task item, which uses a different render path) should end the
enclosing list.

## 2. Approach — depth-stack with neighbour-aware reconciliation

A new helper `manageListMembership(qblk, kind, attrs, listStack)`
runs once per block iteration inside `StyleApplier::applyFormats`.
State is a per-pass `std::vector<ListStackEntry>`, sorted by depth
ascending (deepest at end):

```cpp
struct ListStackEntry {
    int depth;
    QString markerStyle;
    QTextList *list;
};
```

### 2.1 Per-block decision

For each block in the walk:

- **Non-ListItem block** — clear `listStack` entirely (a paragraph,
  heading, code block, etc. between two list items ends the
  enclosing list under CommonMark semantics). Remove `qblk` from any
  existing list via `qblk.textList()->remove(qblk)` so it doesn't
  cling to a stale list across structural rewrites.
- **Task ListItem** (`markerStyle == "task"`) — Qt renders tasks via
  the native `QTextBlockFormat::Marker`, not `QTextList`. Treat as a
  list-style transition:
  - Pop entries with `depth > taskDepth`.
  - If top entry has `depth == taskDepth`, pop it (task interrupts
    the same-depth ordered/unordered list of any style).
  - Remove `qblk` from any existing list. Do not push to the stack
    (tasks have no QTextList membership).
- **Non-task ListItem** at `(depth, markerStyle)`:
  - Pop entries while `top.depth > depth`.
  - If non-empty and `top.depth == depth`:
    - If `top.markerStyle == markerStyle`: reuse — `top.list->add(qblk)`.
    - Else: pop top entry, fall through to "create new" below.
  - If empty or `top.depth < depth`: create a new `QTextList` with
    `setIndent(depth + 1)` and `ListDecimal` for `dot`/`paren` or
    `ListDisc` for `minus`/`plus`/`star`/unknown. Push
    `{depth, markerStyle, newList}` to the stack.

### 2.2 Behaviour examples

| Source | Result |
|--------|--------|
| `1. a\n2. b\n3. c` | One shared list. Renders `1, 2, 3`. |
| `1. a\n\nparagraph\n\n2. b` | Paragraph clears stack. Two separate lists. Renders `1, ..., 1`. |
| `1. a\n- b` | Same depth, marker mismatch. Two separate lists. |
| `1. a\n   1. nested\n2. b` | "2. b" pops nested entry, resumes outer list. Renders `1, ?, 2`. (Nested is in its own list, renders `1.`) |
| `1. a\n   - [ ] task\n2. b` | Task doesn't push (deeper depth than outer). "2. b" finds outer list still at top, resumes. Renders `1, ?, 2`. |
| `1. a\n- [ ] task\n2. b` | Task at same depth pops outer entry. "2. b" creates new list. Renders `1, ?, 1`. |

### 2.3 Hash gate interaction

The recent hash-gate-over-attrs change (`42720f3`) pulled
`blockAttrs(id)` lookup outside the hash gate, so the data needed by
`manageListMembership` is available cheaply on every iteration.

`manageListMembership` runs **outside** the hash gate, on every
block, even hash-skipped ones. This is load-bearing: if a paragraph
is inserted between two formerly-adjacent ordered items, the second
item's content hash is unchanged (hash-skip path), but list
reconciliation must still happen — else the old `QTextList` spans
the gap and the paragraph renders as if it were inside a list.

`applyListItem` becomes format-only — drops the
`cursor.createList(lf)` block. Its responsibility shrinks to:
margins, char format, and the task-marker `QTextBlockFormat::Marker`
for tasks. List membership is the walk's concern, not the per-block
formatter's.

## 3. Tests

Three new slots in `tst_styled_dogfood_invariants`:

1. `ordered_list_items_share_one_list_with_continuous_numbering` —
   load `"1. one\n2. two\n3. three\n"`. Assert all three model
   blocks' QTextBlocks return the same non-null `textList()`.

2. `paragraph_between_items_breaks_list` — load
   `"1. one\n\nbreak\n\n2. two\n"`. Assert the two ListItem
   QTextBlocks return different non-null `textList()` pointers, and
   the paragraph block returns null `textList()`.

3. `nested_list_then_outer_resumes` — load
   `"1. outer\n   1. nested\n2. outer-two\n"`. Assert blocks 0 and
   2 share one list; block 1 has a different non-null list.

### Falsifiability

For test 1: revert the `prevList->add(qblk)` branch so each item
always gets a fresh list. Test fails (lists differ).

For test 3: revert the "pop entries while `top.depth > depth`" step
so the stack doesn't unwind on outer resumption. Test fails ("2.
outer-two" lands in a new list rather than resuming).

## 4. Scope and exclusions

**In scope:**
- `manageListMembership` helper + state-tracked block walk.
- `applyListItem` refactor (drop `createList`).
- Three new test slots.
- CLAUDE.md update: remove the v0.2 caveat about single-item lists.
- Queue #8.4 closeout.

**Out of scope:**
- User-typed `MarkerNumber` honoring (`5. first\n6. second` always
  renders as `1, 2` because Qt's `QTextListFormat` doesn't support a
  start offset). Tracked but not blocking; needs a separate spec if
  ever fixed.
- Re-numbering across collab edits (handled implicitly by re-running
  `applyFormats` on every cascade).
- Source-view list-item marker reconstruction (queue #8.3 — separate
  spec).

## 5. Files touched

- `libs/markoff-styled/src/StyleApplier.cpp` — refactor
  `applyListItem` (drop `createList`); add `manageListMembership`
  helper; add list-stack state + call in the block walk.
- `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` —
  three new slots.
- `libs/markoff-styled/CLAUDE.md` — invariants section: update the
  ListItem marker-rendering paragraph to reflect continuous
  numbering.
- `docs/queue.md` — close #8.4.

## 6. Definition of done

- Three new test slots pass; falsifiability proofs (run pre-fix or
  with targeted reverts) confirm each slot is meaningful.
- Existing `tst_styled_dogfood_invariants` slots pass —
  particularly `attr_toggle_re_renders_task_marker` (task ListItem
  still renders its checkbox correctly under the new list-membership
  logic).
- Full fast suite at the 2026-05-29 baseline (249/254 — no new
  failures; the same five pre-existing failures remain).
- CLAUDE.md updated; queue #8.4 closed.

## 7. Risks and notes

- **List removal under structural edits.** When a block is removed
  from a list via `QTextList::remove`, Qt also clears the block's
  `QTextBlockFormat::objectIndex` and `indent`. That can interact
  with margins set by `applyListItem`. Test 2's paragraph block has
  to assert `textList() == nullptr` — if it doesn't, the removal
  didn't happen and we have a leak.
- **`add` semantics.** `QTextList::add(QTextBlock)` is idempotent
  if the block already belongs to the same list, and moves the
  block out of any other list if not. So `prevList->add(qblk)` on
  an already-in-list block is a cheap no-op.
- **`createList` ordering vs `setBlockFormat`.** `createList`
  updates the block's `objectIndex` after any prior `setBlockFormat`
  call. Since `applyListItem`'s `setBlockFormat` runs inside the
  hash gate (format pass) and `manageListMembership` runs after
  (outside the hash gate), the list assignment lands last — margins
  set by `applyListItem` survive, and the new `objectIndex` wins
  over whatever was there before.
- **Stack lifetime.** The stack is a local in `applyFormats`,
  reset on every cascade. No cross-pass state to worry about.
- **`qblk.textList()->remove(qblk)`.** Qt allows removing a block
  from its list. Verify it doesn't leave the list in a "zero items"
  half-alive state — if it does, an empty list would still claim
  the document's object-index slot and waste memory. Per Qt source,
  empty `QTextList`s are kept alive (document-owned); this is fine
  for our purposes (the document already has plenty of these from
  the pre-fix one-list-per-item world; the new code reduces, not
  increases, the total).
