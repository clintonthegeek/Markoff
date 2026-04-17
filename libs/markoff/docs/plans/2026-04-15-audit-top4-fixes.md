# Audit Top-4 Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four broken public API methods on `Markoff::Editor` that return item-local results instead of document-global results: `selectAll()`, `cut()`, `goToLine()`, `cursorLine()`/`cursorColumn()`.

**Architecture:** Add coordinate-mapping methods to `SceneCoordinator` that translate between global source-line positions and (item, local block) pairs. Rewrite the four broken Editor methods to use these mappings. Extract the inter-item separator logic into a shared helper so `toMarkdown()` and the mapping methods can't disagree.

**Tech Stack:** Qt6 (Core, Gui, Widgets, Test) >= 6.8; C++20; `QT_QPA_PLATFORM=offscreen` for tests.

**Spec:** [`../specs/2026-04-15-audit-top4-fixes-design.md`](../specs/2026-04-15-audit-top4-fixes-design.md). Read it before starting.

---

## File structure

### New files

| File | Purpose |
|------|---------|
| `tests/tst_global_coordinates.cpp` | Tests for coordinate mapping and all four Editor fixes |

### Modified files

| File | Purpose |
|------|---------|
| `src/SceneCoordinator.h` | Add `GlobalPosition`, `ItemPosition` structs; new public methods |
| `src/SceneCoordinator.cpp` | Implement mapping methods; extract `interItemNewlines()` helper; refactor `toMarkdown()` |
| `src/Editor.cpp` | Rewrite `selectAll()`, `cut()`, `goToLine()`, `cursorLine()` |
| `tests/CMakeLists.txt` | Register new test executable |

---

## Task 1: Extract `interItemNewlines` helper and refactor `toMarkdown()`

**Files:**
- Modify: `src/SceneCoordinator.h`
- Modify: `src/SceneCoordinator.cpp:124-139`

This extracts the separator logic into a reusable function so the coordinate mapping (Task 2) and serialization stay in sync.

- [ ] **Step 1: Add helper declaration to SceneCoordinator.h**

In `src/SceneCoordinator.h`, add inside the `private:` section, before the `MarkdownTextItem *createTextItem` line:

```cpp
    static int interItemNewlines(bool prevIsText, bool currIsText);
```

- [ ] **Step 2: Implement the helper and refactor `toMarkdown()`**

In `src/SceneCoordinator.cpp`, add the helper immediately above the existing `toMarkdown()` method:

```cpp
int SceneCoordinator::interItemNewlines(bool prevIsText, bool currIsText)
{
    return (prevIsText && currIsText) ? 1 : 2;
}
```

Then refactor `toMarkdown()` to use it. Replace the entire method body:

```cpp
QString SceneCoordinator::toMarkdown() const
{
    QString result;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i > 0) {
            int nlCount = interItemNewlines(m_items[i - 1]->isTextItem(),
                                            m_items[i]->isTextItem());
            for (int n = 0; n < nlCount; ++n)
                result += QLatin1Char('\n');
        }
        result += m_items[i]->toMarkdown();
    }
    return result;
}
```

- [ ] **Step 3: Build and run existing tests to verify no regression**

Run:
```bash
cd build && cmake --build . --target markoff && ctest -R markoff --output-on-failure
```
Expected: All existing tests pass. `toMarkdown()` behavior is identical.

- [ ] **Step 4: Commit**

```bash
git add src/SceneCoordinator.h src/SceneCoordinator.cpp
git commit -m "refactor(markoff): extract interItemNewlines helper from toMarkdown

Prepares for global coordinate mapping — the separator logic must be
shared between toMarkdown() and the new mapping methods."
```

---

## Task 2: Add `sourceLineCount()` helper

**Files:**
- Modify: `src/SceneCoordinator.h`
- Modify: `src/SceneCoordinator.cpp`

Counts the number of source lines a single `MarkdownTextItem` contributes. Must handle U+FFFC inline objects that expand to multi-line source (display math `$$\n...\n$$`). This logic already exists in `allMarkdown()` and `ensureHeadingMap()` — we extract it as a standalone helper.

