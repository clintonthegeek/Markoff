# Styled Structural-Key Authority — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `markoff-styled` from corrupting the model when a structural key (Enter/Backspace/Tab/Delete) is pressed inside a `QTextList` item, by owning structural keys in a `QTextEdit::keyPressEvent` override that routes to a new core decision handler instead of letting Qt's native list machinery run.

**Architecture:** A pure `Markoff::StructuralKeyHandler` in `markoff-core` decides (block kind × key) → `Cmd::*` mutations + caret target. `SourceTextDocumentBinding` gains `handleStructuralKey()` (resolving the block, collapsing any selection first) which calls the handler and stages `m_pendingCaret`. A new `Markoff::Styled::StructuralTextEdit : QTextEdit` intercepts structural keys (and Ctrl+Z/Y) before native editing and forwards them.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets/Test), CMake. Tests run under `QT_QPA_PLATFORM=offscreen` via `scripts/run-tests.sh`.

**Spec:** [`docs/specs/2026-05-29-styled-structural-key-authority-design.md`](../specs/2026-05-29-styled-structural-key-authority-design.md)

**Build/test commands used throughout:**
```bash
cmake --build build-dev -j 8 --target <target>
QT_QPA_PLATFORM=offscreen ./build-dev/bin/<test_binary>
# or: scripts/run-tests.sh --bin <test_binary>
```

---

## File Structure

| File | Responsibility | New? |
|---|---|---|
| `libs/markoff-core/include/markoff/core/StructuralKeyHandler.h` | Public decl: `StructuralResult`, `StructuralKeyHandler::handle()` | new |
| `libs/markoff-core/src/StructuralKeyHandler.cpp` | (kind × key) decision rules → `Cmd::*` mutations | new |
| `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h` | + `handleStructuralKey()`, + private `deleteSepRange()` | modify |
| `libs/markoff-core/src/SourceTextDocumentBinding.cpp` | extract cross-block delete; implement `handleStructuralKey()` | modify |
| `libs/markoff-styled/src/StructuralTextEdit.{h,cpp}` | `QTextEdit` subclass: intercept structural keys + undo/redo | new |
| `libs/markoff-styled/src/Editor.cpp` | construct `StructuralTextEdit`; wire binding pointer | modify |
| `libs/markoff-styled/include/markoff/styled/Editor.h` | member type + out-of-line `textEdit()` | modify |
| `libs/markoff-core/tests/tst_structural_key_handler.cpp` | unit tests for the handler | new |
| `libs/markoff-core/tests/tst_binding_structural_key.cpp` | binding `handleStructuralKey` + selection collapse | new |
| `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` | + the queue #8.8 falsifiable repro + per-row widget tests | modify |
| `libs/markoff-core/tests/CMakeLists.txt`, `libs/markoff-styled/CMakeLists.txt`, `libs/markoff-styled/tests/CMakeLists.txt` | register new sources/tests | modify |

---

## Task 1: Core `StructuralKeyHandler` — skeleton + Paragraph/Heading

