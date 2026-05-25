# `markoff-source-widget` — fully-owned QtWidgets Source view

**Status:** spec
**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`

## 1. Motivation

Corbomite (the only consumer at present) uses `libs/markoff-source` — a vendored Qutepart-cpp wrapper — for its Source view. Qutepart was a quick adopt during the Phase A absorption; it solves a real problem (mature fold engine, indent, Kate-XML highlighting) but the gutter is Kate's native UI and the source-mode visuals are out of our hands. The project is moving toward fully-owned classes so that views can share an aesthetic identity over time.

This spec defines a new library `libs/markoff-source` — a from-scratch QtWidgets Source editor on the new `markoff-foundation`. v0 ships line-numbered editing with CRDT undo, KSyntaxHighlighting, and a find bar. Fold support and other affordances come in subsequent sub-projects without architectural rework.

## 2. Scope

**In scope (v0):**

- A `Markoff::Source::Editor` class — `QPlainTextEdit` subclass, the public widget.
- A `Markoff::Source::FindBar` widget — find UI; standalone, host places it.
- A private `Gutter` widget child of the editor, painting line numbers.
- Document binding: `SourceTextDocumentBinding` relocated from `libs/markoff-view-qml/src/` to `libs/markoff-core/` so both consumers (the existing QML `SourceEditor.qml` and the new widget) share one implementation.
- `KSyntaxHighlighting` attached to the editor's `QTextDocument` for markdown.
- Cursor + selection round-trip through `Session` via the relocated binding's existing cycle-guard logic.
- `Ctrl+Z` / `Ctrl+Y` invoke `MarkoffDocument::undo()` / `redo()`. The `QTextDocument`'s undo stack is disabled (the binding already does this).
- Theme consumption via a `setTheme(const Markoff::Theme &)` method. Repaint on change.
- Tests at `libs/markoff-source/tests/` covering construction, document binding round-trip, find-bar behaviour.

**Out of v0 (deferred — none precluded):**

- Fold arrows / fold logic / fold gutter column.
- Emoji completion popup.
- Multi-cursor UI / secondary selection display (foundation supports it; UI is later).
- Replace UI (foundation has `ReplaceController`).
- Link activation (Ctrl-click).
- Command-facade-driven toolbar.
- Block-level decorations / inline math / etc. — those are Live-render's job.
- A shared "view base" QWidget across views — explicitly out (the user is moving away from a generic view class on the consumer side).

## 3. Architecture

### 3.1 Outer shape

`Markoff::Source::Editor` is a direct `QPlainTextEdit` subclass. Standard Qt CodeEditor pattern. The gutter is a sibling `QWidget` child of the viewport, repainted when the viewport scrolls (`QPlainTextEdit::updateRequest`) and when block count changes (`blockCountChanged`). The find bar is a separate widget the host places where it wants — not embedded.

### 3.2 Document binding (relocated)

`SourceTextDocumentBinding` currently lives at `libs/markoff-view-qml/src/SourceTextDocumentBinding.{h,cpp}`. Its public API takes a `QObject *` for what it calls `qtQuickDocument` (a Qt Quick `TextDocument`-handle wrapper that exposes the underlying `QTextDocument`).

Relocation:

- Files move to `libs/markoff-core/src/SourceTextDocumentBinding.{h,cpp}` (header in `libs/markoff-core/include/markoff-foundation/SourceTextDocumentBinding.h`).
- Property `qtQuickDocument` → `textDocument`. The setter accepts a `QTextDocument *` directly. The QML use-site in `SourceEditor.qml` (`textDocumentBinding.qtQuickDocument: textArea.textDocument`) updates to pass the underlying document via a small one-line accessor (Qt 6's `QQuickTextDocument::textDocument()`).
- The class's MOC headers move with it. CMake exports it under `Markoff::Core` (the foundation library target).
- The QML library no longer compiles its own copy; it depends on the foundation export.
- Any tests that currently exercise the binding (none stand out — `tst_view_qml_source_binding.cpp` exercises an integration; that test stays in markoff-view-qml and now uses the foundation-relocated class).

The cycle guards (`m_applyingLocalEdit`, `m_applyingRemoteEdit`) and the UTF-16/UTF-8 conversion helpers are unchanged — pure relocation plus a property rename.

### 3.3 The Editor class

```
class Markoff::Source::Editor : public QPlainTextEdit {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document
               WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *);

    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

