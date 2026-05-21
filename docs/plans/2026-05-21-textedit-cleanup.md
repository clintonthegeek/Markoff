# TextEdit interface cleanup — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the recommendations from the 2026-05-21 TextEdit interface audit. Land L1 (cross-block-selection mutating-key bug), factor the duplicated key dispatcher, bring MathDelegate under coverage (closes L5/L6), and add window-level Shortcut bindings (closes L8).

**Architecture:** Reuses approach (α) from the audit: keep the existing `Keys.onPressed` early-return pattern, but factor the duplicated Ctrl-modifier intercept block into a shared QML JS helper. The L1 fix lives in that helper for printable-char interception and in QML before structural-key delivery for Backspace/Delete/Return.

**Spec:** [`docs/specs/2026-05-21-textedit-interface-audit.md`](../specs/2026-05-21-textedit-interface-audit.md) — sections 6 and 7 are the authoritative design.

**Tech stack:** Qt 6.8+, QML, JS modules (`.mjs`), C++20.

---

## File Structure

**Create:**
- `libs/markoff-live/qml/delegates/KeyDispatch.mjs` — exported JS helper. One entry point `tryDispatchCtrlChord(event, ctx)` returns `true` if the chord was handled (and event.accepted should be set true). Context object `ctx = { binding }`.
- `libs/markoff-live/tests/tst_live_render_cross_block_mutating_key.cpp` — new C++/QtTest binary covering L1 with falsifiability.

**Modify:**
- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` — make `flushPendingDocumentChanges()` `Q_INVOKABLE` so QML can drive a sync flush after `deleteSelection()`.
- `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — replace the inline Ctrl-modifier block (lines ~245–297) with a call to `KeyDispatch.tryDispatchCtrlChord`. Add the L1 mutating-key collapse early-out.
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — same: replace inline Ctrl-modifier block, add L1 collapse.
- `libs/markoff-live/qml/delegates/MathDelegate.qml` — `latexEdit`'s `Keys.onPressed` (lines ~96–102) extended to call `KeyDispatch.tryDispatchCtrlChord` first; closes L5 (undo/redo) and L6 (clipboard) with the explicit decision to route through document layer (vs TextEdit-local).
- `libs/markoff-live/qml/LiveView.qml` — add window-level `Shortcut` elements for Bold/Italic/Strike/InlineCode/Link/Heading0..6/Save/Undo/Redo. Closes L8 for the standalone Markoff app.
- `libs/markoff-live/tests/CMakeLists.txt` — register `tst_live_render_cross_block_mutating_key`.
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` — adopt the dispatcher in the falsifiability test where applicable (e.g. update the existing `ctrl_c_after_three_block_drag_copies_all_three_blocks` to confirm the refactor preserves behavior).
- `docs/queue.md` — discipline-log entries cross-linked to the audit, and resolution notes for what landed.

**Unchanged but in scope to verify:** `LiveStructuralKeyHandler` — the L1 fix is QML-side; structural-key handler stays as is.

---

## Task 1: Make `flushPendingDocumentChanges` Q_INVOKABLE

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`

- [ ] **Step 1:** Add `Q_INVOKABLE` to the declaration. Find the existing line:

```cpp
    void flushPendingDocumentChanges();
```

Replace with:

```cpp
    Q_INVOKABLE void flushPendingDocumentChanges();
```

- [ ] **Step 2:** Build and confirm clean.

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean (Q_INVOKABLE is a moc-time annotation; no .cpp change).