**Files:**
- Create: `libs/markoff-core/include/markoff/core/StructuralKeyHandler.h`
- Create: `libs/markoff-core/src/StructuralKeyHandler.cpp`
- Create: `libs/markoff-core/tests/tst_structural_key_handler.cpp`
- Modify: `libs/markoff-core/src/CMakeLists.txt` (or the core lib's source list) and `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Create the header**

`libs/markoff-core/include/markoff/core/StructuralKeyHandler.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

/// Result of a structural-key decision. `handled == false` means the key
/// was not a structural operation for this block/caret and the caller
/// should fall through to native editing. When handled, `caretBlock` +
/// `caretByteInBlock` declare where the caret should land after the model
/// settles (within-block UTF-8 byte offset).
struct StructuralResult {
    bool     handled = false;
    BlockId  caretBlock;
    uint32_t caretByteInBlock = 0;
};

/// Pure, view-agnostic structural-key dispatcher. Decides what a structural
/// key (Enter/Backspace/Delete/Tab) means for `block` given the caret byte
/// offset within it, applies the corresponding `Cmd::*` mutations to `doc`,
/// and returns the intended caret. Assumes an empty selection — the caller
/// (the binding) collapses any selection before calling.
class MARKOFF_CORE_EXPORT StructuralKeyHandler {
public:
    /// `key`/`modifiers` are Qt::Key / Qt::KeyboardModifiers as ints (kept
    /// as ints to avoid a Qt-namespace include in this header).
    static StructuralResult handle(MarkoffDocument &doc, BlockId block,
                                   int key, int modifiers,
                                   uint32_t caretByteInBlock);
};

}  // namespace Markoff
```

- [ ] **Step 2: Write the failing test (Paragraph cases)**

`libs/markoff-core/tests/tst_structural_key_handler.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QtCore/qnamespace.h>

#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralKeyHandler.h>

using namespace Markoff;

class TstStructuralKeyHandler : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void paragraph_enter_at_end_creates_block_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(first).size());

        auto r = StructuralKeyHandler::handle(doc, first, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void paragraph_enter_mid_splits() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 5u);  // after "Alpha"
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 2);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(r.caretBlock, after[1]);
        QCOMPARE(r.caretByteInBlock, 0u);
    }

    void paragraph_shift_enter_soft_break() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::ShiftModifier, 5u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);  // no split
        QCOMPARE(doc.blockText(b), QByteArrayLiteral("Alpha\nBravo"));
        QCOMPARE(r.caretBlock, b);
        QCOMPARE(r.caretByteInBlock, 6u);  // after the inserted '\n'
    }

    void paragraph_backspace_at_start_merges() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        auto r = StructuralKeyHandler::handle(doc, blocks[1], Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 1);
        QCOMPARE(doc.blockText(after[0]), QByteArrayLiteral("AlphaBravo"));
        QCOMPARE(r.caretByteInBlock, 5u);  // join at end of "Alpha"
    }

    void paragraph_backspace_mid_not_handled() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Backspace,
                                              Qt::NoModifier, 3u);
        QVERIFY(!r.handled);
        QCOMPARE(doc.blockText(b), QByteArrayLiteral("Alpha"));  // untouched
    }

    void paragraph_delete_at_end_merges_next() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        const auto blocks = doc.iterateBlocks();
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(blocks[0]).size());
        auto r = StructuralKeyHandler::handle(doc, blocks[0], Qt::Key_Delete,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("AlphaBravo"));
    }
};

QTEST_MAIN(TstStructuralKeyHandler)
#include "tst_structural_key_handler.moc"
```

- [ ] **Step 3: Register the new core source + test**

In the markoff-core library `CMakeLists.txt` source list, add `src/StructuralKeyHandler.cpp` alongside the existing core sources (e.g. next to `src/SourceTextDocumentBinding.cpp`).

In `libs/markoff-core/tests/CMakeLists.txt`, append:
```cmake
add_executable(tst_structural_key_handler tst_structural_key_handler.cpp)
add_test(NAME tst_structural_key_handler COMMAND tst_structural_key_handler)
target_link_libraries(tst_structural_key_handler PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_structural_key_handler PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4: Run the test, verify it fails to link/compile**

Run: `cmake --build build-dev -j 8 --target tst_structural_key_handler`
Expected: FAIL — `StructuralKeyHandler::handle` undefined (no `.cpp` body yet).

- [ ] **Step 5: Implement the Paragraph/Heading rules**

`libs/markoff-core/src/StructuralKeyHandler.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/StructuralKeyHandler.h>

#include <QtCore/qnamespace.h>
#include <algorithm>
#include <vector>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

namespace Markoff {

namespace {

int indexOf(const std::vector<BlockId> &blocks, BlockId id) {
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        if (blocks[size_t(i)] == id) return i;
    return -1;
}

int indentOf(MarkoffDocument &doc, BlockId id) {
    const auto attrs = doc.blockAttrs(id);
    auto it = attrs.find(AttrNames::IndentLevel);
    if (it != attrs.end() && std::holds_alternative<int>(*it))
        return std::get<int>(*it);
    return 0;
}

// --- Paragraph / Heading -------------------------------------------------

StructuralResult paragraphEnter(MarkoffDocument &doc, BlockId block,
                                int modifiers, uint32_t caretByte) {
    const QByteArray text = doc.blockText(block);
    const bool isShift = (modifiers & Qt::ShiftModifier) != 0;

    if (isShift) {
        Cmd::insertSoftBreak(doc, block, caretByte);
        return {true, block, caretByte + 1};
    }

    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);

    if (caretByte == static_cast<uint32_t>(text.size())) {
        const BlockId nb = Cmd::enterAtEnd(doc, block);  // new para after
        return {true, nb, 0};
    }
    if (caretByte == 0) {
        BlockId nb;
        if (idx > 0) {
            nb = Cmd::enterAtEnd(doc, blocks[size_t(idx - 1)]);  // insert before `block`
        } else {
            UndoLog::Transaction t(doc.d2UndoLog());
            nb = doc.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        }
        return {true, nb, 0};  // caret in the new empty para (matches live)
    }
    // Mid-block split.
    const QByteArray suffix = text.mid(static_cast<int>(caretByte));
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(block, caretByte,
                          static_cast<uint32_t>(suffix.size()), QByteArray{}, t);
    const BlockId nb = doc.d2InsertBlock(block, BlockKind::Paragraph, t);
    doc.d2ApplyBufferEdit(nb, 0, 0, suffix, t);
    return {true, nb, 0};
}

StructuralResult paragraphBackspace(MarkoffDocument &doc, BlockId block,
                                    uint32_t caretByte) {
    if (caretByte != 0) return {};               // not at start
    const auto blocks = doc.iterateBlocks();
    if (indexOf(blocks, block) <= 0) return {};  // first block / not found
    auto res = Cmd::backspaceMerge(doc, block);
    if (res.mergedInto.isNull()) return {};
    return {true, res.mergedInto, res.cursorByteOffset};
}

StructuralResult paragraphDelete(MarkoffDocument &doc, BlockId block,
                                 uint32_t caretByte) {
    const QByteArray text = doc.blockText(block);
    if (caretByte != static_cast<uint32_t>(text.size())) return {};  // not at end
    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (idx < 0 || idx >= static_cast<int>(blocks.size()) - 1) return {};  // last block
    Cmd::deleteMerge(doc, block);
    return {true, block, caretByte};
}

}  // namespace

StructuralResult StructuralKeyHandler::handle(MarkoffDocument &doc, BlockId block,
                                              int key, int modifiers,
                                              uint32_t caretByteInBlock) {
    if (block.isNull()) return {};
    const BlockKind kind = doc.blockKind(block);

    const bool isEnter = (key == Qt::Key_Return || key == Qt::Key_Enter);

    switch (kind) {
    case BlockKind::Paragraph:
    case BlockKind::Heading:
        if (isEnter)               return paragraphEnter(doc, block, modifiers, caretByteInBlock);
        if (key == Qt::Key_Backspace) return paragraphBackspace(doc, block, caretByteInBlock);
        if (key == Qt::Key_Delete)    return paragraphDelete(doc, block, caretByteInBlock);
        return {};
    default:
        return {};  // other kinds added in Tasks 2-3
    }
}

}  // namespace Markoff
```

- [ ] **Step 6: Run the test, verify it passes**

Run: `cmake --build build-dev -j 8 --target tst_structural_key_handler && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_structural_key_handler`
Expected: PASS (6 slots).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff/core/StructuralKeyHandler.h \
        libs/markoff-core/src/StructuralKeyHandler.cpp \
        libs/markoff-core/tests/tst_structural_key_handler.cpp \
        libs/markoff-core/CMakeLists.txt libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(core): StructuralKeyHandler — paragraph/heading structural keys"
```

---

## Task 2: ListItem rules in the handler

**Files:**
- Modify: `libs/markoff-core/src/StructuralKeyHandler.cpp`
- Modify: `libs/markoff-core/tests/tst_structural_key_handler.cpp`

- [ ] **Step 1: Write the failing tests (ListItem)**