signals:
    void documentChanged();
    void themeChanged();

protected:
    void keyPressEvent(QKeyEvent *) override;       // Ctrl+Z/Y → CRDT undo
    void resizeEvent(QResizeEvent *) override;      // gutter geometry
    void scrollContentsBy(int dx, int dy) override; // gutter sync (or use viewport-update path)
    void paintEvent(QPaintEvent *) override;        // standard

private:
    void onUpdateRequest(const QRect &rect, int dy);    // update gutter
    void onBlockCountChanged(int newBlockCount);         // resize gutter
    void recomputeGutterWidth();
    void rebuildExtraSelections();                       // find-bar matches

    SourceTextDocumentBinding *m_binding = nullptr;     // owned QObject child
    Gutter                    *m_gutter = nullptr;       // owned QWidget child of viewport
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter = nullptr;
    Markoff::MarkoffDocument  *m_document = nullptr;     // not owned
    Markoff::Theme             m_theme = Markoff::Theme::defaultLight();
};
```

Cursor/selection lifting: the binding already mirrors `m_qtDoc` cursor changes into `Session::primarySelection()`. The widget exposes Q_PROPERTYs for cursor + selection on the binding (which the relocated header already does); no need to add a parallel surface on the Editor. Hosts that want to read cursor state can `editor->binding()->cursorPosition()` — but we keep `binding()` private and instead delegate via Q_PROPERTYs on the Editor: `cursorPosition`, `selectionStart`, `selectionEnd`. (These are forwarded via inline getters in the header.)

`QTextDocument` undo is disabled by the binding (it calls `setUndoRedoEnabled(false)`). `keyPressEvent` intercepts Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z and calls `m_document->undo()` / `redo()`.

### 3.4 The Gutter

A private `QWidget` subclass at `libs/markoff-source/src/Gutter.{h,cpp}`. Constructor takes a `Editor *` (the parent editor it belongs to).

- `sizeHint()` returns `(gutterWidth(), 0)` where `gutterWidth() = digitWidth * digits(blockCount) + (lr-padding)`.
- `paintEvent` walks `firstVisibleBlock()` → `lastVisibleBlock()`, paints line number digits right-aligned, separator on the right edge.
- Listens implicitly via `Editor::onUpdateRequest` and `Editor::onBlockCountChanged` driving its `update()`.

Theme:
- Background: `theme.color(Slot::EditorBackground)` slightly darkened (or `Slot::GutterBackground` if it materialises in Theme — currently not; we use `EditorBackground`).
- Digit colour: `theme.color(Slot::TextDefault)` at ~50% alpha for non-current line; full alpha for current line.
- Separator: 1-px on the right, `theme.color(Slot::TextDefault)` at ~20% alpha.

The Gutter widget is a child of the **viewport** of the QPlainTextEdit (not of the editor itself), positioned at viewport `(0,0)` with width `gutterWidth()` and full viewport height. The QPlainTextEdit's `setViewportMargins(gutterWidth(), 0, 0, 0)` keeps the text content offset to the right of the gutter.

This is the canonical Qt pattern. It is single-column at v0; if/when fold arrows come, the painter grows a second column or refactors to the legacy `GutterColumn` polymorphic shape — that's a follow-up sub-project's call.

### 3.5 The FindBar

A `QWidget` at `libs/markoff-source/src/FindBar.{h,cpp}` (header at `libs/markoff-source/include/markoff/source/widget/FindBar.h`).

```
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

public slots:
    void show();   // also activates the line-edit
    void hide();   // also clears extra-selection highlights

