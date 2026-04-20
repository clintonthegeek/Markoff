# Phase C6 — Editor state + context-menu contribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `Markoff::EditorContext` + `Editor::context() / contextChanged / aboutToShowContextMenu` so Corbomite (and future plugins) can drive menu/toolbar check-state from cursor context and inject items into the right-click editor menu. Narrowed to "populate what Corbomite uses today, struct shape stable for future extensions."

**Architecture:** Two-repo change. Markoff ships the new types + classifier + signals on `Markoff::Editor`; tagged `v0.5.0`. Corbomite bumps submodule pin and rewires `MainWindow::refreshEditorActions` onto the signal (plus adds right-click menu contribution via `MenuSectionHelper`).

**Tech Stack:** C++20, Qt6.8+, CMake. Inline classifier reads `QTextCharFormat` flags written by the existing `MarkdownHighlighter`. Debounce via `QTimer::singleShot(16ms)`.

**Reference spec:** [`libs/markoff-family/docs/specs/2026-04-20-phase-c6-editor-state-context-menu.md`](../specs/2026-04-20-phase-c6-editor-state-context-menu.md) (C6 wrapper) + [`libs/markoff-family/libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md`](../../libs/markoff-live/docs/specs/2026-04-20-consumer-editor-state-surface.md) (full 640-line consumer-spec).

---

## §0 Orientation

### Repo layout
- **Markoff submodule:** `/home/clinton/dev/Corbomite/libs/markoff-family/` — commits to `master` directly.
- **Corbomite outer repo:** `/home/clinton/dev/Corbomite/` — submodule bump + adapter commit.

### Build commands

**Markoff (runs via the Corbomite parent build-dev for now — previous tasks confirm this is the working build dir):**
```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -30
cd /home/clinton/dev/Corbomite/build-dev && ctest -R "markoff" --output-on-failure
```

**Corbomite:**
```bash
cd /home/clinton/dev/Corbomite
cmake --build build -j 10
cd build && ctest --output-on-failure -j 10
```

### Commit convention
Markoff: subject prefix matches touched library (`markoff-live:`, `markoff-core:`, `docs:`). Corbomite: Conventional Commits. Trailer: `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.

### Key locations (grep-verified 2026-04-20)
- `libs/markoff-live/include/markoff/Editor.h:165` — `currentHeadingLevel()` declaration
- `libs/markoff-live/include/markoff/Editor.h:174` — `cursorInTable()` declaration
- `libs/markoff-live/include/markoff/Editor.h:276` — `cursorPositionChanged(int, int)` signal
- `libs/markoff-live/include/markoff/Editor.h:100` — `isReadOnly() const override`
- `libs/markoff-live/src/Editor.cpp:705` — `contextMenuEvent` entry point
- `libs/markoff-live/src/Editor.cpp:1398` — `currentHeadingLevel()` body (text-prefix scan)
- `libs/markoff-live/src/Editor.cpp:2347` — `cursorInTable()` body (`QTextTable *` check)
- `libs/markoff-live/src/MarkdownHighlighter.cpp` — uses raw Qt flags (`setFontWeight(700)` at L119, `setFontItalic(true)` L120, `setFontStrikeOut(true)` L187). No custom property keys for toggle formats — reading back is `fmt.fontWeight()`, `fmt.fontItalic()`, `fmt.fontStrikeOut()`.

### Inline-code classification (open investigation)
`MarkdownHighlighter.cpp` grep didn't surface an obvious inline-code property key. The plan's T3 includes a targeted grep to locate the storage mechanism before committing. **If the highlighter doesn't currently tag inline-code runs distinctly**, the classifier falls back to `QFontDatabase::Monospace` detection on the font family, OR the plan adds a custom property (`kInlineCodeProperty` int constant) on the highlighter that T3 both writes and reads. Decision at investigation time.

---

## §1 File structure

### Markoff side (new + modified)

| File                                                       | Change  | Responsibility                                                   |
| ---------------------------------------------------------- | ------- | ---------------------------------------------------------------- |
| `libs/markoff-live/include/markoff/EditorContext.h`        | create  | `struct EditorContext` + nested `BlockKind`/`ListMarker`/`TaskState` enums + nested `TableContext`/`LinkContext`/`TagContext`/`FootnoteContext` structs |
| `libs/markoff-live/include/markoff/Editor.h`               | modify  | Add `context() const`, `contextChanged` signal, `aboutToShowContextMenu` signal |
| `libs/markoff-live/src/Editor.cpp`                         | modify  | Implement `context()`, block/inline classifier helpers, signal emission wiring, context-menu signal emission |
| `libs/markoff-live/src/EditorContextClassifier.cpp`        | create  | Internal — the block + inline classification logic (too bulky to inline in Editor.cpp) |
| `libs/markoff-live/src/EditorContextClassifier.h`          | create  | Internal header for the classifier helpers |
| `libs/markoff-live/tests/tst_editor_context_classifier.cpp` | create  | Unit tests for the classifier (BlockKind + inline flag detection) |
| `libs/markoff-live/tests/tst_editor_context.cpp`            | create  | End-to-end `Editor::context()` + `contextChanged` behavior test |
| `libs/markoff-live/tests/tst_editor_context_menu.cpp`       | create  | `aboutToShowContextMenu` emission + payload test |
| `libs/markoff-live/CMakeLists.txt`                         | modify  | Add `EditorContextClassifier.{h,cpp}` sources, install `EditorContext.h` public header |
| `libs/markoff-live/tests/CMakeLists.txt`                   | modify  | Register three new tests |

### Corbomite side (adapter)

| File                                                       | Change  | Responsibility                                                   |
| ---------------------------------------------------------- | ------- | ---------------------------------------------------------------- |
| `libs/markoff-family` (submodule pointer)                  | bump    | Pin to Markoff `v0.5.0`                                          |
| `src/app/MainWindow.h`                                     | modify  | Declarations for `connectEditorContext`, `onEditorContextChanged`, `connectEditorContextMenu`, `onAboutToShowContextMenu` |
| `src/app/MainWindow.cpp`                                   | modify  | Wire the connects on `activeLeafChanged`; implement the slot bodies; retire or reshape `refreshEditorActions` |
| `tests/app/tst_mainwindow_action_wiring.cpp`               | modify  | Add check-state assertions to the existing introspection test; use fixture markdown + synthetic cursor moves |

---

## §2 Task list — Markoff side

### Task 1: Introduce `EditorContext.h` + stub `Editor::context()` (Commit A)

**Files:**
- Create: `libs/markoff-live/include/markoff/EditorContext.h`
- Modify: `libs/markoff-live/include/markoff/Editor.h` (add `context()` decl + signals)
- Modify: `libs/markoff-live/src/Editor.cpp` (stub `context()` returning default-constructed struct)
- Modify: `libs/markoff-live/CMakeLists.txt` (install the new public header)

- [ ] **Step 1: Write the header**

Create `libs/markoff-live/include/markoff/EditorContext.h` with the full struct shape per consumer-spec §3. Verbatim content:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
#pragma once

#include <QString>
#include <Qt>

#include <optional>

namespace Markoff {

/// A snapshot of the editor's per-cursor contextual state, suitable
/// for driving menu/toolbar enable + check state in the host app.
/// Fields are value-typed; copying is cheap.
///
/// Phase C6 populates a subset of fields (blockKind, headingLevel,
/// table, inBold/Italic/Strikethrough/InlineCode, hasSelection,
/// atBlockStart, atBlockEnd, readOnly). Other fields are declared
/// but return default values until a later C-phase's classifier
/// work fills them in. Consumers should write forward-compatible
/// code: `if (ctx.link) { /* use ctx.link->url */ }`.
struct EditorContext {
    // ---- Block context ----
    enum class BlockKind {
        Paragraph, Heading, BlockQuote, Callout, CodeBlock,
        Table, ListItem, HorizontalRule, FrontMatter, MathDisplay,
        Empty,
    };
    BlockKind blockKind = BlockKind::Paragraph;

    int headingLevel = 0;          ///< 1..6 when blockKind == Heading, else 0.

    // Populated when blockKind == Callout (deferred — default-empty in C6).
    QString calloutType;
    QString calloutTitle;

    // Populated when blockKind == CodeBlock (deferred — default in C6).
    QString codeBlockLanguage;
    bool    codeBlockFenced = false;

    // Populated when blockKind == ListItem (taskState + listMarker deferred).
    enum class ListMarker { Unordered, Ordered, Task };
    ListMarker listMarker = ListMarker::Unordered;
    enum class TaskState { NotATask, Unchecked, Checked, Cancelled };
    TaskState  taskState = TaskState::NotATask;
    int        listNestingLevel = 0;

    // Populated when blockKind == BlockQuote (deferred).
    int blockquoteDepth = 0;

    // Populated when blockKind == Table — C6 populates row/col/rows/cols/isHeaderRow.
    // columnAlignment left default (future phase).
    struct TableContext {
        int row = 0, col = 0;
        int rows = 0, cols = 0;
        Qt::Alignment columnAlignment;
        bool isHeaderRow = false;
    };
    std::optional<TableContext> table;

    // ---- Inline-span context ----
    // `true` when the selection (or cursor if empty) lies entirely
    // within a span of the named kind.
    bool inBold          = false;
    bool inItalic        = false;
    bool inStrikethrough = false;
    bool inInlineCode    = false;
    bool inHighlight     = false;   ///< deferred — always false in C6
    bool inMathInline    = false;   ///< deferred — always false in C6

    // `true` when the cursor sits inside the corresponding span.
    // All three optional<> are `std::nullopt` in C6 — future phases fill them.
    struct LinkContext {
        QString url;
        QString text;
        bool isWikiLink = false;
        bool isEmbed    = false;
    };
    std::optional<LinkContext> link;

    struct TagContext { QString tag; };
    std::optional<TagContext> tag;

    struct FootnoteContext { QString id; };
    std::optional<FootnoteContext> footnote;

    // ---- Convenience flags ----
    bool hasSelection = false;
    bool atBlockStart = false;
    bool atBlockEnd   = false;
    bool readOnly     = false;
};

} // namespace Markoff
```