- [ ] **Step 3:** Commit.

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h
git commit -m "feat(live): make flushPendingDocumentChanges Q_INVOKABLE for QML"
```

---

## Task 2: Write the L1 failing test

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_cross_block_mutating_key.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1:** Create the test file with four slots — Backspace, Delete, Return, printable-char — each verifying that a cross-block selection collapses to a single block when the mutating key is pressed.

The test uses the harness pattern from `tst_live_render_qml_integration.cpp` (load doc, drive QML, assert on document state) since the bug is at the QML/TextEdit boundary.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <markoff/core/MarkoffDocument.h>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

class TestCrossBlockMutatingKey : public QObject {
    Q_OBJECT
private slots:
    void backspace_on_cross_block_selection_collapses() {
        QmlIntegrationFixture fix(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                  /*expectedRowCount=*/3);

        // Drag-select from row 0 qtPos 2 to row 2 qtPos 3 — spans the
        // three blocks.
        QObject *cs = fix.binding()->property("cursorState").value<QObject*>();
        QVERIFY(cs);
        QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, 2));
        QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(int, 3));
        QCOMPARE(cs->property("hasSelection").toBool(), true);

        // Send Backspace to the focused TextEdit via the window.
        QTest::keyClick(fix.window(), Qt::Key_Backspace);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // Expectation: the three blocks have collapsed into one with the
        // surviving prefix ("al") + suffix ("ma") joined → "alma".
        QCOMPARE(fix.document()->iterateBlocks().size(), 1u);
        const auto id = fix.document()->iterateBlocks()[0];
        QCOMPARE(fix.document()->blockText(id), QByteArray("alma"));
    }

    void delete_on_cross_block_selection_collapses() {
        QmlIntegrationFixture fix(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                  /*expectedRowCount=*/3);

        QObject *cs = fix.binding()->property("cursorState").value<QObject*>();
        QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, 2));
        QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(int, 3));

        QTest::keyClick(fix.window(), Qt::Key_Delete);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        QCOMPARE(fix.document()->iterateBlocks().size(), 1u);
        const auto id = fix.document()->iterateBlocks()[0];
        QCOMPARE(fix.document()->blockText(id), QByteArray("alma"));
    }

    void printable_char_on_cross_block_selection_replaces() {
        QmlIntegrationFixture fix(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                  /*expectedRowCount=*/3);

        QObject *cs = fix.binding()->property("cursorState").value<QObject*>();
        QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, 2));
        QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(int, 3));

        QTest::keyClick(fix.window(), 'X');
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // The three blocks collapse with the typed X replacing the
        // selected range → "alXma".
        QCOMPARE(fix.document()->iterateBlocks().size(), 1u);
        const auto id = fix.document()->iterateBlocks()[0];
        QCOMPARE(fix.document()->blockText(id), QByteArray("alXma"));
    }

    void return_on_cross_block_selection_replaces_with_paragraph_break() {
        QmlIntegrationFixture fix(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                  /*expectedRowCount=*/3);

        QObject *cs = fix.binding()->property("cursorState").value<QObject*>();
        QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, 2));
        QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
            Q_ARG(int, 2), Q_ARG(int, 3));

        QTest::keyClick(fix.window(), Qt::Key_Return);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // Return on a non-empty selection should: collapse the selection,
        // then insert a paragraph break at the collapse point. Result:
        // two blocks "al" and "ma".
        const auto blocks = fix.document()->iterateBlocks();
        QCOMPARE(blocks.size(), 2u);
        QCOMPARE(fix.document()->blockText(blocks[0]), QByteArray("al"));
        QCOMPARE(fix.document()->blockText(blocks[1]), QByteArray("ma"));
    }
};

QTEST_MAIN(TestCrossBlockMutatingKey)
#include "tst_live_render_cross_block_mutating_key.moc"
```

- [ ] **Step 2:** Register in CMake. After the existing `tst_live_render_qml_integration` block (or near it — find the right location), add:

```cmake
qt_add_executable(tst_live_render_cross_block_mutating_key
    tst_live_render_cross_block_mutating_key.cpp
    QmlIntegrationFixture.cpp
    LiveRealisticInputHarness.cpp
)
target_link_libraries(tst_live_render_cross_block_mutating_key
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Qml Qt6::Quick
    markoff-live-app-internalplugin markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_cross_block_mutating_key
         COMMAND tst_live_render_cross_block_mutating_key)
```

(Use the exact link list from the existing `tst_live_render_qml_integration` CMake entry — copy that block verbatim and just rename the target.)