signals:
    void closed();
};
```

Internals:
- `QLineEdit` for needle.
- "Prev" / "Next" `QToolButton`s (use `QIcon::fromTheme("go-up"/"go-down")`, mnemonic-free).
- "Close" `QToolButton` (`QIcon::fromTheme("window-close")`).
- A `Markoff::SearchEngine` instance (or `SearchController` if its API is closer; foundation has both — picker decided in implementation).
- Match highlighting is driven into the editor by setting `extraSelections()` on it; FindBar holds a list of `QTextEdit::ExtraSelection` and calls `editor->setExtraSelections(...)`.
- "Match count" is shown inline next to the line-edit (`"3 of 12"`).

The FindBar does NOT live inside the Editor. The host (Corbomite, our test app, anyone) constructs it with `new FindBar(editor)` and places it in their own layout (typically below or above the editor). Show/hide is the host's call.

### 3.6 Theme integration

`setTheme(const Theme &)` stores the theme, then:
1. Updates `QPalette` for the editor: `Base` → `Slot::EditorBackground`, `Text` → `Slot::TextDefault`, `Highlight` → `Slot::SelectionBackground`, `HighlightedText` → `Slot::TextDefault`.
2. Calls `setPalette(p)`.
3. Calls `m_gutter->update()`.
4. Re-attaches `KSyntaxHighlighter` colours via the foundation's existing `Kf6SyntaxHighlightService` (already present at `libs/markoff-core/include/markoff-foundation/Kf6SyntaxHighlightService.h`). If the service exposes a "apply theme" method we use it; otherwise we set the highlighter's theme via its native API (`KSyntaxHighlighting::Repository::theme(...)`) — exact path determined at implementation time when reading the service header.

The FindBar's button icons use `QIcon::fromTheme()` so they inherit the Plasma icon set; its colours come from `QPalette` (which the host typically sets), not from `Markoff::Theme`. (The system Theme is for editor content, not chrome.)

## 4. File layout

```
libs/markoff-source/
├── CMakeLists.txt
├── CLAUDE.md
├── include/markoff/source/widget/
│   ├── Editor.h
│   └── FindBar.h
├── src/
│   ├── Editor.cpp
│   ├── FindBar.cpp
│   ├── Gutter.h         (private)
│   └── Gutter.cpp       (private)
├── tests/
│   ├── CMakeLists.txt
│   ├── tst_source_widget_editor.cpp
│   ├── tst_source_widget_binding_roundtrip.cpp
│   └── tst_source_widget_findbar.cpp
└── app/
    ├── CMakeLists.txt
    └── main.cpp           (small dev sandbox; opens argv[1])
```

```
libs/markoff-core/
├── include/markoff-foundation/
│   └── SourceTextDocumentBinding.h        (RELOCATED here from markoff-view-qml)
└── src/
    └── SourceTextDocumentBinding.cpp      (RELOCATED here from markoff-view-qml)
```

```
libs/markoff-view-qml/
├── src/
│   └── SourceTextDocumentBinding.{h,cpp}  (DELETED — now in foundation)
└── qml/
    └── SourceEditor.qml                   (one-line update: textDocumentBinding.textDocument: textArea.textDocument)