- [ ] **Step 1: Add declaration to SceneCoordinator.h**

In `src/SceneCoordinator.h`, add in the `private:` section, after the `interItemNewlines` declaration:

```cpp
    static int sourceLineCount(const MarkdownTextItem *item);
```

- [ ] **Step 2: Implement `sourceLineCount()`**

In `src/SceneCoordinator.cpp`, add after the `interItemNewlines` method. This must include the header for `MathTextObject` to access `RawProperty`:

At the top of the file, verify that `#include "MathTextObject.h"` is present (it already is — used by `ensureHeadingMap`). Then add:

```cpp
int SceneCoordinator::sourceLineCount(const MarkdownTextItem *item)
{
    QTextDocument *doc = item->document();
    int lines = 0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QString blockText = block.text();
        if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
            lines += 1 + blockText.count(QLatin1Char('\n'));
        } else {
            int blockNewlines = 0;
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                const QString raw = frag.charFormat()
                    .property(MathTextObject::RawProperty).toString();
                const QString t = frag.text();
                if (!raw.isEmpty() && t.size() == 1
                    && t.at(0) == QChar::ObjectReplacementCharacter) {
                    blockNewlines += raw.count(QLatin1Char('\n'));
                } else {
                    for (QChar c : t)
                        if (c == QLatin1Char('\n')) ++blockNewlines;
                }
            }
            lines += 1 + blockNewlines;
        }
    }
    return lines;
}
```

The count is: number of blocks, plus extra lines contributed by multi-line U+FFFC expansions. A single-block item with no newlines returns 1. A 3-block item returns 3. A 1-block item containing `$$\nfoo\n$$` returns 3.

- [ ] **Step 3: Build to verify compilation**

Run:
```bash
cd build && cmake --build . --target markoff
```
Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/SceneCoordinator.h src/SceneCoordinator.cpp
git commit -m "feat(markoff): add sourceLineCount helper for text items

Counts source lines including multi-line U+FFFC expansion. Needed by
the global coordinate mapping methods."
```

---

## Task 3: Add `globalPositionOf()` and `itemAtGlobalLine()`

**Files:**
- Modify: `src/SceneCoordinator.h`
- Modify: `src/SceneCoordinator.cpp`

These are the core coordinate mapping methods.

- [ ] **Step 1: Add structs and method declarations to SceneCoordinator.h**

In `src/SceneCoordinator.h`, add the structs in the `public:` section, right after the `explicit SceneCoordinator(...)` constructor and before `void loadMarkdown(...)`:

```cpp
    struct GlobalPosition {
        int line = 1;    // 1-based source line in flat markdown
        int column = 1;  // 1-based column within that line
    };

    struct ItemPosition {
        MarkdownTextItem *item = nullptr;
        int localBlockNumber = 0;
    };

    GlobalPosition globalPositionOf(const MarkdownTextItem *item,
                                     int localBlockNumber,
                                     int columnInBlock) const;

    ItemPosition itemAtGlobalLine(int globalLine) const;
