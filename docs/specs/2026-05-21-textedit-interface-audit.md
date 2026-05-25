# TextEdit interface audit — systematic conflicts with the document layer

Date: 2026-05-21
Branch: `exploration/new-foundation`
Status: design audit (not a plan, not a spec)

## 0. Framing

The dogfood passes in the last two sessions have surfaced two classes of bug
of identical shape: QML's `TextEdit` is a self-contained text editor with its
own per-instance state, and when one is focused it silently consumes
keyboard shortcuts and gestures that the library's own document layer
(`LiveActionController`, `LiveClipboardController`, `LiveFormatController`,
`LiveCursorState`, `LiveStructuralKeyHandler`, `LiveNavigationController`,
`d2UndoLog`) is supposed to handle. The most recent fixes are commit
`8524d13` (Ctrl+C/X/V/A clipboard + Select All) and commit `586d7c7`
(Ctrl+Z/Y undo/redo) — both apply the same pattern: a `Keys.onPressed`
early-return in the delegate that catches the chord before `TextEdit`'s
built-in handler runs.

The pattern is the right pattern. What is missing is a complete map of the
surface area: every TextEdit instance, every chord it eats, every gesture
it interprets, and whether the document layer has an opinion that conflicts.
This document is that map. It is intended to convert the next session from
"fix the next dogfood-surfaced symptom" into "decide which conflicts matter,
remove or codify each one once, and stop accumulating one-off intercepts."

## 1. TextEdit instances in the library

There are exactly three `TextEdit` instantiations in `libs/markoff-live/qml/`:

| Location | Block kinds served | Role |
|---|---|---|
| `qml/delegates/UnifiedInlineTextDelegate.qml:156` (`edit`) | paragraph, heading, blockquote, list-item | Primary content surface for the four `text-inline` delegate-class block kinds. Persistent across within-class kind transitions (paragraph↔heading↔blockquote↔list-item). |
| `qml/delegates/CodeBlockDelegate.qml:34` (`edit`) | code-block | Primary content surface for code blocks. Hosts the KSyntaxHighlighting attached object. |
| `qml/delegates/MathDelegate.qml:72` (`latexEdit`) | math | Auxiliary surface — visible only when the cursor is in `BlockInternalEdit` variant for that block. Edits the LaTeX source; the render view above it shows the raw block text in monospace. |

There is also one `TextInput` (`qml/delegates/CodeBlockDelegate.qml:219`,
`langInput`) used to edit the code-fence language tag. It is auxiliary and
only visible when the language tag is tapped; it has its own `Keys.onReturnPressed`
and `Keys.onEscapePressed` and never holds focus across structural edits.
It does not participate in the document-layer text path and is out of scope
for this audit.

The host application's own input surfaces (e.g. the `langInput` above, the
`SourceEditor` in markoff-source, find-bar QLineEdits) are separate engines
and out of scope. This audit is exclusively about the three TextEdits that
mediate user editing of D2 block content in live mode.

## 2. Native TextEdit behaviors we depend on

The relationship is asymmetric: the document layer treats TextEdit as a
*view* over a block's CRDT buffer, but TextEdit treats itself as the canonical
editor. The places where we lean on TextEdit's native behaviour are the
seams where conflict is most likely.

Signals we subscribe to and consume:

- **`onContentsChange(qtPos, removed, added)`** is the load-bearing edit
  event. `LiveEditBinding::onContentsChange` (`src/LiveEditBinding.cpp:114`)
  converts the (Qt UTF-16 code-unit) coordinates into UTF-8 byte offsets and
  calls `MarkoffDocument::d2ApplyBufferEdit`. This is the only path by which
  user typing reaches the CRDT.
- **`onCursorPositionChanged`** is consumed inline in both
  `UnifiedInlineTextDelegate.qml:199` and `CodeBlockDelegate.qml:61`, which
  forward to `LiveCursorState::syncFromTextEdit`. This is how typing,
  native within-block arrow keys, word-jump (Ctrl+Left/Right inside a
  block), Home/End, and mouse re-positioning of the caret all reach
  `LiveCursorState`.
- **`inputMethodComposing`** is bound to `LiveEditBinding.composing` in both
  text-bearing delegates (`UnifiedInlineTextDelegate.qml:75`,
  `CodeBlockDelegate.qml:30`). Setting `composing=false` after it was true
  triggers `flushPendingComposition` (`src/LiveEditBinding.cpp:203`), which
  replaces the whole block buffer with the post-commit text.

Properties we read at intercept time:

- **`cursorPosition`**, **`selectionStart`/`selectionEnd`**, **`length`**,
  and **`text`** are read by both delegates' `Keys.onPressed` handlers to
  build the `Ctx` passed into `LiveStructuralKeyHandler::tryHandle` and
  `LiveNavigationController::tryHandle`. They are also read by
  `InlineHighlighterAttached` for caret/selection-aware delimiter rendering
  (`UnifiedInlineTextDelegate.qml:222`).
