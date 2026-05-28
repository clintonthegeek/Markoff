# Flat-view Enter semantics + caret-authority chokepoint — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Enter create a paragraph (caret moving into it) in the `markoff-styled` and `markoff-source` flat-text views, via a non-canonicalising interactive ingress plus a single binding-owned caret-authority chokepoint; retire the dead int-property cursor layer.

**Architecture:** New `MarkoffDocument::applyInteractiveNewline` (permits transient empty blocks; leaves canonical `applyFlatEdit` untouched). `SourceTextDocumentBinding` becomes the single authority for the post-structural-edit caret: structural ops stage a `m_pendingCaret{BlockId, offsetInBlock}`; the tail of `onD2DocumentChanged` resolves it to a sep-view position and emits one new signal `caretResolved(int,int)`; each widget connects that to `setTextCursor`. The unused int-property layer is deleted; `syncFromSession` is rewritten to sep-view coordinates and routed through the same emit point.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets/Test), CMake, CRDT-backed `markoff-core`.

**Authoritative spec:** [`../specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`](../specs/2026-05-27-flat-view-enter-and-caret-authority-design.md). Read it (and `docs/INVARIANTS.md`) before starting. HEAD reproduction of the bug is already recorded in the spec §1 — the structural-edit-is-dropped failure is established.

**Build/test commands (this repo):**
- Build a target: `cmake --build build-dev --target <t> -j 8`
- Run one binary offscreen: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/<t> [slotName]`
- Fast suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
- Known pre-existing failures (do NOT let them block): `tst_live_render_e2_nav_shift_extend`, `tst_live_render_focus_chokepoint_invariant`, `tst_live_render_cursor_typing_invariant`.

---

## Task 1: Core — `applyInteractiveNewline` (interactive ingress)

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h` (declaration, after `applyFlatEdit` at `:184-187`)
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (definition, after `applyFlatEdit` ends at `:1661`)
- Create: `libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

class TstD2InteractiveNewline : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_block_end_creates_transient_empty_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));  // [Hello, World]
        // No-sep end of "Hello" == byte 5.
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 3);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));   // transient empty
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral("World"));
        QVERIFY(newBlk == blocks[1]);  // caret target = the new empty block
    }

    void enter_mid_block_splits_with_tail_in_new_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("HelloWorld"));  // one block
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("World"));
        QVERIFY(newBlk == blocks[1]);
    }

    void enter_at_document_end_creates_empty_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QVERIFY(newBlk == blocks[1]);
    }

    void enter_at_block_start_pushes_content_down() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(0, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral(""));     // blank line above
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Hello"));
        QVERIFY(newBlk == blocks[1]);  // caret stays with the content
    }
};

QTEST_APPLESS_MAIN(TstD2InteractiveNewline)
#include "tst_d2_interactive_newline.moc"
```

Append to `libs/markoff-core/tests/d2/CMakeLists.txt`:

```cmake
qt_add_executable(tst_d2_interactive_newline tst_d2_interactive_newline.cpp)
target_link_libraries(tst_d2_interactive_newline PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_interactive_newline COMMAND tst_d2_interactive_newline)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null && cmake --build build-dev --target tst_d2_interactive_newline -j 8`
Expected: FAIL to compile — `applyInteractiveNewline` is not a member of `MarkoffDocument`.

- [ ] **Step 3: Declare the method**

In `libs/markoff-core/include/markoff/core/MarkoffDocument.h`, immediately after the `applyFlatEdit(...)` declaration (ends at `:187`):

```cpp
    /// Interactive ingress (WYSIWYG Enter): split the block containing the
    /// no-separator global byte offset `atByte` on a single newline. The head
    /// stays in the current block; the tail moves into a NEW Paragraph block
    /// inserted immediately after. The tail MAY be empty — this is the
    /// deliberate exception to the no-empty-block canonical rule that
    /// `applyFlatEdit` enforces (see binding-robustness spec). Performs NO
    /// newline-run collapsing. Interior-boundary bias is previous-block:
    /// `atByte` at the end of block N resolves to (N, end), not (N+1, 0).
    /// Returns the BlockId the caret should occupy at offset 0 (the new block).
    BlockId applyInteractiveNewline(uint32_t atByte, Origin origin);
```

- [ ] **Step 4: Implement the method**

In `libs/markoff-core/src/MarkoffDocument.cpp`, immediately after `applyFlatEdit`'s closing brace (`:1661`):