```

You will also need a forward declaration of `MarkdownTextItem` — it's already present near the top of the header (`class MarkdownTextItem;`).

- [ ] **Step 2: Implement `globalPositionOf()`**

In `src/SceneCoordinator.cpp`, add after the `sourceLineCount` method:

```cpp
SceneCoordinator::GlobalPosition
SceneCoordinator::globalPositionOf(const MarkdownTextItem *item,
                                    int localBlockNumber,
                                    int columnInBlock) const
{
    int line = 1; // 1-based
    for (int i = 0; i < m_items.size(); ++i) {
        if (i > 0)
            line += interItemNewlines(m_items[i - 1]->isTextItem(),
                                      m_items[i]->isTextItem());

        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            if (mti == item) {
                // Walk blocks within this item up to localBlockNumber.
                QTextDocument *doc = mti->document();
                for (QTextBlock block = doc->begin();
                     block.isValid() && block.blockNumber() < localBlockNumber;
                     block = block.next()) {
                    const QString blockText = block.text();
                    if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                        line += 1 + blockText.count(QLatin1Char('\n'));
                    } else {
                        int blockNewlines = 0;
                        for (auto it = block.begin(); !it.atEnd(); ++it) {
                            const QTextFragment frag = it.fragment();
                            if (!frag.isValid()) continue;
                            const QString raw = frag.charFormat()
                                .property(MathTextObject::RawProperty).toString();
                            const QString t = frag.text();
                            if (!raw.isEmpty() && t.size() == 1
                                && t.at(0) == QChar::ObjectReplacementCharacter) {
                                blockNewlines += raw.count(QLatin1Char('\n'));
                            } else {
                                for (QChar c : t)
                                    if (c == QLatin1Char('\n')) ++blockNewlines;
                            }
                        }
                        line += 1 + blockNewlines;
                    }
                }
                return {line, columnInBlock + 1};
            }
            line += sourceLineCount(mti);
        } else {
            line += 1 + m_items[i]->toMarkdown().count(QLatin1Char('\n'));
        }
    }
    return {line, columnInBlock + 1};
}
```

- [ ] **Step 3: Implement `itemAtGlobalLine()`**

In `src/SceneCoordinator.cpp`, add immediately after `globalPositionOf`:

```cpp
SceneCoordinator::ItemPosition
SceneCoordinator::itemAtGlobalLine(int globalLine) const
{
    int line = 1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i > 0)
            line += interItemNewlines(m_items[i - 1]->isTextItem(),
                                      m_items[i]->isTextItem());

        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            int itemLines = sourceLineCount(mti);
            if (globalLine < line + itemLines) {
                // Target is within this item. Walk blocks to find the local one.
                int remaining = globalLine - line;
                QTextDocument *doc = mti->document();
                for (QTextBlock block = doc->begin();
                     block.isValid(); block = block.next()) {
                    if (remaining <= 0)
                        return {mti, block.blockNumber()};

                    const QString blockText = block.text();
                    int blockLines;
                    if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                        blockLines = 1 + blockText.count(QLatin1Char('\n'));
                    } else {
                        int blockNewlines = 0;
                        for (auto it = block.begin(); !it.atEnd(); ++it) {
                            const QTextFragment frag = it.fragment();
                            if (!frag.isValid()) continue;
                            const QString raw = frag.charFormat()
                                .property(MathTextObject::RawProperty).toString();
                            const QString t = frag.text();
                            if (!raw.isEmpty() && t.size() == 1
                                && t.at(0) == QChar::ObjectReplacementCharacter) {
                                blockNewlines += raw.count(QLatin1Char('\n'));
                            } else {
                                for (QChar c : t)
                                    if (c == QLatin1Char('\n')) ++blockNewlines;
                            }
                        }
                        blockLines = 1 + blockNewlines;
                    }
                    remaining -= blockLines;
                }
                // Past last block — clamp to last block
                QTextBlock last = doc->lastBlock();
                return {mti, last.isValid() ? last.blockNumber() : 0};
            }
            line += itemLines;
        } else {
            int itemLines = 1 + m_items[i]->toMarkdown().count(QLatin1Char('\n'));
            if (globalLine < line + itemLines) {
                // Target is within a block item. Return nullptr — can't place cursor here.
                return {nullptr, 0};
            }
            line += itemLines;
        }
    }
    // Past end of document — return last text item at its last block.
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i]->isTextItem()) {
            auto *mti = static_cast<MarkdownTextItem *>(m_items[i]);
            QTextBlock last = mti->document()->lastBlock();
            return {mti, last.isValid() ? last.blockNumber() : 0};
        }
    }
    return {nullptr, 0};
}
```

- [ ] **Step 4: Build to verify compilation**

Run:
```bash
cd build && cmake --build . --target markoff
```
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add src/SceneCoordinator.h src/SceneCoordinator.cpp
git commit -m "feat(markoff): add global coordinate mapping to SceneCoordinator

globalPositionOf() maps (item, localBlock, column) to 1-based source
line/column. itemAtGlobalLine() does the inverse. Both use the same
separator and U+FFFC expansion logic as toMarkdown()."
```

---

## Task 4: Write tests for coordinate mapping