- **`cursorRectangle`** is read by `LiveNavigationController` via
  `editItem->property("cursorRectangle")` (`LiveNavigationController.cpp:183`)
  to drive vertical arrow navigation with `positionAt(x, y)`.
- **`textDocument`** (a `QQuickTextDocument`) is the handle
  `LiveEditBinding::setTextDocument` uses to subscribe to the inner
  `QTextDocument::contentsChange` signal. The same `QQuickTextDocument` is
  the target of `InlineHighlighterAttached`, which paints `QTextCharFormat`
  ranges by walking the inner `QTextDocument` via a `QSyntaxHighlighter`.

Properties we write:

- **`cursorPosition`** is written from the delegate's `applySelection()`
  (both text-bearing delegates) and `takeFocus()` (all three). Every write
  fires `onCursorPositionChanged`, which is the reason
  `isApplyingSelection()` and `isApplyingTextUpdate()` re-entrance guards
  exist.
- **`forceActiveFocus()`** is the focus-chokepoint exit: every
  `LiveCursorState::tryResolvePending` ends with
  `QMetaObject::invokeMethod(it->root, "takeFocus", ...)` which in turn
  calls `edit.forceActiveFocus()`.
- **`text`** is *not* written by our code directly on the TextEdit; we write
  to the inner `QTextDocument` via `m_listenedDoc->setPlainText(m_text)` in
  `LiveEditBinding::pushTextToDocument` (`src/LiveEditBinding.cpp:69`).
  This is gated by `m_applyingTextUpdate`.

Properties we configure to neutralise behaviours we do not want:

- **`selectByMouse: false`** in both text-bearing delegates. This is the
  *load-bearing* configuration that gives the LiveView's own MouseArea
  authority over selection. If this were ever flipped to true, TextEdit
  would build its own local selection on every mouse drag and the entire
  cross-block-selection architecture (`LiveCursorState::begin/extend` from
  `LiveView.qml:onPositionChanged`) would be silently bypassed.
- **`persistentSelection: true`** so that a visible selection drawn by
  `applySelection()` survives the TextEdit losing focus (which it does
  every time the cursor crosses a block boundary; another delegate becomes
  the focused one).
- **`textFormat: TextEdit.PlainText`** uniformly. Rich-text paste would
  otherwise reach the TextEdit and bypass `LiveClipboardController`.

## 3. Native TextEdit behaviors that conflict with our model

This is the inventory of every native behaviour TextEdit ships, paired with
the document layer's expectation and the current state of conflict.

### 3.1 Clipboard chord set (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+A)

TextEdit's built-in handler operates on its *own within-block* selection.
Our `LiveClipboardController` operates on the cross-block selection state
held in `LiveCursorState`.

(a) Conflict: yes. A drag that spans three blocks is held in
    `LiveCursorState::m_selectionAnchor` plus the active end in
    `currentTextCaret()`; the focused TextEdit knows only its own block's
    `selectionStart..selectionEnd` and would copy only that fragment.
(b) Intercept: present in both text-bearing delegates' `Keys.onPressed`
    blocks (UnifiedInlineTextDelegate.qml:241–283,
    CodeBlockDelegate.qml:88–124). Routes to
    `liveBinding.clipboardController.{copy,cut,paste}` and
    `cursorState.selectAll()`.
(c) Classification: closed. The 2026-05-21 fix (commit `8524d13`) is the
    canonical pattern this audit recommends generalising in §6.

MathDelegate's `latexEdit` is *not* covered by the intercept. The math
LaTeX source surface has no `Keys.onPressed` block for these chords. A
user editing math source and pressing Ctrl+C inside `BlockInternalEdit`
gets TextEdit's native within-block copy. This is probably correct (the
LaTeX source is a single-block surface and the cross-block model has no
opinion on what selecting a fragment of math source means), but it is
worth naming as a deliberate gap rather than an oversight.

### 3.2 Undo / Redo (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z)

TextEdit has its own per-document `QTextDocument` undo stack. Our undo log
is `MarkoffDocument::d2UndoLog`, populated by every `d2ApplyBufferEdit`
through `UndoLog::Transaction`.

(a) Conflict: severe. Native Ctrl+Z rolls back the *QTextDocument* by one
    edit, which then fires `contentsChange` and feeds the rollback through
    `LiveEditBinding::onContentsChange` as if it were a fresh user edit.
    The CRDT now has both the original edit and its "inverse" recorded as
    separate operations, and the d2 undo log has divergent state from the
    QTextDocument. Subsequent edits compound the divergence.
(b) Intercept: present in both text-bearing delegates
    (UnifiedInlineTextDelegate.qml:270–290, CodeBlockDelegate.qml:115–132).
    Routes to `liveBinding.actionController.{undoAction,redoAction}.trigger()`.
(c) Classification: closed for the two text-bearing delegates. MathDelegate's
    `latexEdit` does *not* intercept Ctrl+Z/Y, which means undo inside a
    math source edit goes to QTextDocument's local stack and not d2UndoLog.
    Classify as **bug-but-not-yet-noticed** for MathDelegate; the symptom
    will be "undo while editing math source rolls back the LaTeX text
    locally but the next save shows the change anyway, because the CRDT
    holds the typed-then-undone bytes."