- [ ] **Step 3:** Reconfigure CMake and build.

```bash
cmake -S . -B build-dev
cmake --build build-dev --target tst_live_render_cross_block_mutating_key -j 8
```

- [ ] **Step 4:** Run the test and confirm all 4 slots FAIL with the bug-already-in-prod behavior.

```bash
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_live_render_cross_block_mutating_key
```

Expected: backspace / delete / printable / return all fail because the cross-block selection is NOT collapsed; only the focused block's within-block selection is mutated by TextEdit.

- [ ] **Step 5:** Commit the failing test (per invariant 4 — falsifiability is the prerequisite for the fix).

```bash
git add libs/markoff-live/tests/tst_live_render_cross_block_mutating_key.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "test: L1 falsifiable — cross-block mutating-key bug

Four slots cover the bug-already-in-prod identified as L1 in the
2026-05-21 TextEdit interface audit: backspace, delete, printable
char, and Return on a cross-block selection. All FAIL on HEAD
because the TextEdit consumes the key locally and only mutates
the focused block. The L1 fix in the next commit makes them pass.

Per invariant 4 — the test fails before the fix."
```

---

## Task 3: Create the shared KeyDispatch.mjs and implement L1 + Ctrl-modifier intercepts

**Files:**
- Create: `libs/markoff-live/qml/delegates/KeyDispatch.mjs`

- [ ] **Step 1:** Create the module exporting two functions.

```javascript
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared key dispatcher for the three TextEdit-bearing delegates
// (UnifiedInlineTextDelegate, CodeBlockDelegate, MathDelegate). Centralises
// the Ctrl-modifier chord intercepts and the cross-block-selection collapse
// for mutating keys.
//
// Recommended by the 2026-05-21 TextEdit interface audit (approach α).
// Replaces the duplicated Keys.onPressed Ctrl-modifier blocks in each
// delegate.
//
// USAGE in a delegate's Keys.onPressed:
//
//   import "KeyDispatch.mjs" as KeyDispatch
//   ...
//   Keys.onPressed: (event) => {
//       if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding })) return
//       if (KeyDispatch.collapseSelectionIfMutating(event, { binding: root.liveBinding })) {
//           // selection collapsed; let the rest of the handler (or TextEdit)
//           // process the residual semantics of the keystroke.
//       }
//       ... existing structural/nav handling ...
//   }

// Modifier helpers.
const isCtrl  = (mods) => (mods & Qt.ControlModifier) !== 0
const isShift = (mods) => (mods & Qt.ShiftModifier)   !== 0
const isAlt   = (mods) => (mods & Qt.AltModifier)     !== 0

// Dispatch the standard Ctrl-modifier chords. Returns `true` and sets
// event.accepted = true if handled.
//
// Chords:
//   Ctrl+C, Ctrl+X, Ctrl+V  → clipboardController.copy/cut/paste
//   Ctrl+A                  → cursorState.selectAll
//   Ctrl+Z                  → actionController.undoAction
//   Ctrl+Y, Ctrl+Shift+Z    → actionController.redoAction
function tryDispatchCtrlChord(event, ctx) {
    const binding = ctx.binding
    if (!binding) return false
    const k    = event.key
    const mods = event.modifiers

    // Ctrl+Shift+Z is the alt-redo chord.
    if (isCtrl(mods) && isShift(mods) && !isAlt(mods) && k === Qt.Key_Z) {
        const ac = binding.actionController
        if (ac && ac.redoAction) ac.redoAction.trigger()
        event.accepted = true
        return true
    }

    if (!isCtrl(mods) || isShift(mods) || isAlt(mods)) return false

    const clip = binding.clipboardController
    const cs   = binding.cursorState
    const ac   = binding.actionController

    if (k === Qt.Key_C) { if (clip) clip.copy();  event.accepted = true; return true }
    if (k === Qt.Key_X) { if (clip) clip.cut();   event.accepted = true; return true }
    if (k === Qt.Key_V) { if (clip) clip.paste(); event.accepted = true; return true }
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

// L1 fix per audit. When a cross-block selection is active and the keystroke
// would mutate text (Backspace/Delete/Return/Enter/printable char), collapse
// the selection first via the document layer, then either accept-and-stop
// (for pure-delete cases) or fall through (for Return / printable, where the
// residual action lands on the collapsed cursor).
//
// Returns: { handled: boolean, accepted: boolean }
//   handled  — whether the function did something
//   accepted — whether event.accepted should be set true (and other handlers
//              short-circuited)
function collapseSelectionIfMutating(event, ctx) {
    const binding = ctx.binding
    if (!binding) return { handled: false, accepted: false }
    const cs = binding.cursorState
    if (!cs || !cs.hasSelection) return { handled: false, accepted: false }

    const k = event.key
    const mods = event.modifiers
    const isBackspaceOrDelete = (k === Qt.Key_Backspace || k === Qt.Key_Delete)
    const isReturnOrEnter     = (k === Qt.Key_Return || k === Qt.Key_Enter)
    // Printable char: event.text is non-empty + not a control-modified chord
    // (those were already dispatched above). Filter out chars that are
    // navigation/structural keys disguised as text.
    const hasText = event.text && event.text.length > 0
                    && !isCtrl(mods) && !isAlt(mods)
    const isPrintable = hasText && isReturnOrEnter === false
                        && isBackspaceOrDelete === false
                        // Filter Tab and Escape — they have event.text but
                        // aren't text-entry.
                        && k !== Qt.Key_Tab && k !== Qt.Key_Escape

    if (!isBackspaceOrDelete && !isReturnOrEnter && !isPrintable) {
        return { handled: false, accepted: false }
    }

    cs.deleteSelection()
    // Force the model to reflect the collapse so TextEdit re-binds to the
    // surviving block before processing any residual key effect.
    binding.flushPendingDocumentChanges()

    if (isBackspaceOrDelete) {
        // Selection delete IS the entire intended action.
        event.accepted = true
        return { handled: true, accepted: true }
    }
    // For Return/printable: let normal handling resume — the selection is
    // collapsed, so the structural-key handler (Return) or TextEdit
    // (printable char) will act on the collapsed cursor.
    return { handled: true, accepted: false }
}

// Public exports.
WorkerScript // no-op anchor to keep the JS engine treating this as ESM
</javascript>
```

