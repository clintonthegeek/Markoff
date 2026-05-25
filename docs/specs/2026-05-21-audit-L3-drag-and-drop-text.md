# Audit L3 — Drag-and-drop of external text

**Date:** 2026-05-21
**Status:** Spec + impl in same session.
**Branch:** `exploration/new-foundation`
**Source:** `docs/specs/2026-05-21-textedit-interface-audit.md` §3.6(c), §5 (L3).

## The bug

QML's `TextEdit` inherits Qt's drag-drop handlers. When the user drops
a text fragment from another application onto a TextEdit, Qt's native
handler runs `paste()`-like logic inside the receiving block: the text
is inserted at the drop point in **that one block**, without going
through `LiveClipboardController`. No structured-paste routing, no
cross-block selection collapse, no opportunity for the document layer
to interpret the dropped content.

## Decision

Intercept drops at the LiveView level via a top-level `DropArea`,
mirroring the L2 middle-click pattern. The intercept:

1. Hit-tests the drop point with the existing `hit()` function.
2. Collapses any active cross-block selection by moving the cursor to
   the hit point via `cursorState.begin(blockIndex, qtPos)`. (Same
   "drop-at-point" semantic as middle-click paste; differs from
   TextEdit's "drop-into-focused-block" default.)
3. Calls a new `LiveClipboardController::pasteText(QString)` which
   uses the document layer's `applyFlatEdit` to insert the text at
   the cursor position, exactly the way the flat-text fallback in
   `paste()` already does.

Scope: text/plain only for L3. Structured paste from drag-drop (our
own `application/x-markoff-blocks` MIME, dropped from another window
of the same app) is a future extension if dogfood demands it; not
worth the QMimeData → QML plumbing for v1.

## Why this over alternatives

α. **DropArea overlay (this spec).** Single intercept, no per-delegate
   plumbing, mirrors the L2 architecture.

β. **Disable drag-drop entirely.** Some applications do this. Rejected:
   drag-drop text is a long-standing convention; users will want it.

γ. **Per-delegate intercept in each TextEdit.** More plumbing for the
   same result. Rejected.

δ. **Let TextEdit handle natively.** The status quo. Rejected per the
   bug description.

## Implementation

### C++

`libs/markoff-live/include/markoff/live/LiveClipboardController.h`:

- Add `Q_INVOKABLE void pasteText(const QString &text);`.

`libs/markoff-live/src/LiveClipboardController.cpp`:

- New `pasteText(text)`:
  - Same anchor-resolution / `flatByteOffset` block as `pasteFrom`,
    but takes the text directly instead of reading from a clipboard
    mode.
  - Calls `document->applyFlatEdit(startByte, endByte, text.toUtf8(),
    Origin::UserEdit)`.
  - Clears selection.

Factor the anchor-resolution block out of `pasteFrom` into a small
helper `resolveSelectionByteRange(out_start, out_end)` so both
`pasteFrom` and `pasteText` share it. Returns false if the cursor /
selection isn't resolvable; both callers early-return on false.

### QML

`libs/markoff-live/qml/LiveView.qml`:

- New `DropArea` element overlaying the ListView, similar to the
  MouseArea pattern. `z` slightly higher than the MouseArea so its
  drop handlers run first (and consume the drop before TextEdit can).
- `onEntered`: `drag.accept(Qt.CopyAction)` if `drag.hasText`.
- `onDropped`:
  - If not `drop.hasText`, `drop.accepted = false; return`.
  - `const r = root.hit(drop.x, drop.y)`.
  - If `r.blockIndex < 0`, `drop.accepted = false; return`.
  - `binding.cursorState.begin(r.blockIndex, r.qtPos)` to move caret.
  - `binding.cursorState.establishFocus(...)`.
  - `binding.clipboardController.pasteText(drop.text)`.
  - `drop.acceptProposedAction()`.

### Test

`tst_live_render_drop_text_qml` (new). Synthesizes a `QDropEvent` via
`QCoreApplication::sendEvent(window, &dropEvent)` and asserts the
text lands at the expected position in the expected block.

Slots:

1. `drop_text_inserts_at_drop_point_in_target_block` — drop "DROP" at
   `(row 1, qtPos 3)` in `"alpha\n\nbravo\n"`; row 1 becomes
   `"braDROPvo"`.
2. `drop_text_into_different_block_than_focused_lands_at_drop_point` —
   focus row 0, drop at row 1; paste lands in row 1.
3. `drop_without_text_payload_is_safe_noop` — drop with no text
   payload doesn't crash, leaves doc unchanged.
4. `drop_collapses_active_cross_block_selection_to_drop_point` —
   active cross-block selection; drop at unrelated block; selection
   collapses, paste lands at drop point.

The drag-event synthesis uses `QDropEvent` constructed with a
`QMimeData` carrying `text/plain`; the receiving window's
`event()` handler routes it through the QML DropArea normally. This
exercises the production path (DropArea.onDropped → C++ controller),
not a synonym.

## Out of scope

- Drag-and-drop OUT of Markoff (drag-start from a selection). Not in
  audit scope; separate spec if dogfooded.
- text/html, text/uri-list, image/* — text/plain only for L3.
- Drop targeting a non-text-bearing block (image, HR). Hit-test
  returns those rows but the drop should land at the start of the
  next text-bearing row (or no-op if none). Implementation: defer to
  `cursorState.begin`'s existing behaviour for non-text rows; the
  drop falls through gracefully (no-op).

## Resolution

`docs/queue.md`: strike L3 with resolution pointer to this spec + test.