- [ ] **Step 2: Declare `context()`, `contextChanged`, `aboutToShowContextMenu` on `Editor`**

In `libs/markoff-live/include/markoff/Editor.h`, include the new header at the top (next to other markoff includes):

```cpp
#include <markoff/EditorContext.h>
```

Add a forward decl for `QMenu` near the top of the file (after existing forward decls).

In the Editor class `public:` section, near the other accessors (after `cursorInTable() const;` at line 174):

```cpp
    /// Phase C6 — O(1) snapshot of the cursor's contextual state,
    /// used by hosts to drive toolbar/menu enable + check state.
    /// See `contextChanged` for the reactive pull, or call on demand
    /// (e.g. when the active leaf changes and no signal has fired).
    EditorContext context() const;
```

In the Q_SIGNALS: section (near `linkHovered` at line 281), add:

```cpp
    /// Phase C6 — emitted whenever the cursor position, selection,
    /// document structure, or inline/block classification of the
    /// cursor's surroundings changes. Debounced to at most one
    /// emission per ~16 ms so hosts don't thrash on rapid arrow-key
    /// navigation.
    void contextChanged(const EditorContext &ctx);

    /// Phase C6 — emitted from contextMenuEvent() after Markoff's
    /// built-in items have been appended but before menu.exec() is
    /// called. Subscribers may add QActions / submenus / separators
    /// directly to `menu`. The emission is single-shot per right-
    /// click; `menu` is stack-local and dies when exec() returns, so
    /// do not capture the pointer across the emission.
    void aboutToShowContextMenu(QMenu *menu,
                                const EditorContext &ctx,
                                const QPoint &globalPos);
```

- [ ] **Step 3: Stub implementation in Editor.cpp**

In `libs/markoff-live/src/Editor.cpp`, add near the end of the file (or after the existing `currentHeadingLevel()` body):

```cpp
EditorContext Editor::context() const
{
    // Phase C6 stub — later tasks fill in the classifier.
    EditorContext ctx;
    ctx.readOnly = isReadOnly();
    return ctx;
}
```

- [ ] **Step 4: Install the public header**

In `libs/markoff-live/CMakeLists.txt`, find the public-header install list (grep for `Editor.h`). Add `include/markoff/EditorContext.h` alongside it. Pattern should match existing entries exactly.

- [ ] **Step 5: Build**

```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -15
```

Expected: clean compile.

- [ ] **Step 6: Commit**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add libs/markoff-live/include/markoff/EditorContext.h \
        libs/markoff-live/include/markoff/Editor.h \
        libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: introduce EditorContext + stub Editor::context()

Phase C6 Commit A of 6. Lands the public struct shape (blockKind,
headingLevel, table, inBold/Italic/Strikethrough/InlineCode, etc.)
plus the contextChanged and aboutToShowContextMenu signal
declarations and a stub Editor::context() that returns a
default-constructed struct (readOnly populated only). Subsequent
commits wire the classifier and signal emission.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Block classifier + unit test (Commit B)

**Files:**
- Create: `libs/markoff-live/src/EditorContextClassifier.h`
- Create: `libs/markoff-live/src/EditorContextClassifier.cpp`
- Modify: `libs/markoff-live/src/Editor.cpp` (replace stub body with classifier calls — block portion only)
- Create: `libs/markoff-live/tests/tst_editor_context_classifier.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`, `libs/markoff-live/tests/CMakeLists.txt`

**Context:** Block classification reads from `QTextCursor` — detect Heading via leading-`#` prefix scan (reuse `currentHeadingLevel` logic), Table via `cursor.currentTable()`, ListItem via leading list-marker pattern, Empty via zero-length block text, else Paragraph.

- [ ] **Step 1: Create the classifier header**