### 3.3 Cursor navigation (Arrow keys, Home, End, Ctrl+Home, Ctrl+End, Ctrl+arrow, PageUp/PageDown)

The navigation surface is heavily entwined and worth walking carefully.

(a) Plain arrows, Home, End within a block: TextEdit's native handler moves
    `cursorPosition`. `onCursorPositionChanged` then fires and calls
    `LiveCursorState::syncFromTextEdit`. Conflict: no — this is the
    intended single-source-of-truth flow for within-block motion.
    `LiveNavigationController::tryHandle` *does* claim plain Left/Right
    (`LiveNavigationController.cpp:188,198`) and intentionally routes
    within-block motion through `applyMotion`, which calls
    `m_cursorState->begin(targetRow, targetPos)`; the delegate's
    `applySelection` then writes `cursorPosition` to match. So we are
    actually fighting our own intercept slightly: plain Left at the
    middle of a block could equally well have been left to TextEdit
    native. The decision to claim them everywhere (`LiveNavigationController.cpp:138`,
    the "Option B" comment) is deliberate and means `m_cursorState` is
    canonical for *every* arrow press. Intercept: present, by claim in
    Keys.onPressed `isNav` block.
(b) Cross-block plain arrow (Left at col 0, Right at col-end, Up/Down at
    top/bottom visual line of block): handled in
    `LiveNavigationController::tryHandle` via `previousNavigableRow` /
    `nextNavigableRow` plus a `requestTextCaretAtRow` through the cursor
    chokepoint. Conflict: yes, would otherwise be eaten by TextEdit as a
    no-op (cursor at start) or do nothing useful. Intercept: present.
(c) Shift+arrow within / across blocks: intercept claims it; routes through
    `applyMotion`'s `shift` branch, which calls `cs->begin` (lazy anchor)
    then `cs->extend`. Conflict: severe without intercept — TextEdit's
    native Shift+arrow builds a local selection that diverges from
    `m_selectionAnchor`. Intercept: present.
(d) Ctrl+Left / Ctrl+Right (word-boundary jump) *within* a block:
    `LiveNavigationController::tryHandle` explicitly returns `NotHandled`
    when `qtPos > 0` (Left) or `qtPos < blockText.length()` (Right) at
    lines 106 and 119. The comment "word-boundary within block handled by
    TextEdit natively" makes this a deliberate concession. TextEdit's
    native word-jump moves the cursor; `onCursorPositionChanged` fires;
    `syncFromTextEdit` updates `m_cursor`. **This works because
    `m_cursor` is faithful to whatever TextEdit decides about word
    boundaries** — there is no document-layer opinion about words.
    Conflict: no, by design.
(e) Ctrl+Shift+Left / Ctrl+Shift+Right (word-extend selection): same
    pattern at `LiveNavigationController.cpp:61,72`. Returns `NotHandled`
    inside a block, intercepts at the boundary. **Here the concession is
    less clearly correct:** TextEdit's native word-extend builds its own
    local selection that *competes* with `m_selectionAnchor`. If
    `m_selectionAnchor` is empty (no active selection), TextEdit will
    extend its own selection and `onCursorPositionChanged` will fire,
    causing `syncFromTextEdit` to move `m_cursor` to the new word
    boundary — but `m_selectionAnchor` is never set, so the cross-block
    state is "caret moved, no selection," while the visible TextEdit
    selection (drawn by TextEdit itself) is the highlighted word. This
    is a divergence. Classify: **bug-but-not-yet-noticed** — symptom on
    surfacing will be "Ctrl+Shift+Right highlights a word visibly, but
    Ctrl+C copies just the caret position because the cross-block
    selection state has no anchor." Worth a falsifiable test.
(f) Ctrl+Home, Ctrl+End: claimed unconditionally by
    `LiveNavigationController::tryHandle` (lines 84, 92) and routed via
    the cursor chokepoint to the first/last text-bearing row. Conflict:
    yes without intercept. Intercept: present.
(g) Page Up / Page Down: claimed via the `isNav` set and routed through
    `applyMotion`. Conflict: TextEdit's native PageUp/PageDown would do
    nothing useful (no scroll view within the TextEdit). Intercept:
    present.

### 3.4 Deletion (Backspace, Delete, Ctrl+Backspace, Ctrl+Delete)

(a) Backspace / Delete at any in-block position: TextEdit handles
    natively; `contentsChange` fires; `LiveEditBinding` translates to
    `d2ApplyBufferEdit`. No conflict. Intercept: the delegate *does*
    claim Backspace/Delete in `isStructural` and routes through
    `LiveStructuralKeyHandler::tryHandle`, which for in-block deletes
    returns `false` (`LiveStructuralKeyHandler.cpp:170` is the only
    cross-block handler — start/end of block). When `tryHandle` returns
    false, the delegate's `Keys.onPressed` block falls through
    *without* `event.accepted = true`, and the TextEdit then handles the
    delete natively. Conflict: no.