**Files:**
- Create: `tests/tst_global_coordinates.cpp`
- Modify: `tests/CMakeLists.txt`

Write the tests BEFORE fixing the Editor methods, so we verify the foundation independently.

- [ ] **Step 1: Create the test file**

Create `tests/tst_global_coordinates.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QClipboard>
#include <QTest>
#include <markoff/Editor.h>
#include "SceneCoordinator.h"
#include "MarkdownTextItem.h"
#include "SelectionManager.h"
#include "SelectionScene.h"

using namespace Markoff;

// =========================================================================
// Coordinate mapping tests
// =========================================================================

class TstGlobalCoordinates : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void singleItemLineMapping();
    void twoTextItemsLineMapping();
    void textBlockTextLineMapping();
    void itemAtGlobalLineRoundTrip();
    void itemAtGlobalLinePastEnd();
    void itemAtGlobalLineInBlockItem();
};

void TstGlobalCoordinates::singleItemLineMapping()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("line1\nline2\nline3"));
    auto *coord = editor.coordinatorForTesting();
    QCOMPARE(coord->items().size(), 1);

    auto *ti = static_cast<MarkdownTextItem *>(coord->items().first());
    // First line
    auto gp0 = coord->globalPositionOf(ti, 0, 0);
    QCOMPARE(gp0.line, 1);
    QCOMPARE(gp0.column, 1);

    // Second line, column 3
    auto gp1 = coord->globalPositionOf(ti, 1, 3);
    QCOMPARE(gp1.line, 2);
    QCOMPARE(gp1.column, 4); // 1-based

    // Third line
    auto gp2 = coord->globalPositionOf(ti, 2, 0);
    QCOMPARE(gp2.line, 3);
}

void TstGlobalCoordinates::twoTextItemsLineMapping()
{
    // A table in the middle forces two text items with a block between them.
    Editor editor;
    editor.setPlainText(QStringLiteral(
        "alpha\nbravo\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\ncharlie\ndelta"));
    auto *coord = editor.coordinatorForTesting();

    // Find the second text item (after the table).
    MarkdownTextItem *secondText = nullptr;
    int textCount = 0;
    for (auto *item : coord->items()) {
        if (item->isTextItem()) {
            ++textCount;
            if (textCount == 2)
                secondText = static_cast<MarkdownTextItem *>(item);
        }
    }
    if (!secondText) {
        QSKIP("Splitter did not produce two text items — table not recognized");
        return;
    }

    // "charlie" is the first line of the second text item.
    // Source: "alpha\nbravo" = 2 lines, then \n\n (2 newlines for block sep),
    // table = 3 lines, then \n\n (2 newlines for block sep), then "charlie".
    // Global line for "charlie" = 2 + 2 + 3 + 2 + 0 ... let's just verify
    // it's greater than the first item's last line.
    auto *firstText = static_cast<MarkdownTextItem *>(coord->items().first());
    auto lastOfFirst = coord->globalPositionOf(firstText, 1, 0); // "bravo"
    auto firstOfSecond = coord->globalPositionOf(secondText, 0, 0); // "charlie"
    QVERIFY(firstOfSecond.line > lastOfFirst.line);
    QCOMPARE(firstOfSecond.column, 1);
}

void TstGlobalCoordinates::textBlockTextLineMapping()
{
    // Verify the inter-item separator count by checking absolute line numbers.
    Editor editor;
    editor.setPlainText(QStringLiteral("one\n\n| H |\n|---|\n| V |\n\ntwo"));
    auto *coord = editor.coordinatorForTesting();

    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No trailing text item");
        return;
    }
    auto gp = coord->globalPositionOf(lastText, 0, 0);
    // "one" = line 1, blank = line 2, table rows = lines 3-5, blank = line 6, "two" = line 7
    QCOMPARE(gp.line, 7);
}

void TstGlobalCoordinates::itemAtGlobalLineRoundTrip()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    auto *coord = editor.coordinatorForTesting();
    auto *ti = static_cast<MarkdownTextItem *>(coord->items().first());

    // For each block, round-trip through globalPositionOf → itemAtGlobalLine.
    for (int b = 0; b < 3; ++b) {
        auto gp = coord->globalPositionOf(ti, b, 0);
        auto ip = coord->itemAtGlobalLine(gp.line);
        QCOMPARE(ip.item, ti);
        QCOMPARE(ip.localBlockNumber, b);
    }
}

void TstGlobalCoordinates::itemAtGlobalLinePastEnd()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("short"));
    auto *coord = editor.coordinatorForTesting();

    // Line 999 should clamp to last item, last block.
    auto ip = coord->itemAtGlobalLine(999);
    QVERIFY(ip.item != nullptr);
    QCOMPARE(ip.localBlockNumber, 0); // single-line doc
}

void TstGlobalCoordinates::itemAtGlobalLineInBlockItem()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("text\n\n| H |\n|---|\n| V |\n\nmore"));
    auto *coord = editor.coordinatorForTesting();

    // Line 3 is inside the table block. itemAtGlobalLine should return nullptr
    // (can't place cursor in a block item).
    auto ip = coord->itemAtGlobalLine(3);
    QCOMPARE(ip.item, nullptr);
}

QTEST_MAIN(TstGlobalCoordinates)
#include "tst_global_coordinates.moc"
```