Note: the `WorkerScript` anchor on the last line is a workaround if QML's `.mjs` parser is finicky. If the file works without it, remove the line.

- [ ] **Step 2:** Build (no other changes yet — confirm the module file is at least parseable).

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: clean. If QML compiler complains about the `.mjs` file, switch the extension to `.js` and use `.import "KeyDispatch.js" as KeyDispatch` in the importers.

- [ ] **Step 3:** Commit the helper.

```bash
git add libs/markoff-live/qml/delegates/KeyDispatch.mjs
git commit -m "feat(live-qml): shared KeyDispatch module for delegate intercepts

Per audit approach α — consolidates the Ctrl-modifier chord intercepts
and the L1 cross-block-selection collapse into one helper that
UnifiedInlineTextDelegate, CodeBlockDelegate, and MathDelegate all use.

No callers yet; this commit just lands the module."
```

---

## Task 4: Adopt the dispatcher in UnifiedInlineTextDelegate

**Files:**
- Modify: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`

- [ ] **Step 1:** At the top of the file, after the existing imports, add:

```qml
import "KeyDispatch.mjs" as KeyDispatch
```

- [ ] **Step 2:** Replace the inline Ctrl-modifier block (lines ~245–297, the `if ((mods & Qt.ControlModifier) ...)` block ending with the Ctrl+Shift+Z handler) with one call:

```qml
            if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding })) return

            // L1: cross-block-selection-aware mutating keys.
            const _collapse = KeyDispatch.collapseSelectionIfMutating(event, { binding: root.liveBinding })
            if (_collapse.accepted) return
            // If _collapse.handled is true but accepted is false, the selection
            // was collapsed; fall through to normal structural/nav handling so
            // Return splits the block or the printable char inserts.
