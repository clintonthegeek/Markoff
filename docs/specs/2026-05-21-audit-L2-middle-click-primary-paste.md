# Audit L2 — Middle-click PRIMARY-selection paste

**Date:** 2026-05-21
**Status:** Spec + impl in same session.
**Branch:** `exploration/new-foundation`
**Source:** `docs/specs/2026-05-21-textedit-interface-audit.md` §3.6(b), §5 (L2).

## The bug

On X11 / XWayland, middle-click pastes the X11 PRIMARY selection (the
auto-populated "last selected anywhere" buffer, distinct from the
CLIPBOARD selection driven by Ctrl+C/V). Qt's `TextEdit` handles
middle-click natively, calling `paste(QClipboard::Selection)` on its
internal cursor. The result:

- The paste lands inside the **focused** delegate, regardless of where
  the user actually middle-clicked. (X11 convention is paste-at-click;
  Qt's native handler ignores the click position.)
- The paste **bypasses** `LiveClipboardController`. No structured-paste
  routing — even if PRIMARY happens to carry our own
  `application/x-markoff-blocks` MIME (it doesn't today, but it might
  in a future where copy populates PRIMARY too), the structured path
  isn't taken.
- The paste targets only the focused block; cross-block insertion
  semantics aren't applied.

## Decision

Intercept middle-click at the LiveView's `MouseArea` level and route
through a new `LiveClipboardController::pastePrimary()`. The intercept:

1. Hit-tests the click point with the existing `hit()` function.
2. Moves the cursor to the hit point via `cursorState.begin(blockIndex,
   qtPos)` — clearing any active selection. This is the
   "paste-at-click" semantic.
3. Calls `clipboardController.pastePrimary()`. Reuses the same
   structured / flat paste routing as `paste()`, but reads from
   `QClipboard::Selection` instead of `QClipboard::Clipboard`.

## Why this over alternatives

α. **Intercept at MouseArea (this spec).** Single intercept point;
   no per-delegate plumbing. Uses the same `hit()` function as
   left/right clicks. Adds one Q_INVOKABLE method to the existing
   clipboard controller.

β. **Disable middle-click entirely (don't paste).** Some applications
   do this. Rejected: PRIMARY-selection paste is a long-standing
   X11/Linux convention; removing it without explicit user opt-in is
   surprising.

γ. **Per-delegate intercept inside each `TextEdit`'s
   `Keys.forwardTo` / `MouseArea` overlay.** More plumbing for the
   same result. Rejected.

δ. **Let TextEdit handle it natively.** The status quo. Rejected
   per the bug description: paste-at-focus instead of paste-at-click,
   no structured routing, no cross-block selection collapse.

## Implementation

### C++

`libs/markoff-live/include/markoff/live/LiveClipboardController.h`:

- Add `Q_INVOKABLE void pastePrimary();`.

`libs/markoff-live/src/LiveClipboardController.cpp`:

- Factor the body of `paste()` into a private helper
  `pasteFrom(QClipboard::Mode)`. The existing `paste()` calls
  `pasteFrom(QClipboard::Clipboard)`; the new `pastePrimary()` calls
  `pasteFrom(QClipboard::Selection)`.
- `pasteFrom` early-returns if `QApplication::clipboard()
  ->supportsSelection()` is false AND mode is Selection (so on
  platforms without PRIMARY, `pastePrimary()` is a safe no-op).

### QML

`libs/markoff-live/qml/LiveView.qml`:

- Add `Qt.MiddleButton` to `mouseArea.acceptedButtons`.
- Add a `MiddleButton` branch to `onPressed`: hit-test, move cursor
  via `cursorState.begin`, then call
  `binding.clipboardController.pastePrimary()`.
- Set `event.accepted = true` to prevent fall-through to TextEdit.

The press-vs-click question: X11 convention is paste-on-press (xterm
behaviour). Qt's `QTextEdit` does its middle-paste in
`mousePressEvent`. We do the same in `onPressed` for parity.

### Test

`tst_live_render_middle_click_paste_qml` (new, QML integration
fixture). Slots:

1. `middle_click_pastes_primary_selection_at_click_point` — pre-populate
   PRIMARY with `"injected"`, middle-click at `(row 1, qtPos 5)`,
   assert row 1's text now contains `"injected"` inserted at that
   position. Skips if `clipboard->supportsSelection()` returns false
   (offscreen QPA may not).
2. `middle_click_when_primary_unavailable_is_safe_noop` — runs
   regardless; if `supportsSelection()` is false, middle-click must
   not crash and must not produce any document change.
3. `middle_click_does_not_paste_into_focused_block_when_clicked_in_other_block`
   — focus block 0, middle-click in block 1; the paste lands in block
   1, not block 0. This pins the "paste-at-click, not paste-at-focus"
   semantic.

## Out of scope

- Populating PRIMARY on selection (the "select to copy" half of the
  X11 convention). Many Qt apps don't do this either; `TextEdit`'s
  native populate is unaffected by this spec. Track separately if
  ever needed.
- Wayland-native primary-selection protocol. Qt 6 abstracts this via
  `QClipboard::supportsSelection()`; the spec works the same way
  whether the backend is X11 or wayland-primary-selection.

## Resolution

`docs/queue.md`: strike L2 with resolution pointer to this spec + test.