- [ ] **Step 2: Register test in CMakeLists.txt**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_global_coordinates tst_global_coordinates.cpp)
add_test(NAME tst_markoff_global_coordinates COMMAND tst_markoff_global_coordinates)
target_link_libraries(tst_markoff_global_coordinates PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_global_coordinates PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_global_coordinates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run the coordinate tests**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates
```
Expected: All 6 tests pass. If any fail due to table splitting not producing expected item counts, the test uses `QSKIP` — adjust the markdown input to match what `MarkdownSplitter` actually produces.

- [ ] **Step 4: Commit**

```bash
git add tests/tst_global_coordinates.cpp tests/CMakeLists.txt
git commit -m "test(markoff): add global coordinate mapping tests

Tests globalPositionOf/itemAtGlobalLine round-trip, multi-item line
numbering, past-end clamping, and block-item null return."
```

---

## Task 5: Fix `selectAll()`

**Files:**
- Modify: `src/Editor.cpp`

- [ ] **Step 1: Add test case to `tst_global_coordinates.cpp`**

Add a new test class section to the file. First, add to the class declaration:

```cpp
    void selectAllCrossBoundary();
```

Then add the implementation:

```cpp
void TstGlobalCoordinates::selectAllCrossBoundary()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("alpha\n\n| H |\n|---|\n| V |\n\nbravo"));
    editor.show();
    QApplication::processEvents();

    editor.selectAll();
    QApplication::processEvents();

    auto *mgr = editor.coordinatorForTesting()->items().isEmpty()
        ? nullptr
        : static_cast<SelectionScene *>(editor.scene())->selectionManager();
    // After selectAll, the selection manager should be in cross-boundary mode.
    // Verify by copying — the clipboard should contain the full document text.
    editor.copy();
    QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("alpha")));
    QVERIFY(clipboard.contains(QStringLiteral("bravo")));
}
```

Note: `editor.scene()` returns `QGraphicsScene *`. We need to cast to `SelectionScene *`. Since this test file already includes `SelectionScene.h`, this is fine. However, `Editor::scene()` is a `QGraphicsView` method returning `QGraphicsScene *`. We can verify through the copy result instead of directly inspecting the manager.

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates selectAllCrossBoundary
```
Expected: FAIL — the current `selectAll()` only selects within the focused item, so the clipboard won't contain "bravo".

- [ ] **Step 3: Fix `selectAll()` in Editor.cpp**

In `src/Editor.cpp`, replace the `selectAll()` method. Find:

```cpp
void Editor::selectAll() { if (auto *ti = focusedTextItem()) ti->textControl()->selectAll(); }
```

Replace with:

```cpp
void Editor::selectAll()
{
    auto *mgr = m_scene->selectionManager();
    QKeyEvent e(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    mgr->handleKeyPress(&e);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates selectAllCrossBoundary
```
Expected: PASS.

- [ ] **Step 5: Run all existing tests to verify no regression**

