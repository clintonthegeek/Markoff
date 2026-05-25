# Audit L4 — Ctrl+Shift+Left/Right within-block word-extend

**Date:** 2026-05-21
**Status:** Spec + plan; implementation in same session.
**Branch:** `exploration/new-foundation`
**Source:** `docs/specs/2026-05-21-textedit-interface-audit.md` §3.3(e), §5 (L4).

## The bug

`LiveNavigationController::tryHandle` claims Ctrl+Shift+Left/Right
only at the block boundary (`qtPos == 0` for Left, `qtPos ==
blockText.length()` for Right). Within-block the chord falls through
to TextEdit's native handler.

TextEdit's native Ctrl+Shift+Left/Right builds a within-block visual
selection by calling `QTextCursor::WordLeft` / `WordRight` with
`KeepAnchor`. The `onCursorPositionChanged` then fires and
`syncFromTextEdit` moves `m_cursor` (the active end) to the new word
boundary. **But `m_selectionAnchor` is never set.**

Result: the user sees a highlighted word in the TextEdit, but the
cross-block selection state thinks there is no selection. The next
`Ctrl+C` copies just the caret position because
`LiveClipboardController::copy` reads from the document-layer
selection range, not from TextEdit's local `selectedText`.

This is the exact same divergence that `Ctrl+C`/`X`/`V`/`A` had before
the 2026-05-21 cross-block clipboard fix: a focused TextEdit operating
on its own within-block selection while the document-layer state lags.

## Decision

Claim Ctrl+Shift+Left/Right within a block in
`LiveNavigationController::tryHandle`. Compute the target qtPos using
`QTextBoundaryFinder` in `Word` mode (Qt's standard Unicode word
boundary, same definition TextEdit uses natively). Route the result
through the same `cursorState->begin (lazy anchor) + extend` path the
rest of the Shift-modified motions use.

Word-boundary semantics (matching Qt's `QTextCursor::WordLeft` /
`WordRight`):

- **Left** from `qtPos`: find the previous Word-mode boundary whose
  reason includes `StartOfItem` (start of a word). Skip
  end-of-whitespace boundaries.
- **Right** from `qtPos`: find the next Word-mode boundary whose
  reason includes `EndOfItem` (end of a word). Treats trailing
  whitespace as part of the word, matching TextEdit's WordRight.
- Edge: at `qtPos == 0` with Left, fall through to the existing
  cross-block branch (jump to end of previous block).
- Edge: at `qtPos == blockText.length()` with Right, fall through to
  the existing cross-block branch (jump to start of next block).

The visual feedback (the highlighted span) comes from the delegate's
`applySelection()` slot, which runs on every `cursorState`
`selectionChanged` emit and calls `TextEdit.moveCursorSelection` to
draw the range. This is the same render path as plain Shift+Left/Right.

## Why this over alternatives

α. **Document-layer claim (this spec).** Resolves the divergence
   cleanly at the cursor-state layer. Word-boundary computation lives
   in C++ where it can be unit-tested. Matches the precedent of the
   plain Shift+Left/Right intercept.

β. **Let TextEdit handle it, sync the anchor in
   `syncFromTextEdit`.** The sync function would have to infer that
   the cursor jumped via a word-extend chord rather than typing or a
   plain arrow, which it can't reliably do (the signal carries no
   key-event provenance). Rejected.

γ. **Let TextEdit handle it, have `LiveClipboardController::copy` fall
   back to TextEdit's selectedText when the document-layer selection
   is empty.** Solves the symptom for Ctrl+C but leaves the underlying
   divergence in place — Shift+arrow extending from a Ctrl+Shift+arrow
   selection would behave inconsistently because the anchor still
   isn't set. Rejected.

## Implementation plan

### Code change

`libs/markoff-live/src/LiveNavigationController.cpp`:

1. Add two static helpers at the top of the anonymous namespace:

   ```cpp
   static int previousWordBoundary(const QString &text, int from);
   static int nextWordBoundary(const QString &text, int from);
   ```

   Both use `QTextBoundaryFinder(QTextBoundaryFinder::Word, text)`.
   `previousWordBoundary` walks back via `toPreviousBoundary` until it
   finds a boundary whose `boundaryReasons() & StartOfItem` is set.
   `nextWordBoundary` walks forward looking for `EndOfItem`. Edge
   returns: 0 for "no previous word"; `text.length()` for "no next
   word".

2. Rewrite the existing Ctrl+Shift+Left/Right block (lines 58–80):

   ```cpp
   if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
       if (key == Qt::Key_Left)  return wordExtendLeft (blockIndex, qtPos, blockText);
       if (key == Qt::Key_Right) return wordExtendRight(blockIndex, qtPos, blockText);
   }
   ```

   The two new private methods encapsulate both the within-block word
   step and the cross-block fallthrough.

3. Each branch:
   - Compute `targetRow`, `targetPos`:
     - Left: if `qtPos > 0`, `(targetRow, previousWordBoundary)`;
       else fallthrough to `previousNavigableRow`'s end.
     - Right: if `qtPos < blockText.length()`,
       `(targetRow, nextWordBoundary)`; else fallthrough to
       `nextNavigableRow`'s start (qtPos = 0).
   - `clearDesiredVisualX`.
   - `if (cursorState->anchorBlock() < 0) begin(blockIndex, qtPos)`.
   - `extend(targetRow, targetPos)`.
   - If `targetRow != blockIndex`, `requestTextCaretAtRow`.
   - Return `Handled`.

### Test

`tst_live_render_ctrl_shift_word_extend_qml` (new, QML integration
fixture). Five slots:

1. `ctrl_shift_left_within_block_sets_cross_block_anchor` —
   `"hello world|"` → Ctrl+Shift+Left → assert document-layer
   selection is `[6, 11]` (start-of-"world" to end), and TextEdit's
   visible selection matches.
2. `ctrl_shift_right_within_block_sets_cross_block_anchor` —
   `"|hello world"` → Ctrl+Shift+Right → assert selection covers
   `"hello "` (Qt's WordRight includes trailing space). Tolerant
   variant: assert selection ends at either 5 ("hello") or 6
   ("hello ") to accommodate Qt-version drift.
3. `ctrl_shift_left_at_qtpos_zero_jumps_to_prev_block_end` —
   regression: cross-block fallthrough still works.
4. `ctrl_shift_left_within_block_then_ctrl_c_copies_visible_selection`
   — the user-facing payoff. After Ctrl+Shift+Left within a block,
   Ctrl+C must put the visibly-selected text on the clipboard. Pre-
   fix this would copy nothing because the document-layer anchor was
   empty.
5. `ctrl_shift_left_extends_existing_cross_block_selection` —
   integration: with an existing cross-block selection,
   Ctrl+Shift+Left from within the active block extends the active
   end by one word, leaving the anchor (in the other block)
   untouched.

Falsifiability stub: a separate commit (immediately preceding the
fix) inserts a debug return that breaks the in-block path. Test 4
fails. Stub is then reverted and the fix lands.

## Out of scope

- IME composition + Ctrl+Shift+arrow — audit L7 covers IME holistically.
- Per-word visual chunking of the highlighted range (the highlight is
  a single contiguous span; words are not individually outlined). Not a
  user concern.
- Alt+Shift+arrow (column-mode selection). We don't support column
  mode; not applicable.

## Resolution

`docs/queue.md` Discipline Log: strike the L4 line in the audit-closure
block, add resolution pointer to this spec + the new test file.