```

The rest of the `Keys.onPressed` (structural keys, nav, format keys) remains untouched.

- [ ] **Step 3:** Build.

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: clean.

- [ ] **Step 4:** Run the L1 test — expect at least the Backspace, Delete, and printable-char slots to PASS. (Return may still need work depending on how the residual handler treats it.)

```bash
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_live_render_cross_block_mutating_key
```

If Return fails: investigate whether `LiveStructuralKeyHandler::tryHandle` is called with the collapsed cursor and handles Return correctly. If not, add a follow-up step inside this task; do NOT skip the slot.

- [ ] **Step 5:** Run the full Live suite to confirm no regression.

```bash
scripts/run-tests.sh -R '^tst_live'
```

Expected: all previously-green tests still green. The L1 slots all PASS.

- [ ] **Step 6:** Commit.

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml
git commit -m "feat(live-qml): UnifiedInline adopts KeyDispatch + L1 collapse

Replaces the inline Ctrl-modifier intercept block with a one-line
call to KeyDispatch.tryDispatchCtrlChord. Adds L1 cross-block
selection collapse via collapseSelectionIfMutating.

Closes L1 in the focused-block-is-UnifiedInline case (paragraphs,
headings, blockquotes, list-items)."
```

---

## Task 5: Adopt the dispatcher in CodeBlockDelegate

**Files:**
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 1:** Mirror Task 4's pattern. Add the import at the top:

```qml
import "KeyDispatch.mjs" as KeyDispatch
```

- [ ] **Step 2:** Replace the inline Ctrl-modifier block in `Keys.onPressed` (lines ~90–124) with:

```qml
            if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding })) return
            const _collapse = KeyDispatch.collapseSelectionIfMutating(event, { binding: root.liveBinding })
            if (_collapse.accepted) return
```

- [ ] **Step 3:** Build + run the L1 test + full Live suite (as Task 4 Step 4-5).

```bash
cmake --build build-dev --target markoff_live -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_live_render_cross_block_mutating_key
scripts/run-tests.sh -R '^tst_live'
```

Expected: all green.

- [ ] **Step 4:** Commit.

```bash
git add libs/markoff-live/qml/delegates/CodeBlockDelegate.qml
git commit -m "feat(live-qml): CodeBlockDelegate adopts KeyDispatch + L1 collapse"
```

---

## Task 6: MathDelegate's latexEdit adopts dispatcher (closes L5/L6)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml`

- [ ] **Step 1:** Add the import:

```qml
import "KeyDispatch.mjs" as KeyDispatch
```

- [ ] **Step 2:** Find the `latexEdit` TextEdit's `Keys.onPressed` (currently handles only Escape). Extend it to call the dispatcher first:

```qml
            Keys.onPressed: (event) => {
                if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding })) return
                // ... existing Escape handling ...
            }
```

This brings Ctrl+C/X/V/A/Z/Y/Shift+Z under coverage for the LaTeX-source-edit surface (closes audit L5 + L6 with the explicit decision to route through the document layer).

- [ ] **Step 3:** Build + full Live suite.

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -R '^tst_live'
```

Expected: all green. No new test slot required for L5/L6 — coverage is symmetric with the existing Ctrl+C/Z tests on the other delegates.

- [ ] **Step 4:** Commit.

```bash
git add libs/markoff-live/qml/delegates/MathDelegate.qml
git commit -m "feat(live-qml): MathDelegate latexEdit adopts KeyDispatch