Run:
```bash
cd build && cmake --build . && ctest -R markoff --output-on-failure
```
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/Editor.cpp tests/tst_global_coordinates.cpp
git commit -m "fix(markoff): selectAll() now selects across all items

Was delegating to focusedTextItem()->textControl()->selectAll() which
only selected within one QTextDocument. Now routes through
SelectionManager, matching the context menu's Select All behavior."
```

---

## Task 6: Fix `cut()` to remove block items

**Files:**
- Modify: `src/SceneCoordinator.h`
- Modify: `src/SceneCoordinator.cpp`
- Modify: `src/Editor.cpp`

- [ ] **Step 1: Add test case to `tst_global_coordinates.cpp`**

Add to the class declaration:

```cpp
    void cutRemovesBlockItems();
```

Add the implementation:

```cpp
void TstGlobalCoordinates::cutRemovesBlockItems()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("before\n\n| H |\n|---|\n| V |\n\nafter"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    int itemsBefore = coord->items().size();

    // Select all, then cut.
    editor.selectAll();
    QApplication::processEvents();
    editor.cut();
    QApplication::processEvents();

    // All items should be removed or emptied. The table block item must not
    // survive the cut. The text in the clipboard should contain the table.
    QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("| H |")));

    // The editor's text should be empty (or near-empty — just empty text items).
    QString remaining = editor.toPlainText().trimmed();
    QVERIFY(!remaining.contains(QStringLiteral("| H |")));
    QVERIFY(!remaining.contains(QStringLiteral("before")));
    QVERIFY(!remaining.contains(QStringLiteral("after")));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates cutRemovesBlockItems
```
Expected: FAIL — the table block item survives the cut.

- [ ] **Step 3: Add `removeBlockItem()` to SceneCoordinator**

In `src/SceneCoordinator.h`, add in the `public:` section after the `ItemPosition itemAtGlobalLine(...)` declaration:

```cpp
    void removeBlockItem(int index);
