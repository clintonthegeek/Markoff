// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared key dispatcher for the TextEdit-bearing delegates
// (UnifiedInlineTextDelegate, CodeBlockDelegate, MathDelegate's latexEdit).
// Centralises the Ctrl-modifier chord intercepts and the cross-block
// selection collapse for mutating keys.
//
// Recommended by `docs/specs/2026-05-21-textedit-interface-audit.md`
// approach (α). Replaces the previously-duplicated Keys.onPressed
// Ctrl-modifier blocks in each delegate. See also
// `docs/plans/2026-05-21-textedit-cleanup.md`.
//
// USAGE in a delegate's Keys.onPressed:
//
//   import "KeyDispatch.js" as KeyDispatch
//   ...
//   Keys.onPressed: (event) => {
//       if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding })) return
//       const _c = KeyDispatch.collapseSelectionIfMutating(event, { binding: root.liveBinding })
//       if (_c.accepted) return
//       // _c.handled may be true (selection was collapsed); fall through
//       // so the structural / nav / TextEdit handlers process the residual
//       // semantics of the keystroke (Return-splits-block, printable-char-inserts).
//       ...existing structural and navigation handling...
//   }

.pragma library


// --- modifier helpers ------------------------------------------------------

function _isCtrl(mods)  { return (mods & Qt.ControlModifier) !== 0 }
function _isShift(mods) { return (mods & Qt.ShiftModifier)   !== 0 }
function _isAlt(mods)   { return (mods & Qt.AltModifier)     !== 0 }


// --- public API ------------------------------------------------------------

// Dispatch the standard Ctrl-modifier chords. Returns `true` (and sets
// event.accepted = true) if the chord was recognised and handled; false
// otherwise.
//
// Chords handled:
//   Ctrl+C, Ctrl+X, Ctrl+V  → clipboardController.copy/cut/paste
//   Ctrl+A                  → cursorState.selectAll
//   Ctrl+Z                  → actionController.undoAction
//   Ctrl+Y, Ctrl+Shift+Z    → actionController.redoAction
//
// All chord handlers are no-op safe when the corresponding controller
// pointer is null.
function tryDispatchCtrlChord(event, ctx) {
    const binding = ctx.binding
    if (!binding) return false
    const k    = event.key
    const mods = event.modifiers

    // Ctrl+Shift+Z is the alt-redo chord, handled before the bare-Ctrl branch
    // so the Shift modifier isn't misread as a separate chord category.
    if (_isCtrl(mods) && _isShift(mods) && !_isAlt(mods) && k === Qt.Key_Z) {
        const ac = binding.actionController
        if (ac && ac.redoAction) ac.redoAction.trigger()
        event.accepted = true
        return true
    }

    if (!_isCtrl(mods) || _isShift(mods) || _isAlt(mods)) return false

    const clip = binding.clipboardController
    const cs   = binding.cursorState
    const ac   = binding.actionController

    if (k === Qt.Key_C) { if (clip) clip.copy();    event.accepted = true; return true }
    if (k === Qt.Key_X) { if (clip) clip.cut();     event.accepted = true; return true }
    if (k === Qt.Key_V) { if (clip) clip.paste();   event.accepted = true; return true }
    if (k === Qt.Key_A) { if (cs)   cs.selectAll(); event.accepted = true; return true }
    if (k === Qt.Key_Z) {
        if (ac && ac.undoAction) ac.undoAction.trigger()
        event.accepted = true
        return true
    }
    if (k === Qt.Key_Y) {
        if (ac && ac.redoAction) ac.redoAction.trigger()
        event.accepted = true
        return true
    }
    return false
}


// L1 fix per the 2026-05-21 audit. When a cross-block selection is active
// and the keystroke would mutate text (Backspace/Delete/Return/printable
// char), collapse the selection first via the document layer, then either
// accept-and-stop (for pure-delete cases) or fall through (for Return and
// printable char, where the residual action lands on the collapsed cursor).
//
// Returns: { handled: bool, accepted: bool }
//   handled  — true if the function did something (collapsed the selection)
//   accepted — true if event.accepted has been set and the caller should
//              return immediately
function collapseSelectionIfMutating(event, ctx) {
    const binding = ctx.binding
    if (!binding) return { handled: false, accepted: false }
    const cs = binding.cursorState
    if (!cs || !cs.hasSelection || !cs.hasSelection())
        return { handled: false, accepted: false }

    const k    = event.key
    const mods = event.modifiers

    const isBackspaceOrDelete = (k === Qt.Key_Backspace || k === Qt.Key_Delete)
    const isReturnOrEnter     = (k === Qt.Key_Return || k === Qt.Key_Enter)

    // Printable: event.text non-empty and not a control-modified chord
    // (the Ctrl-chord dispatcher above already handled those). Exclude
    // Return/Enter (they have event.text == "\r"), Tab, Escape — all may
    // carry non-empty text but are not text-entry.
    const hasText = event.text && event.text.length > 0
                    && !_isCtrl(mods) && !_isAlt(mods)
    const isPrintable = hasText
                        && !isReturnOrEnter
                        && !isBackspaceOrDelete
                        && k !== Qt.Key_Tab
                        && k !== Qt.Key_Escape

    if (!isBackspaceOrDelete && !isReturnOrEnter && !isPrintable) {
        return { handled: false, accepted: false }
    }

    cs.deleteSelection()
    // Force the model to reflect the collapse synchronously so the residual
    // handlers see a single-block state.
    if (binding.flushPendingDocumentChanges) binding.flushPendingDocumentChanges()

    if (isBackspaceOrDelete) {
        // Selection delete is the entire intended action.
        event.accepted = true
        return { handled: true, accepted: true }
    }

    // For Return/Enter or printable char we want the residual action to
    // land on the collapsed cursor. After deleteSelection + model rebuild,
    // the TextEdit's local cursorPosition may have been clamped to the
    // new text bounds (typically end-of-text); directly position it at the
    // collapse point so TextEdit's residual handler (printable insert)
    // or the structural-key handler (Return-splits) acts there.
    if (ctx.textEdit && cs.activeBlock && cs.activeQtPos) {
        const qtPos = cs.activeQtPos()
        if (qtPos >= 0) ctx.textEdit.cursorPosition = qtPos
    }
    return { handled: true, accepted: false }
}