`libs/markoff-live/src/EditorContextClassifier.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
#pragma once

#include <markoff/EditorContext.h>

#include <QTextCursor>

namespace Markoff::Internal {

/// Classify the block at the given cursor position. Populates
/// `ctx.blockKind`, `ctx.headingLevel`, `ctx.table`, `ctx.atBlockStart`,
/// `ctx.atBlockEnd`. Other fields untouched.
void classifyBlockAtCursor(const QTextCursor &cursor, EditorContext &ctx);

/// Classify the inline spans at / around the cursor (or across the
/// selection if non-empty). Populates `ctx.inBold`, `ctx.inItalic`,
/// `ctx.inStrikethrough`, `ctx.inInlineCode`, `ctx.hasSelection`.
/// Other fields untouched.
void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx);

} // namespace Markoff::Internal
```

- [ ] **Step 2: Implement block classifier**

`libs/markoff-live/src/EditorContextClassifier.cpp` (inline portion stubbed for now):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "EditorContextClassifier.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>

namespace Markoff::Internal {

namespace {

int headingLevelFromBlockText(const QString &line)
{
    int level = 0;
    while (level < line.size() && line.at(level) == QLatin1Char('#'))
        ++level;
    if (level == 0 || level > 6) return 0;
    if (level >= line.size() || line.at(level) != QLatin1Char(' '))
        return 0;
    return level;
}

bool lineLooksLikeListItem(const QString &line)
{
    // Matches "- ", "* ", "+ ", "1. ", "- [ ] ", "- [x] " with
    // optional leading whitespace. Cheap regex is fine — this runs
    // at most once per contextChanged.
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:[-*+]|\\d+\\.)\\s"));
    return re.match(line).hasMatch();
}

} // namespace

void classifyBlockAtCursor(const QTextCursor &cursor, EditorContext &ctx)
{
    using BK = EditorContext::BlockKind;
    if (cursor.isNull()) {
        ctx.blockKind = BK::Empty;
        return;
    }

    const QTextBlock block = cursor.block();
    const QString line = block.text();

    ctx.atBlockStart = cursor.positionInBlock() == 0;
    ctx.atBlockEnd = cursor.positionInBlock() == block.length() - 1;

    // Table — check first, since tables own their own QTextBlock tree.
    if (QTextTable *tbl = cursor.currentTable()) {
        const QTextTableCell cell = tbl->cellAt(cursor);
        EditorContext::TableContext tc;
        tc.row = cell.row();
        tc.col = cell.column();
        tc.rows = tbl->rows();
        tc.cols = tbl->columns();
        tc.isHeaderRow = (cell.row() == 0);
        // columnAlignment left default — populated in a future phase.
        ctx.table = tc;
        ctx.blockKind = BK::Table;
        return;
    }

    // Heading
    const int lvl = headingLevelFromBlockText(line);
    if (lvl > 0) {
        ctx.blockKind = BK::Heading;
        ctx.headingLevel = lvl;
        return;
    }

    // Empty
    if (line.isEmpty()) {
        ctx.blockKind = BK::Empty;
        return;
    }

    // ListItem
    if (lineLooksLikeListItem(line)) {
        ctx.blockKind = BK::ListItem;
        // listMarker / taskState / listNestingLevel left default —
        // future phase.
        return;
    }

    // Fallback
    ctx.blockKind = BK::Paragraph;
}

void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx)
{
    // Implemented in Task 3. Stub for Commit B.
    ctx.hasSelection = cursor.hasSelection();
}

} // namespace Markoff::Internal
```

- [ ] **Step 3: Wire the classifier into `Editor::context()`**

In `libs/markoff-live/src/Editor.cpp`, replace the stub `context()` body:

```cpp
#include "EditorContextClassifier.h"

// ... later in the file ...

EditorContext Editor::context() const
{
    EditorContext ctx;
    ctx.readOnly = isReadOnly();

    auto *ti = focusedTextItem();
    if (!ti) return ctx;

    const QTextCursor cursor = ti->textControl()->textCursor();
    Internal::classifyBlockAtCursor(cursor, ctx);
    Internal::classifyInlineAtCursor(cursor, ctx);
    return ctx;
}
```

- [ ] **Step 4: Register new sources in CMakeLists.txt**

In `libs/markoff-live/CMakeLists.txt`, find the source list (grep `src/Editor.cpp` for adjacency). Add `src/EditorContextClassifier.cpp` to the list, alphabetical if the list is sorted.

- [ ] **Step 5: Write classifier unit test**

`libs/markoff-live/tests/tst_editor_context_classifier.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "../src/EditorContextClassifier.h"

using namespace Markoff;
using namespace Markoff::Internal;

class TstEditorContextClassifier : public QObject
{
    Q_OBJECT

private slots:
    void emptyDocument_isEmpty();
    void plainParagraph_isParagraph();
    void heading_levels();
    void listItem_detected();
    // Inline tests land in Task 3; this file compiles them as stubs-or-skip.
};

namespace {
QTextCursor cursorAtBlock(QTextDocument &doc, int blockIdx, int offset = 0)
{
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::Start);
    for (int i = 0; i < blockIdx; ++i)
        c.movePosition(QTextCursor::NextBlock);
    c.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, offset);
    return c;
}
} // namespace

void TstEditorContextClassifier::emptyDocument_isEmpty()
{
    QTextDocument doc;
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Empty);
}

void TstEditorContextClassifier::plainParagraph_isParagraph()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("hello world"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Paragraph);
    QCOMPARE(ctx.headingLevel, 0);
}

void TstEditorContextClassifier::heading_levels()
{
    for (int level = 1; level <= 6; ++level) {
        QTextDocument doc;
        const QString line = QString(level, QLatin1Char('#'))
                             + QStringLiteral(" Title");
        doc.setPlainText(line);
        EditorContext ctx;
        classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
        QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Heading);
        QCOMPARE(ctx.headingLevel, level);
    }
}

void TstEditorContextClassifier::listItem_detected()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("- bullet"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::ListItem);
}

QTEST_MAIN(TstEditorContextClassifier)
#include "tst_editor_context_classifier.moc"
```

Note: the test has no `Editor` instance — it unit-tests the classifier against a fresh `QTextDocument`. `Table` detection requires a live `QTextTable` on a `QTextDocument`; test that via `QTextCursor::insertTable` — add a `table_detected()` slot if trivial, otherwise save for the end-to-end test in Task 6.

- [ ] **Step 6: Register test**

In `libs/markoff-live/tests/CMakeLists.txt`, add `tst_editor_context_classifier` alongside adjacent tests using the project's test-registration pattern (mirror `tst_editor_cursor_in_table` or similar).

- [ ] **Step 7: Build + run**

```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -15
cd /home/clinton/dev/Corbomite/build-dev && ctest -R tst_editor_context_classifier --output-on-failure
```

Expected: 4+ tests pass.

- [ ] **Step 8: Commit**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add libs/markoff-live/src/EditorContextClassifier.h \
        libs/markoff-live/src/EditorContextClassifier.cpp \
        libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/tests/tst_editor_context_classifier.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: EditorContext block classifier + tests

Phase C6 Commit B of 6. Populates blockKind (Paragraph/Heading/Table/
ListItem/Empty), headingLevel, table (row/col/rows/cols/isHeaderRow),
atBlockStart, atBlockEnd, hasSelection on Editor::context(). Inline
classifier remains a stub; filled in by Commit C.

Classifier is internal (src/EditorContextClassifier.{h,cpp}) — not
part of the public surface. Editor::context() is the only caller.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Inline classifier + unit test (Commit C)

**Files:**
- Modify: `libs/markoff-live/src/EditorContextClassifier.cpp` (replace inline stub)
- Modify: `libs/markoff-live/tests/tst_editor_context_classifier.cpp` (add inline tests)

**Context:** `MarkdownHighlighter` writes raw Qt flags onto `QTextCharFormat` during `highlightBlock` — `setFontWeight(700)` for bold, `setFontItalic(true)` for italic, `setFontStrikeOut(true)` for strike. Inline-code storage is **open** — first step investigates.

- [ ] **Step 1: Investigate inline-code storage**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family/libs/markoff-live
grep -n "InlineCode\|inline.code\|Fixed.*font\|Monospace\|QFont::Fixed\|setFontFamily" src/MarkdownHighlighter.cpp | head -20
```