Append these slots to `TstStructuralKeyHandler`:
```cpp
    void listitem_enter_at_end_inserts_item_after() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId first = blocks[0];
        QCOMPARE(doc.blockKind(first), BlockKind::ListItem);
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(first).size());
        auto r = StructuralKeyHandler::handle(doc, first, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        const auto after = doc.iterateBlocks();
        QCOMPARE(int(after.size()), 3);
        QCOMPARE(doc.blockKind(after[1]), BlockKind::ListItem);
        QCOMPARE(doc.blockText(after[1]), QByteArrayLiteral(""));
        QCOMPARE(r.caretBlock, after[1]);
    }

    void listitem_enter_empty_at_indent0_exits_list() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- \n"));  // empty item
        const BlockId b = doc.iterateBlocks()[0];
        auto r = StructuralKeyHandler::handle(doc, b, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockKind(b), BlockKind::Paragraph);  // demoted
    }

    void listitem_tab_indents() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId second = blocks[1];  // needs a preceding item at indent 0
        auto r = StructuralKeyHandler::handle(doc, second, Qt::Key_Tab,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        const auto attrs = doc.blockAttrs(second);
        QVERIFY(attrs.contains(Markoff::AttrNames::IndentLevel));
        QCOMPARE(std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)), 1);
    }

    void listitem_shift_tab_outdents() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n    - nested\n"));
        const auto blocks = doc.iterateBlocks();
        const BlockId nested = blocks[1];
        QCOMPARE(std::get<int>(doc.blockAttrs(nested).value(Markoff::AttrNames::IndentLevel)), 1);
        auto r = StructuralKeyHandler::handle(doc, nested, Qt::Key_Tab,
                                              Qt::ShiftModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(std::get<int>(doc.blockAttrs(nested).value(Markoff::AttrNames::IndentLevel)), 0);
    }

    void listitem_backspace_at_start_indent0_merges() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        const auto blocks = doc.iterateBlocks();
        auto r = StructuralKeyHandler::handle(doc, blocks[1], Qt::Key_Backspace,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
    }
```

> Note on the empty-item fixture: tree-sitter may load `"- \n"` with an
> empty content buffer. If `loadFromMarkdown` of a bare empty item proves
> flaky, seed via `"- x\n"` then `doc.d2ApplyBufferEdit(first, 0, 1, {}, t)`
> to empty it before the `handle` call. Verify the loaded buffer with a
> `qDebug()` first if the assertion is surprising.

- [ ] **Step 2: Run, verify failure**

Run: `cmake --build build-dev -j 8 --target tst_structural_key_handler && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_structural_key_handler`
Expected: the new ListItem slots FAIL (handler returns `{}` for ListItem — `default:` branch).

- [ ] **Step 3: Implement the ListItem rules**

Add to the anonymous namespace in `StructuralKeyHandler.cpp` (before `StructuralKeyHandler::handle`):
```cpp
StructuralResult listItemEnter(MarkoffDocument &doc, BlockId block,
                               uint32_t caretByte) {
    const QByteArray content = doc.blockText(block);
    const int indent = indentOf(doc, block);
    UndoLog::Transaction t(doc.d2UndoLog());

    if (content.isEmpty() && indent > 0) {                      // outdent
        doc.d2SetBlockAttr(block, AttrNames::IndentLevel, indent - 1, t);
        Cmd::renumberRunStartingAt(doc, block, t);
        return {true, block, 0};
    }
    if (content.isEmpty() && indent == 0) {                     // exit list
        doc.d2SetBlockKind(block, BlockKind::Paragraph, t);
        doc.d2SetBlockAttr(block, AttrNames::MarkerStyle, QString{}, t);
        return {true, block, 0};
    }
    if (caretByte == 0) {                                       // item before
        const BlockId nb = Cmd::insertListItemBefore(doc, block, t);
        Cmd::renumberRunStartingAt(doc, nb, t);
        return {true, block, 0};  // follow the original content item
    }
    if (caretByte == static_cast<uint32_t>(content.size())) {   // item after
        const BlockId nb = Cmd::insertListItemAfter(doc, block, t);
        Cmd::renumberRunStartingAt(doc, nb, t);
        return {true, nb, 0};
    }
    // Mid-content split.
    const QByteArray suffix = content.mid(static_cast<int>(caretByte));
    doc.d2ApplyBufferEdit(block, caretByte,
                          static_cast<uint32_t>(suffix.size()), QByteArray{}, t);
    const BlockId nb = Cmd::insertListItemAfter(doc, block, t);
    doc.d2ApplyBufferEdit(nb, 0, 0, suffix, t);
    Cmd::renumberRunStartingAt(doc, nb, t);
    return {true, nb, 0};
}

StructuralResult listItemBackspace(MarkoffDocument &doc, BlockId block,
                                   uint32_t caretByte) {
    if (caretByte != 0) return {};                  // in-line → native
    const int indent = indentOf(doc, block);
    if (indent > 0) {                               // outdent
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2SetBlockAttr(block, AttrNames::IndentLevel, indent - 1, t);
        Cmd::renumberRunStartingAt(doc, block, t);
        return {true, block, 0};
    }
    const auto blocks = doc.iterateBlocks();
    if (indexOf(blocks, block) <= 0) return {};
    auto res = Cmd::backspaceMerge(doc, block);
    if (res.mergedInto.isNull()) return {};
    {   // backspaceMerge self-transacts; renumber in a follow-up (2 undo
        // entries — matches live's documented limitation).
        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, res.mergedInto, t);
    }
    return {true, res.mergedInto, res.cursorByteOffset};
}

StructuralResult listItemDelete(MarkoffDocument &doc, BlockId block,
                                uint32_t caretByte) {
    const QByteArray content = doc.blockText(block);
    if (caretByte < static_cast<uint32_t>(content.size())) return {};
    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (idx < 0 || idx >= static_cast<int>(blocks.size()) - 1) return {};
    Cmd::deleteMerge(doc, block);
    {
        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, block, t);
    }
    return {true, block, caretByte};
}

StructuralResult listItemTab(MarkoffDocument &doc, BlockId block, int modifiers) {
    const int indent = indentOf(doc, block);
    const bool shift = (modifiers & Qt::ShiftModifier) != 0;
    const int newIndent = shift ? std::max(0, indent - 1) : std::min(6, indent + 1);
    if (newIndent == indent) return {true, block, /*caret restored by caller*/ 0};

    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (!shift) {
        // Indent only if a preceding ListItem at the current indent exists.
        bool parentFound = false;
        for (int k = idx - 1; k >= 0; --k) {
            const BlockId prev = blocks[size_t(k)];
            if (doc.blockKind(prev) != BlockKind::ListItem) break;
            const int prevIndent = indentOf(doc, prev);
            if (prevIndent == indent) { parentFound = true; break; }
            if (prevIndent < indent) break;
        }
        if (!parentFound) return {true, block, 0};  // refuse; consume key
    }

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2SetBlockAttr(block, AttrNames::IndentLevel, newIndent, t);
    Cmd::renumberRunStartingAt(doc, block, t);
    if (idx > 0) Cmd::renumberRunStartingAt(doc, blocks[size_t(idx - 1)], t);
    return {true, block, 0};
}
```