```cpp
Markoff::BlockId MarkoffDocument::applyInteractiveNewline(uint32_t atByte,
                                                          Origin origin)
{
    Q_UNUSED(origin);
    UndoLog::Transaction t(d2UndoLog());

    auto blocks = iterateBlocks();

    // Empty document: create the first paragraph so there is something to split.
    if (blocks.empty()) {
        d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        blocks = iterateBlocks();
    }

    // Resolve atByte -> (block index, byteInBlock) in no-sep coordinates with
    // previous-block bias at an interior boundary: the `<=` test takes block N
    // when atByte == end-of-N (rather than start-of-N+1). This is what makes
    // Enter-at-end-of-paragraph split the paragraph the user is leaving.
    uint32_t cursor = 0;
    int idx = -1;
    uint32_t byteInBlock = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const uint32_t sz = static_cast<uint32_t>(blockText(blocks[i]).size());
        const uint32_t blkEnd = cursor + sz;
        if (atByte <= blkEnd) {
            idx = static_cast<int>(i);
            byteInBlock = atByte - cursor;
            break;
        }
        cursor = blkEnd;
    }
    if (idx == -1) {  // past end -> last block at its end
        idx = static_cast<int>(blocks.size()) - 1;
        byteInBlock = static_cast<uint32_t>(blockText(blocks[size_t(idx)]).size());
    }

    const BlockId block = blocks[size_t(idx)];
    const QByteArray text = blockText(block);
    const QByteArray tail = text.mid(static_cast<int>(byteInBlock));

    // Trim the tail off the current block (head stays).
    if (!tail.isEmpty()) {
        d2ApplyBufferEdit(block, byteInBlock,
                          static_cast<uint32_t>(tail.size()), QByteArray(), t);
    }
    // New block after `block`, seeded with the tail (may be empty).
    BlockId newBlk = d2InsertBlock(block, BlockKind::Paragraph, t);
    if (!tail.isEmpty()) {
        d2ApplyBufferEdit(newBlk, 0, 0, tail, t);
    }
    return newBlk;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build-dev --target tst_d2_interactive_newline -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_interactive_newline`
Expected: PASS (4 slots).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp \
        libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "feat(core): applyInteractiveNewline — WYSIWYG Enter split

Non-canonicalising interactive ingress that permits a transient empty
block (the deliberate exception to applyFlatEdit's no-empty-block rule).
Returns the BlockId the caret should occupy. Spec
docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Binding — caret-authority chokepoint + Enter dispatch

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Create: `libs/markoff-styled/tests/tst_styled_binding_caret.cpp`
- Modify: `libs/markoff-styled/tests/CMakeLists.txt`

This task adds the chokepoint infrastructure (`caretResolved` signal, `m_pendingCaret`, `sepViewPosOf`, `emitCaret`), the pure-Enter dispatch, and the merge-path pending assignment. It does NOT yet delete the int-property layer (Task 3) or wire the widgets (Task 4); binding-level tests use `QTextDocument` + `QSignalSpy` directly.

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-styled/tests/tst_styled_binding_caret.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

using Markoff::SourceTextDocumentBinding;

class TstStyledBindingCaret : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_paragraph_end_resolves_caret_to_new_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        b.setMarkoffDocument(&doc);
        b.setTextDocument(&qdoc);  // seeds qdoc with flatView "Hello\n\nWorld"
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("Hello\n\nWorld"));

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Caret at end of "Hello" (sep-view pos 5), press Enter.
        QTextCursor c(&qdoc);
        c.setPosition(5);
        c.insertText(QStringLiteral("\n"));  // fires contentsChange

        QTRY_COMPARE(spy.count(), 1);
        // New empty block sits between Hello and World; its start in sep-view
        // is after "Hello\n\n" == position 7.
        QCOMPARE(spy.at(0).at(0).toInt(), 7);
        QCOMPARE(spy.at(0).at(1).toInt(), 7);
        // Model gained a block.
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
    }

    void ordinary_typing_does_not_resolve_caret() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        b.setMarkoffDocument(&doc);
        b.setTextDocument(&qdoc);

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Type "X" inside "Hello" (pos 2).
        QTextCursor c(&qdoc);
        c.setPosition(2);
        c.insertText(QStringLiteral("X"));
        QTest::qWait(50);  // let any debounced d2 signal fire

        QCOMPARE(spy.count(), 0);  // chokepoint only fires for structural ops
    }
};

