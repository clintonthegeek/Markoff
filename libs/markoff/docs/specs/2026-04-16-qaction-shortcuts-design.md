# QAction Shortcut Registry & Tab Smart-Indent — Design Spec

**Date:** 2026-04-16
**Status:** IMPLEMENTED — see `Editor::createActions()` and the `ActionId` enum in `include/markoff/Editor.h`.
**Scope:** markoff library (`Editor` widget) + host integration guidance

## Goal

Replace all hardcoded keyboard shortcut handling in `Editor::keyPressEvent` with proper `QAction`-based shortcuts. Add smart Tab/Shift+Tab behavior (indent/outdent for lists, insert tab mid-line). Make the editor a well-behaved Qt widget whose shortcuts are discoverable and remappable by host applications.

## Non-goals

- Changing TextControl's `QKeySequence`-based text editing primitives (cursor movement, word deletion, etc.) — these already use the correct Qt pattern.
- Changing contextual key handling in TextControl (Tab/Escape/Return in tables, Backspace near tables) — these are modal text-engine behaviors, not user-remappable shortcuts.
- Adding KF6 dependencies to markoff — the library stays pure Qt6.

## Problem

`Editor::keyPressEvent` contains ~15 hardcoded `if (key == Qt::Key_X)` checks that intercept editor commands before they reach the scene. These shortcuts are invisible to Qt's action/shortcut infrastructure, can't be remapped by the host app's shortcut editor (KDE's `KActionCollection`), and duplicate work that QAction was designed to do.

Additionally, Tab outside of table cells falls through to Qt's focus chain (because `Editor` inherits `QGraphicsView`'s default `StrongFocus` policy which includes `TabFocus`), jumping to the next widget instead of being consumed by the editor.

## Design

### 1. ActionId enum and action registry

A new public enum `ActionId` in the `Markoff` namespace lists every editor command that should be a QAction:

```cpp
enum class ActionId {
    Undo, Redo,
    Cut, Copy, Paste, SelectAll,
    Find, FindNext, FindPrevious, Replace,
    ZoomIn, ZoomOut,
    ToggleBold, ToggleItalic, ToggleStrikethrough, ToggleInlineCode,
    InsertLink, InsertWikiLink, InsertImage,
    InsertCodeBlock, InsertBlockQuote, InsertHorizontalRule,
    IncreaseHeading, DecreaseHeading, ToggleCheckbox,
    ToggleFoldAtCursor, FoldAll, UnfoldAll,
};
```

Editor stores a `QHash<ActionId, QAction*> m_actions` populated in the constructor.

### 2. Public API

```cpp
/// Returns the QAction for the given command, or nullptr.
QAction *action(ActionId id) const;

/// Returns all registered actions for bulk integration with host
/// action collections.
QList<QAction*> actions() const;
```

### 3. Action creation

Each action is created with:
- `objectName` matching a stable string ID (e.g. `"markoff_undo"`, `"markoff_toggle_bold"`) for serialization/lookup by host apps.
- Default `QKeySequence` shortcut where a standard one exists (`QKeySequence::Undo`, `QKeySequence::Copy`, etc.).
- `text()` for menu integration (e.g. `tr("Undo")`, `tr("Toggle Bold")`).
- Connected to the existing public slot on Editor.

The action is parented to the Editor. Its `shortcutContext` is set to `Qt::WidgetWithChildrenShortcut` so the shortcut only fires when the Editor (or a child, i.e. the SearchBar) has focus. This prevents conflicts when the host has multiple editors or other widgets with the same key sequence.

### 4. Default shortcut table