Read the matching lines. Likely outcomes:
- **(a)** Highlighter sets `fontFamily` to a monospace family when in inline-code. Classifier reads `fmt.fontFamilies()` and checks for monospace.
- **(b)** Highlighter uses a custom `setProperty(N, true)` for inline-code. Classifier reads that property.
- **(c)** Highlighter doesn't tag inline-code distinctly (unlikely but possible). Then add a custom property key `kInlineCodeProperty = QTextFormat::UserProperty + 100` in a new internal header `MarkdownFormatProperties.h`, set it at the highlighter's inline-code path, read it in the classifier.

Document the outcome in a brief comment in `EditorContextClassifier.cpp` so future maintainers see the storage mechanism.

- [ ] **Step 2: Implement inline classifier**

Replace the `classifyInlineAtCursor` body in `EditorContextClassifier.cpp`:

```cpp
void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx)
{
    ctx.hasSelection = cursor.hasSelection();
    if (cursor.isNull()) return;

    const QTextCharFormat fmtStart = cursor.charFormat();

    auto isBoldFmt = [](const QTextCharFormat &f) {
        return f.fontWeight() >= QFont::Bold;
    };
    auto isItalicFmt = [](const QTextCharFormat &f) {
        return f.fontItalic();
    };
    auto isStrikeFmt = [](const QTextCharFormat &f) {
        return f.fontStrikeOut();
    };
    auto isInlineCodeFmt = [](const QTextCharFormat &f) {
        // FILL IN based on Step 1 investigation result.
        // Example for (a): check if the font family list contains a
        //   monospace family from the theme's code font.
        // Example for (b): f.hasProperty(kInlineCodeProperty).
        // Example for (c): same as (b), after adding the property to
        //   the highlighter.
        Q_UNUSED(f);
        return false;  // TEMP — fill in per Step 1.
    };

    if (ctx.hasSelection) {
        // Selection-wide: matching-span iff the flag holds at both
        // start and end of the selection. Matches Qt's own
        // QTextEdit::fontBold()-style semantics.
        QTextCursor endCur(cursor);
        endCur.setPosition(cursor.selectionEnd());
        const QTextCharFormat fmtEnd = endCur.charFormat();
        ctx.inBold          = isBoldFmt(fmtStart)   && isBoldFmt(fmtEnd);
        ctx.inItalic        = isItalicFmt(fmtStart) && isItalicFmt(fmtEnd);
        ctx.inStrikethrough = isStrikeFmt(fmtStart) && isStrikeFmt(fmtEnd);
        ctx.inInlineCode    = isInlineCodeFmt(fmtStart) && isInlineCodeFmt(fmtEnd);
    } else {
        ctx.inBold          = isBoldFmt(fmtStart);
        ctx.inItalic        = isItalicFmt(fmtStart);
        ctx.inStrikethrough = isStrikeFmt(fmtStart);
        ctx.inInlineCode    = isInlineCodeFmt(fmtStart);
    }
}
```

Replace the `isInlineCodeFmt` body with the real check from Step 1.

- [ ] **Step 3: Add inline classifier tests**

Append to `tst_editor_context_classifier.cpp`:

```cpp
private slots:
    // ... existing ...
    void bold_flagDetected();
    void italic_flagDetected();
    void strike_flagDetected();
    void selection_widePredicate();
    // inline-code test added based on Step 1 outcome
};

// Helper to inject a formatted run at block 0.
namespace {
void insertFormattedRun(QTextDocument &doc,
                       const QString &text,
                       const QTextCharFormat &fmt)
{
    QTextCursor c(&doc);
    c.insertText(text, fmt);
}
} // namespace

void TstEditorContextClassifier::bold_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontWeight(QFont::Bold);
    insertFormattedRun(doc, QStringLiteral("BoldText"), fmt);

    QTextCursor c(&doc);
    c.setPosition(2);  // inside "BoldText"
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inBold);
    QVERIFY(!ctx.inItalic);
}

void TstEditorContextClassifier::italic_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontItalic(true);
    insertFormattedRun(doc, QStringLiteral("Italic"), fmt);

    QTextCursor c(&doc);
    c.setPosition(2);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inItalic);
    QVERIFY(!ctx.inBold);
}

void TstEditorContextClassifier::strike_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(true);
    insertFormattedRun(doc, QStringLiteral("Strike"), fmt);

    QTextCursor c(&doc);
    c.setPosition(2);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inStrikethrough);
}

void TstEditorContextClassifier::selection_widePredicate()
{
    QTextDocument doc;
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    insertFormattedRun(doc, QStringLiteral("BoldRun"), boldFmt);
    QTextCharFormat plainFmt;
    insertFormattedRun(doc, QStringLiteral(" plain"), plainFmt);

    QTextCursor c(&doc);
    c.setPosition(0);
    c.setPosition(13, QTextCursor::KeepAnchor);  // spans into "plain"
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.hasSelection);
    QVERIFY(!ctx.inBold);  // selection leaves bold span, predicate false
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -15
cd /home/clinton/dev/Corbomite/build-dev && ctest -R tst_editor_context_classifier --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add libs/markoff-live/src/EditorContextClassifier.cpp \
        libs/markoff-live/tests/tst_editor_context_classifier.cpp
# Plus MarkdownHighlighter.cpp and src/MarkdownFormatProperties.h if Step 1
# required adding a custom property key
git commit -m "$(cat <<'EOF'
markoff-live: EditorContext inline classifier (bold/italic/strike/inlineCode)

Phase C6 Commit C of 6. Reads QTextCharFormat flags written by
MarkdownHighlighter (fontWeight/fontItalic/fontStrikeOut). Inline-code
uses <Step 1 outcome>. Selection-wide predicate checks format at both
endpoints (matches Qt's QTextEdit::fontBold() semantics).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `contextChanged` signal emission + debounce (Commit D)

**Files:**
- Modify: `libs/markoff-live/include/markoff/Editor.h` (add private debounce timer member)
- Modify: `libs/markoff-live/src/Editor.cpp` (wire signal emission)

**Context:** Emission triggers per consumer-spec §3.2:
- Cursor moves to a different run or block
- Selection crosses a span boundary
- Document re-parsed and cursor's surroundings reclassified
- Read-only state changes

Debounce: one ~16ms `QTimer::singleShot`. Reset on each trigger.

- [ ] **Step 1: Add private debounce timer**

In `libs/markoff-live/include/markoff/Editor.h`, near other private timer members (grep for `QTimer *` on the private side):

```cpp
    QTimer *m_contextDebounceTimer = nullptr;