QTEST_MAIN(TstStyledBindingCaret)
#include "tst_styled_binding_caret.moc"
```

Append to `libs/markoff-styled/tests/CMakeLists.txt`:

```cmake
add_executable(tst_styled_binding_caret tst_styled_binding_caret.cpp)
add_test(NAME tst_styled_binding_caret COMMAND tst_styled_binding_caret)
target_link_libraries(tst_styled_binding_caret
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_binding_caret
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build-dev >/dev/null && cmake --build build-dev --target tst_styled_binding_caret -j 8`
Expected: FAIL to compile — `caretResolved` is not a member of `SourceTextDocumentBinding`.

- [ ] **Step 3: Add the chokepoint infrastructure to the header**

In `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`:

Add `#include <optional>` near the top includes (after `#include <QtGlobal>` at `:9`).

Add to the `Q_SIGNALS:` block (after `:96`):

```cpp
    /// The binding-resolved caret, in sep-view (QTextDocument) coordinates.
    /// The owning widget applies this to its real caret. start==active is a
    /// collapsed caret. This is the SOLE caret-output of the binding.
    void caretResolved(int start, int active);
```

Add to the `private:` method section (after `syncFromSession();` decl at `:120`):

```cpp
    /// Sep-view (QTextDocument, UTF-16) position of `byteInBlock` within
    /// `block`: sum of each preceding block's UTF-16 length + 2 per "\n\n"
    /// separator, plus the in-block UTF-16 offset.
    int sepViewPosOf(Markoff::BlockId block, int byteInBlock) const;

    /// Map a no-separator global byte offset (the space resolveTextAnchor
    /// returns) to a sep-view QTextDocument position.
    int noSepByteToSepViewPos(quint32 noSepByte) const;

    /// Single emit point for the resolved caret.
    void emitCaret(int start, int active);
```

Add to the `private:` member section (after `m_applyingBackendCursor` at `:137`):

```cpp
    struct PendingCaret { Markoff::BlockId block; int offsetInBlock = 0; };
    /// Set by a structural op to declare the intended post-edit caret; resolved
    /// + emitted at the tail of onD2DocumentChanged once the reverse diff settles.
    std::optional<PendingCaret> m_pendingCaret;
```

- [ ] **Step 4: Implement helpers + dispatch in the cpp**

In `libs/markoff-core/src/SourceTextDocumentBinding.cpp`, add `#include <optional>` near the top includes, then add these definitions (e.g. just before `onQtContentsChange` at `:344`):

```cpp
int SourceTextDocumentBinding::sepViewPosOf(Markoff::BlockId block,
                                            int byteInBlock) const
{
    if (!m_markoffDocument) return 0;
    int pos = 0;
    for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
        const QByteArray text = m_markoffDocument->blockText(id);
        if (id == block) {
            return pos + byteOffsetToQtPos(text, static_cast<quint32>(byteInBlock));
        }
        pos += QString::fromUtf8(text).size();  // UTF-16 code units
        pos += 2;                                // interBlockSeparator() "\n\n"
    }
    return pos;  // block not found (defensive) -> end of document
}

int SourceTextDocumentBinding::noSepByteToSepViewPos(quint32 noSepByte) const
{
    if (!m_markoffDocument) return 0;
    quint32 cursor = 0;
    for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
        const quint32 sz =
            static_cast<quint32>(m_markoffDocument->blockText(id).size());
        if (noSepByte <= cursor + sz) {
            return sepViewPosOf(id, static_cast<int>(noSepByte - cursor));
        }
        cursor += sz;
    }
    // Past the last block: clamp to document end.
    const auto blocks = m_markoffDocument->iterateBlocks();
    if (blocks.empty()) return 0;
    const Markoff::BlockId last = blocks.back();
    return sepViewPosOf(last,
        static_cast<int>(m_markoffDocument->blockText(last).size()));
}

void SourceTextDocumentBinding::emitCaret(int start, int active)
{
    Q_EMIT caretResolved(start, active);
}
```

In `onQtContentsChange`, immediately after `m_applyingLocalEdit = true;` (`:365`) and BEFORE the `hitStart`/`hitEnd` resolution:

```cpp
    // ── Pure single Enter: interactive newline split (WYSIWYG paragraph) ──
    // A bare Enter (no selection, exactly one "\n" inserted) creates a real
    // paragraph — possibly a transient empty one — via the interactive
    // ingress, and declares the caret target. Everything else (paste,
    // multi-newline, selection+Enter) keeps its existing routing below.
    if (charsRemoved == 0 && insertedUtf8 == QByteArrayLiteral("\n")) {
        const quint32 noSep =
            sepViewToNoSepByteForEdit(doc, sepStart, /*biasForward=*/false);
        const Markoff::BlockId newBlk =
            doc->applyInteractiveNewline(noSep, Markoff::Origin::UserEdit);
        m_pendingCaret = PendingCaret{ newBlk, 0 };
        m_applyingLocalEdit = false;
        return;
    }
```

In `onQtContentsChange`, inside the cross-block-non-structural merge branch (`if (!insertedHasNewline && hitStart && hitEnd && hitStart->blockId != hitEnd->blockId)`, `:403`), immediately before `m_applyingLocalEdit = false;` (`:433`):

```cpp
        // B.3: post-merge caret lands at the merge point = end of the start
        // block's surviving head (byteInBlock where the trim began).
        m_pendingCaret = PendingCaret{ hitStart->blockId,
                                       static_cast<int>(hitStart->byteInBlock) };
```

- [ ] **Step 5: Re-assert the pending caret at the tail of `onD2DocumentChanged`**

In `libs/markoff-core/src/SourceTextDocumentBinding.cpp`, replace the body of `onD2DocumentChanged` (`:448-495`) so the early `actual == expected` return becomes a guarded diff, and the pending caret is re-asserted afterward:

```cpp
void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_applyingLocalEdit) return;
    if (!m_textDocument) return;
    if (!m_subscribedDoc) return;

    const QString expected = QString::fromUtf8(m_subscribedDoc->flatView());
    const QString actual   = m_textDocument->toPlainText();

    if (actual != expected) {
        // ── Incremental diff: longest common prefix + suffix ────────────────
        // Replace only the minimal contiguous changed span via QTextCursor so
        // character formatting outside the changed region is preserved.
        int p = 0;
        const int minLen = std::min(actual.size(), expected.size());
        while (p < minLen && actual.at(p) == expected.at(p)) ++p;
        if (p > 0 && p < actual.size() && actual.at(p - 1).isHighSurrogate()) --p;

        int s = 0;
        const int maxS = minLen - p;
        while (s < maxS
               && actual.at(actual.size() - 1 - s) == expected.at(expected.size() - 1 - s))
            ++s;
        if (s > 0 && actual.at(actual.size() - s).isLowSurrogate()) --s;

        const int removeFrom = p;
        const int removeTo   = actual.size() - s;
        const QString middle = expected.mid(p, expected.size() - s - p);

        m_applyingRemoteEdit = true;
        QTextCursor c(m_textDocument);
        c.setPosition(removeFrom);
        c.setPosition(removeTo, QTextCursor::KeepAnchor);
        c.insertText(middle);
        m_applyingRemoteEdit = false;
    }

    // ── Re-assert the caret declared by a structural op ─────────────────────
    // The QTextDocument is now settled. No singleShot: d2DocumentChanged is
    // already debounced past the synchronous keystroke (INVARIANTS §6).
    if (m_pendingCaret) {
        const int pos = sepViewPosOf(m_pendingCaret->block,
                                     m_pendingCaret->offsetInBlock);
        emitCaret(pos, pos);
        m_pendingCaret.reset();
    }
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build-dev --target tst_styled_binding_caret -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_binding_caret`
Expected: PASS (2 slots). Also re-run Task 1's binary to confirm no core regression: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_interactive_newline` → PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-styled/tests/tst_styled_binding_caret.cpp \
        libs/markoff-styled/tests/CMakeLists.txt
git commit -m "feat(core): binding caret-authority chokepoint + Enter dispatch

SourceTextDocumentBinding becomes the single authority for the
post-structural-edit caret: pure Enter routes to applyInteractiveNewline
and stages m_pendingCaret; cross-block merge stages the join point; the
tail of onD2DocumentChanged resolves it to a sep-view position and emits
the new caretResolved signal. No singleShot (debounce already defers).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: Binding — rewrite `syncFromSession` to sep-view; retire dead int-property layer

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_binding_caret.cpp` (add the Session test)

The int-property layer (`cursorPosition`/`selectionStart`/`selectionEnd` + setters + `*Changed` signals + their members + `m_applyingBackendCursor` + `pushSelectionToSession`) has zero consumers (verified repo-wide). Delete it; `caretResolved` is its sole successor. `syncFromSession` is the genuine model→view (collab/undo) mechanism — rewrite it to sep-view coordinates and route it through `emitCaret`.

- [ ] **Step 1: Write the failing test**

Add this slot to `libs/markoff-styled/tests/tst_styled_binding_caret.cpp` (inside the class):

```cpp
    void session_selection_change_resolves_caret_in_sep_view() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QTextDocument qdoc;
        SourceTextDocumentBinding b;
        auto *session = doc.createSession();
        b.setMarkoffDocument(&doc);
        b.setTextDocument(&qdoc);
        b.setSession(session);

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Place a collapsed selection at the start of "World" (no-sep byte 5).
        // In sep-view that is position 7 ("Hello\n\n" = 7). The OLD
        // no-separator concatenation would have wrongly returned 5.
        const auto anchor = doc.textAnchorAt(5, /*rightBias*/ false);
        Markoff::Selection sel;
        sel.anchor = anchor;
        sel.active = anchor;
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        QTRY_VERIFY(spy.count() >= 1);
        const auto last = spy.last();
        QCOMPARE(last.at(0).toInt(), 7);
        QCOMPARE(last.at(1).toInt(), 7);
    }