> **Caret on Tab:** Tab keeps the caret at its current byte offset. The
> handler returns `caretByteInBlock = 0` as a placeholder; the binding
> (Task 5) preserves the *original* caret byte for `Key_Tab` rather than
> using the returned value (Tab never moves the caret within the line).

Replace the `default:` branch in `handle()` with a `ListItem` case:
```cpp
    case BlockKind::ListItem:
        if (isEnter)                  return listItemEnter(doc, block, caretByteInBlock);
        if (key == Qt::Key_Backspace) return listItemBackspace(doc, block, caretByteInBlock);
        if (key == Qt::Key_Delete)    return listItemDelete(doc, block, caretByteInBlock);
        if (key == Qt::Key_Tab)       return listItemTab(doc, block, modifiers);
        return {};
    default:
        return {};
```

Also handle `Shift+Tab`: Qt delivers Shift+Tab as `Qt::Key_Backtab`. In
`handle()`, normalise before the switch:
```cpp
    int normKey = key;
    if (key == Qt::Key_Backtab) { normKey = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }
```
and use `normKey` in the comparisons.

- [ ] **Step 4: Run, verify pass**

Run: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_structural_key_handler` (after rebuild)
Expected: PASS (all slots, ~11).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/StructuralKeyHandler.cpp libs/markoff-core/tests/tst_structural_key_handler.cpp
git commit -m "feat(core): StructuralKeyHandler — ListItem enter/backspace/delete/tab"
```

---

## Task 3: CodeBlock, BlockQuote, HorizontalRule rules

**Files:**
- Modify: `libs/markoff-core/src/StructuralKeyHandler.cpp`
- Modify: `libs/markoff-core/tests/tst_structural_key_handler.cpp`

- [ ] **Step 1: Write failing tests**

Append:
```cpp
    void codeblock_enter_inserts_soft_break_not_split() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode\n```\n"));
        // Find the CodeBlock block.
        BlockId code;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::CodeBlock) { code = id; break; }
        QVERIFY(!code.isNull());
        const uint32_t endByte = static_cast<uint32_t>(doc.blockText(code).size());
        const int blockCountBefore = int(doc.iterateBlocks().size());
        auto r = StructuralKeyHandler::handle(doc, code, Qt::Key_Return,
                                              Qt::NoModifier, endByte);
        QVERIFY(r.handled);
        QCOMPARE(int(doc.iterateBlocks().size()), blockCountBefore);  // no split
        QCOMPARE(r.caretBlock, code);
    }

    void codeblock_tab_inserts_four_spaces() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("```\ncode\n```\n"));
        BlockId code;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::CodeBlock) { code = id; break; }
        const QByteArray before = doc.blockText(code);
        auto r = StructuralKeyHandler::handle(doc, code, Qt::Key_Tab,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockText(code), QByteArray("    ") + before);
        QCOMPARE(r.caretByteInBlock, 4u);
    }

    void blockquote_empty_enter_exits_to_paragraph() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("> quoted\n"));
        BlockId q;
        for (auto id : doc.iterateBlocks())
            if (doc.blockKind(id) == BlockKind::BlockQuote) { q = id; break; }
        QVERIFY(!q.isNull());
        // Empty the buffer first (simulate an empty quote line).
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(q, 0,
                static_cast<uint32_t>(doc.blockText(q).size()), QByteArray{}, t);
        }
        auto r = StructuralKeyHandler::handle(doc, q, Qt::Key_Return,
                                              Qt::NoModifier, 0u);
        QVERIFY(r.handled);
        QCOMPARE(doc.blockKind(q), BlockKind::Paragraph);
    }
```

- [ ] **Step 2: Run, verify failure**

Expected: new slots FAIL (handler returns `{}` for these kinds).

- [ ] **Step 3: Implement**

Add to the anonymous namespace:
```cpp
StructuralResult codeBlockEnter(MarkoffDocument &doc, BlockId block,
                                uint32_t caretByte) {
    Cmd::insertSoftBreak(doc, block, caretByte);   // literal '\n', no split
    return {true, block, caretByte + 1};
}

StructuralResult codeBlockTab(MarkoffDocument &doc, BlockId block,
                              uint32_t caretByte) {
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(block, caretByte, 0, QByteArrayLiteral("    "), t);
    return {true, block, caretByte + 4};
}

StructuralResult blockQuoteEnter(MarkoffDocument &doc, BlockId block,
                                 uint32_t caretByte) {
    if (doc.blockText(block).isEmpty()) {          // exit quote
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2SetBlockKind(block, BlockKind::Paragraph, t);
        return {true, block, 0};
    }
    return paragraphEnter(doc, block, /*modifiers=*/0, caretByte);  // split keeps kind via insert-after Paragraph
}
```

> **BlockQuote split nuance:** `paragraphEnter`'s mid/end split inserts a
> *Paragraph* after. For a quote, the new block should keep
> `BlockQuoteDepth`/`BlockQuoteRunId`. If the per-row BlockQuote tests
> (Task 8) show the new block losing quote depth, set the depth/runId
> attrs on the returned `caretBlock` here via `d2SetBlockAttr` reading the
> source block's attrs. Keep this minimal — full quote-split fidelity is a
> known follow-up if dogfood needs it.

Extend `handle()`:
```cpp
    case BlockKind::CodeBlock:
        if (isEnter)                  return codeBlockEnter(doc, block, caretByteInBlock);
        if (key == Qt::Key_Backspace) return paragraphBackspace(doc, block, caretByteInBlock);
        if (key == Qt::Key_Delete)    return paragraphDelete(doc, block, caretByteInBlock);
        if (normKey == Qt::Key_Tab && (modifiers & Qt::ShiftModifier) == 0)
                                      return codeBlockTab(doc, block, caretByteInBlock);
        return {};
    case BlockKind::BlockQuote:
        if (isEnter)                  return blockQuoteEnter(doc, block, caretByteInBlock);
        if (key == Qt::Key_Backspace) return paragraphBackspace(doc, block, caretByteInBlock);
        if (key == Qt::Key_Delete)    return paragraphDelete(doc, block, caretByteInBlock);
        return {};
    case BlockKind::HorizontalRule:
        if (isEnter) { const BlockId nb = Cmd::enterAtEnd(doc, block); return {true, nb, 0}; }
        return {};