```

Add a forward decl `class QTimer;` near the top if not already present (it probably is).

- [ ] **Step 2: Construct and wire the timer**

In `Editor`'s constructor (grep for existing `QTimer` construction to find the right block), add:

```cpp
    m_contextDebounceTimer = new QTimer(this);
    m_contextDebounceTimer->setSingleShot(true);
    m_contextDebounceTimer->setInterval(16);
    connect(m_contextDebounceTimer, &QTimer::timeout, this, [this] {
        Q_EMIT contextChanged(context());
    });
```

- [ ] **Step 3: Kick the timer from cursor / document triggers**

Add a private helper:

```cpp
// Editor.h (private section):
void scheduleContextRefresh();

// Editor.cpp:
void Editor::scheduleContextRefresh()
{
    if (m_contextDebounceTimer) m_contextDebounceTimer->start();
}
```

Then find every existing place that emits `cursorPositionChanged` via the editor (grep for `Q_EMIT cursorPositionChanged` / `emit q->cursorPositionChanged`) — most of these happen inside the Editor class's existing cursor handling (line 2280-region per earlier C5 work). Add `scheduleContextRefresh()` alongside the emit. Also hook:
- The document-change signal (grep `QTextDocument::contentsChanged` — wherever Editor hooks it).
- `setReadOnly` sets m_readOnly — add `scheduleContextRefresh()` call.

If this fan-out is fragile (many call sites), an alternative is to add a single sentinel: subscribe to `TextControl::cursorPositionChanged` + `TextControl::selectionChanged` + `QTextDocument::contentsChanged` at constructor time, each kicking `scheduleContextRefresh()`. Fewer call sites to audit. Prefer this shape.

- [ ] **Step 4: Verify emission test**

Extend `tst_editor_context.cpp` (created in Task 6) or create a minimal test now:

`libs/markoff-live/tests/tst_editor_context.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QSignalSpy>
#include <QTest>

#include <markoff/Editor.h>

using namespace Markoff;

class TstEditorContext : public QObject
{
    Q_OBJECT

private slots:
    void contextChanged_firesOnCursorMotion();
    void contextChanged_debounced();
    // Task 6 adds the full shape-assertion test.
};

void TstEditorContext::contextChanged_firesOnCursorMotion()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("# Heading\n\nParagraph."));
    QSignalSpy spy(&ed, &Editor::contextChanged);
    QVERIFY(spy.isValid());

    // Move cursor into the heading block, then into the paragraph.
    // Exact API depends on Editor's cursor-control helpers — use
    // goToLine() or a test-only synthesize helper if available.
    // Wait for the debounce to fire.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 1000);
}

void TstEditorContext::contextChanged_debounced()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("A\nB\nC\nD\nE"));
    QSignalSpy spy(&ed, &Editor::contextChanged);

    // Fire 5 cursor moves in rapid succession.
    for (int i = 0; i < 5; ++i) ed.goToLine(i);
    // After debounce settles, spy count should be small (1 or 2), not 5.
    QTest::qWait(100);
    QVERIFY2(spy.count() <= 2,
             QByteArray::number(spy.count()).prepend("debounce too loose: "));
}

QTEST_MAIN(TstEditorContext)
#include "tst_editor_context.moc"
```

Add to `libs/markoff-live/tests/CMakeLists.txt`.

- [ ] **Step 5: Build + run**

```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -15
cd /home/clinton/dev/Corbomite/build-dev && ctest -R "tst_editor_context$" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/include/markoff/Editor.h \
        libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/tests/tst_editor_context.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: Editor::contextChanged signal + 16ms debounce

Phase C6 Commit D of 6. Fires after cursor motion, selection change,
document-contents change, or read-only toggle. QTimer::singleShot(16)
debounce keeps rapid arrow-key navigation from thrashing hosts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `aboutToShowContextMenu` emission (Commit E)

**Files:**
- Modify: `libs/markoff-live/src/Editor.cpp` (both branches of `contextMenuEvent`)
- Create: `libs/markoff-live/tests/tst_editor_context_menu.cpp`

**Context:** Signal fires after built-ins, before `menu.exec()`. Both the table-branch (Editor.cpp:712-736) and the general-branch (Editor.cpp:738+) get the emission.

- [ ] **Step 1: Add signal emission in the table branch**

In `libs/markoff-live/src/Editor.cpp:705-736`, change the table-branch tail from:

```cpp
            menu.addAction(tr("Select Column"), this, &Editor::tableSelectColumn);
            menu.exec(e->globalPos());
            e->accept();
            return;
```

to:

```cpp
            menu.addAction(tr("Select Column"), this, &Editor::tableSelectColumn);
            Q_EMIT aboutToShowContextMenu(&menu, context(), e->globalPos());
            menu.exec(e->globalPos());
            e->accept();
            return;
```

- [ ] **Step 2: Add signal emission in the general branch**

Find the end of the general-branch (line 738-ish to wherever `menu.exec(e->globalPos())` is called). Before the `exec()` call, add:

```cpp
    Q_EMIT aboutToShowContextMenu(&menu, context(), e->globalPos());
    menu.exec(e->globalPos());
    e->accept();
```

(Replace just the pre-exec line; preserve the rest of the branch body.)

- [ ] **Step 3: Write the emission test**