Closes audit L5 (Ctrl+Z in math source bypassing d2UndoLog) and
L6 (clipboard chords in math source bypassing LiveClipboardController).
Explicit decision: when editing LaTeX source in BlockInternalEdit
variant, all standard Ctrl-modifier chords route through the document
layer for consistency with the surrounding paragraphs."
```

---

## Task 7: Add window-level Shortcut bindings in LiveView.qml (closes L8)

**Files:**
- Modify: `libs/markoff-live/qml/LiveView.qml`

- [ ] **Step 1:** Find the existing `Shortcut` block (currently has zoom + dark-toggle bindings around line 142). Add Shortcut elements for each unbound LiveActionController QAction.

For each of: `boldAction`, `italicAction`, `strikeAction`, `inlineCodeAction`, `linkAction`, `undoAction`, `redoAction`, `saveAction`, `heading0Action`, `heading1Action`, `heading2Action`, `heading3Action`, `heading4Action`, `heading5Action`, `heading6Action` — add a `Shortcut` element that triggers the action. Pattern:

```qml
    Shortcut {
        sequence: liveBinding && liveBinding.actionController
                  ? liveBinding.actionController.boldAction.shortcut
                  : ""
        onActivated: if (liveBinding && liveBinding.actionController)
                         liveBinding.actionController.boldAction.trigger()
    }
```

Repeat for each. Use the same `liveBinding && liveBinding.actionController ? ... : ""` null-guard pattern as the existing zoom Shortcuts.

If the existing zoom block uses `Instantiator` or `Repeater`, mirror that pattern with a model of action names.

- [ ] **Step 2:** Build + run the QML integration tests to confirm no regression.

```bash
cmake --build build-dev --target markoff_live -j 8
scripts/run-tests.sh -R 'qml_integration|font_scale_actions|actions_dispatch'
```

Expected: all green.

- [ ] **Step 3:** Commit.

```bash
git add libs/markoff-live/qml/LiveView.qml
git commit -m "feat(live-qml): window-level Shortcut bindings for editor actions

Closes audit L8 — Bold/Italic/Strike/InlineCode/Link/Heading0..6/
Save/Undo/Redo QActions had setShortcut() configured but no QML
Shortcut element binding them. Standalone markoff-live-app
now responds to these chords; the Corbomite host was already
handling them via KAction's global shortcut registration."
```

---

## Task 8: Falsifiability proof for L1 (per invariant 4)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/KeyDispatch.mjs` (temporary stub then revert)

- [ ] **Step 1:** Stub `collapseSelectionIfMutating` to return `{ handled: false, accepted: false }` unconditionally.

```javascript
function collapseSelectionIfMutating(event, ctx) {
    return { handled: false, accepted: false }
}
```

- [ ] **Step 2:** Run the L1 test and confirm all 4 slots FAIL.

```bash
cmake --build build-dev --target markoff_live -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_live_render_cross_block_mutating_key
```

Expected: all 4 FAIL (regression to pre-fix behavior).

- [ ] **Step 3:** Commit the stub.

```bash
git add libs/markoff-live/qml/delegates/KeyDispatch.mjs
git commit -m "test: falsifiability stub — L1 collapseSelectionIfMutating

Reverted by the next commit. Per invariant 4."
```

- [ ] **Step 4:** Revert.

```bash
git revert HEAD --no-edit
```

- [ ] **Step 5:** Re-run and confirm green.

```bash
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_live_render_cross_block_mutating_key
```

Expected: all 4 PASS.

---

## Task 9: Update the discipline log + audit closeout

**Files:**
- Modify: `docs/queue.md`

- [ ] **Step 1:** In the Discipline Log section, add an entry that points future agents at the audit doc:

```markdown
- 2026-05-21 `libs/markoff-live/qml/delegates/` — meta — every QML
  TextEdit in this library has a documented intercept architecture:
  see `docs/specs/2026-05-21-textedit-interface-audit.md` for the
  conflict map and `docs/plans/2026-05-21-textedit-cleanup.md` for
  the implementation history. Before adding a new Ctrl-modifier chord
  or mutating-key handler in any delegate, route it through
  `qml/delegates/KeyDispatch.mjs` rather than duplicating logic.
```

- [ ] **Step 2:** Add closeout entries for each landed item, mirroring the existing close pattern (`~~...~~ → fixed in <commit>`):