```

- [ ] **Step 4: Run, verify pass**

Expected: PASS (all slots, ~14).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/StructuralKeyHandler.cpp libs/markoff-core/tests/tst_structural_key_handler.cpp
git commit -m "feat(core): StructuralKeyHandler — codeblock/blockquote/hr"
```

---

## Task 4: Extract `deleteSepRange` from the binding (refactor, no behavior change)

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp:381-418`

- [ ] **Step 1: Confirm existing binding tests pass first (baseline)**

Run: `scripts/run-tests.sh -R 'tst_binding'`
Expected: PASS (`tst_binding_forward`, `tst_binding_reverse`). Record the result — this refactor must leave them green.

- [ ] **Step 2: Add the private method declaration**

In `SourceTextDocumentBinding.h`, after `noSepByteToSepViewPos` (line 110), add:
```cpp
    /// Delete the sep-view byte range [sepLo, sepHi) from the model via
    /// direct D2 primitives (the cross-block merge path). Returns the
    /// collapsed caret as (block, byteInBlock). Shared by onQtContentsChange
    /// (selection delete) and handleStructuralKey (selection collapse).
    PendingCaret deleteSepRange(quint32 sepLo, quint32 sepHi);
```
(Move the `struct PendingCaret { ... };` declaration above this method if the compiler complains about use-before-definition — it currently sits at line 131; relocate it to just below the `private:` label.)

- [ ] **Step 3: Implement `deleteSepRange` by lifting the existing block from `onQtContentsChange`**

Cut the body of the cross-block non-structural branch (`SourceTextDocumentBinding.cpp:381-418`, the `if (!insertedHasNewline && hitStart && hitEnd && hitStart->blockId != hitEnd->blockId)` block) into a new method. The lifted logic resolves its own `hitStart`/`hitEnd`:
```cpp
SourceTextDocumentBinding::PendingCaret
SourceTextDocumentBinding::deleteSepRange(quint32 sepLo, quint32 sepHi) {
    Markoff::MarkoffDocument *doc = m_markoffDocument;
    const auto hitStart = Markoff::Detail::findBlockAtSepByte(doc, sepLo, /*biasForward=*/false);
    const auto hitEnd   = Markoff::Detail::findBlockAtSepByte(doc, sepHi, /*biasForward=*/true);
    if (!hitStart || !hitEnd) return PendingCaret{};

    if (hitStart->blockId == hitEnd->blockId) {
        // Within one block: plain delete.
        UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock,
                               hitEnd->byteInBlock - hitStart->byteInBlock,
                               QByteArray(), t);
        return PendingCaret{ hitStart->blockId, static_cast<int>(hitStart->byteInBlock) };
    }

    const auto allBlocks = doc->iterateBlocks();
    const QByteArray endTail = doc->blockText(hitEnd->blockId)
                                   .mid(static_cast<int>(hitEnd->byteInBlock));
    UndoLog::Transaction t(doc->d2UndoLog());
    const uint32_t startBlockSize =
        static_cast<uint32_t>(doc->blockText(hitStart->blockId).size());
    const uint32_t trimLen = startBlockSize - hitStart->byteInBlock;
    if (trimLen > 0)
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock, trimLen, QByteArray(), t);
    for (int i = hitStart->blockIndex + 1; i < hitEnd->blockIndex; ++i)
        doc->d2RemoveBlock(allBlocks[size_t(i)], t);
    doc->d2RemoveBlock(hitEnd->blockId, t);
    doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock, 0, endTail, t);
    return PendingCaret{ hitStart->blockId, static_cast<int>(hitStart->byteInBlock) };
}
```
(Requires `#include <markoff/core/UndoLog.h>` — already transitively available; add if the build complains.)

Then in `onQtContentsChange`, replace the lifted branch with a call:
```cpp
    if (!insertedHasNewline && hitStart && hitEnd && hitStart->blockId != hitEnd->blockId) {
        m_pendingCaret = deleteSepRange(sepStart, sepEnd);
        // The original branch appended `insertedUtf8` after the merge; preserve that:
        if (!insertedUtf8.isEmpty() && m_pendingCaret) {
            UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(m_pendingCaret->block,
                                   static_cast<uint32_t>(m_pendingCaret->offsetInBlock),
                                   0, insertedUtf8, t);
        }
        m_applyingLocalEdit = false;
        return;
    }
```

> **Behavior-preservation check:** the original branch (lines 406-409)
> appended `insertedUtf8 + endTail` to the start block. `deleteSepRange`
> stitches `endTail`; the `insertedUtf8` append above restores the rest.
> For a pure selection-delete `insertedUtf8` is empty, so the append is a
> no-op. Keep the caret at the merge point either way.

- [ ] **Step 4: Run the binding tests, verify still green**

Run: `scripts/run-tests.sh -R 'tst_binding'`
Expected: PASS (identical to Step 1 baseline — pure refactor).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp
git commit -m "refactor(core): extract deleteSepRange from onQtContentsChange"
```

---

## Task 5: Binding `handleStructuralKey` (empty-selection path)

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Create: `libs/markoff-core/tests/tst_binding_structural_key.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Declare the public method**

In `SourceTextDocumentBinding.h`, in the public section after `setTextDocument` (line 71):
```cpp
    /// Apply a structural key (Enter/Backspace/Delete/Tab/Backtab) at the
    /// given QTextDocument caret. `qtPos`/`qtAnchor` are sep-view UTF-16
    /// positions; a non-empty selection (qtPos != qtAnchor) is collapsed
    /// first via deleteSepRange. Returns true if the key was a handled
    /// structural op (caller should consume the key event); false → caller
    /// falls through to native editing. Stages m_pendingCaret on success.
    bool handleStructuralKey(int key, int modifiers, int qtPos, int qtAnchor);
```

- [ ] **Step 2: Write the failing test**