```

(Requires `#include <markoff/core/Selection.h>` and `#include <markoff/core/Session.h>` at the top of the test file — add them.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-dev --target tst_styled_binding_caret -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_binding_caret session_selection_change_resolves_caret_in_sep_view`
Expected: FAIL — `syncFromSession` currently emits the int signals (not `caretResolved`) and computes in no-separator coordinates, so either the spy stays 0 or the value is 5, not 7.

- [ ] **Step 3: Delete the int-property layer from the header**

In `libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h`:

- Delete the three `Q_PROPERTY` blocks for `cursorPosition`/`selectionStart`/`selectionEnd` (`:47-58`).
- Delete the getter/setter declarations (`:81-88`).
- Delete the `cursorPositionChanged`/`selectionStartChanged`/`selectionEndChanged` signal declarations (`:94-96`).
- Delete the `pushSelectionToSession()` declaration (`:115-117`).
- Delete the members `m_applyingBackendCursor` (`:137`) and `m_cursorPosition`/`m_selectionStart`/`m_selectionEnd` (`:139-141`).
- Update the class doc comment (`:25-32`): replace the "Cursor + selection are lifted… Two cycle guards… A third (`m_applyingBackendCursor`)…" paragraph with:

```cpp
/// Cursor + selection authority: the binding owns the post-structural-edit
/// caret. Structural ops stage an intended caret; onD2DocumentChanged resolves
/// it (sep-view) and emits `caretResolved`, which the owning widget applies via
/// setTextCursor. `syncFromSession` resolves an externally-driven
/// Session::primarySelection() (collaborator / undo) through the same signal.
/// Two cycle guards (`m_applyingLocalEdit` / `m_applyingRemoteEdit`) prevent
/// forward/reverse bounceback.
```