(b) Backspace at qtPos=0 of block > 0, Delete at end of block <
    last-row: claimed by `LiveStructuralKeyHandler` for block-merge.
    Intercept: present.
(c) Ctrl+Backspace / Ctrl+Delete (delete-word-back/forward): not
    intercepted explicitly. The `isStructural` test only checks the
    raw key. The chord falls through `Keys.onPressed`'s
    `if (isStructural) { ... return; }` path because `event.accepted`
    is *not* set in that branch when `tryHandle` returns false. So
    TextEdit's native Ctrl+Backspace runs and word-deletes inside the
    block. `contentsChange` fires; `LiveEditBinding` translates the
    multi-character delete to a single `d2ApplyBufferEdit` with
    `removedBytes` covering the word. Conflict: **probably no**, because
    `contentsChange` carries the exact (qtPos, removed, added) tuple
    and word-boundary intelligence is TextEdit's, same as Ctrl+Left.
    Classify: **by-design-because-our-model-agrees** — but unverified
    by test; should have a falsifiable test that asserts a
    Ctrl+Backspace at column 7 in "hello world" produces a single
    contentsChange with removed=6 (`"world "` if the boundary is the
    space, depending on word definition). At the boundary (column 0
    going back, end-of-block going forward), `tryHandle` will fire
    its merge logic on the *first* of the cross-block presses but
    cannot tell that the user wanted word-distance — same as plain
    Backspace at column 0.
(d) Backspace / Delete while a cross-block selection exists: this is
    the dogfood-found "Backspace/Delete on selection deletes wrong
    region" of the 2026-05-21 cross-block-selection fix. The
    `m_applyingSelectionEmit` guard fixed the *upward* asymmetry, but
    the actual selection-deletion path is
    `LiveCursorState::deleteSelectionRange`, called from
    `LiveCursorState::deleteSelection`. **No delegate Keys.onPressed
    currently calls this.** The delegate sees Backspace, routes to
    `LiveStructuralKeyHandler`, which has no cross-block-selection
    branch, returns false, and TextEdit deletes one character from the
    focused block — the user expected the whole multi-block selection
    to vanish. Classify: **bug-already-in-prod**; will surface the next
    time someone selects across blocks and presses Backspace.
    `LiveStructuralKeyHandler::tryHandle` needs an early-out: "if
    `cursorState.hasSelection()`, call `cursorState.deleteSelection()`
    and return true."

### 3.5 Direct text entry (printable characters, including via composition)

(a) Plain printable char with selection: same as 3.4(d). TextEdit
    replaces its within-block `selectionStart..selectionEnd` with the
    typed character. Cross-block selection state is not consulted.
    Classify: **bug-already-in-prod** — type a letter while a
    cross-block selection is active and you get a one-character
    paragraph plus the other blocks intact. Fix is symmetric to 3.4(d):
    on the keypress, the delegate (or upstream of TextEdit) should
    delete the cross-block selection first, then let TextEdit handle
    the insertion of the new char.
(b) Plain printable char with no selection: routed normally through
    `contentsChange` → `d2ApplyBufferEdit`. Conflict: no.
(c) IME composition: handled by the `composing` binding and
    `flushPendingComposition` path. The intercept skips the
    `contentsChange`-driven edit while composing and applies a
    single-shot whole-block replace on commit. This is a reasonable
    approximation but loses the within-composition (qtPos, removed,
    added) granularity. Classify: **probably-by-design-because-our-model-agrees**
    on the commit path, but **unverified**: dogfood with a CJK IME or
    a Compose-key dead key has not been exercised under D2. The
    `composing` and `flushPendingComposition` path is structurally
    correct but no falsifiable test pins it.

### 3.6 Mouse — handled at the LiveView level

`LiveView.qml` overlays the ListView with a `MouseArea {
preventStealing: true; acceptedButtons: Qt.LeftButton | Qt.RightButton }`
that captures every press, drag, click, double-click. The MouseArea's
`hit(mouseX, mouseY)` then computes a (blockIndex, qtPos) via each
delegate's `positionAt()` function. Combined with `selectByMouse: false`
on the TextEdits, this means the TextEdits never see a `MouseEvent`
themselves — Qt's MouseArea consumes them first.

That covers single-click caret placement, drag selection, double-click
(handled in `LiveView.qml:onDoubleClicked` with our own word-boundary
regex `/[\p{L}\p{N}_]/u`), triple-click (handled via
`_clickCount` counter), and right-click (routed to
`LiveContextMenuHandler`).

(a) Conflict: no. The overlay MouseArea + `selectByMouse: false`
    architecture removes the conflict at the source.