```

In `src/SceneCoordinator.cpp`, add after the `itemAtGlobalLine` method:

```cpp
void SceneCoordinator::removeBlockItem(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (m_items[index]->isTextItem()) return; // safety: only block items

    m_scene->removeItem(m_items[index]->asGraphicsItem());
    delete m_items[index]->asGraphicsItem();
    m_items.removeAt(index);
    m_headingMapDirty = true;
}
```

- [ ] **Step 4: Fix `cut()` in Editor.cpp**

In `src/Editor.cpp`, replace the entire `cut()` method. Find:

```cpp
void Editor::cut()
{
    // Copy first (handles both cross-boundary and within-item).
    copy();
    // Then delete the selected content.
    auto *mgr = m_scene->selectionManager();
    if (mgr) {
        // Delete across all items that have selections.
        for (auto *item : m_coordinator->items()) {
            if (item->isTextItem()) {
                auto *ti = static_cast<MarkdownTextItem *>(item);
                QTextCursor c = ti->textControl()->textCursor();
                if (c.hasSelection())
                    c.removeSelectedText();
            }
        }
        mgr->clearSelection();
    }
}
```

Replace with:

```cpp
void Editor::cut()
{
    copy();

    // Remove selected text from text items.
    for (auto *item : m_coordinator->items()) {
        if (item->isTextItem()) {
            auto *ti = static_cast<MarkdownTextItem *>(item);
            QTextCursor c = ti->textControl()->textCursor();
            if (c.hasSelection())
                c.removeSelectedText();
        }
    }

    // Remove fully-selected block items (iterate in reverse for stable indices).
    bool structureChanged = false;
    const auto &items = m_coordinator->items();
    for (int i = items.size() - 1; i >= 0; --i) {
        if (!items[i]->isTextItem() && items[i]->isFullySelected()) {
            m_coordinator->removeBlockItem(i);
            structureChanged = true;
        }
    }

    m_scene->selectionManager()->clearSelection();

    if (structureChanged) {
        m_scene->setSelectableItems(m_coordinator->items());
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates cutRemovesBlockItems
```
Expected: PASS.

- [ ] **Step 6: Run all tests for regression check**

Run:
```bash
cd build && ctest -R markoff --output-on-failure
```
Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/SceneCoordinator.h src/SceneCoordinator.cpp src/Editor.cpp tests/tst_global_coordinates.cpp
git commit -m "fix(markoff): cut() now removes fully-selected block items

Previously, cut() only removed selected text from text items. Tables and
images that were fully selected survived the cut. Now removes them from
the scene via SceneCoordinator::removeBlockItem()."
```

---

## Task 7: Fix `goToLine()`

**Files:**
- Modify: `src/Editor.cpp`

- [ ] **Step 1: Add test cases to `tst_global_coordinates.cpp`**

Add to the class declaration:

```cpp
    void goToLineFirstItem();
    void goToLineSecondItem();
    void goToLinePastEnd();
```

Add the implementations:

```cpp
void TstGlobalCoordinates::goToLineFirstItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    editor.show();
    QApplication::processEvents();

    editor.goToLine(2);
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), 2);
}

void TstGlobalCoordinates::goToLineSecondItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("first\n\n| H |\n|---|\n| V |\n\nsecond\nthird"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    // Find the last text item to determine its global line.
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No second text item");
        return;
    }
    auto gp = coord->globalPositionOf(lastText, 0, 0);

    editor.goToLine(gp.line);
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), gp.line);
}

void TstGlobalCoordinates::goToLinePastEnd()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("one\ntwo"));
    editor.show();
    QApplication::processEvents();

    editor.goToLine(999);
    QApplication::processEvents();

    // Should clamp to last line, not crash.
    QCOMPARE(editor.cursorLine(), 2);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates goToLineSecondItem
```
Expected: FAIL — current `goToLine()` navigates within the focused item only.

- [ ] **Step 3: Fix `goToLine()` in Editor.cpp**

In `src/Editor.cpp`, replace the entire `goToLine()` method. Find:

```cpp
void Editor::goToLine(int line)
{
    if (!m_coordinator) return;
    auto *ti = focusedTextItem();
    if (!ti) return;
    auto *tc = ti->textControl();
    QTextCursor cursor = tc->textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 1; i < line; ++i)
        cursor.movePosition(QTextCursor::NextBlock);
    tc->setTextCursor(cursor);
    ensureFocusedCursorVisible();
}
```

Replace with:

```cpp
void Editor::goToLine(int line)
{
    if (!m_coordinator) return;

    auto pos = m_coordinator->itemAtGlobalLine(line);
    if (!pos.item) {
        // Line is inside a block item or document is empty.
        // Try to focus the nearest text item after that line.
        // For now, just ensure visible at current position.
        ensureFocusedCursorVisible();
        return;
    }

    pos.item->setFocus();
    QTextBlock block = pos.item->document()->findBlockByNumber(pos.localBlockNumber);
    QTextCursor cursor(pos.item->document());
    if (block.isValid())
        cursor.setPosition(block.position());
    else
        cursor.movePosition(QTextCursor::End);
    pos.item->textControl()->setTextCursor(cursor);
    ensureFocusedCursorVisible();
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates goToLineFirstItem goToLineSecondItem goToLinePastEnd
```
Expected: All 3 pass.

- [ ] **Step 5: Run all tests for regression check**

Run:
```bash
cd build && ctest -R markoff --output-on-failure
```
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/Editor.cpp tests/tst_global_coordinates.cpp
git commit -m "fix(markoff): goToLine() now navigates across item boundaries

Was navigating block-by-block within the focused item's QTextDocument.
Now uses SceneCoordinator::itemAtGlobalLine() to find the correct item
and local block for any global source line number."
```

---

## Task 8: Fix `cursorLine()`

**Files:**
- Modify: `src/Editor.cpp`

- [ ] **Step 1: Add test cases to `tst_global_coordinates.cpp`**

Add to the class declaration:

```cpp
    void cursorLineFirstItem();
    void cursorLineSecondItem();
```

Add the implementations:

```cpp
void TstGlobalCoordinates::cursorLineFirstItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    editor.show();
    QApplication::processEvents();

    // Cursor starts at line 1.
    QCOMPARE(editor.cursorLine(), 1);

    // Move to line 3.
    editor.goToLine(3);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), 3);
}