- [ ] **Step 4: Delete the int-property definitions + rewrite `syncFromSession` in the cpp**

In `libs/markoff-core/src/SourceTextDocumentBinding.cpp`:

- Delete the `cursorPosition()`/`selectionStart()`/`selectionEnd()` getters and the `setCursorPosition`/`setSelectionStart`/`setSelectionEnd` setters (`:131-174`).
- Delete `pushSelectionToSession()` (`:176-189`).
- Replace `syncFromSession()` (`:196-229`) with:

```cpp
void SourceTextDocumentBinding::syncFromSession()
{
    if (!m_session || !m_markoffDocument || !m_textDocument) return;
    // Do not fight a local edit mid-flight; the structural path re-asserts the
    // caret from m_pendingCaret at the tail of onD2DocumentChanged instead.
    if (m_applyingLocalEdit) return;

    const Markoff::Selection sel = m_session->primarySelection();
    const quint32 anchorByte = m_markoffDocument->resolveTextAnchor(sel.anchor);
    const quint32 activeByte = m_markoffDocument->resolveTextAnchor(sel.active);
    // resolveTextAnchor returns NO-SEPARATOR global bytes; map each to a
    // sep-view QTextDocument position (the prior implementation concatenated
    // blockText without separators — off by one separator per crossed boundary).
    emitCaret(noSepByteToSepViewPos(anchorByte),
              noSepByteToSepViewPos(activeByte));
}
```

- [ ] **Step 5: Build the whole tree to confirm no consumers of the deleted layer**

Run: `cmake --build build-dev -j 8`
Expected: PASS — clean build. (Repo-wide search already confirmed zero consumers of `cursorPosition`/`selectionStart`/`selectionEnd`/their setters/signals.) If any reference surfaces, it is in a file not yet inspected; fix that call site to use the new `caretResolved` signal or remove it.