(b) Open question: middle-click X11 PRIMARY-selection paste. On X11
    (and XWayland), a middle-click traditionally pastes the PRIMARY
    selection. `MouseArea` with `acceptedButtons: Qt.LeftButton |
    Qt.RightButton` does not handle Qt.MiddleButton, so the event
    propagates — past the MouseArea overlay, past the disabled
    `selectByMouse`, to the TextEdit, which *does* honour middle-click
    PRIMARY-paste natively when focused. The pasted text bypasses
    `LiveClipboardController`. Classify: **bug-but-not-yet-noticed**;
    symptom on surfacing will be "middle-click somewhere on a focused
    block pastes the X11 PRIMARY selection without going through the
    clipboard controller, so the paste is single-block and not
    structured."
(c) Open question: drag-and-drop of text into a TextEdit. TextEdit
    inherits Qt's drag-drop handlers and will accept a text drop into
    its own block. There is no document-layer drop handler. Classify:
    **bug-but-not-yet-noticed**; same shape as middle-click paste.

### 3.7 Hover / tooltip / context-menu chrome

TextEdit ships a `linkActivated` signal and hover for embedded HTML
anchors. We render plain text + InlineHighlighter formats, so the
`linkActivated` signal never fires here; link activation is handled by
`LiveView.qml`'s own Ctrl+click and Ctrl+hover paths
(`LiveView.qml:242,325`). Conflict: no.

Right-click context menu: TextEdit also has no built-in QML context menu
in Qt 6 (the QtWidgets `QTextEdit` had one; the QML `TextEdit` does not),
so the right-click is owned by `LiveView`'s MouseArea unconditionally.
Conflict: no.

## 4. Current interception architecture

There is no single architecture — there are five overlapping mechanisms,
arranged loosely from outermost to innermost:

1. **Window-level QML `Shortcut` elements** in `LiveView.qml:142–161` for
   the four zoom + dark-toggle chords (Ctrl+=, Ctrl+-, Ctrl+0,
   Ctrl+Shift+D). These reach the actions regardless of which TextEdit
   has focus, because `Shortcut` is window-scoped, and they `trigger()`
   the corresponding QAction.

2. **The LiveView-level MouseArea overlay** (`LiveView.qml:203–377`)
   with `preventStealing: true`, which captures every mouse event before
   the TextEdits see it. This is the load-bearing mechanism for mouse.

3. **Per-delegate `Keys.onPressed` handlers** with `Keys.priority:
   Keys.BeforeItem`. Both text-bearing delegates have this set. In Qt
   QML, `Keys.priority: Keys.BeforeItem` causes the `Keys` attached
   property's handlers to run *before* the target item's own key
   handling. So when a TextEdit has focus and the user presses Ctrl+C,
   the delegate's `Keys.onPressed` handler runs first; if it sets
   `event.accepted = true`, TextEdit's native handler does not see the
   event. The clipboard, undo/redo, and selectAll intercepts work this
   way. The structural/navigation routing also works this way for the
   keys it claims.

4. **The QActions on `LiveActionController` have `setShortcut()` set**
   (`LiveActionController.cpp:50–69`) for Cut/Copy/Paste/SelectAll/
   Undo/Redo/Bold/Italic/Strike/InlineCode/Link/Heading0–6/Save/Zoom/
   Delete. **These shortcuts are not bound to anything in production
   QML except for the four Zoom/dark-toggle Shortcut elements.** The
   QAction-side shortcut is dead in the standalone test app: nothing
   calls `addAction()` on the QQuickWindow, and there are no per-action
   `Shortcut` elements in `LiveView.qml`. Bold/Italic/Strike/InlineCode/
   Link/Heading0–6 are reached only via the context menu (which builds
   from the same QActions, `LiveContextMenuHandler.cpp:48`) or, in the
   case of Undo/Redo, via the manual `actionController.undoAction.trigger()`
   calls inside the delegate's Ctrl+Z handler. The
   `LiveView.qml:140` comment ("Other QActions (cut/copy/paste/save/
   undo/redo/bold/italic/link) still need analogous Shortcut wiring —
   currently no-ops in the standalone test app; tracked separately")
   is accurate — and the dogfood findings are partly explained by it:
   when these QActions don't fire, the host app's keyboard surface is
   *whatever TextEdit decides to do*.

5. **C++ re-entrance guards** (`LiveEditBinding::m_applyingTextUpdate`,
   `LiveCursorState::m_applyingSelectionEmit`) for the two narrow
   programmatic-write-back cases. These are not interception per se —
   they exist to handle the case where *our* code writes back into
   TextEdit (via `setPlainText` or `cursorPosition = ...`) and we
   don't want our own write to be re-ingested as a user event.

The duplication between UnifiedInlineTextDelegate and CodeBlockDelegate
is structural. The `Keys.onPressed` block in each is roughly:

- 41 lines (UnifiedInlineTextDelegate.qml:236–324)
- 75 lines (CodeBlockDelegate.qml:83–157)

The first 50 lines (the Ctrl+modifier block — clipboard chords + undo/redo
chords) are nearly identical (modulo the absence of `isLevelChange` in
the CodeBlockDelegate variant, which is correct — code-blocks are not
heading-promotable). The `isStructural` and `isNav` sets differ as they
should: UnifiedInline includes Return/Enter/Tab (list-item)/Escape
(paragraph); CodeBlock includes only Backspace/Delete/Tab. Both
duplicate the `isNav` set verbatim.