```

## 5. Test plan

Three test binaries in `libs/markoff-source/tests/`:

### 5.1 `tst_source_widget_editor`

- `editor_constructs_with_no_document()` — `Editor e; QVERIFY(e.document() == nullptr);` plus minimal sanity (no crashes on paint with offscreen QPA, gutter widget exists, theme has a value).
- `setDocument_attaches_and_seed_text_appears()` — create a `MarkoffDocument`, seed `"hello world"`, `e.setDocument(&doc); QCOMPARE(e.toPlainText(), "hello world")`.
- `setTheme_repaints_gutter()` — sentinel theme color, `setTheme`, `e.gutter()->grab()` (or just check `e.theme().color(Slot::EditorBackground) == sentinel`). The visual check is shallow; test just confirms the setter wires through.

### 5.2 `tst_source_widget_binding_roundtrip`

- `typing_propagates_to_markoff_document()` — set up editor + doc; `QTest::keyClicks(&editor, "abc")`; QCOMPARE(`doc.toMarkdownUtf8()`, `"abc"`).
- `external_doc_edit_propagates_to_editor()` — apply a `MarkoffEdit` directly on the doc; `QTRY_COMPARE(editor.toPlainText(), expected)` after `parseUpdated` propagation.
- `cursor_round_trips_through_session()` — set cursor in editor, read `Session::primarySelection()`; mutate primarySelection externally, confirm editor's text-cursor moves.
- `crdt_undo_via_ctrl_z()` — type "abc"; `QTest::keyClick(&editor, Qt::Key_Z, Qt::ControlModifier)`; `QCOMPARE(doc.toMarkdownUtf8(), "")`.

### 5.3 `tst_source_widget_findbar`

- `findbar_finds_first_match()` — seed "the quick brown fox"; create `FindBar(&editor)`; programmatically set needle "quick" via the line-edit's `setText`; call `findNext()`; assert editor's `extraSelections()` has at least one entry covering the match range.
- `findbar_next_prev_navigation()` — seed text with three matches; verify next/prev buttons step through them and "match count" shows "1 of 3" / "2 of 3" etc.
- `findbar_close_clears_highlights()` — verify on hide, `editor.extraSelections()` is empty.

All tests use `QT_QPA_PLATFORM=offscreen` and `QApplication` (not `QCoreApplication`) since they instantiate widgets.

## 6. Dev sandbox (app)

`libs/markoff-source/app/main.cpp` opens `argv[1]` (a markdown file), creates a `MarkoffDocument`, an `Editor`, a `FindBar`, lays them out in a `QMainWindow` (editor central, find-bar at bottom hidden by default with Ctrl+F to toggle), and shows it. Roughly 60 LOC. Not part of the public API — just a runnable smoke for hand-testing.

## 7. Sequencing

The work breaks into logical chunks the implementation plan will sequence:

1. **Foundation relocation.** Move `SourceTextDocumentBinding` to `markoff-foundation`, rename property, update `markoff-view-qml`'s SourceEditor.qml, run all 34 existing tests.
2. **Scaffold the new lib.** Skeleton CMake + CLAUDE.md + empty Editor/FindBar/Gutter headers + `tst_source_widget_editor` placeholder + hookup at top-level CMake.
3. **Editor core.** `setDocument` + binding wiring + KSyntaxHighlighting attachment + Ctrl+Z/Y forwarding. `tst_source_widget_editor` and most of `tst_source_widget_binding_roundtrip` pass.
4. **Gutter.** Line-number paint + scroll sync + theme colours. No new tests yet (visual; gets `editor.gutter()` accessor for testability if helpful).
5. **Theme.** `setTheme` → palette + gutter repaint + KSyntaxHighlighter theme. The `setTheme_repaints_gutter` test passes.
6. **FindBar.** UI + SearchEngine wiring + ExtraSelections. `tst_source_widget_findbar` passes.
7. **Dev sandbox app.** Wraps everything for hand-testing.

## 8. Risks / open issues

- **`KSyntaxHighlighting::Repository::theme(...)` API may be different from what `Kf6SyntaxHighlightService` already wraps.** Need to read that header during implementation. Worst case: the service handles it and we just call `service.applyTo(highlighter, theme)`. Best case: it's already exactly the contract we need.
- **The QML `SourceEditor.qml` update** needs to pass a real `QTextDocument*` to the renamed `textDocument` property. Qt 6's QML `TextArea.textDocument` returns a `QQuickTextDocument` object which has a `textDocument()` accessor. We use `textArea.textDocument.textDocument` on the QML side, OR we accept a `QQuickTextDocument *` in the binding and dereference internally — implementation picks the cleaner one.
- **`tst_view_qml_source_binding.cpp`** in `markoff-view-qml/tests/` currently links the binding from `markoff-view-qml`. After relocation, the same test should still work — it's now linking through the foundation. CMake update: `target_link_libraries(... markoff_core)` is added; the binding's symbols come from there. Verify no regressions in the existing 34-test suite.
- **`Editor`'s `Q_PROPERTY` for `document`** is a `MarkoffDocument *` pointer. `Markoff::MarkoffDocument` is already `Q_DECLARE_METATYPE`-d in foundation (it has to be for QML to use it), so this works.

## 9. Constraints

- C++20, Qt 6.8+, CMake 3.19+.
- SPDX header `GPL-3.0-or-later` on every new file.
- `QIcon::fromTheme()` for icons.
- `tr()` for any user-visible string (FindBar's "Prev"/"Next"/"Close" tooltips, etc.).
- Build with `-j 8`.
- Don't touch master.
- All existing 34 tests in the worktree must remain green after each phase.
- No QML imports in the new library. It is QtWidgets-only.
- The new library is a sibling to `markoff-view-qml` and `markoff-foundation`; it depends on `markoff-foundation` and on `KF6::SyntaxHighlighting`. It does NOT depend on `markoff-view-qml`.