void TstGlobalCoordinates::cursorLineSecondItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aa\nbb\n\n| H |\n|---|\n| V |\n\ncc\ndd"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No second text item");
        return;
    }

    auto gpCC = coord->globalPositionOf(lastText, 0, 0);
    auto gpDD = coord->globalPositionOf(lastText, 1, 0);

    editor.goToLine(gpCC.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpCC.line);

    editor.goToLine(gpDD.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpDD.line);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates cursorLineSecondItem
```
Expected: FAIL — current `cursorLine()` returns item-local block number.

- [ ] **Step 3: Fix `cursorLine()` in Editor.cpp**

In `src/Editor.cpp`, replace the `cursorLine()` method. Find:

```cpp
int Editor::cursorLine() const
{
    auto *ti = focusedTextItem();
    if (!ti) return 1;
    return ti->textControl()->textCursor().blockNumber() + 1;
}
```

Replace with:

```cpp
int Editor::cursorLine() const
{
    auto *ti = focusedTextItem();
    if (!ti || !m_coordinator) return 1;

    auto gp = m_coordinator->globalPositionOf(
        ti,
        ti->textControl()->textCursor().blockNumber(),
        ti->textControl()->textCursor().positionInBlock());
    return gp.line;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
cd build && cmake --build . --target tst_markoff_global_coordinates && QT_QPA_PLATFORM=offscreen ./bin/tst_markoff_global_coordinates cursorLineFirstItem cursorLineSecondItem
```
Expected: Both PASS.

- [ ] **Step 5: Run all tests for regression check**

Run:
```bash
cd build && ctest -R markoff --output-on-failure
```
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/Editor.cpp tests/tst_global_coordinates.cpp
git commit -m "fix(markoff): cursorLine() now returns global source line

Was returning blockNumber()+1 within the focused item's QTextDocument.
Now uses SceneCoordinator::globalPositionOf() to compute the 1-based
global source line across all items."
```

---

## Task 9: Final integration test and verification

**Files:**
- Modify: `tests/tst_global_coordinates.cpp`

A combined end-to-end test that exercises all four fixes in sequence.

- [ ] **Step 1: Add integration test**

Add to the class declaration:

```cpp
    void endToEndAllFixes();
```

Add the implementation:

```cpp
void TstGlobalCoordinates::endToEndAllFixes()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral(
        "# Title\n\nParagraph one.\n\n| Col |\n|-----|\n| Val |\n\nParagraph two.\nLine ten."));
    editor.show();
    QApplication::processEvents();

    // Fix 4: cursorLine starts at 1.
    QCOMPARE(editor.cursorLine(), 1);

    // Fix 3: goToLine to "Line ten." (last line).
    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    QVERIFY(lastText);
    auto gpLast = coord->globalPositionOf(lastText,
        lastText->document()->blockCount() - 1, 0);

    editor.goToLine(gpLast.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpLast.line);

    // Fix 1: selectAll selects across all items.
    editor.selectAll();
    QApplication::processEvents();
    editor.copy();
    QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("Title")));
    QVERIFY(clipboard.contains(QStringLiteral("Line ten.")));

    // Fix 2: cut removes everything including the table.
    editor.selectAll();
    QApplication::processEvents();
    editor.cut();
    QApplication::processEvents();
    QString remaining = editor.toPlainText().trimmed();
    QVERIFY(!remaining.contains(QStringLiteral("| Col |")));
    QVERIFY(!remaining.contains(QStringLiteral("Title")));
}
```

- [ ] **Step 2: Run the full test suite**

Run:
```bash
cd build && cmake --build . && ctest -R markoff --output-on-failure
```
Expected: ALL tests pass, including the new integration test and all 15 existing test executables.

- [ ] **Step 3: Commit**

```bash
git add tests/tst_global_coordinates.cpp
git commit -m "test(markoff): add end-to-end integration test for audit top-4 fixes

Exercises selectAll, cut, goToLine, and cursorLine in sequence on a
multi-item document with text + table + text."
```