| ActionId | Default Shortcut | QKeySequence Constant |
|----------|------------------|-----------------------|
| Undo | Ctrl+Z | `QKeySequence::Undo` |
| Redo | Ctrl+Y / Ctrl+Shift+Z | `QKeySequence::Redo` |
| Cut | Ctrl+X | `QKeySequence::Cut` |
| Copy | Ctrl+C | `QKeySequence::Copy` |
| Paste | Ctrl+V | `QKeySequence::Paste` |
| SelectAll | Ctrl+A | `QKeySequence::SelectAll` |
| Find | Ctrl+F | `QKeySequence::Find` |
| FindNext | F3 | `QKeySequence::FindNext` |
| FindPrevious | Shift+F3 | `QKeySequence::FindPrevious` |
| Replace | Ctrl+H | `QKeySequence::Replace` |
| ZoomIn | Ctrl+= | `QKeySequence::ZoomIn` |
| ZoomOut | Ctrl+- | `QKeySequence::ZoomOut` |
| ToggleBold | Ctrl+B | `QKeySequence::Bold` |
| ToggleItalic | Ctrl+I | `QKeySequence::Italic` |
| ToggleStrikethrough | Ctrl+Shift+X | custom |
| ToggleInlineCode | Ctrl+` | custom |
| InsertLink | Ctrl+K | custom |
| InsertWikiLink | Ctrl+Shift+K | custom |
| InsertImage | (none) | — |
| InsertCodeBlock | (none) | — |
| InsertBlockQuote | (none) | — |
| InsertHorizontalRule | (none) | — |
| IncreaseHeading | (none) | — |
| DecreaseHeading | (none) | — |
| ToggleCheckbox | (none) | — |
| ToggleFoldAtCursor | (none) | — |
| FoldAll | (none) | — |
| UnfoldAll | (none) | — |

Actions without a default shortcut are still registered as QActions for menu/command-palette integration — the host can assign shortcuts via `KActionCollection::setDefaultShortcut`.

### 5. keyPressEvent cleanup

After action registration, `Editor::keyPressEvent` is stripped of all category 1 handling. The remaining body:

```cpp
void Editor::keyPressEvent(QKeyEvent *e)
{
    // Tab smart-indent (see §6) — must be handled here because
    // QAction can't express "Tab, but only outside tables"
    if (handleTabKey(e))
        return;

    // Forward to scene (which routes to TextControl for text editing)
    QGraphicsView::keyPressEvent(e);

    // Post-processing: ensure cursor visible after movement/editing keys
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_Return: case Qt::Key_Enter:
    case Qt::Key_Backspace: case Qt::Key_Delete:
        ensureFocusedCursorVisible();
        break;
    default:
        if (!e->text().isEmpty())
            ensureFocusedCursorVisible();
        break;
    }

    detectCompletionTriggers(e->text());
}
```

All Ctrl+C/X/V/A/F/H, F3, Ctrl+Home/End, Ctrl+Plus/Minus, and PageUp/Down handling is removed — QAction dispatch fires the slots before `keyPressEvent` is ever called.

### 6. Tab smart-indent

Tab must be handled in `keyPressEvent` (not as a QAction) because its behavior is context-dependent: inside a table, TextControl handles Tab for cell navigation; outside a table, Editor handles it for indentation.

**Focus policy fix:** Editor's constructor sets `setFocusPolicy(Qt::StrongFocus)` — but QGraphicsView already defaults to this. The real issue is that Tab events reach Qt's focus chain before the view's key handler. The fix: override `event()` to intercept `QEvent::KeyPress` for Tab/Shift+Tab before Qt's focus machinery processes them, matching the pattern used by QTextEdit and QPlainTextEdit internally.

```cpp
bool Editor::event(QEvent *e)
{
    if (e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QGraphicsView::event(e);
}
```

**Tab behavior outside tables:**

| Context | Tab | Shift+Tab |
|---------|-----|-----------|
| Cursor on a list item line (at or before first non-space char, or line is solely a list marker) | Indent list item (prepend 2 spaces to line in source) | Dedent list item (remove up to 2 leading spaces) |
| Cursor at or before first non-space char (non-list) | Indent block (prepend 2 spaces) | Dedent block (remove up to 2 leading spaces) |
| Cursor mid-line (after non-space content) | Insert tab character (or spaces per editor settings) | No-op (ignored, falls through) |
| Cursor inside a table | Handled by TextControl (unchanged) | Handled by TextControl (unchanged) |

**Implementation:** A private `bool Editor::handleTabKey(QKeyEvent *e)` method. Returns `true` if consumed, `false` to let TextControl handle it (table case). The method:

1. Gets the focused `MarkdownTextItem` and its `QTextCursor`.
2. Checks `cursor.currentTable()` — if in a table, returns `false` (TextControl handles it).
3. Determines the current line's text and cursor column.
4. If cursor is at or before the first non-whitespace character: indent/dedent the line by manipulating the source text (select from line start, prepend/remove spaces).
5. If cursor is mid-line and Tab (not Shift+Tab): insert a tab character or spaces.
6. Accepts the event and returns `true`.

The indent operation works on the markdown source line (via `MarkdownTextItem::textCursor()`) using `QTextCursor` block selection, not by modifying the parsed tree. This triggers a reparse which updates list nesting visually.

### 7. SelectionManager shortcut deduplication

`SelectionManager::handleKeyPress` currently checks for `Ctrl+A` and `Ctrl+C` with hardcoded key comparisons. After this change:

- **Ctrl+A:** The `SelectAll` QAction fires `Editor::selectAll()`. `Editor::selectAll()` already delegates to `SelectionManager` for cross-boundary selection. Remove the Ctrl+A check from `SelectionManager::handleKeyPress`.
- **Ctrl+C:** The `Copy` QAction fires `Editor::copy()`. `Editor::copy()` already checks for cross-boundary selection and delegates to `SelectionManager::createMimeData()`. Remove the Ctrl+C check from `SelectionManager::handleKeyPress`.
- **Escape:** Keep in `SelectionManager` — this is a modal state exit (clearing cross-boundary selection), not a user-remappable command.

### 8. Ctrl+Home/End and PageUp/Down

These are cursor movement commands that currently live in `Editor::keyPressEvent` because they operate across the multi-item scene (jumping to first/last item, scrolling by viewport height). They become QActions:

- `Ctrl+Home` / `Ctrl+End` → not separate ActionIds; these are standard cursor movement sequences (`QKeySequence::MoveToStartOfDocument` / `MoveToEndOfDocument`). However, the default QKeySequence matching in TextControl won't work because these are *scene-level* operations (jumping across items). They stay as QActions with custom shortcuts `Qt::CTRL | Qt::Key_Home` and `Qt::CTRL | Qt::Key_End`.

Wait — these are better handled as overrides rather than QActions, because they need to also support Shift variants (Ctrl+Shift+Home for select-to-start). A QAction for "jump to start" can't distinguish "jump" from "select to start."

**Resolution:** Ctrl+Home/End and PageUp/Down remain in `keyPressEvent`. They are scene-level navigation that depends on modifier state (shift for selection extension). This is the same category as arrow keys — text editing primitives that happen to be at the Editor level instead of TextControl. They stay as hardcoded key checks.

Updated remaining `keyPressEvent` body:

```cpp
void Editor::keyPressEvent(QKeyEvent *e)
{
    bool shift = e->modifiers() & Qt::ShiftModifier;
    bool ctrl  = e->modifiers() & Qt::ControlModifier;

    // Tab smart-indent (§6)
    if (handleTabKey(e))
        return;

    // Scene-level navigation that depends on modifier state
    if (e->key() == Qt::Key_Home && ctrl) {
        jumpToDocumentEdge(true, shift);
        return;
    }
    if (e->key() == Qt::Key_End && ctrl) {
        jumpToDocumentEdge(false, shift);
        return;
    }
    if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) {
        pageUpDown(e->key() == Qt::Key_PageUp, shift);
        return;
    }

    QGraphicsView::keyPressEvent(e);

    // Post-processing: ensure cursor visible
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_Return: case Qt::Key_Enter:
    case Qt::Key_Backspace: case Qt::Key_Delete:
        ensureFocusedCursorVisible();
        break;
    default:
        if (!e->text().isEmpty())
            ensureFocusedCursorVisible();
        break;
    }

    detectCompletionTriggers(e->text());
}
```

### 9. TextControl shortcut deduplication

TextControl currently has its own `QKeySequence::SelectAll` and `QKeySequence::Copy` matching (lines 896-908). After this change, the QActions on Editor fire first (because `Qt::WidgetWithChildrenShortcut` matches before the event reaches the scene). TextControl's matching becomes dead code for these two sequences.

**Resolution:** Leave TextControl's `QKeySequence` matching intact. It's inherited from Qt's QTextControl and serves as a correct fallback if the Editor-level action is ever removed or if TextControl is used standalone. No harm in the redundancy — the event is already consumed by the QAction before TextControl sees it.

### 10. Host integration pattern

The host app (Corbomite `MainWindow`) integrates markoff's actions into `KActionCollection` like this:

```cpp
// In MainWindow::setupEditor()
auto *ac = actionCollection();
Markoff::Editor *mk = activeEditor()->editor();

