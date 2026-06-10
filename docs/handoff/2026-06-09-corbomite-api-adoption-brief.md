# Corbomite API adoption brief — MarkdownView contract v2

**Date:** 2026-06-09 (written after arc completion 2026-06-10)
**Audience:** Corbomite engineer swapping the three Markoff view leaves
behind the finalized `Markoff::MarkdownView` base contract.
**Markoff commit:** this brief was written against Markoff `master` after
Task 13 lands (Task 12 SHA `ffbde444`; Task 13 = the commit containing
this file).
**Corbomite verified against:** `b6ae2c0f` (Corbomite HEAD at time of writing).

> **Re-pin guidance:** adopt at the commit containing this file (Task 13).
> **Never re-pin into the `8c13c5d..079ac1f` window** — those commits render
> styled tables but contain the list-after-table SIGSEGV fixed in `b1b238f`.

---

## 1. The finalized base contract

`Markoff::MarkdownView` (`libs/markoff-core/include/markoff/core/MarkdownView.h`)
is the common base for all three view leaves. After this arc every common
consumer operation dispatches polymorphically through this contract with zero
`qobject_cast` switches.

### Signals

```cpp
void documentChanged(Markoff::MarkoffDocument *doc);
void cursorPositionChanged(int line, int column);   // 1-based flat visual line + col
void scrollPositionChanged(float pos);               // 0.0..1.0 scroll fraction
void themeChanged();
void fontScaleChanged(qreal scale);
void contextChanged(const Markoff::EditorContext &ctx);
```

### Virtual methods

```cpp
// Document
virtual void setDocument(MarkoffDocument *doc);
virtual MarkoffDocument *document() const;

// Cursor (CursorPos is {int line; int column;}, 1-based flat visual lines)
virtual CursorPos cursorPosition() const;
virtual void setCursorPosition(CursorPos);

// Scroll (fraction 0.0–1.0)
virtual float scrollPositionVisualLine() const;
virtual void  setScrollPositionVisualLine(float);

// Read-only
virtual void setReadOnly(bool ro);
virtual bool isReadOnly() const;

// Leaf capability queries (inline)
virtual bool hasCursor()  const { return false; }
virtual bool hasEditing() const { return false; }

// Find
virtual void attachFindController(FindController *fc);   // default: qWarning + no-op
virtual void detachFindController();

// Undo/redo (base-implemented: calls doc->undoD2/redoD2; no-op while read-only)
virtual void undo();
virtual void redo();

// Theme / font scale
virtual Theme theme() const;
virtual void setTheme(const Theme &t);
virtual qreal fontScale() const;
virtual void  setFontScale(qreal s);   // clamped to [0.25, 4.0] at base

// Format verbs (default: no-op; hasEditing() advertises support)
virtual void toggleBold();
virtual void toggleItalic();
virtual void toggleStrikethrough();
virtual void toggleInlineCode();
virtual void insertLink();
virtual void setHeadingLevel(int level);   // 0 strips ATX markers, 1..6 sets
```

### Per-leaf public classes

| Leaf | Class | Extra public API (leaf-specific only) |
|---|---|---|
| `markoff-live` | `Markoff::Live::EditorWidget` | `binding()` — raw access to `LiveListModelBinding` for leaf-specific wiring (find/cursor-state). Consumer-owned; EditorWidget does not take ownership of attached controllers. |
| `markoff-source` | `Markoff::Source::Editor` | `plainTextEdit()` — inner `QPlainTextEdit` (needed for Gutter, low-level tests; **stop using it for undo/redo and theme**). |
| `markoff-styled` | `Markoff::Styled::Editor` | `textEdit()`, `setSession()`, `setLinkService()`, `setFromContext()`. |

---

## 2. Migration table

Line numbers verified against Corbomite `b6ae2c0f`.

### NoteEditorWidget.cpp — find-attach switch (lines 484–506)

**Current:** `qobject_cast<Live::EditorWidget*>` / `qobject_cast<Source::Editor*>` switch
to call `attachFindController` / `detachFindController` on the typed pointers.