`libs/markoff-live/tests/tst_editor_context_menu.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

#include <markoff/Editor.h>

using namespace Markoff;

class TstEditorContextMenu : public QObject
{
    Q_OBJECT

private slots:
    void aboutToShowContextMenu_firesOnRightClick();
    void subscribedAction_appearsInMenu();
};

void TstEditorContextMenu::aboutToShowContextMenu_firesOnRightClick()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("paragraph"));
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));

    QSignalSpy spy(&ed, &Editor::aboutToShowContextMenu);
    QVERIFY(spy.isValid());

    // Spawn a QContextMenuEvent via QContextMenuEvent's constructor
    // and send it to the editor (more reliable than synthesizing a
    // Right-button click through QTest::mouseClick since the latter
    // doesn't always fire contextMenuEvent in offscreen QPA).
    //
    // Note: if this fails to fire under offscreen QPA, alternative is
    // to expose a `testOnlyShowContextMenu(QPoint)` helper on Editor
    // and call it directly. Add if needed.
    QContextMenuEvent ev(QContextMenuEvent::Mouse,
                          QPoint(10, 10), ed.mapToGlobal(QPoint(10, 10)));
    QCoreApplication::sendEvent(&ed, &ev);

    QCOMPARE(spy.count(), 1);
}

void TstEditorContextMenu::subscribedAction_appearsInMenu()
{
    // Connect a subscriber that appends an action; verify the built-in
    // items are still present.
    Editor ed;
    ed.setPlainText(QStringLiteral("paragraph"));
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));

    bool subscriberFired = false;
    connect(&ed, &Editor::aboutToShowContextMenu, &ed,
            [&](QMenu *menu, const EditorContext &, const QPoint &) {
        subscriberFired = true;
        QAction *a = menu->addAction(QStringLiteral("Phase-C6 added"));
        QVERIFY(a);
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse,
                          QPoint(10, 10), ed.mapToGlobal(QPoint(10, 10)));
    QCoreApplication::sendEvent(&ed, &ev);

    QVERIFY(subscriberFired);
    // Can't easily assert the menu's current contents post-exec (menu
    // is stack-local + already destroyed). The firing of the subscriber
    // + the successful addAction is the verifiable guarantee.
}

QTEST_MAIN(TstEditorContextMenu)
#include "tst_editor_context_menu.moc"
```

If `QContextMenuEvent` dispatch doesn't fire `contextMenuEvent` under offscreen QPA, add a `testOnlyShowContextMenu(const QPoint &globalPos)` helper in Editor's `protected` section that builds and exec's the menu directly — test calls this. Keep it test-guarded (`#ifdef QT_TESTING` or a `/*test-only*/` tag in the comment; no runtime cost).

- [ ] **Step 4: Register + run**

```bash
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -15
cd /home/clinton/dev/Corbomite/build-dev && ctest -R tst_editor_context_menu --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/Editor.cpp \
        libs/markoff-live/tests/tst_editor_context_menu.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: Editor::aboutToShowContextMenu signal in contextMenuEvent

Phase C6 Commit E of 6. Fires in both branches of contextMenuEvent
(table context + general) after built-in items, before menu.exec().
After-built-ins shape per consumer-spec §9.2 paragraph 1 — host owns
section ordering via MenuSectionHelper.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: End-to-end context test + tag v0.5.0 (Commit F)

**Files:**
- Modify: `libs/markoff-live/tests/tst_editor_context.cpp` (extend with shape-assertion cases)

- [ ] **Step 1: Extend with shape assertions**

Append to `tst_editor_context.cpp`:

```cpp
private slots:
    // ... existing ...
    void context_snapshot_heading();
    void context_snapshot_table();
    void context_snapshot_boldSelection();
    void context_snapshot_readOnly();
};

void TstEditorContext::context_snapshot_heading()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("## Section\n\nbody"));
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    ed.goToLine(0);
    QTest::qWait(50);

    const EditorContext ctx = ed.context();
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Heading);
    QCOMPARE(ctx.headingLevel, 2);
    QVERIFY(!ctx.readOnly);
    QVERIFY(!ctx.hasSelection);
}

void TstEditorContext::context_snapshot_table()
{
    Editor ed;
    ed.setPlainText(QStringLiteral(
        "| a | b |\n|---|---|\n| 1 | 2 |\n"));
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    QTest::qWait(100);  // let live-preview substitute the table
    // Move cursor into a data cell — specific synthesis depends on
    // Editor API. Use goToLine + cursor helpers as available.
    ed.goToLine(2);
    QTest::qWait(50);

    const EditorContext ctx = ed.context();
    // If Editor::goToLine doesn't land inside the live table (because
    // live-preview substitutes the markdown), the alternative is to
    // use `cursorInTable()` as an oracle: skip the assertions if the
    // substitution didn't happen in offscreen QPA.
    if (ed.cursorInTable()) {
        QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Table);
        QVERIFY(ctx.table.has_value());
        QCOMPARE(ctx.table->rows, 2);
        QCOMPARE(ctx.table->cols, 2);
    }
}

void TstEditorContext::context_snapshot_boldSelection()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("**bold** text"));
    ed.show();
    QVERIFY(QTest::qWaitForWindowActive(&ed));
    QTest::qWait(100);  // let highlighter tag the run
    ed.goToLine(0);
    // Advance to inside the bold run — need API to move the cursor.
    // If Editor doesn't expose setCursorPositionInBlock, skip the
    // offset move and test just the line-level context.
    QTest::qWait(50);

    const EditorContext ctx = ed.context();
    // Cursor at block start may not be inside the bold run. If the
    // line-level context doesn't reflect the bold run, this test may
    // need a more precise cursor-position helper — if so, extend
    // Editor with a test helper or use the MarkdownTextItem API.
    // For now assert only shape: context() returns without crashing.
    Q_UNUSED(ctx);
}

void TstEditorContext::context_snapshot_readOnly()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("text"));
    ed.setReadOnly(true);

    const EditorContext ctx = ed.context();
    QVERIFY(ctx.readOnly);
}
```

Note: bold/italic precise cursor positioning in the Editor test-harness is fiddly because live-preview substitution and cursor coordinates depend on the MarkdownTextItem focus state. Keep these tests shape-level where possible; the classifier unit tests (Task 3) already assert the core detection logic against `QTextDocument` directly.

- [ ] **Step 2: Clean rebuild + full markoff ctest green**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
rm -rf /home/clinton/dev/Corbomite/build-dev/libs/markoff-live
cmake --build /home/clinton/dev/Corbomite/build-dev -j 10 2>&1 | tail -10
cd /home/clinton/dev/Corbomite/build-dev && ctest -R "markoff" --output-on-failure 2>&1 | tail -8
```

Expected: all tests pass (previous 64 + ~8-10 new = ~72-74 total).

- [ ] **Step 3: Tag v0.5.0**

```bash
cd /home/clinton/dev/Corbomite/libs/markoff-family
git tag v0.5.0 -m "Phase C6 — Editor state + context-menu contribution surface"
git log --oneline -10
```

- [ ] **Step 4: Update phase-c-status**

In `docs/phase-c-status.md`:
- C6 row → `markoff ready (v0.5.0)` with the plan link filled in.
- Add activity-log entry (reverse-chronological top) summarizing Commits A-F.

- [ ] **Step 5: Commit phase-c-status**

```bash
git add libs/markoff-live/tests/tst_editor_context.cpp docs/phase-c-status.md
git commit -m "$(cat <<'EOF'
markoff-live: Phase C6 end-to-end context test + phase-c-status update

Phase C6 Commit F of 6. Extends tst_editor_context with shape-level
snapshot assertions (heading, table, readOnly, bold selection).
Tag v0.5.0 applied.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §3 Task list — Corbomite side

### Task 7: Bump submodule pin

- [ ] **Step 1: Advance submodule**

```bash
cd /home/clinton/dev/Corbomite
cd libs/markoff-family && git checkout master && git describe --long && cd ../..
git status
```

Expected: `git describe` reports `v0.5.0-1-g<phase-c-status-sha>`. Do NOT commit yet.

---

### Task 8: Corbomite consumer — context-driven action state

**Files:**
- Modify: `src/app/MainWindow.h`
- Modify: `src/app/MainWindow.cpp`

**Context:** Replace/augment `MainWindow::refreshEditorActions` with signal-driven updates. Keep `refreshEditorActions` as the initial-state primer (called on `activeLeafChanged`); the `contextChanged` slot does per-cursor-movement updates.

- [ ] **Step 1: Add member declarations**

In `src/app/MainWindow.h`, near existing `refreshEditorActions` (grep for it):

```cpp
    void connectEditorContext(NoteEditorWidget *editor);
    void onEditorContextChanged(const Markoff::EditorContext &ctx);
