# Handoff — cursor-authority fix for the flat-text view leaves

> **2026-05-27.** Branch `master`. This is the orientation brief for a
> fresh session to spec + implement the §B cursor-authority fix described
> in [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md). Read
> the guide's §B first; this brief assumes it.

## What just happened (context)

`markoff-styled` (the QTextEdit view leaf, landed 2026-05-26) went through
dogfood. The text-sync class of bugs (boundary drift, `setPlainText` wipe,
viewport jump) was root-caused and fixed at the `markoff-core` binding layer
— the RT1–RT6 work, commits `f5cdc4e..10ed95a`, spec
[`../specs/2026-05-27-markoff-core-binding-robustness-design.md`](../specs/2026-05-27-markoff-core-binding-robustness-design.md).
That closes guide §A for the flat-text leaves.

Then the user hit the **next** solved-in-live problem: *"when I hit Enter at
the end of a paragraph, the caret jumps to the end of the following
paragraph."* Their framing: *"I'm just finding all the same bugs I had in the
QML view… these are all SOLVED PROBLEMS."* That triggered the creation of the
[View Implementor's Guide](../VIEW-IMPLEMENTORS-GUIDE.md) (committed `eb97a80`)
so the catalog of cross-cutting view concerns is written down once. **This
brief is the follow-through: actually fix the §B cluster.**

## The task

Port `markoff-live`'s cursor-authority pattern (`establishFocus`) to the
**single-document binding** (`SourceTextDocumentBinding`), so the QTextEdit's
caret is re-asserted from a model anchor after the model settles. Closing
guide **§B.1** (caret re-assert after a structural edit) drags **§B.2**
(survive model rebuild), **§B.3** (multi-block selection-delete caret), and
**§B.4** (undo/redo caret) along with it, because they all share the same
unwired-authority root.

This fixes `markoff-styled` **and** `markoff-source` at once — they share the
binding.

## Root cause (confirmed)

1. Enter at end of a paragraph → `applyFlatEdit` canonicalises the inserted
   `\n` to a `\n\n` separator.
2. The reverse-path diff in `SourceTextDocumentBinding::onD2DocumentChanged`
   re-inserts via `QTextCursor::insertText`, which leaves the *visible* caret
   riding past the inserted separator — at the **start of the next block**.
3. Nothing re-asserts the *intended* caret. The binding has the machinery to
   know where the caret should be (`syncFromSession`,
   `SourceTextDocumentBinding.cpp:196`) but:
   - it emits `cursorPositionChanged` / `selectionStartChanged` /
     `selectionEndChanged` **signals designed for QML property bindings** —
     nothing in the styled `Editor` connects them to
     `QTextEdit::setTextCursor`, so the real QTextEdit caret is never moved;
   - it computes positions by concatenating `blockText` with **no
     separators** (`utf8 += blockText(id)`, lines 209–211) while the QTextEdit
     holds **sep-view** text — so even the position it computes is in the
     wrong coordinate space (off by 1 per preceding block boundary).

## The shape of the fix (to be confirmed in the spec brainstorm)

Mirror `establishFocus`'s contract — *"declare the intended post-edit caret
as an anchor before mutating; re-resolve it to a view caret after the model
settles"* — in the single-document world:

1. In `SourceTextDocumentBinding::onQtContentsChange`, when an edit is
   structural (the path that calls `applyFlatEdit`), capture the **intended
   post-edit caret** as a `Markoff::TextAnchor` (or push it to the
   `Session` selection) *before* the model mutates.
2. After `onD2DocumentChanged` finishes the reverse-sync diff, re-resolve
   that anchor to a **sep-view** QTextEdit position and apply it via
   `QTextEdit::setTextCursor`.
3. Fix `syncFromSession` to operate in **sep-view** coordinates (it currently
   concatenates without separators) so any Session-driven caret update lands
   correctly — this is what makes §B.2 (external/collab/undo-driven caret
   updates) work too.
4. Wire the binding's caret-authority output to the QTextEdit. Today the
   `Editor` (`libs/markoff-styled/src/Editor.cpp`) never connects the
   binding's cursor signals to `setTextCursor`. Decide where the wire lives:
   the binding could own the QTextEdit cursor directly, or expose a
   "resolved caret" the `Editor` applies. **This is an L3 authority
   decision — write it down per INVARIANTS §2 before coding.**

## Required process (not optional)

This is squarely in the focus/caret seam. The eight invariants in
[`../INVARIANTS.md`](../INVARIANTS.md) apply. In particular:

- **Brainstorm first** (`superpowers:brainstorming`) — the L3 "who owns the
  QTextEdit caret" decision (step 4 above) must be settled in the spec
  before the plan. Cite the developmental record (INVARIANTS §1): the
  live-side answer is `LiveCursorState` as a single chokepoint; the spec
  must state the single-document analogue explicitly.
- **Retire the old authority in the same plan** (INVARIANTS §3): if the
  binding's `cursorPositionChanged`-signal path is superseded by a direct
  `setTextCursor` wire, name it and remove/redirect it in the same plan —
  don't leave two paths writing the caret.
- **Falsifiable test first** (INVARIANTS §4): the regression test is
  end-to-end at the widget level. Model it on
  `tst_styled_dogfood_invariants::typing_at_boundary_does_not_wipe_or_leap`
  (`libs/markoff-styled/tests/`). The new slot: load a multi-paragraph doc,
  put the caret at end of paragraph 1, send `Enter` via `QTest::keyClick`,
  assert the caret lands at the **start of the new empty block** (or
  wherever the agreed structural semantics put it) — **not** at the start of
  the old paragraph 2. Prove it fails on HEAD before fixing.
- **Watch for new `singleShot(0)` / re-entrance guards** (INVARIANTS §6/§7):
  the reverse-sync settle-then-reassert ordering will tempt a deferred
  dispatch. If you add one, justify it in the commit (the kind-transition
  `singleShot(0)` precedent in styled is justified in
  `markoff-styled/CLAUDE.md` §v0.1).

Then `superpowers:writing-plans`, then `superpowers:subagent-driven-development`.

## Authoritative references

| Doc | Why |
|-----|-----|
| [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md) §B | the contract being satisfied; the design reference |
| [`../specs/2026-05-22-cursor-authority-decision.md`](../specs/2026-05-22-cursor-authority-decision.md) | the live-side L3 decision; the chokepoint + within-block-sync contract to mirror |
| [`../specs/2026-05-27-markoff-core-binding-robustness-design.md`](../specs/2026-05-27-markoff-core-binding-robustness-design.md) | the forward/reverse path the fix extends |
| [`../handoff/2026-05-07-live-binding-developmental-history.md`](../handoff/2026-05-07-live-binding-developmental-history.md) | why the live pipeline looks like it does (INVARIANTS §1 citation) |

## Key code locations

- `libs/markoff-core/src/SourceTextDocumentBinding.cpp`:
  `onQtContentsChange` (forward dispatch, ~`:344`), `onD2DocumentChanged`
  (reverse sync, ~`:448`), `syncFromSession` (`:196`),
  `pushSelectionToSession` (`:176`), `m_cursorPosition` machinery (`:131`–`:229`).
- `libs/markoff-live/src/LiveCursorState.cpp`: `establishFocus` (`:376`),
  `tryResolvePending` (`:460`) — the pattern to mirror.
- `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`: the structural cases
  that each end in `establishFocus(intendedBlock, intendedOffset)` — the
  catalogue of "what caret does each structural edit intend."
- `libs/markoff-styled/src/Editor.cpp`: where the binding↔QTextEdit caret
  wire is currently missing.

## Test baseline

Run the fast inner loop before and after:

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Known pre-existing failures unrelated to this work (do not let them block):
`tst_live_render_e2_nav_shift_extend`,
`tst_live_render_focus_chokepoint_invariant`,
`tst_live_render_cursor_typing_invariant`. Note `focus_chokepoint_invariant`
is itself an undo/redo-caret edge case on the live side (guide §B.4 🟡) — if
your fix touches shared cursor machinery, check you have not perturbed it.

## Adjacent follow-ups (lower priority, already logged)

From the binding-robustness work, in `queue.md` Discipline Log:
- double `iterateBlocks()` walk in the (now-removed) `sepViewToNoSepByte`
  → confirm it is gone after the reverse-path rewrite.
- whole-multi-block-delete empty-survivor edge (IdList lacks `clear()`
  semantics for a wholesale-replace on a non-fresh doc).

Neither blocks the §B fix.

## Definition of done

- Guide §B.1 regression test green (proven falsifiable first).
- The appendix status table in
  [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md) updated:
  B.1/B.2/B.3 → ✅ for source + styled (B.4 as far as it honestly goes),
  and the per-concern status lines edited to match.
- `markoff-styled/CLAUDE.md` and `markoff-source/CLAUDE.md` "§B open" notes
  updated to "closed."
- Full fast suite green except the three known pre-existing failures.
- User dogfood confirms Enter-at-paragraph-end places the caret correctly.