```cpp
// CURRENT (lines 484–506)
if (auto *live = qobject_cast<Markoff::Live::EditorWidget*>(leaf))
    live->attachFindController(fc);
else if (auto *src = qobject_cast<Markoff::Source::Editor*>(leaf))
    src->attachFindController(fc);
```

**New:** call on the base pointer directly — `attachFindController` is now a
`virtual` on `MarkdownView`. No switch needed; styled leaf also supports find
(fills Corbomite's Reading-mode gap).

```cpp
// NEW (both showFindBar and hideFindBar)
if (auto *leaf = activeLeaf())
    leaf->attachFindController(fc);   // polymorphic; works for all three leaves

// ... and in hideFindBar:
if (auto *leaf = activeLeaf())
    leaf->detachFindController();
```

**Behavioral note:** `attachFindController` must be called **after** `setDocument`.
Attaching before `setDocument` leaves the d2 connection on the old (nullptr) doc;
swapping the doc while attached similarly leaves a stale connection — same constraint
as the source adapter.

---

### MainWindow.cpp — undo/redo (lines 1267–1293)

**Current:** three-way leaf-type switch for undo/redo:

```cpp
// CURRENT (lines 1267–1293)
// Source branch: src->plainTextEdit()->undo()   ← Qt widget undo, NOT undoD2
// Live branch:   lac->undoAction()->trigger()   ← LiveActionController
```

The source branch (`plainTextEdit()->undo()`) is the dual-authority anti-pattern
flagged in INVARIANTS §3: it invokes QPlainTextEdit's own undo stack, not
`MarkoffDocument::undoD2`. Both stacks can diverge.

**New:** call on the base pointer via `NoteEditorWidget::activeLeaf()`:

```cpp
// NEW — single call, no switch
if (auto *view = activeMarkdownView())
    view->undo();   // base-implemented: doc->undoD2(); no-op while read-only
```

For the redo path: `view->redo()`.

The base `undo()`/`redo()` are already wired to `undoD2`/`redoD2` in
`MarkdownView.cpp`; all three leaves inherit them. No-op while `isReadOnly()`
(undo is a mutation; a read-only view must not change the model through the
Edit menu).

---

### NoteEditorWidget.cpp — theme propagation (line 132)

**Current:** `applyThemeToAllLeaves()` is a no-op with a TODO comment
noting the live leaf's path is `binding()->setTheme(...)` — which has a
broken/stub implementation since the `ThemeService` port.

**New:** call `view->setTheme(t)` on the base pointer. Live's `EditorWidget`
overrides `setTheme` to store in the base (for `themeChanged()` bookkeeping)
and forward to the binding. No `binding()` escape hatch needed.

```cpp
void NoteEditorWidget::applyThemeToAllLeaves()
{
    if (!m_themeService) return;
    const Markoff::Theme t = m_themeService->currentMarkoffTheme();
    for (auto *view : allLeaves())   // or iterate m_editor / m_sourceEditor / m_styledReadingView
        if (view) view->setTheme(t);
}
```

---

### MainWindow.cpp — format-action wiring (lines 1525–1605)

**Current:** `addEditorActionForwarded` hard-codes two typed dispatch paths
— a `LiveActionController` accessor for live and a `Source::Editor` method
pointer for source. Styled leaf (reading mode) has no format verbs today.

**New shape (one base call per action):**

```cpp
// Replace the typed dispatch with:
connect(formatBoldAction, &QAction::triggered, this, [this]() {
    if (auto *view = activeMarkdownView()) view->toggleBold();
});
// ... same pattern for toggleItalic, toggleStrikethrough, toggleInlineCode,
//     insertLink, setHeadingLevel(N)
```

`hasEditing()` returns `true` on source + live (when not read-only) and
`false` on the styled reading-mode leaf — use it to drive the action's
`setEnabled` state from `contextChanged` or on leaf-switch.

**Styled format verbs:** styled now implements all six verbs. They route
through `Markoff::FormatOps` (the same logic lifted from the source leaf).
They emit a `qWarning` and do nothing when the caret is at or after the
first rendered table frame (the table is opaque — no flat-byte coordinate
mapping into it). If styled reading mode should be read-only (which it
currently is, set at construction), the base `setReadOnly(true)` already
makes `hasEditing()` return false; the verbs also early-return while read-only
(the FormatOps path checks the binding's state).

---

### contextChanged — toolbar enable/disable state

**Current:** `connectEditorContext` is a no-op with a TODO
(`MainWindow.cpp:623–629`); `onEditorContextChanged` is also a stub
(`lines 631–638`).

**New:** all three leaves now emit `contextChanged(const Markoff::EditorContext &ctx)`.
Wire it from `activeLeaf()` on every leaf-switch:

```cpp
connect(leaf, &Markoff::MarkdownView::contextChanged,
        this, &MainWindow::onEditorContextChanged);
```

`EditorContext` fields:
```cpp
struct EditorContext {
    QString blockKind;      // BlockKindNames::* constant (e.g. "Heading")
    int     headingLevel;   // 1–6 (0 if not a heading)
    bool    inTable;
    int     tableRow;
    int     tableCol;
};
```

Use `ctx.blockKind` for heading-level radio state; `ctx.inTable` for
table-operation action gating; `ctx.headingLevel` for the H1–H6 checkable
group. The `hasEditing()` flag (from the leaf itself) still gates
bold/italic/strikethrough/code/link — EditorContext does not carry an
`isReadOnly` field. Use `leaf->isReadOnly()` directly.

---

### Ephemeral-state capture/restore + goToLine + line/col statusbar

**Current:** all three are stubbed TODOs citing `cursorLine`/`scrollPosition`
being unavailable on the new leaves.

**New:** all three leaves implement the base `cursorPosition()` /
`setCursorPosition()` / `scrollPositionVisualLine()` / `setScrollPositionVisualLine()`.

```cpp
// Capture
EphemeralState s;
s.cursorLine   = leaf->cursorPosition().line;
s.cursorColumn = leaf->cursorPosition().column;
s.scrollFrac   = leaf->scrollPositionVisualLine();

// Restore
leaf->setCursorPosition({s.cursorLine, s.cursorColumn});
leaf->setScrollPositionVisualLine(s.scrollFrac);

// goToLine (MainWindow.cpp:2609 TODO)
leaf->setCursorPosition({targetLine, 1});

// Line/col statusbar (MainWindow.cpp:1903 TODO)
connect(leaf, &Markoff::MarkdownView::cursorPositionChanged,
        this, [this](int line, int col) {
            m_cursorPosLabel->setText(i18n("Ln %1, Col %2", line, col));
        });
```

`CursorPos` line model is **flat visual lines**: each model block contributes
one flat line (plus its internal `\n` count — a code block with 5 source lines
contributes 5 flat lines). Paragraph/ListItem/setext-Heading/BlockQuote have
their internal `\n` collapsed at load time and contribute exactly 1 flat line
each. This matches `QTextBlock` numbering in source/styled
(`blockNumber()+1`, `positionInBlock()+1`) and is the direct replacement for
the old `cursorLine`/`cursorColumn` pair.

---

### Reading-mode read-only

**Current:** `m_styledReadingView->setReadOnly(true)` is already called at
construction (`NoteEditorWidget.cpp:191`) — this is **correct and unchanged**.

**New:** the behavior is now honest. `Styled::Editor::setReadOnly(true)` blocks
all mutation ingress (typing, Enter/Backspace structural keys, paste, undo
mutations) while keeping caret movement, selection, copy, link activation, and
find working. This is what the reading-mode contract requires.

**Additionally**, reading mode now gains `attachFindController` (see find-attach
section above) — previously silent no-op; now works.

---

## 3. Behavior notes (the honest contract)

### Live read-only

`Live::EditorWidget::setReadOnly(true)` blocks all mutation at six ingress
gates: `LiveEditBinding::onContentsChange`, `LiveStructuralKeyHandler::tryHandle`,
`LiveClipboardController::paste/pasteText/pastePrimary/cut/deleteSelection`,
`TableEditBinding::applyCellEdit`, and `LiveActionController` (disables
cut/paste/delete/undo/redo/format/heading QActions). Navigation, selection,
copy, link activation, zoom, and find keep working.

### Styled find adapter constraints

`StyledFindAdapter` is frame-aware. For blocks whose content is rendered as a
`QTextTable` frame (i.e. `BlockKind::Table`):

- Matches inside the table frame **are counted** by `FindController` (they
  participate in the "3 of 12 matches" display).
- They **are not highlighted** in the `QTextEdit` (the table frame is
  opaque; `ExtraSelections` on overlapping positions do not paint).
- Navigation to an in-frame match **scrolls the QTextEdit to the table frame**
  (the closest reachable position).

`attachFindController` must be called **after** `setDocument`. Calling it
before `setDocument`, or swapping the document while attached, leaves the d2
signal connection on the old document — detach first, then swap, then
re-attach.

### CursorPos line model

`CursorPos` is `{int line, int column}`, 1-based. `line` is an index into
the flat visual line sequence: each model block contributes 1 + (count of
internal `\n` characters). Paragraph, ListItem, setext-Heading, and
BlockQuote blocks have their internal newlines collapsed at load time, so
they always contribute exactly 1 visual line. A `CodeBlock` with 5 lines of
source contributes 5 visual lines. This is the replacement for the old
`cursorLine` API.

### Format verbs — styled table frame guard

`Styled::Editor::toggleBold/Italic/Strikethrough/InlineCode/insertLink/setHeadingLevel`
emit a `qWarning` and return without mutating when the cursor is at or after
the first rendered table frame in the document. This is a v1 limitation of
the opaque-block seam (the table is a `QTextTable` frame whose flat-byte
coordinate mapping is unreliable). In practice, the styled leaf is used in
read-only reading mode where format verbs are disabled by `isReadOnly()` —
the table frame guard is a belt-and-braces protection for the case where
styled is ever used in edit mode with tables.

### contextChanged emission — known staleness window

Source and styled recompute `EditorContext` on `QPlainTextEdit`/`QTextEdit::cursorPositionChanged`
only. This means a block kind-change that does NOT move the caret (e.g. a
structural key that demotes a heading to a paragraph but leaves the caret
at the same position within the block) can leave the emitted context stale
until the next caret movement. In practice every structural key (Enter,
Backspace, Tab, `#`-prefix typing) moves the caret, so the window is
narrow. Live does not have this gap (it also recomputes on model
`dataChanged` kind-transition events). Severity: low. Tracked in queue.

### Find-highlight color

The find-highlight color is currently a **hardcoded soft yellow** in all
three leaf find adapters (`SourceFindAdapter`, `StyledFindAdapter`,
`LiveFindAdapter`). It does not follow the `Markoff::Theme` color slots.
Theme-integration is a follow-up item (tracked in queue).

---

## 4. What is NOT in this arc (non-goals)

These remain as before — document here so you don't spend time looking
for APIs that don't exist yet:

- **Session unification across leaves.** Live auto-creates a `Session`
  in `setDocument`; styled accepts a session via `setSession()`; source
  uses an internal session. Each leaf manages its own session lifetime.
- **Word count / text-changed signal on the base.** Consumers connect
  to `MarkoffDocument::d2DocumentChanged` and compute word count from
  `serializeForSave()` or a block walk.
- **Cursor restoration after undo.** `MarkdownView::undo()`/`redo()` restore
  document content correctly. Caret placement after undo uses each leaf's
  own default behavior (no guarantee of returning to the pre-edit position).
  This is VIEW-IMPLEMENTORS-GUIDE §B.4 (partial; tracked).
- **In-table find highlighting.** Matches inside a `QTextTable` frame are
  counted and navigated-to but not painted. See §3 above.
- **Corbomite-side `addEditorActionForwarded` helper cleanup.** The typed
  dispatch helper itself can be simplified or deleted once the format verbs
  are routed through the base. Out of scope here; clean up at Corbomite's
  own pace.