`libs/markoff-core/tests/tst_binding_structural_key.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>
#include <QtCore/qnamespace.h>

#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

using namespace Markoff;

// Wire a binding to a QTextDocument seeded from widgetFlatView(), mirroring
// the styled leaf's setup.
class TstBindingStructuralKey : public QObject {
    Q_OBJECT
    static void wire(MarkoffDocument &doc, QTextDocument &qdoc,
                     SourceTextDocumentBinding &b) {
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);
    }
private Q_SLOTS:
    void enter_at_end_of_first_bullet_keeps_heading_and_inserts_item() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("### H\n\n- one\n- two\n"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);

        // qtPos at end of "one": "### H"(5) + "\n"(1) + "one"(3) = 9.
        const QString flat = QString::fromUtf8(doc.widgetFlatView());
        QCOMPARE(qdoc.toPlainText(), flat);
        const int qtPos = 9;

        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Return, Qt::NoModifier,
                                                   qtPos, qtPos);
        QVERIFY(handled);
        // Spin the event loop for the debounced d2DocumentChanged + caret.
        QTRY_VERIFY(caretSpy.count() >= 1);

        // Heading block unchanged; a new empty ListItem sits between the bullets.
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("### H"));
        QCOMPARE(doc.blockKind(blocks[2]), BlockKind::ListItem);
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
    }

    void typing_key_returns_false() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // 'A' is not a structural key.
        QVERIFY(!b.handleStructuralKey(Qt::Key_A, Qt::NoModifier, 5, 5));
    }
};

QTEST_MAIN(TstBindingStructuralKey)
#include "tst_binding_structural_key.moc"
```

Register in `libs/markoff-core/tests/CMakeLists.txt`:
```cmake
add_executable(tst_binding_structural_key tst_binding_structural_key.cpp)
add_test(NAME tst_binding_structural_key COMMAND tst_binding_structural_key)
target_link_libraries(tst_binding_structural_key PRIVATE Qt6::Test Qt6::Gui Qt6::Widgets markoff_core)
set_tests_properties(tst_binding_structural_key PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run, verify failure**

Run: `cmake --build build-dev -j 8 --target tst_binding_structural_key`
Expected: FAIL — `handleStructuralKey` undefined.

- [ ] **Step 4: Implement `handleStructuralKey` (empty-selection path only)**

In `SourceTextDocumentBinding.cpp`, add `#include <markoff/core/StructuralKeyHandler.h>` and `#include <QtCore/qnamespace.h>`, then:
```cpp
bool SourceTextDocumentBinding::handleStructuralKey(int key, int modifiers,
                                                    int qtPos, int qtAnchor) {
    if (!m_markoffDocument || !m_textDocument) return false;
    Markoff::MarkoffDocument *doc = m_markoffDocument;

    // Selection collapse handled in Task 6; for now require empty selection.
    if (qtPos != qtAnchor) return false;

    const QByteArray preBytes = doc->widgetFlatView();
    const QString    preText  = QString::fromUtf8(preBytes);
    const quint32 sepPos = qtPosToByteOffset(preText, qtPos);
    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepPos, /*biasForward=*/false);
    if (!hit) return false;

    const bool isTab = (key == Qt::Key_Tab || key == Qt::Key_Backtab);
    const uint32_t caretByteBefore = hit->byteInBlock;

    m_applyingLocalEdit = true;
    Markoff::StructuralResult r =
        Markoff::StructuralKeyHandler::handle(*doc, hit->blockId, key, modifiers,
                                              hit->byteInBlock);
    m_applyingLocalEdit = false;

    if (!r.handled) return false;

    // Tab keeps the caret at its original byte offset within the same block.
    if (isTab)
        m_pendingCaret = PendingCaret{ r.caretBlock, static_cast<int>(caretByteBefore) };
    else
        m_pendingCaret = PendingCaret{ r.caretBlock, static_cast<int>(r.caretByteInBlock) };
    return true;
}
```
(Requires `#include <markoff/core/Detail/FlatBlockResolve.h>` — already present at the top of the file.)

- [ ] **Step 5: Run, verify pass**

Run: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_binding_structural_key`
Expected: PASS (2 slots).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-core/tests/tst_binding_structural_key.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(core): SourceTextDocumentBinding::handleStructuralKey (empty selection)"
```

---

## Task 6: Selection collapse-then-apply

**Files:**
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-core/tests/tst_binding_structural_key.cpp`

- [ ] **Step 1: Write the failing test**

Append to `TstBindingStructuralKey`:
```cpp
    void enter_with_selection_collapses_then_splits() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- on=DELETE=two\n"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        wire(doc, qdoc, b);
        // Select "=DELETE=" inside the single bullet, then Enter.
        // buffer = "on=DELETE=two"; "on"=2 .. "=DELETE=" ends at 10.
        const int selStart = 2, selEnd = 10;  // qt positions within the only block
        QSignalSpy caretSpy(&b, &SourceTextDocumentBinding::caretResolved);
        const bool handled = b.handleStructuralKey(Qt::Key_Return, Qt::NoModifier,
                                                   selEnd, selStart);
        QVERIFY(handled);
        QTRY_VERIFY(caretSpy.count() >= 1);
        const auto blocks = doc.iterateBlocks();
        // Selection removed, then split at the collapse point.
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("on"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("two"));
    }
```

- [ ] **Step 2: Run, verify failure**

Expected: FAIL — `handleStructuralKey` returns false for `qtPos != qtAnchor`.

- [ ] **Step 3: Implement the collapse**

Replace the early `if (qtPos != qtAnchor) return false;` with:
```cpp
    const QByteArray preBytes = doc->widgetFlatView();
    const QString    preText  = QString::fromUtf8(preBytes);

    // Non-empty selection: delete it through the model, collapse the caret,
    // then dispatch the structural op at the collapse point.
    if (qtPos != qtAnchor) {
        const int lo = std::min(qtPos, qtAnchor);
        const int hi = std::max(qtPos, qtAnchor);
        const quint32 sepLo = qtPosToByteOffset(preText, lo);
        const quint32 sepHi = qtPosToByteOffset(preText, hi);
        m_applyingLocalEdit = true;
        PendingCaret collapse = deleteSepRange(sepLo, sepHi);
        m_applyingLocalEdit = false;
        if (collapse.block.isNull()) return false;
        // Re-dispatch as an empty-selection op at the collapsed caret.
        m_applyingLocalEdit = true;
        Markoff::StructuralResult r = Markoff::StructuralKeyHandler::handle(
            *doc, collapse.block, key, modifiers,
            static_cast<uint32_t>(collapse.offsetInBlock));
        m_applyingLocalEdit = false;
        if (!r.handled) {
            // Selection was still deleted; land caret at the collapse point.
            m_pendingCaret = collapse;
            return true;
        }
        m_pendingCaret = PendingCaret{ r.caretBlock, static_cast<int>(r.caretByteInBlock) };
        return true;
    }
```
Move the existing `sepPos`/`hit` resolution (added in Task 5) below this block so it only runs for the empty-selection case. Add `#include <algorithm>` if not present.