Drift risk: a Ctrl-modifier chord landed in one delegate but not the
other is a present risk. Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z exist in both
because both were added in the same commit (`586d7c7`). If a sixth
chord lands in only one, a focused code-block TextEdit would behave
differently than a focused paragraph. The shared intercept block wants
to be one file.

`Keys.priority: Keys.BeforeItem` is set on each TextEdit. There is no
attached `Keys` on the delegate root above the TextEdit in the
UnifiedInline / CodeBlock cases (so the `Keys.BeforeItem` value here is
relative to the TextEdit itself — the `Keys.onPressed` handler is on
the TextEdit element, with priority "before the TextEdit's own
handling"). In MathDelegate the situation is different: the delegate
root has a top-level `Keys.onPressed` (lines 139–154) for F2 / Delete /
Backspace when `isSelected`, and the latexEdit TextEdit has its own
`Keys.onPressed` (lines 96–102) for Escape only. The MathDelegate
inherits no clipboard / undo / nav handling at all when latexEdit has
focus.

## 5. Latent conflicts — the punch list of bugs that will surface next

Numbered in roughly decreasing order of likelihood-to-be-noticed:

L1. **Typing or Backspace/Delete while a cross-block selection is
    active** (§3.4d, §3.5a). The cross-block selection state is held
    in `LiveCursorState::m_selectionAnchor` and the active end's
    TextCaret. A keypress on a focused TextEdit replaces the *focused
    block's* within-block selection (if any) with the typed character,
    leaving the other blocks intact. Symptom: drag-select three
    paragraphs, type "x" — the focused paragraph becomes "x" and the
    other two remain. The user expected all three to collapse to "x".

L2. **Middle-click PRIMARY-selection paste on X11/XWayland** (§3.6b).
    Symptom: middle-click while a TextEdit is focused pastes the X11
    PRIMARY selection (often whatever was last selected anywhere on the
    desktop) into the focused block, bypassing
    `LiveClipboardController`. Single-block, no structured-paste
    routing.

L3. **Drag-and-drop of external text into a TextEdit** (§3.6c).
    Symptom: drop a text fragment from another application onto a
    block; TextEdit accepts it as a single-block plain-text insert
    without going through clipboard controller / structured paste.

L4. **Ctrl+Shift+Left/Right inside a block** (§3.3e). Symptom: TextEdit
    draws a within-block word selection but `m_selectionAnchor`
    remains empty; Ctrl+C copies only the caret position; the visible
    highlight is misleading.

L5. **Undo / Redo inside math source edit** (§3.2). Symptom: Ctrl+Z
    while editing math LaTeX source rolls back the QTextDocument
    locally; the next save reveals the bytes are still in the CRDT
    because d2UndoLog was never consulted. Divergence accumulates
    across multiple edit cycles.

L6. **Clipboard chords inside math source edit** (§3.1). Symptom: less
    bad than L5 — TextEdit handles Ctrl+C/X/V locally on the LaTeX
    source. Probably acceptable, but it should be a deliberate decision.

L7. **IME composition under D2** (§3.5c). Symptom: untested entirely.
    Dead keys (Compose) and CJK IMEs may or may not produce correct
    d2 buffer state. The wholesale `flushPendingComposition` swap is
    structurally lossy in coordinate granularity but correct in final
    content; that may or may not interact correctly with concurrent
    remote edits in D5.

L8. **Ctrl+B / Ctrl+I / Ctrl+E / Ctrl+K / Ctrl+Shift+X / Ctrl+0..6**
    in non-heading blocks: the QActions have these shortcuts set
    (`LiveActionController.cpp:56–66`) but no QML `Shortcut` binds
    them. The chord falls through `Keys.onPressed` (which only matches
    Ctrl+Shift+0..6 *inside* a heading block, `UnifiedInlineTextDelegate.qml:293`)
    and reaches TextEdit. TextEdit ignores Ctrl+B as an unknown chord
    and the keypress is dropped. Symptom: the user presses Ctrl+B
    while in a paragraph and nothing happens. Bold is reachable only
    via the right-click context menu. (Note: Bold/Italic toggles
    against the *current selection* via `LiveFormatController`, so
    the path is structurally correct once the shortcut fires; the
    missing piece is the shortcut firing.) Strictly this is not a
    TextEdit-eats-it bug — it's the inverse: TextEdit doesn't eat it,
    and nothing else routes it. But it belongs in the audit because
    the *fix* (window-level Shortcut element binding to the QAction)
    is a cousin of the §4-item-4 gap, and the right architecture
    for §6 handles it the same way.

L9. **Ctrl+Backspace / Ctrl+Delete cross-block** (§3.4c boundary
    case). Symptom: at qtPos=0 with Ctrl held, `LiveStructuralKeyHandler`
    runs the same merge logic as plain Backspace and the Ctrl modifier
    is ignored. The user might have expected "delete previous word
    across the boundary" but gets "merge previous block." Low
    severity but worth a deliberate decision.

L10. **Mouse word-boundary on double-click** is *not* a TextEdit
    behaviour anymore — `LiveView.qml:onDoubleClicked` re-implements
    it with its own regex. So this conflict is closed by overriding.
    Listed here to document the closure rather than as a bug.

The bugs that are "bug-already-in-prod" and most likely to surface in
the next dogfood pass are L1 (because it follows naturally from the
2026-05-21 cross-block-selection fixes — once the selection works,
typing or deleting on it is the next thing the user does) and L8
(because the user just dogfooded toolbar restoration and will reach
for Ctrl+B).

## 6. Recommended architecture

The framing of the question — keep the per-delegate intercept, factor
it, or replace it with something heavier — invites a brief survey
before recommending.

(α) **Keep `Keys.onPressed` early-return per delegate, but factor the
    common block into a shared QML component.** The Ctrl+C/X/V/A/Z/Y/
    Shift+Z block is 100% shared between the two text-bearing delegates.
    The `isNav` set is shared. The structural keys differ but are short.
    A `FormatKeyDispatcher.qml` (or simply a JavaScript helper module
    `KeyDispatch.mjs`) exposing a `tryDispatch(event, liveBinding,
    modelIndex, edit)` function the delegates call from a single line
    in `Keys.onPressed` reduces drift. Math's `latexEdit` can call the
    same dispatcher to inherit clipboard/undo/etc when in
    `BlockInternalEdit` mode (closes L5, L6). This is mechanically
    small, easy to verify with the existing falsifiable tests, and
    extends naturally to add new chords (L1 selection-aware-typing,
    L8 format shortcuts).

(β) **A single C++ event filter installed on the QQuickWindow that
    intercepts before QML delivery.** Architecturally this is what
    the QAction-with-setShortcut approach implicitly wants, since
    QActions installed on a top-level window get reached via
    `QShortcutEvent` ahead of focus-widget delivery. The problem in
    practice: QQuickWindow does not have `addAction()`, and the
    `QShortcut`-via-`Shortcut` QML element binding gives us most of
    this behaviour at the QML level without C++ event-filter plumbing.
    A C++ filter installed via `QQuickWindow::installEventFilter`
    intercepts QKeyEvents before QML delivery, but that *also* short-
    circuits any QML focus state we might want to respect (e.g. "is
    `langInput` the focused TextInput? then Ctrl+C should target the
    language tag, not the block content"). The cost of plumbing
    around that is higher than the cost of approach (α).

(γ) **Configure TextEdit's `Keys.forwardTo` or disable specific
    built-in shortcuts at TextEdit instantiation.** Qt's QML
    `TextEdit` does not expose granular per-shortcut disabling — its
    Ctrl+C/X/V/A/Z/Y handling lives inside `QQuickTextEdit::keyPressEvent`
    in the C++ implementation and is not toggleable from QML. The
    closest hook is `Keys.forwardTo`, which is a list of items that
    receive the key event *before* the TextEdit. That's effectively
    `Keys.priority: Keys.BeforeItem` on an attached `Keys` of a sibling
    item, and it amounts to the same mechanism as (α) with a slightly
    different topology. Not meaningfully different.

(δ) **Switch to a TextInput-only model (no built-in undo).** Qt's
    `TextInput` is single-line and intentionally minimal — no
    multi-line, no built-in undo, no built-in clipboard chord handling.
    The two text-bearing delegates would become much harder to write,
    multi-line list-items and code-blocks would need custom layout,
    and the wrap-mode behaviour would need re-implementation. Probably
    not worth it. Worth naming as the nuclear option only.

**Recommendation: approach (α), factor the existing intercept into a
single shared QML JS helper, and *simultaneously* add the missing
window-level `Shortcut` bindings for the QActions that have
`setShortcut()` configured but no QML binding (L8).** Reasoning:

1. The existing intercept mechanism is correct and well-understood. The
   2026-05-21 fixes establish the pattern; the discipline log already
   names them. The job is to stop hand-copying the same block into
   each new delegate.
2. The factorisation is small enough to do in one session, with
   falsifiable tests existing for the chord set already
   (`ctrl_c_after_three_block_drag_copies_all_three_blocks` covers
   clipboard chords, the Ctrl+Z fix in `586d7c7` is testable via the
   undo log).
3. The window-level Shortcut for Bold/Italic/etc closes L8 with the
   same mechanism the four zoom shortcuts already use. The QML
   Shortcut element already exists in `LiveView.qml:142–161` for the
   zoom set; adding nine more is repetitive but mechanical. (Or one
   `Instantiator { model: [actions...] }` over the action list, which
   is cleaner.)
4. Math's `latexEdit` inheriting the factored dispatcher closes L5
   and resolves L6 with an explicit decision (route through
   clipboard controller, or document the deliberate choice not to).
5. L1 (typing/deleting on cross-block selection) is *not* fixed by
   the factorisation itself — it's a behavioural change in the
   structural-key path. But it lives naturally in the same
   dispatcher (or in `LiveStructuralKeyHandler::tryHandle` as an
   early-out): "if `cursorState.hasSelection()` and the keypress
   would mutate text, call `cursorState.deleteSelection()` first,
   then optionally accept/forward." The dispatcher is the place
   where "mutate text" is meaningful.

The eventual unification with the existing
`m_applyingTextUpdate` / `isApplyingSelection` redesign (D7 of the
markoff-live freeze spec, both Discipline-Log-named) is downstream of
this. The factorisation step does not require touching the re-entrance
guards; it just consolidates the surface they protect. When the
freeze-spec redesign lands, it will replace the guards but the
dispatcher remains.

A note on what *not* to do: approach (β)'s event filter is tempting
because it would also let us route Ctrl+B / Ctrl+K through `QShortcut`
machinery without per-key QML wiring. The reason to resist it for now
is that the codebase already has the QML-Shortcut precedent (zoom + dark
toggle); adding nine more is small. Switching to a C++ event filter at
the same time as the factorisation expands the blast radius for no
benefit. Filer is a candidate for the eventual freeze-spec redesign if
the dispatcher proves too noisy in QML, but it should not block this
audit's recommendations.

## 7. Action items

Ordered by blast radius (smallest first) and dependency (later items
build on earlier ones). Not a plan — a checklist for the next session
to scope from.

- [ ] **Falsifiable tests for the latent conflicts**, before any fix:
      L1 (type-on-cross-block-selection), L2 (middle-click paste; may
      not be testable under offscreen platform), L4 (Ctrl+Shift+Right
      visible-selection-vs-anchor divergence), L5 (Ctrl+Z in
      MathDelegate latexEdit). Prove falsifiable by stubbing the
      handler off and confirming the test fails (invariant 4).

- [ ] **Add L1 fix into the structural-key path**:
      `LiveStructuralKeyHandler::tryHandle` early-out when
      `cursorState.hasSelection()` and the key is a mutating one
      (Backspace, Delete, Return/Enter, or any text-entry — for the
      printable-char case the entry point is different and the
      delegate's `Keys.onPressed` is the natural place). This is
      independent of the factorisation and can land first.

- [ ] **Factor the per-delegate Keys.onPressed block** into a shared
      QML JS helper (e.g. `qml/delegates/KeyDispatch.mjs` exporting
      `dispatch(event, ctx)` with `ctx = { binding, modelIndex,
      edit, kind }`). Both UnifiedInlineTextDelegate and
      CodeBlockDelegate call into it for the Ctrl-modifier chord
      block; nav and structural call sites stay where they are (they
      already delegate to controllers). MathDelegate's `latexEdit`
      Keys.onPressed adopts the same dispatcher, closing L5/L6 with
      an explicit decision.

- [ ] **Add window-level QML Shortcut bindings** in `LiveView.qml`
      for Bold/Italic/Strike/InlineCode/Link/Heading0..6/Save and
      any other QAction with a `setShortcut()` that currently has no
      QML binding. Closes L8. Use an `Instantiator` or `Repeater`
      over the QAction set if Qt 6.8 allows enumerating them; one
      static `Shortcut` per action otherwise.

- [ ] **L2 / L3 (middle-click paste, drag-drop)**: the simplest
      defence is to add `Qt.MiddleButton` to the LiveView MouseArea's
      `acceptedButtons` (consuming the press) and to set
      `Qt.ImhMultiLine | Qt.ImhNoPredictiveText` plus a
      `DropArea.enabled: false` on the TextEdit, or to add a real
      `DropArea` at LiveView level that routes through
      `LiveClipboardController.paste`. Likely needs its own
      design pass; defer behind §7.3 unless dogfood surfaces L2/L3.

- [ ] **MathDelegate clipboard/undo coverage**: once the dispatcher
      is shared, latexEdit's Keys.onPressed delegates to it; the
      explicit decision is whether `BlockInternalEdit` Ctrl+C should
      copy the LaTeX source via TextEdit-local clipboard (current
      behaviour, deliberate) or via `LiveClipboardController.copy`
      (cross-block contract, but the cross-block selection model
      doesn't currently understand "within a math source edit"). The
      decision needs a one-paragraph spec note.

- [ ] **Discipline-log close**: add an entry pointing at this audit
      document so the next agent who lands a TextEdit-related chord
      reads this first. The existing entries at 2026-05-21 for
      clipboard and undo should be cross-linked.

- [ ] **Eventually, fold the re-entrance guards into the dispatcher
      redesign**: the dispatcher is the natural place to centralise
      "are we mid-write-back" state. Both `m_applyingTextUpdate` and
      `m_applyingSelectionEmit` exist because programmatic writes to
      TextEdit re-enter through the same signals user input does;
      a dispatcher that knows when it is the source of a write can
      tag the events. Not in scope for the factorisation pass, but
      worth aiming the redesign at this convergence.
