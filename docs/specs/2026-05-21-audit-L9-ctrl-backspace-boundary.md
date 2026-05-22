# Audit L9 — Ctrl+Backspace / Ctrl+Delete at block boundary

**Date:** 2026-05-21
**Status:** Decision recorded; pinned by regression test.
**Branch:** `exploration/new-foundation`
**Source:** `docs/specs/2026-05-21-textedit-interface-audit.md` §3.4(c), §5 (L9).

## Decision

Ctrl+Backspace at `qtPos = 0` and Ctrl+Delete at `qtPos = blockText.length`
behave **exactly like plain Backspace / Delete at the same position**:
they trigger the cross-block merge path in
`LiveStructuralKeyHandler::tryHandle`. The Ctrl modifier is ignored at
the boundary. No "word-delete across the block boundary" semantics.

In-block Ctrl+Backspace / Ctrl+Delete (any qtPos that isn't the
boundary) continues to be handled natively by Qt's `TextEdit`, which
does the word-distance deletion within the focused block. The
`LiveEditBinding` translates the resulting multi-character delta into a
single `d2ApplyBufferEdit` against the per-block CRDT buffer. This is
unchanged.

## Why

1. **Matches every other Qt-based editor.** Qt's `QPlainTextEdit` and
   QML `TextEdit` both ignore the Ctrl modifier at the start of a
   document when handling Backspace — Ctrl is a "look for a word
   boundary in this direction" hint, and at qtPos=0 there is no
   in-block boundary, so the hint is dropped. Our block boundary is the
   structural equivalent of the document boundary in a single
   `TextEdit`. Treating it consistently is the principle of least
   surprise.

2. **Word-delete-across-boundary is structurally harder than it sounds.**
   It would compose a structural merge with a word-distance inline delete
   in a single user-visible undo step. The merge changes the buffer
   layout: the qtPos to word-back-from is in the post-merge buffer, but
   the cursor position that the user "intended" is in the pre-merge
   buffer. Threading that through `d2UndoLog::Transaction` is doable but
   tricky, and the cost-benefit isn't there given (1).

3. **It is already the behaviour today.** This spec changes nothing in
   production code; it ratifies the current behaviour explicitly so the
   audit item can close, and adds a regression test so a future
   refactor of `LiveStructuralKeyHandler::tryHandle` (e.g. the
   modifier-handling rewrite contemplated in audit §6) doesn't
   accidentally drift.

## What this is NOT

- Not a statement about Ctrl+Backspace IN-BLOCK. That path remains
  TextEdit-native and the `LiveEditBinding`-to-CRDT translation already
  handles it correctly per audit §3.4(c). The in-block path is
  exercised by every existing `tst_live_render_paragraph_edit_*` test.

- Not a statement about Ctrl+Backspace on a cross-block selection.
  That path goes through `KeyDispatch.collapseSelectionIfMutating`
  (the L1 fix) and the selection-delete is the entire action — the
  Ctrl modifier is irrelevant because the active end isn't at a word
  boundary, it's at the end of a selection range. Same as plain
  Backspace on a cross-block selection.

- Not a structural-key rework. `LiveStructuralKeyHandler::tryHandle`
  does not gain a modifier parameter check; it continues to dispatch
  by `(BlockKind, Qt::Key)` only.

## Test

`tst_live_render_ctrl_backspace_boundary_qml` (new). Three slots:

1. `ctrl_backspace_in_block_word_deletes_via_text_edit_native_path` —
   confirms in-block word-delete works.
2. `ctrl_backspace_at_qtpos_zero_merges_blocks_like_plain_backspace` —
   confirms the boundary case ignores Ctrl, merges with previous
   block.
3. `ctrl_delete_at_end_of_block_merges_with_next_like_plain_delete` —
   confirms the symmetric case for forward.

The test lives under the QML integration fixture (`QmlIntegrationFixture`)
so it exercises the real production callsite — the delegate's
`Keys.onPressed` routing through `KeyDispatch` and on to
`LiveStructuralKeyHandler`. Per INVARIANTS.md invariant 5 (tests
exercise the production callsite, not a synonym).

## Resolution of audit L9

Closed by this spec + the regression test. No production code change.
Update `docs/queue.md` Discipline Log: strike the L9 line in the
audit-closure block, add resolution pointer to this spec.

## Out of scope

- Ctrl+Shift+Left/Right inside a block — audit L4, its own spec.
- Ctrl+Home / Ctrl+End — already handled by the navigation controller
  (`LiveNavigationController::tryHandle`); not part of this decision.
- IME interactions with Ctrl-modified deletion — audit L7, its own spec.