> **Note:** the collapse + structural op are two transactions (delete, then
> op). Undo therefore takes two presses to fully reverse a selection+Enter.
> This matches the spec §6 transaction note; a future single-transaction
> `deleteSepRange(…, Transaction&)` overload closes it.

- [ ] **Step 4: Run, verify pass**

Expected: PASS (3 slots).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/SourceTextDocumentBinding.cpp libs/markoff-core/tests/tst_binding_structural_key.cpp
git commit -m "feat(core): handleStructuralKey collapse-then-apply for selections"
```

---

## Task 7: `StructuralTextEdit` subclass + Editor wiring + undo/redo

**Files:**
- Create: `libs/markoff-styled/src/StructuralTextEdit.h`
- Create: `libs/markoff-styled/src/StructuralTextEdit.cpp`
- Modify: `libs/markoff-styled/include/markoff/styled/Editor.h`
- Modify: `libs/markoff-styled/src/Editor.cpp`
- Modify: `libs/markoff-styled/CMakeLists.txt` (add the new source to the lib)

- [ ] **Step 1: Create `StructuralTextEdit.h`**

`libs/markoff-styled/src/StructuralTextEdit.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTextEdit>

namespace Markoff { class SourceTextDocumentBinding; }

namespace Markoff::Styled {

/// QTextEdit that intercepts structural keys (Enter/Backspace/Delete/Tab/
/// Backtab) and undo/redo chords BEFORE Qt's native editing runs, routing
/// them to the SourceTextDocumentBinding / MarkoffDocument. Everything else
/// (typing, navigation, selection) falls through to native QTextEdit.
class StructuralTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit StructuralTextEdit(QWidget *parent = nullptr);
    void setBinding(Markoff::SourceTextDocumentBinding *b) { m_binding = b; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    Markoff::SourceTextDocumentBinding *m_binding = nullptr;
};

}  // namespace Markoff::Styled
```

- [ ] **Step 2: Create `StructuralTextEdit.cpp`**

`libs/markoff-styled/src/StructuralTextEdit.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StructuralTextEdit.h"

#include <QKeyEvent>
#include <QTextCursor>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace Markoff::Styled {

StructuralTextEdit::StructuralTextEdit(QWidget *parent) : QTextEdit(parent) {}

void StructuralTextEdit::keyPressEvent(QKeyEvent *e) {
    if (m_binding) {
        const int key = e->key();
        const auto mods = e->modifiers();

        // Undo/redo (styled's QTextDocument undo is disabled; route to D2).
        if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) {
            Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
            if (doc && key == Qt::Key_Z && !(mods & Qt::ShiftModifier)) {
                doc->undoD2(); e->accept(); return;
            }
            if (doc && (key == Qt::Key_Y
                        || (key == Qt::Key_Z && (mods & Qt::ShiftModifier)))) {
                doc->redoD2(); e->accept(); return;
            }
        }

        // Structural keys.
        const bool structural =
            key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Backspace || key == Qt::Key_Delete
            || key == Qt::Key_Tab || key == Qt::Key_Backtab;
        if (structural) {
            const QTextCursor c = textCursor();
            if (m_binding->handleStructuralKey(
                    key, static_cast<int>(mods),
                    c.position(), c.anchor())) {
                e->accept();
                return;
            }
        }
    }
    QTextEdit::keyPressEvent(e);
}

}  // namespace Markoff::Styled
```

> `markoffDocument()` is a public accessor on `SourceTextDocumentBinding`
> (header line 64). `handleStructuralKey` was added in Tasks 5-6.

- [ ] **Step 3: Wire it into `Editor`**

In `Editor.h`: add a forward decl `class StructuralTextEdit;` inside
`namespace Markoff::Styled` (next to `class StyleApplier;`). Change the
member declaration:
```cpp
    StructuralTextEdit                     *m_editor       = nullptr;
```
Change the inline accessor to a declaration (StructuralTextEdit is only
forward-declared here, so the derived→base upcast must happen out of line):
```cpp
    QTextEdit *textEdit() const;
```

In `Editor.cpp`: add `#include "StructuralTextEdit.h"`. Change construction
(`Editor.cpp:23`) from `m_editor(new QTextEdit(this))` to
`m_editor(new StructuralTextEdit(this))`. Add the out-of-line accessor:
```cpp
QTextEdit *Editor::textEdit() const { return m_editor; }
```
In `Editor::setDocument`, right after the binding is created
(`Editor.cpp:66-76`, inside the `if (!m_binding)` block), wire the pointer:
```cpp
        m_editor->setBinding(m_binding);
```

- [ ] **Step 4: Register the new source**

In `libs/markoff-styled/CMakeLists.txt`, add `src/StructuralTextEdit.cpp` to
the `markoff_styled` target's source list (next to `src/Editor.cpp`).

- [ ] **Step 5: Build, verify it compiles + existing styled tests still pass**

Run: `cmake --build build-dev -j 8 --target markoff_styled && scripts/run-tests.sh -R 'tst_styled'`
Expected: build OK; the existing styled tests still pass (the
`enter_at_paragraph_end_*`, `backspace_at_block_start_*` etc. now flow
through `handleStructuralKey` instead of the observer path — they must
remain green; if any regress, the handler caret math is off, debug before
proceeding).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-styled/src/StructuralTextEdit.h libs/markoff-styled/src/StructuralTextEdit.cpp \
        libs/markoff-styled/include/markoff/styled/Editor.h libs/markoff-styled/src/Editor.cpp \
        libs/markoff-styled/CMakeLists.txt
git commit -m "feat(styled): StructuralTextEdit intercepts structural keys + undo/redo"
```

---

## Task 8: Falsifiable widget-level tests (queue #8.8 repro + per-row)

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`

- [ ] **Step 1: Add the queue #8.8 falsifiable repro slot**

Append to `TstStyledDogfoodInvariants`:
```cpp
    // Queue #8.8: Enter at end of a bullet under a heading must NOT merge the
    // bullet into the heading. Falsifiable: fails on the pre-fix HEAD
    // (heading text gains the bullet body + a duplicated first char).
    void enter_at_end_of_bullet_under_heading_keeps_structure() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "### C1 seam\n\n- Replace the option with injection.\n- Retire the shim.\n"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(600, 400);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        QTextDocument *qdoc = e.textEdit()->document();
        // Find the QTextBlock for the first bullet and place caret at its end.
        int pos = -1;
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next())
            if (b.text().startsWith(QStringLiteral("Replace the"))) {
                pos = b.position() + b.text().length(); break;
            }
        QVERIFY(pos >= 0);
        QTextCursor c(qdoc);
        c.setPosition(pos);
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(100);

        const auto blocks = doc.iterateBlocks();
        // Heading untouched.
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("### C1 seam"));
        // A new empty ListItem sits between the two bullets.
        QCOMPARE(doc.blockKind(blocks[2]), Markoff::BlockKind::ListItem);
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[1]),
                 QByteArrayLiteral("Replace the option with injection."));
        // QTextDocument stayed in sync with the model.
        QCOMPARE(qdoc->toPlainText(), QString::fromUtf8(doc.widgetFlatView()));
    }
```