// Standard actions — merge markoff's QAction into KDE's action collection.
// KActionCollection takes over shortcut management (including persistence
// and the shortcut editor dialog).
for (auto *a : mk->actions()) {
    ac->addAction(a->objectName(), a);
}
```

This gives KDE's shortcut editor full visibility and control over markoff's shortcuts. The user can remap them in Settings → Configure Shortcuts.

The host can also override defaults:

```cpp
ac->setDefaultShortcut(mk->action(Markoff::ActionId::Find),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
```

### 11. Test app update

The markoff test app (`app/MainWindow.cpp`) currently creates its own QActions for File/Edit/Format menus. After this change, it can pull actions directly from the editor:

```cpp
editMenu->addAction(m_editor->action(Markoff::ActionId::Undo));
editMenu->addAction(m_editor->action(Markoff::ActionId::Redo));
// etc.
```

This eliminates the duplicate action creation in the test app.

### 12. Read-only mode

When `Editor::setReadOnly(true)` is called, editing actions (cut, paste, undo, redo, formatting, insert, tab-indent) should be disabled. Navigation and read actions (copy, find, zoom) stay enabled.

Implementation: `setReadOnly` iterates `m_actions` and calls `setEnabled(!readOnly)` on editing actions, leaving navigation/read actions enabled. A helper classifies each ActionId as editing or non-editing.

## Files changed

| File | Change |
|------|--------|
| `include/markoff/Editor.h` | Add `ActionId` enum, `action()`, `actions()` methods, `event()` override, `handleTabKey()`, `m_actions` member |
| `src/Editor.cpp` | Action creation in constructor, `keyPressEvent` cleanup, `event()` override, `handleTabKey()` implementation, read-only action gating |
| `src/SelectionManager.cpp` | Remove Ctrl+A and Ctrl+C handling |
| `app/MainWindow.cpp` | Use editor actions instead of creating duplicates |
| `tests/` | Tests for action existence, Tab indent/dedent, read-only action state |

## Testing

- **Action registry:** Verify all ActionIds return non-null QActions with expected shortcuts.
- **Shortcut firing:** Simulate `QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier)` and verify undo fires.
- **Tab smart-indent:** Test Tab on list items (indent), Shift+Tab (dedent), Tab mid-line (insert), Tab in table (cell navigation unchanged).
- **Read-only gating:** Verify editing actions are disabled when read-only, navigation actions remain enabled.
- **Host override:** Verify that changing a shortcut via `QAction::setShortcut` works and the original hardcoded path doesn't interfere.