- [ ] **Step 6: Run the binding test suite to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_binding_caret`
Expected: PASS (3 slots).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff/core/SourceTextDocumentBinding.h \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-styled/tests/tst_styled_binding_caret.cpp
git commit -m "refactor(core): retire dead int-cursor layer; syncFromSession -> sep-view

The cursorPosition/selectionStart/selectionEnd properties+signals+setters
had zero consumers (QML-era scaffolding). Delete them; caretResolved is the
sole caret-output. Rewrite syncFromSession to sep-view coordinates (fixing
the no-separator off-by-a-block bug) and route it through the same emit
point — the inbound collab/undo caret mechanism (guide B.2/B.4).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: Widget wiring + end-to-end falsifiable tests

**Files:**
- Modify: `libs/markoff-styled/src/Editor.cpp` (connect in `setDocument`, `:65-68`)
- Modify: `libs/markoff-source/src/Editor.cpp` (connect in ctor, after `:50`)
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` (add slots)

- [ ] **Step 1: Write the failing end-to-end tests**

Add these slots to `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` (inside the class; the file already includes the needed headers):

```cpp
    void enter_at_paragraph_end_creates_block_and_places_caret() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        QTextCursor c(qdoc);
        c.setPosition(5);  // end of "Alpha"
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        // 1. A new (empty) block was created between Alpha and Bravo.
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        // 2. Caret landed at the start of the new empty block (sep-view pos 7),
        //    NOT stranded in the gap (6) nor at the start of "Bravo".
        QCOMPARE(e.textEdit()->textCursor().position(), 7);
    }

    void enter_at_document_end_creates_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(5);  // end of "Alpha"
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 2);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.textEdit()->textCursor().position(), 7);  // start of new block
    }

    void enter_mid_paragraph_splits_with_caret_at_new_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));  // one block
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(5);  // between Alpha and Bravo
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Return);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(e.textEdit()->textCursor().position(), 7);  // start of "Bravo"
    }

    void backspace_at_block_start_merges_with_caret_at_join() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.textEdit()->document());
        c.setPosition(7);  // start of "Bravo"
        e.textEdit()->setTextCursor(c);
        QTest::keyClick(e.textEdit(), Qt::Key_Backspace);
        QTest::qWait(80);

        // Blocks merged into one "AlphaBravo".
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 1);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("AlphaBravo"));
        // Caret at the join point = end of "Alpha" = sep-view pos 5.
        QCOMPARE(e.textEdit()->textCursor().position(), 5);
    }
```

- [ ] **Step 2: Run to verify the structural slots fail on the un-wired widget**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants`
Expected: `enter_at_paragraph_end…`, `enter_at_document_end…`, and `backspace_at_block_start…` FAIL on the caret-position assertion (the binding now creates the block and emits `caretResolved`, but no widget slot applies it yet, so the caret is where Qt left it). `enter_mid_paragraph…` may pass incidentally. This is the falsifiability demonstration for the widget wire.

- [ ] **Step 3: Wire `caretResolved` in the styled Editor**

In `libs/markoff-styled/src/Editor.cpp`, inside `setDocument`, in the `if (!m_binding) { … }` block (`:65-68`), after `m_binding->setTextDocument(...)`:

```cpp
    if (!m_binding) {
        m_binding = new Markoff::SourceTextDocumentBinding(this);
        m_binding->setTextDocument(m_editor->document());
        connect(m_binding, &Markoff::SourceTextDocumentBinding::caretResolved,
                this, [this](int start, int active) {
                    QTextCursor c(m_editor->document());
                    c.setPosition(start);
                    if (active != start)
                        c.setPosition(active, QTextCursor::KeepAnchor);
                    m_editor->setTextCursor(c);
                });
    }
```

- [ ] **Step 4: Wire `caretResolved` in the source Editor**

In `libs/markoff-source/src/Editor.cpp`, in the constructor body immediately after `m_binding->setTextDocument(m_editor->document());` (`:50`):

```cpp
    connect(m_binding, &Markoff::SourceTextDocumentBinding::caretResolved,
            this, [this](int start, int active) {
                QTextCursor c(m_editor->document());
                c.setPosition(start);
                if (active != start)
                    c.setPosition(active, QTextCursor::KeepAnchor);
                m_editor->setTextCursor(c);
            });
```

- [ ] **Step 5: Run to verify all slots pass**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants`
Expected: PASS (all slots, including the 4 new ones).

- [ ] **Step 6: Verify the source widget still builds**

Run: `cmake --build build-dev --target markoff_source -j 8`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-styled/src/Editor.cpp \
        libs/markoff-source/src/Editor.cpp \
        libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "feat(styled,source): apply caretResolved to the widget caret