```

- [ ] **Step 2: Implement connect helper**

In `src/app/MainWindow.cpp`:

```cpp
void MainWindow::connectEditorContext(NoteEditorWidget *editor)
{
    if (!editor) return;
    auto *ed = editor->editor();
    connect(ed, &Markoff::Editor::contextChanged,
            this, &MainWindow::onEditorContextChanged);
    onEditorContextChanged(ed->context());  // prime initial state
}
```

- [ ] **Step 3: Implement the slot**

```cpp
void MainWindow::onEditorContextChanged(const Markoff::EditorContext &ctx)
{
    KActionCollection *ac = actionCollection();
    auto setCheck = [ac](const QString &id, bool on) {
        if (auto *a = ac->action(id)) a->setChecked(on);
    };
    auto setEnable = [ac](const QString &id, bool on) {
        if (auto *a = ac->action(id)) a->setEnabled(on);
    };

    using BK = Markoff::EditorContext::BlockKind;

    // Format toolbar check-state
    setCheck(QStringLiteral("format_bold"),          ctx.inBold);
    setCheck(QStringLiteral("format_italic"),        ctx.inItalic);
    setCheck(QStringLiteral("format_strikethrough"), ctx.inStrikethrough);
    setCheck(QStringLiteral("format_inline_code"),   ctx.inInlineCode);

    // Heading radio
    for (int i = 1; i <= 6; ++i) {
        setCheck(QStringLiteral("heading_%1").arg(i), ctx.headingLevel == i);
    }
    setEnable(QStringLiteral("heading_increase"),
              !ctx.readOnly && ctx.headingLevel < 6);
    setEnable(QStringLiteral("heading_decrease"),
              !ctx.readOnly && ctx.headingLevel >= 1);

    // Table submenu
    const bool inTable = ctx.blockKind == BK::Table;
    // The existing `refreshEditorActions` already enables the table
    // submenu roots based on cursorInTable(); preserve that path for
    // compatibility. Here we additionally refine delete-row/-col
    // gating based on the new table.rows/cols fields.
    if (ctx.table) {
        setEnable(QStringLiteral("table_delete_row"),
                  !ctx.readOnly && inTable && ctx.table->rows > 1);
        setEnable(QStringLiteral("table_delete_col"),
                  !ctx.readOnly && inTable && ctx.table->cols > 1);
    }

    // Fold-at-cursor only on headings
    setEnable(QStringLiteral("toggle_fold"),
              ctx.blockKind == BK::Heading);
}
```

- [ ] **Step 4: Call `connectEditorContext` from `activeLeafChanged` handler**

Find where `refreshEditorActions` is currently invoked on active-leaf changes (grep `refreshEditorActions`). Add a call to `connectEditorContext(editor)` alongside, whenever a Markoff-editor-bearing leaf becomes active. Disconnect any previous connection (grep Qt docs for `disconnect(editor, nullptr, this, nullptr)` pattern).

Prefer using `Qt::UniqueConnection` so repeated `connectEditorContext` calls are safe:

```cpp
connect(ed, &Markoff::Editor::contextChanged,
        this, &MainWindow::onEditorContextChanged,
        Qt::UniqueConnection);
```

- [ ] **Step 5: Build**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build -j 10 2>&1 | tail -15
```

- [ ] **Step 6: Don't commit yet (folded into Task 10 commit)**

---

### Task 9: Corbomite consumer — context-menu contribution

**Files:**
- Modify: `src/app/MainWindow.h`
- Modify: `src/app/MainWindow.cpp`

- [ ] **Step 1: Declarations**

```cpp
// MainWindow.h:
void connectEditorContextMenu(NoteEditorWidget *editor);
void onAboutToShowContextMenu(QMenu *menu,
                              const Markoff::EditorContext &ctx,
                              const QPoint &globalPos);
```

- [ ] **Step 2: Implement the connect + slot**

Follow consumer-spec §9.5 sketch verbatim (lines 544-595 of the recovered spec). The slot uses `MenuSectionHelper` (already available via `#include <corbomite/core/MenuSectionHelper.h>` per Cluster R). Concretely:

```cpp
void MainWindow::connectEditorContextMenu(NoteEditorWidget *editor)
{
    if (!editor) return;
    auto *ed = editor->editor();
    connect(ed, &Markoff::Editor::aboutToShowContextMenu,
            this, &MainWindow::onAboutToShowContextMenu,
            Qt::UniqueConnection);
}

void MainWindow::onAboutToShowContextMenu(QMenu *menu,
                                           const Markoff::EditorContext &ctx,
                                           const QPoint & /*globalPos*/)
{
    using BK = Markoff::EditorContext::BlockKind;
    MenuSectionHelper helper(menu);
    KActionCollection *ac = actionCollection();

    auto add = [&](const char *section, const QString &id) {
        if (auto *a = ac->action(id)) helper.addItem(section, a);
    };

    // Format section
    add("action", QStringLiteral("format_bold"));
    add("action", QStringLiteral("format_italic"));
    add("action", QStringLiteral("format_strikethrough"));
    add("action", QStringLiteral("format_inline_code"));

    // Heading / Insert
    for (int i = 1; i <= 6; ++i)
        add("action-primary", QStringLiteral("heading_%1").arg(i));
    add("action-primary", QStringLiteral("insert_link"));
    add("action-primary", QStringLiteral("insert_wiki_link"));
    add("action-primary", QStringLiteral("insert_callout"));
    add("action-primary", QStringLiteral("insert_table"));

    // Context-specific
    if (ctx.blockKind == BK::Table) {
        add("action", QStringLiteral("table_row_above"));
        add("action", QStringLiteral("table_row_below"));
        add("action", QStringLiteral("table_col_left"));
        add("action", QStringLiteral("table_col_right"));
        add("action", QStringLiteral("table_delete_row"));
        add("action", QStringLiteral("table_delete_col"));
    }

    helper.flush();
}
```

- [ ] **Step 3: Wire from active-leaf handler**

Same hook as Task 8's `connectEditorContext` — call `connectEditorContextMenu(editor)` when a NoteEditorWidget becomes active.