- [ ] **Step 2: Add per-row widget tests**

Append slots exercising the keystroke through the real widget for: list
outdent (empty indented item + Enter), list mid-split, Tab indent (with a
preceding sibling), Backspace-merge at list start, CodeBlock Enter (soft
break, no new block), and a selection+Enter collapse. Each follows the same
shape: `loadFromMarkdown` → `setDocument` → `show` → place caret →
`QTest::keyClick` → `qWait` → assert `iterateBlocks()`/`blockText`/`blockKind`
and `qdoc->toPlainText() == widgetFlatView()`. Example (Tab indent):
```cpp
    void tab_in_list_indents_via_widget() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("- one\n- two\n"));
        auto *s = doc.createSession(); e.setSession(s); e.setDocument(&doc);
        e.resize(600, 400); e.show(); QTRY_VERIFY(e.isVisible()); QTest::qWait(50);
        QTextDocument *qdoc = e.textEdit()->document();
        QTextBlock b1 = qdoc->findBlockByNumber(1);  // "two"
        QTextCursor c(qdoc); c.setPosition(b1.position());
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Tab);
        QTest::qWait(100);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(std::get<int>(doc.blockAttrs(blocks[1])
                               .value(Markoff::AttrNames::IndentLevel)), 1);
    }
```
(Add `#include <markoff/core/AttrNames.h>` to the test file.)

- [ ] **Step 3: Run, verify pass (and prove falsifiability)**

Run: `scripts/run-tests.sh --bin tst_styled_dogfood_invariants`
Expected: PASS. To prove the #8.8 slot is falsifiable, temporarily stub the
interception in `StructuralTextEdit::keyPressEvent` so the seam is bypassed
(this reverts to the buggy behavior without breaking the build):
```cpp
void StructuralTextEdit::keyPressEvent(QKeyEvent *e) {
    QTextEdit::keyPressEvent(e);   // TEMP: bypass interception
    return;
    // ... real body below (unreachable while stubbed) ...
}
```
Rebuild, run `tst_styled_dogfood_invariants` — the #8.8 slot MUST FAIL with
the heading-merge corruption (proving the test exercises the real seam).
Then remove the two stub lines and rebuild. (INVARIANTS §4.)

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "test(styled): falsifiable queue #8.8 repro + per-row structural tests"
```

---

## Task 9: Full-suite verification + retire scratch + close-out

**Files:**
- Modify: `docs/queue.md` (mark #8.8 diagnosed/fixed; correct the misattribution)
- Modify: `libs/markoff-styled/CLAUDE.md` (note the new structural-key seam)

- [ ] **Step 1: Run the fast suite baseline**

Run: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
Expected: the pre-existing failures documented in the root `CLAUDE.md`
(the 3 live-side + the `tst_styled_block_formats` / `tst_source_widget_format_ops`
ones) and **zero new** failures. Specifically confirm `tst_styled_*` and
`tst_binding_*` and `tst_structural_key_handler` all pass.

- [ ] **Step 2: Confirm the observer path is no longer reached by styled structural keys**

Temporarily add a `qWarning("OBSERVER STRUCTURAL PATH")` at the top of the
`insertedHasNewline` `applyFlatEdit` branch (`SourceTextDocumentBinding.cpp`
~line 420), rebuild, run `tst_styled_dogfood_invariants`, and confirm the
warning does **not** fire for the structural-key slots (it may still fire for
paste tests, which is correct). Remove the `qWarning` afterward. (INVARIANTS
§3 — old authority retired for the intercepted keys.)

- [ ] **Step 3: Update docs**

In `docs/queue.md`, edit the #8.8 entry: strike the H1/H2/H3 hypotheses,
record the verified root cause (QTextList native-Enter vs observer binding),
the non-#8.1 attribution (window opened at `845fc0f`), and link the spec +
plan. Add a one-line VIEW-IMPLEMENTORS-GUIDE / `markoff-styled/CLAUDE.md`
note that styled now owns structural keys via `StructuralTextEdit` →
`SourceTextDocumentBinding::handleStructuralKey` → core `StructuralKeyHandler`.

- [ ] **Step 4: Manual dogfood (offscreen smoke)**

Run the styled app against the original repro file and confirm no corruption:
```bash
# If markoff-styled has a demo app target; otherwise rely on Task 8's
# headless repro slot as the acceptance gate.
QT_QPA_PLATFORM=offscreen ./build-dev/bin/<styled-app> docs/phase-c-status.md
```
If no styled demo app exists, state that the Task 8 headless slot is the
acceptance gate and skip the GUI run (do NOT use `--direct` without explicit
user permission).

- [ ] **Step 5: Commit**

```bash
git add docs/queue.md libs/markoff-styled/CLAUDE.md
git commit -m "docs: close queue #8.8 — styled structural-key authority landed"
```

---

## Self-Review notes (for the executor)

- **Caret coordinate space:** the handler and `m_pendingCaret` both use
  **within-block UTF-8 byte** offsets; `sepViewPosOf` converts to UTF-16 qt
  positions. Do not pass qt positions where byte offsets are expected.
- **`Key_Backtab`:** Shift+Tab arrives as `Qt::Key_Backtab` with
  ShiftModifier already implied — both the handler (Task 2 normalisation)
  and the subclass (Task 7 `structural` set) account for it.
- **Tab caret:** the handler returns `caretByteInBlock = 0` for Tab; the
  binding overrides it with the pre-edit caret byte (Task 5 `isTab` branch).
- **Transaction granularity:** list backspace-merge + renumber, and
  selection collapse + structural op, each produce 2 undo entries — a known,
  documented limitation matching live.
- **Empty-item load fixtures:** if tree-sitter loads `"- \n"` differently
  than expected, seed-then-empty (Task 2 note). Verify with `qDebug` before
  assuming a handler bug.