```markdown
- ~~Audit L1 (cross-block-selection mutating-key bug)~~ → fixed in
  Task 4-5 of plan `2026-05-21-textedit-cleanup.md`. Falsifiable test
  `tst_live_render_cross_block_mutating_key` (4 slots: backspace,
  delete, printable, return) committed at <SHA-from-task-2> and
  proven falsifiable at <SHA-from-task-8>.
- ~~Audit L5 + L6 (MathDelegate latexEdit undo + clipboard)~~ →
  closed in Task 6 of plan `2026-05-21-textedit-cleanup.md`. latexEdit
  adopts KeyDispatch.tryDispatchCtrlChord.
- ~~Audit L8 (unbound QAction shortcuts in standalone markoff-live-app)~~
  → closed in Task 7. LiveView.qml window-level Shortcut elements
  for Bold/Italic/Strike/InlineCode/Link/Heading0..6/Save/Undo/Redo.
- Audit L2, L3, L4, L7, L9 deferred — not bug-already-in-prod;
  each needs its own design pass. See audit §5 for symptoms.
```

Replace `<SHA-from-task-2>` and `<SHA-from-task-8>` with the actual commit hashes after they land.

- [ ] **Step 3:** Commit.

```bash
git add docs/queue.md
git commit -m "docs: close TextEdit interface audit items in queue.md"
```

---

## Task 10: Full regression check + Corbomite bump + push

**Files:** (none — verification only)

- [ ] **Step 1:** Run the entire test suite.

```bash
scripts/run-tests.sh
```

Expected: all green. New `tst_live_render_cross_block_mutating_key` (4 slots) in the count. The earlier 221 + heading-level test + clipboard fixes baseline + this work should leave us at ~225 binaries.

- [ ] **Step 2:** Push Markoff.

```bash
git push origin HEAD:exploration/new-foundation
```

- [ ] **Step 3:** Bump Corbomite's submodule pin.

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git fetch
git checkout <new-markoff-head>
cd /home/clinton/dev/Corbomite
cmake --build build-dev -j 8
```

Expected: Corbomite builds clean against the new Markoff.

- [ ] **Step 4:** Commit the bump.

```bash
git add libs/markoff-family
git commit -m "chore(submodule): bump markoff-family — TextEdit interface cleanup

Pulls in the L1 cross-block-selection collapse, the KeyDispatch
factorization, MathDelegate's adoption, and the standalone Shortcut
bindings. See Markoff docs/plans/2026-05-21-textedit-cleanup.md."
```

- [ ] **Step 5:** Push Corbomite.

```bash
git push origin port/foundation-exploration
```

---

## Self-review

**Spec coverage:**
- L1 fix (cross-block mutating keys): Task 2 (test) + Task 3-5 (impl) + Task 8 (falsifiability) ✓
- Dispatcher factorization: Task 3 (helper) + Task 4-5 (adoption) ✓
- MathDelegate adoption (L5 + L6): Task 6 ✓
- Window-level Shortcuts (L8): Task 7 ✓
- Discipline log closeout: Task 9 ✓
- Audit L2/L3/L4/L7/L9 explicitly deferred with reasoning in queue.md ✓

**Placeholder scan:** no TBD / TODO / fill-in. The `<SHA-from-task-N>` markers in Task 9 are intentional — they get replaced at execution time once the SHAs exist.

**Type consistency:** `KeyDispatch.tryDispatchCtrlChord(event, ctx)` and `KeyDispatch.collapseSelectionIfMutating(event, ctx)` signatures consistent across the dispatcher itself and all three adopting delegates. `ctx = { binding: root.liveBinding }` shape uniform. Return type of `collapseSelectionIfMutating` is `{ handled, accepted }` consistently.

**Out of scope (deferred with explicit notes in queue.md):**
- L2 — middle-click X11 PRIMARY-selection paste.
- L3 — drag-and-drop of external text.
- L4 — Ctrl+Shift+Left/Right within-block selection vs cross-block anchor.
- L7 — IME composition.
- L9 — Ctrl+Backspace / Ctrl+Delete word semantics at block boundary.
- The eventual unification of `m_applyingTextUpdate` + `m_applyingSelectionEmit` re-entrance guards into the dispatcher (D7 of the markoff-live freeze spec).