Both flat-text Editors connect the binding's caretResolved signal to
setTextCursor — the dumb-applier end of the caret-authority chokepoint.
End-to-end tests: Enter at paragraph-end / document-end creates a block
and places the caret in it; mid-paragraph split; backspace-merge caret.
Closes guide B.1 + B.3 for styled and source.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: Documentation + invariant updates

**Files:**
- Modify: `libs/markoff-core/CLAUDE.md`
- Modify: `docs/VIEW-IMPLEMENTORS-GUIDE.md`
- Modify: `libs/markoff-styled/CLAUDE.md`
- Modify: `libs/markoff-source/CLAUDE.md`
- Modify: `docs/queue.md`

- [ ] **Step 1: Amend the canonical-structure invariant in `markoff-core/CLAUDE.md`**

In the "Single-document binding: canonical structure invariant" section, after the paragraph beginning "This invariant is enforced by `applyFlatEdit`'s canonicalization pass…", add:

```markdown
**Interactive-ingress exception — `applyInteractiveNewline`.** The canonical
"no empty blocks" rule holds for the programmatic ingress (`applyFlatEdit`).
The *interactive* ingress `applyInteractiveNewline` (WYSIWYG Enter, routed from
`SourceTextDocumentBinding::onQtContentsChange` for a bare `\n`) deliberately
MAY create a transient empty block — pressing Enter at end of a paragraph must
produce a new empty paragraph with the caret in it. A still-empty paragraph
collapses on serialize/reload (it round-trips as the ordinary `\n\n`
separator), so it never persists. See
`docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`.
```

- [ ] **Step 2: Correct the §B.1 root-cause prose in `VIEW-IMPLEMENTORS-GUIDE.md`**

In §B.1, replace the **Problem** paragraph (the one starting "User presses Enter at the end of a paragraph. The split happens in the model; `applyFlatEdit` canonicalises…") with:

```markdown
**Problem.** User presses Enter at the end of a paragraph. On the flat-text
leaves this surfaced as two coupled failures (reproduced 2026-05-27, spec
`specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`): (1) the
structural edit was *dropped* — `applyFlatEdit`'s cursor-edit start-of-next-
block bias + empty-head suppression + the no-empty-block invariant made a lone
boundary `\n` a no-op, so no paragraph was created; and (2) the caret drifted
into the inter-block gap, reading as "the caret jumped into the next
paragraph." The fix is therefore *both* a forward-path change (an interactive
ingress, `applyInteractiveNewline`, that creates a real — possibly transient
empty — block) and the caret re-assertion below.
```

In §B.1 **Styled / source**, replace the "❌ **OPEN**…" paragraph with:

```markdown
**Styled / source.** ✅ Solved (2026-05-27). Bare Enter routes through
`SourceTextDocumentBinding::onQtContentsChange` → `applyInteractiveNewline`,
which creates the paragraph and returns the caret's target block. The binding
stages `m_pendingCaret{BlockId, offsetInBlock}` and, at the tail of
`onD2DocumentChanged` (after the reverse diff settles — no `singleShot`,
the signal is already debounced), resolves it to a sep-view position and emits
`caretResolved(start, active)`. Each Editor connects that to `setTextCursor`.
This is the single-document analogue of `LiveCursorState` as the chokepoint.
```

- [ ] **Step 3: Update the §B status table + per-concern lines in `VIEW-IMPLEMENTORS-GUIDE.md`**

In the appendix status table, change the source/styled cells:

```markdown
| B.1 | Caret re-assert after structural edit | ✅ | ✅ | ✅ |
| B.2 | Caret survives model rebuild | ✅ | 🟡 | 🟡 |
| B.3 | Multi-block selection-delete caret | ✅ | ✅ | ✅ |
| B.4 | Undo/redo restores caret | 🟡 | 🟡 | 🟡 |
```

For B.2 **Styled / source**, replace "❌ **OPEN**…" with:

```markdown
**Styled / source.** 🟡 Mechanism wired. `syncFromSession` now resolves an
externally-driven `Session::primarySelection()` to a sep-view caret and emits
`caretResolved`, so a collaborator-driven caret update lands correctly. Full
parity awaits the local caret being pushed *to* the Session (not wired today).
```

For B.3 **Styled / source**, replace "❌ **OPEN**…" with:

```markdown
**Styled / source.** ✅ Solved (2026-05-27). The cross-block merge path stages
`m_pendingCaret{mergedInto, joinOffset}`; the chokepoint (B.1) delivers it.
```

For B.4 **Styled / source**, replace "❌ **OPEN**…" with:

```markdown
**Styled / source.** 🟡 The delivery path exists (any model change re-resolves
from the Session anchor via `syncFromSession`), but full restoration depends on
`undoD2`/`redoD2` repopulating the Session selection — not added in the
2026-05-27 fix. As honest as the live side here.
```

Update the closing paragraph after the table ("**The §B cluster is the active frontier…**") to note B.1/B.3 are now closed for the flat-text leaves and B.2/B.4 are the remaining partials.

- [ ] **Step 4: Close the §B notes in the two leaf CLAUDE.md files**

In `libs/markoff-styled/CLAUDE.md`, change the "Required reading" blockquote line about §B from the "open … active frontier" wording to:

```markdown
> §A (text-sync) and §B.1/§B.3 (Enter/merge caret authority) are **solved**
> (2026-05-27, spec
> `../../docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`).
> §B.2/§B.4 are partials (collab/undo caret); see the guide.
```

In `libs/markoff-source/CLAUDE.md`, change the "Required reading" blockquote about the §B gap to:

```markdown
> §B.1/§B.3 cursor authority is **closed** here too — `markoff-source` shares
> `SourceTextDocumentBinding`, and the 2026-05-27 caret-authority fix
> (`../../docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`)
> wired `caretResolved` → `setTextCursor` in its Editor.
```

- [ ] **Step 5: Mark `docs/queue.md` #7 resolved**

In `docs/queue.md`, mark item #7 (the cursor-authority fix) resolved with a pointer to the spec + the landing commits. Add any Discipline-Log entries for smells touched (none expected — the fix adds no `singleShot`/re-entrance guard).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/CLAUDE.md docs/VIEW-IMPLEMENTORS-GUIDE.md \
        libs/markoff-styled/CLAUDE.md libs/markoff-source/CLAUDE.md docs/queue.md
git commit -m "docs: close guide B.1/B.3 for flat views; interactive-ingress invariant

Correct the guide's §B.1 root-cause prose (the edit was dropped, not
just mis-placed), update the §B status table (B.1/B.3 ✅; B.2/B.4 🟡),
add the applyInteractiveNewline exception to the canonical-structure
invariant, and close the leaf CLAUDE.md §B notes.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6: Full-suite verification + dogfood handoff

**Files:** none (verification + optional handoff note).

- [ ] **Step 1: Run the fast suite**

Run: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
Expected: All green except the three known pre-existing live-side failures (`tst_live_render_e2_nav_shift_extend`, `tst_live_render_focus_chokepoint_invariant`, `tst_live_render_cursor_typing_invariant`). If `tst_live_render_focus_chokepoint_invariant` changes character, confirm we did not perturb shared cursor machinery (we did not touch `markoff-live`).

- [ ] **Step 2: Confirm the new tests are in the suite**

Run: `ctest --test-dir build-dev -R 'interactive_newline|binding_caret|dogfood' -N`
Expected: lists `tst_d2_interactive_newline`, `tst_styled_binding_caret`, `tst_styled_dogfood_invariants`.

- [ ] **Step 3: Dogfood confirmation (user)**

Build the styled demo app and ask the user to confirm: Enter at end of a paragraph creates a new paragraph with the caret in it; Enter at end of document works; backspace at block start merges with the caret at the join. (This is the spec §8 user-dogfood gate.) Per repo policy, the styled leaf has no standalone app target unless one exists — if not, the dogfood is via the host app (Corbomite) or a manual `--nested` run with explicit user permission.

- [ ] **Step 4: Final push (only when the user asks)**

Per repo workflow, commit/push at end of session on the user's go-ahead:

```bash
git push
```

---

## Self-review notes (filled by the planner)

- **Spec coverage:** §3 forward path → Task 1 + Task 2 dispatch. §4 chokepoint/signal/retire → Tasks 2–4. §4 `syncFromSession` rewrite → Task 3. §5 tests → Tasks 1/2/3/4 (core, binding, end-to-end). §6 docs → Task 5. §7 out-of-scope respected (selection+Enter untouched; no user→Session push). §8 DoD → Task 6.
- **Type consistency:** `applyInteractiveNewline(uint32_t, Origin) -> BlockId`, `PendingCaret{BlockId block; int offsetInBlock}`, `caretResolved(int,int)`, helpers `sepViewPosOf(BlockId,int)`, `noSepByteToSepViewPos(quint32)`, `emitCaret(int,int)` — used identically across Tasks 1–4.
- **No placeholders:** every code step shows the code; every run step shows the command + expected result.