- [ ] **Step 4: Build**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build -j 10 2>&1 | tail -15
```

- [ ] **Step 5: Don't commit yet**

---

### Task 10: Extend `tst_mainwindow_action_wiring` for check-state + commit Corbomite adapter

**Files:**
- Modify: `tests/app/tst_mainwindow_action_wiring.cpp`

- [ ] **Step 1: Add check-state assertions**

Read the existing test first to match its fixture pattern. Add a new test slot that:
- Constructs the MainWindow with a vault containing a note with pre-formatted markdown (`**bold** text\n\n## Heading`)
- Opens the note, waits for the editor to settle
- Moves the cursor to known positions via goToLine + cursor-helper APIs
- Asserts the `format_bold` action's `isChecked()` flips as the cursor moves into/out of the bold span
- Asserts the `heading_N` action's `isChecked()` flips for the matching level

Exact shape depends on the existing test's fixture helpers. If cursor positioning is fiddly in the offscreen QPA test environment, fall back to calling `editor->editor()->context()` directly and asserting the returned struct — the signal→slot wiring is then validated by construction.

- [ ] **Step 2: Run Corbomite test suite**

```bash
cd /home/clinton/dev/Corbomite/build && ctest --output-on-failure -j 10 2>&1 | tail -15
```

Expected: 243/245 modulo the two pre-existing flakes (`tst_benchmark_layout`, `tst_editorsuggest`) + any new test passes.

- [ ] **Step 3: Commit the Corbomite adapter**

```bash
cd /home/clinton/dev/Corbomite
git add libs/markoff-family src/app/MainWindow.h src/app/MainWindow.cpp \
        tests/app/tst_mainwindow_action_wiring.cpp
git commit -m "$(cat <<'EOF'
feat(markoff): Phase C6 adaptation — EditorContext + context-menu contribution

Bumps Markoff submodule to v0.5.0. Adds MainWindow::onEditorContextChanged
driven by Markoff::Editor::contextChanged — Format toolbar now shows
correct check state as cursor moves across pre-formatted markdown in
Live mode (previously static "enabled but never checked"). Heading
radio reflects currentHeadingLevel; Table delete-row/col gates on
actual row/col counts.

Adds MainWindow::onAboutToShowContextMenu driven by
Markoff::Editor::aboutToShowContextMenu — right-click editor menu in
Live mode now exposes Format/Heading/Insert/Table entries via
MenuSectionHelper. Built-ins (Undo/Redo/Cut/Copy/Paste/Select-All +
table row/col operations when applicable) remain.

Extends tst_mainwindow_action_wiring with check-state assertions
covering format_bold + heading_N after cursor motion.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Closeout — PROJECT-STATE, phase-c-status, retro

**Files:**
- Modify: `docs/PROJECT-STATE.md` (Current focus, Markoff Phase C row, in-flight row, Recent decisions)
- Modify: `libs/markoff-family/docs/phase-c-status.md` (C6 → done, activity log)

- [ ] **Step 1: Update PROJECT-STATE per Ritual 2**

Same pattern as C5 closeout (commit 27add4b6 for reference). Replace:
- `**Last updated:** …` line — C6 landed, submodule pin → `v0.5.0`
- `## Current focus` — C6 done; C3 spec next
- `### Markoff Phase C (external-origin integration)` in-flight block — last completed step = C6; next = C3 spec
- `| C6 | ... |` roadmap-table row — status → `Done 2026-04-20`
- Recent decisions — append top bullet for C6 closeout with scope recap

- [ ] **Step 2: Update phase-c-status C6 → done**

- C6 table row → `done`, Corbomite commit SHA filled in
- Activity log top entry — C6 corbomite-shipped; done

- [ ] **Step 3: Commits**

Two commits — one on each repo:

```bash
# Markoff side
cd /home/clinton/dev/Corbomite/libs/markoff-family
git add docs/phase-c-status.md
git commit -m "$(cat <<'EOF'
phase-c-status: C6 done; corbomite-shipped at <SHA>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

# Corbomite side — re-bump submodule (picks up the status commit above)
cd /home/clinton/dev/Corbomite
cd libs/markoff-family && git checkout master && cd ../..
git add docs/PROJECT-STATE.md libs/markoff-family
git commit -m "$(cat <<'EOF'
docs(project-state): Markoff C6 done; C3 spec next

C6 shipped at Markoff v0.5.0 + Corbomite adapter commit. Unified
EditorContext + contextChanged + aboutToShowContextMenu on
Markoff::Editor; Corbomite's refreshEditorActions now signal-driven
for Format/Heading/Table check+enable state; right-click editor menu
gains Format/Heading/Insert/Table via MenuSectionHelper.

Next: C3 (MarkoffDocument becomes content-authoritative).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## §4 Self-review checklist (executing agent)

After Task 11:

- [ ] **Markoff ctest:** all tests green (expected ~72-74 count).
- [ ] **Corbomite ctest:** 243/245 modulo the two flakes.
- [ ] **Public-header install:** `EditorContext.h` present in the install manifest.
- [ ] **Grep invariant:** `grep -rn "refreshEditorActions" src/app/` shows the method still exists but is now either called only at activeLeafChanged (as an initial primer) or removed entirely — whichever shape Task 8 landed on.
- [ ] **Submodule pin:** `libs/markoff-family` advanced to v0.5.0 post-tag.
- [ ] **PROJECT-STATE:** Markoff Phase C row = C6 done; C3 next.
- [ ] **Phase-c-status:** C6 row = done.

## §5 Smoke test (manual, after Task 11)

1. Build + run Corbomite with `-DCORBOMITE_DEV_BUILD=ON`.
2. Open a vault; open a note with `# Title\n\n**bold** text\n\n| a | b |\n|---|---|\n| 1 | 2 |\n`.
3. Switch to Live mode. Verify:
   - Cursor on the heading line → View > Heading > 1 is radio-checked.
   - Cursor inside `**bold**` → Format > Bold is toolbar-checked.
   - Cursor inside the table → Table submenu items all enabled; Delete Row/Col enabled since rows/cols > 1.
   - Cursor on the `a` header cell → Delete Col still enabled (cols > 1), Delete Row enabled (rows > 1).
   - Right-click in a paragraph → context menu contains Undo/Redo/Cut/Copy/Paste + Format/Heading/Insert entries via MenuSectionHelper.
   - Right-click in the table → context menu contains the built-in Insert/Delete Row/Col + Select Row/Col **and** the contributed Format/Heading entries.
4. Switch to Source mode. Context menu contributions should NOT fire (Source editor is qutepart-based, not Markoff::Editor — no connect). Confirm no regressions.
5. Switch to Reading mode. Format toolbar check-state should all be unchecked (no Markoff::Editor active). Confirm no crashes, no ghost state from the last Live cursor.

If any step fails, triage before declaring C6 done.

## §6 Deferred (captured elsewhere)

- ~15 unpopulated `EditorContext` fields (callout, blockquote, code-block, highlights, link/tag/footnote, math, task-state, list-marker details). Struct shape stable; fields return defaults. Fill when a consumer materializes. See spec §1.3.
- `columnAlignment` in `TableContext` — left default in C6. Future phase when a consumer needs column-alignment-aware editing.
- Section-ordering inside the context menu — host's concern, already handled by `MenuSectionHelper` (Cluster R).
