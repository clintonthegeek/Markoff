# Flat-view WP unification — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Both flat-text view leaves (`markoff-styled` and `markoff-source`) adopt the word-processor structural rendering that `markoff-live` already uses — single-`\n`-per-boundary in the runtime flat view + paragraph margins providing the visible gap — so Enter at end-of-paragraph produces one extra paragraph gap (not three blank lines).

**Architecture:** A new `MarkoffDocument::widgetFlatView()` (single-`\n` separator, sibling of the canonical `flatView()` which stays the save/parse form). The binding consumes `widgetFlatView()` and its coordinate translation (`findBlockAtSepByte`, `sliceByBlocks`, `sepViewPosOf`, `noSepByteToSepViewPos`) updates separator width 2 → 1. `applyInteractiveNewline` gets one small refinement to behave correctly at the boundary of existing empty blocks. Both Editors apply `QTextBlockFormat::topMargin/bottomMargin` per QTextBlock so the visible gap comes from layout, not whitespace.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets/Test), CMake, CRDT-backed `markoff-core`.

**Authoritative spec:** [`../specs/2026-05-28-flat-view-wp-unification-design.md`](../specs/2026-05-28-flat-view-wp-unification-design.md). Read it (and `docs/INVARIANTS.md`) before starting.

**Build/test commands:**
- Build: `cmake --build build-dev --target <t> -j 8`
- Run one offscreen: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/<t> [slotName]`
- Fast suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
- Known pre-existing failures (do NOT let them block): `tst_live_render_e2_nav_shift_extend`, `tst_live_render_focus_chokepoint_invariant`, `tst_live_render_cursor_typing_invariant`.

---

## Task 1: Core — `widgetFlatView()` runtime accessor

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h` (decl, near `flatView()` at `:208`)
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (defn, near `flatView()` at `:2170`)
- Create: `libs/markoff-core/tests/d2/tst_d2_widget_flat_view.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/d2/tst_d2_widget_flat_view.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

class TstD2WidgetFlatView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void canonical_two_paragraphs_join_with_single_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QCOMPARE(doc.flatView(),       QByteArrayLiteral("Hello\n\nWorld"));
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\nWorld"));
    }

    void empty_block_between_content_renders_as_one_extra_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        // Manufacture an empty block between Hello and World via the
        // interactive ingress (Enter at end of Hello).
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        QCOMPARE(doc.iterateBlocks().size(), 3u);
        // flatView (save form) joins empties with the canonical "\n\n":
        QCOMPARE(doc.flatView(),       QByteArrayLiteral("Hello\n\n\n\nWorld"));
        // widgetFlatView (runtime form) joins with single "\n":
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\n\nWorld"));
    }

    void single_block_no_separator() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello"));
    }

    void empty_document_returns_empty() {
        Markoff::MarkoffDocument doc(1);
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral(""));
    }

    void trailing_empty_block_emits_one_trailing_newline() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        QCOMPARE(doc.iterateBlocks().size(), 2u);
        QCOMPARE(doc.widgetFlatView(), QByteArrayLiteral("Hello\n"));
    }
};

QTEST_APPLESS_MAIN(TstD2WidgetFlatView)
#include "tst_d2_widget_flat_view.moc"
```

Append to `libs/markoff-core/tests/d2/CMakeLists.txt`:

```cmake
qt_add_executable(tst_d2_widget_flat_view tst_d2_widget_flat_view.cpp)
target_link_libraries(tst_d2_widget_flat_view PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d2_widget_flat_view COMMAND tst_d2_widget_flat_view)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build-dev >/dev/null && cmake --build build-dev --target tst_d2_widget_flat_view -j 8`
Expected: FAIL to compile — `widgetFlatView` is not a member of `MarkoffDocument`.

- [ ] **Step 3: Declare the method**

In `libs/markoff-core/include/markoff/core/MarkoffDocument.h`, immediately after the existing `QByteArray flatView() const;` declaration at line 208:

```cpp
    /// Runtime flat view for the WP-rendered flat-text widget views
    /// (`markoff-styled`, `markoff-source`). Joins blocks with a SINGLE
    /// `\n` (one QTextBlock per model block). The visible gap between
    /// paragraphs comes from `QTextBlockFormat::topMargin/bottomMargin`,
    /// not whitespace. Use this — NOT `flatView()` — for the QTextDocument
    /// the user edits.
    ///
    /// `flatView()` remains the canonical save/parse form (`\n\n` between
    /// content blocks).
    QByteArray widgetFlatView() const;
```

- [ ] **Step 4: Implement the method**

In `libs/markoff-core/src/MarkoffDocument.cpp`, immediately after the closing brace of `flatView()` (around line 2184), add:

```cpp
QByteArray MarkoffDocument::widgetFlatView() const
{
    QByteArray out;
    const auto blocks = iterateBlocks();
    if (blocks.empty()) return out;
    for (size_t i = 0; i < blocks.size(); ++i) {
        out += blockText(blocks[i]);
        if (i + 1 < blocks.size())
            out += '\n';  // single-byte WP separator
    }
    return out;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build-dev --target tst_d2_widget_flat_view -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_widget_flat_view`
Expected: PASS (5 slots).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_widget_flat_view.cpp \
        libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "feat(core): widgetFlatView() — runtime flat view for WP-rendered widgets

Sibling of canonical flatView(). Joins blocks with a single \\n instead
of \\n\\n; the visible gap comes from paragraph margins applied at the
widget layer, not whitespace. flatView() remains the save/parse form.
Spec docs/specs/2026-05-28-flat-view-wp-unification-design.md.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Core — `applyInteractiveNewline` boundary refinement

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (definition at `:1663`)
- Modify: `libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp`

- [ ] **Step 1: Write the failing tests**

Add these two slots to the existing class in `libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp` (inside the `private Q_SLOTS:` block, after the existing slots):

```cpp
    void enter_at_start_of_existing_empty_line_pushes_existing_empty_down() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        // First Enter at end of "Hello" -> [Hello, ""].
        const Markoff::BlockId firstEmpty =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QVERIFY(firstEmpty == blocks[1]);
        // Second Enter at the SAME no-sep byte 5 should NOT insert the new
        // block between Hello and firstEmpty — it should land AFTER firstEmpty
        // (vim-faithful: cursor moves down a line).
        const Markoff::BlockId secondEmpty =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 3);
        QVERIFY(blocks[0] == doc.iterateBlocks()[0]);  // Hello
        QVERIFY(blocks[1] == firstEmpty);              // pre-existing empty stays at index 1
        QVERIFY(blocks[2] == secondEmpty);             // new sibling appended after
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
    }

    void enter_with_run_of_empties_inserts_at_last_position() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        // Manufacture [Hello, "", "", World] by two Enters at no-sep 5.
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 4);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[3]), QByteArrayLiteral("World"));
        // One more Enter at the SAME boundary: skip-empties rule attributes
        // to the LAST empty in the run -> new sibling inserted between
        // blocks[2] (the last empty) and blocks[3] (World).
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 5);
        QVERIFY(newBlk == blocks[3]);  // landed between last empty and World
        QCOMPARE(doc.blockText(blocks[4]), QByteArrayLiteral("World"));
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-dev --target tst_d2_interactive_newline -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_interactive_newline enter_at_start_of_existing_empty_line_pushes_existing_empty_down enter_with_run_of_empties_inserts_at_last_position`
Expected: FAIL — current `<=` rule resolves the boundary to Hello (block 0), so the new block is inserted at index 1 (before the existing empty), and `blocks[1] == firstEmpty` is false; the new block is at `blocks[1]`, the old empty bumped to `blocks[2]`.

- [ ] **Step 3: Apply the boundary refinement**

In `libs/markoff-core/src/MarkoffDocument.cpp`, replace the resolution loop inside `applyInteractiveNewline` (the block beginning `// Resolve atByte ...` through the end of the past-end fallback, currently `:1681-1699`) with this skip-empties version:

```cpp
    // Resolve atByte -> (block index, byteInBlock) in no-sep coordinates.
    // At an interior boundary (atByte == cumulative end of block N), the rule
    // is "skip past any run of empty blocks and attribute to the LAST empty
    // in the run" — this makes Enter at the start of an existing empty line
    // insert the new sibling AFTER it (vim-faithful: cursor moves down).
    uint32_t cursor = 0;
    int idx = -1;
    uint32_t byteInBlock = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const uint32_t sz = static_cast<uint32_t>(blockText(blocks[i]).size());
        const uint32_t blkEnd = cursor + sz;
        if (atByte < blkEnd) {                          // strictly within
            idx = static_cast<int>(i);
            byteInBlock = atByte - cursor;
            break;
        }
        if (atByte == blkEnd) {                         // at boundary
            size_t j = i + 1;
            while (j < blocks.size() && blockText(blocks[j]).size() == 0) ++j;
            if (j == i + 1) {
                // No empties after i — attribute to (i, end-of-i).
                idx = static_cast<int>(i);
                byteInBlock = sz;
            } else {
                // Run of empties after i — attribute to the LAST one at offset 0.
                idx = static_cast<int>(j - 1);
                byteInBlock = 0;
            }
            break;
        }
        cursor = blkEnd;
    }
    if (idx == -1) {  // past end -> last block at its end
        idx = static_cast<int>(blocks.size()) - 1;
        byteInBlock = static_cast<uint32_t>(blockText(blocks[size_t(idx)]).size());
    }
```

- [ ] **Step 4: Run all `tst_d2_interactive_newline` slots to confirm**

Run: `cmake --build build-dev --target tst_d2_interactive_newline -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_interactive_newline`
Expected: PASS (6 slots — 4 existing + 2 new).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_interactive_newline.cpp
git commit -m "feat(core): applyInteractiveNewline skip-empties at boundary

At an interior boundary atByte == cumulativeEnd of block N, scan forward
through any run of empty blocks and attribute the resolution to the LAST
empty (offset 0) rather than the content block at its end. This makes
Enter at the start of an existing empty paragraph insert the new sibling
AFTER the existing empty — cursor moves down a line vim/WP-faithfully —
instead of before it. Backward-compatible with all prior Task 1 slots.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: Switch binding + FlatBlockResolve to single-`\n` separator

This is the largest task. It changes the runtime separator from `\n\n` to `\n` in three coupled places: `Detail::findBlockAtSepByte`/`sliceByBlocks` (the `SEP_LEN` constant), `SourceTextDocumentBinding`'s three `flatView()` consumers (switch to `widgetFlatView()`), and `sepViewPosOf` / `noSepByteToSepViewPos` (the `+= 2` becomes `+= 1`). The existing dogfood + binding-caret tests assert positions that shift; their expected values update in the same commit.

**Files:**
- Modify: `libs/markoff-core/src/Detail/FlatBlockResolve.cpp`
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_binding_caret.cpp`

- [ ] **Step 1: Update existing test expectations to the new positions**

In `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`, update the three structural slots' caret-position assertions and the WP "no blank line blowup" addition.

Replace `enter_at_paragraph_end_creates_block_and_places_caret`'s caret assertion (`QCOMPARE(e.textEdit()->textCursor().position(), 7);`) with:

```cpp
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        // WP unification: the QTextDocument plain text adds exactly one
        // '\n' (one new QTextBlock for the empty block), not three blank
        // lines. Length grew by 1, not 4.
        QCOMPARE(qdoc->toPlainText(), QStringLiteral("Alpha\n\nBravo"));
```

(`qdoc` is already in scope as the local `QTextDocument *` from the existing test body.)

Replace `enter_at_document_end_creates_block`'s caret assertion with:

```cpp
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        QCOMPARE(e.textEdit()->document()->toPlainText(), QStringLiteral("Alpha\n"));
```

Replace `enter_mid_paragraph_splits_with_caret_at_new_block`'s caret assertion with:

```cpp
        QCOMPARE(e.textEdit()->textCursor().position(), 6);
        QCOMPARE(e.textEdit()->document()->toPlainText(), QStringLiteral("Alpha\nBravo"));
```

`backspace_at_block_start_merges_with_caret_at_join`'s assertions are unchanged (post-merge string is `"AlphaBravo"` regardless of separator policy; caret at the join is byte length of "Alpha" = 5).

In `libs/markoff-styled/tests/tst_styled_binding_caret.cpp`:

Replace `enter_at_paragraph_end_resolves_caret_to_new_block`'s expectations:

```cpp
        QTRY_COMPARE(spy.count(), 1);
        // WP runtime view: blocks joined by single '\n', so the new empty
        // block's start is at sep-view position 6 ("Hello" + "\n").
        QCOMPARE(spy.at(0).at(0).toInt(), 6);
        QCOMPARE(spy.at(0).at(1).toInt(), 6);
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
```

Replace `session_selection_change_resolves_caret_in_sep_view`'s expectations (anchor at no-sep byte 5 = start of "World" maps to sep-view pos 6 under single-`\n`):

```cpp
        QTRY_VERIFY(spy.count() >= 1);
        const auto last = spy.last();
        QCOMPARE(last.at(0).toInt(), 6);
        QCOMPARE(last.at(1).toInt(), 6);
```

- [ ] **Step 2: Run the updated tests to confirm they fail under the current binding**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants tst_styled_binding_caret -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_binding_caret`
Expected: the structural slots FAIL with actual=7 expected=6. This is the falsifiability gate.

- [ ] **Step 3: Change `FlatBlockResolve` separator constant**

In `libs/markoff-core/src/Detail/FlatBlockResolve.cpp`, change both `SEP_LEN` constants from 2 to 1.

In `findBlockAtSepByte`, replace `constexpr quint32 SEP_LEN = 2;` with:

```cpp
    constexpr quint32 SEP_LEN = 1;  // WP unification: single '\n' between QTextBlocks
```

The body's separator-zone check (`if (sepOff < nextStart)` where `nextStart = blkEnd + SEP_LEN`) now degenerates correctly — when `SEP_LEN == 1`, `nextStart == blkEnd + 1`, and the only `sepOff` value satisfying `blkEnd < sepOff < blkEnd + 1` is the empty set. So the separator-zone bias branch (lines `if (!biasForward) { … } …`) becomes unreachable for sane inputs; that is fine, it stays in place as defensive code.

In `sliceByBlocks`, do the same `SEP_LEN = 2` → `SEP_LEN = 1`.

- [ ] **Step 4: Switch the binding's three `flatView()` consumers to `widgetFlatView()`**

In `libs/markoff-core/src/SourceTextDocumentBinding.cpp`:

`syncQtDocumentFromMarkoff` (line ~`:337`): change `m_subscribedDoc->flatView()` to `m_subscribedDoc->widgetFlatView()`.

`onQtContentsChange` (line ~`:356`): change `doc->flatView()` to `doc->widgetFlatView()`.

`onD2DocumentChanged` (line ~`:457`): change `m_subscribedDoc->flatView()` to `m_subscribedDoc->widgetFlatView()`.

- [ ] **Step 5: Change the binding's coordinate-translation helpers to single-byte separator**

In `libs/markoff-core/src/SourceTextDocumentBinding.cpp`, in `sepViewPosOf` (line ~`:263`), change the per-preceding-block accumulator from `pos += 2;` to:

```cpp
        pos += 1;                                // WP unification: single '\n' separator
```

In `noSepByteToSepViewPos` (line ~`:279`), the walk computes `cursor += sz` between blocks and then defers to `sepViewPosOf`; no separator-width arithmetic is done in this function directly, so no change is needed beyond the helper's update. Verify by inspection that `noSepByteToSepViewPos` still composes correctly with the updated `sepViewPosOf`.

- [ ] **Step 6: Run the updated tests; they should now pass**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants tst_styled_binding_caret -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_binding_caret`
Expected: PASS (all slots).

- [ ] **Step 7: Re-run the existing binding tests to verify no regression**

Run: `cmake --build build-dev --target tst_binding_forward tst_binding_reverse tst_source_widget_binding_roundtrip tst_d2_interactive_newline tst_d2_widget_flat_view -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_binding_forward && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_binding_reverse && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_widget_binding_roundtrip && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_interactive_newline && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_d2_widget_flat_view`
Expected: PASS. If any of `tst_binding_forward`/`tst_binding_reverse`/`tst_source_widget_binding_roundtrip` fail with position-related deltas (`\n\n` → `\n` shifts), update those slots' expected values to the new positions; do not weaken the assertions. (Position adjustments are the only mechanical change permitted here — semantic assertions on block structure should not need adjustment.)

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/src/Detail/FlatBlockResolve.cpp \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp \
        libs/markoff-styled/tests/tst_styled_binding_caret.cpp
# plus any other binding-test files you adjusted in Step 7
git commit -m "feat(core): WP unification — single-\\n runtime separator in flat-text views

Binding now consumes widgetFlatView() (single \\n per boundary) in its
three flat-view call sites. FlatBlockResolve SEP_LEN drops from 2 to 1
in both findBlockAtSepByte and sliceByBlocks. sepViewPosOf adds 1 per
crossed boundary instead of 2. The canonical flatView() is unchanged and
remains the save/parse form. Existing widget-level tests' caret-position
assertions shift accordingly (pos 7 -> pos 6 for 'after Hello + one
\\n'); falsifiability gate proven via Step 2 before the switch.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: Styled — apply paragraph margins via `StyleApplier`

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` (add one slot)

- [ ] **Step 1: Write the failing test**

Add this slot to `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`:

```cpp
    void paragraph_margins_present_on_every_block() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Wait for the styler to run.
        QTest::qWait(50);

        QTextDocument *qdoc = e.textEdit()->document();
        // Both QTextBlocks should carry non-zero top + bottom margins so
        // the visible inter-paragraph gap is layout-driven, not whitespace.
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat bf = b.blockFormat();
            QVERIFY2(bf.topMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 topMargin=%2")
                                .arg(b.blockNumber()).arg(bf.topMargin())));
            QVERIFY2(bf.bottomMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 bottomMargin=%2")
                                .arg(b.blockNumber()).arg(bf.bottomMargin())));
        }
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants paragraph_margins_present_on_every_block`
Expected: FAIL — `baseBlockFormat()` currently sets `setTopMargin(0)` / `setBottomMargin(0)`.

- [ ] **Step 3: Set non-zero margins in `baseBlockFormat`**

In `libs/markoff-styled/src/StyleApplier.cpp`, change `baseBlockFormat()` (currently around `:25-34`) to:

```cpp
QTextBlockFormat baseBlockFormat() {
    QTextBlockFormat fmt;
    // Paragraph margins drive the visible inter-paragraph gap under WP
    // unification (spec 2026-05-28). Hardcoded defaults; theme-driven
    // tuning is a follow-up. Values are point-units; QTextDocument
    // composes margins additively, so adjacent paragraphs sum their
    // bottom+top to ~10pt of total gap, with an empty paragraph contributing
    // ~10pt extra (the visible signal of "one Enter").
    fmt.setTopMargin(5);
    fmt.setBottomMargin(5);
    fmt.setLeftMargin(0);
    fmt.setIndent(0);
    return fmt;
}
```

- [ ] **Step 4: Run to verify it passes + no regression in other styled slots**

Run: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants`
Expected: PASS (all slots).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled/src/StyleApplier.cpp \
        libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "feat(styled): paragraph margins drive the inter-paragraph gap

baseBlockFormat() now sets top/bottom margins of 5pt each. Under WP
unification (spec 2026-05-28) the visible gap between paragraphs comes
from QTextBlockFormat margins, not literal whitespace — so an empty
block between two paragraphs contributes its own margin gap (one
visible 'extra paragraph' worth of vertical space, not three blank
lines). Theme-driven tuning is a follow-up.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: Source — apply paragraph margins in `Markoff::Source::Editor`

**Files:**
- Modify: `libs/markoff-source/src/Editor.cpp`
- Modify: `libs/markoff-source/include/markoff/source/Editor.h`

- [ ] **Step 1: Inspect the source `Editor` to choose the wiring point**

Run: `grep -n "setDocument\|d2DocumentChanged\|connect" libs/markoff-source/src/Editor.cpp | head -20`. Confirm that source's `setDocument(MarkoffDocument *doc)` is the place that already connects the binding to the document. The new margin-applier slot will subscribe to `MarkoffDocument::d2DocumentChanged` in that same method, AFTER `m_binding->setMarkoffDocument(doc)` so the binding's reverse-diff (which alters QTextBlocks) settles first.

- [ ] **Step 2: Declare a private slot in the header**

In `libs/markoff-source/include/markoff/source/Editor.h`, inside the `class Editor`'s `private` section (find `private:` around line 93), add:

```cpp
    /// Re-apply WP-unification paragraph margins to every QTextBlock after
    /// the binding has settled a reverse-diff. Cheap idempotent pass.
    void applyParagraphMargins();
```

- [ ] **Step 3: Write the failing test (add to an existing source-widget test or create a quick check)**

Find or create a tests file. If a `tst_source_widget_construction.cpp` or similar exists, add a slot there. Otherwise create `libs/markoff-source/tests/tst_source_paragraph_margins.cpp` and register it; mirror the styled `paragraph_margins_present_on_every_block` slot but use `Markoff::Source::Editor`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QPlainTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/source/Editor.h>

class TstSourceParagraphMargins : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void margins_present_on_every_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        QTest::qWait(50);

        QTextDocument *qdoc = e.plainTextEdit()->document();
        for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat bf = b.blockFormat();
            QVERIFY2(bf.topMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 topMargin=%2")
                                .arg(b.blockNumber()).arg(bf.topMargin())));
            QVERIFY2(bf.bottomMargin() > 0.0,
                     qPrintable(QStringLiteral("block %1 bottomMargin=%2")
                                .arg(b.blockNumber()).arg(bf.bottomMargin())));
        }
    }
};

QTEST_MAIN(TstSourceParagraphMargins)
#include "tst_source_paragraph_margins.moc"
```

Register in `libs/markoff-source/tests/CMakeLists.txt` (append using the existing add_executable pattern; copy from a sibling like `tst_source_widget_binding_roundtrip` and substitute the new target name; ensure `set_tests_properties` sets `QT_QPA_PLATFORM=offscreen`).

- [ ] **Step 4: Run to verify it fails**

Run: `cmake -S . -B build-dev >/dev/null && cmake --build build-dev --target tst_source_paragraph_margins -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_paragraph_margins`
Expected: FAIL — source Editor does not yet apply margins.

- [ ] **Step 5: Implement `applyParagraphMargins` and wire it**

In `libs/markoff-source/src/Editor.cpp`, define the slot:

```cpp
void Editor::applyParagraphMargins()
{
    if (!m_editor) return;
    QTextDocument *qdoc = m_editor->document();
    if (!qdoc) return;
    QTextCursor c(qdoc);
    QSignalBlocker block(qdoc);   // do NOT loop back through the binding
    c.beginEditBlock();
    for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
        c.setPosition(b.position());
        QTextBlockFormat bf = b.blockFormat();
        // Same values as styled — symmetry maintains a consistent visual
        // gap between view leaves (spec 2026-05-28 §3.5).
        bf.setTopMargin(5);
        bf.setBottomMargin(5);
        c.setBlockFormat(bf);
    }
    c.endEditBlock();
}
```

In the constructor or `setDocument` (whichever owns the binding's connection lifecycle), add the d2-change subscription. In `setDocument`, after the existing `m_binding->setMarkoffDocument(doc);` line, add:

```cpp
    if (doc) {
        QObject::connect(doc, &Markoff::MarkoffDocument::d2DocumentChanged,
                         this, &Editor::applyParagraphMargins,
                         Qt::UniqueConnection);
        // Initial pass — the binding seeded qdoc from widgetFlatView in
        // setMarkoffDocument; margins for the initial blocks need to land too.
        applyParagraphMargins();
    }
```

- [ ] **Step 6: Run to verify it passes + no regression**

Run: `cmake --build build-dev --target tst_source_paragraph_margins tst_source_widget_binding_roundtrip -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_paragraph_margins && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_widget_binding_roundtrip`
Expected: PASS (both binaries).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-source/include/markoff/source/Editor.h \
        libs/markoff-source/src/Editor.cpp \
        libs/markoff-source/tests/tst_source_paragraph_margins.cpp \
        libs/markoff-source/tests/CMakeLists.txt
git commit -m "feat(source): paragraph margins for WP unification

Source Editor now applies QTextBlockFormat top/bottom margins (5pt
each) to every QTextBlock via a post-d2DocumentChanged pass. Same
values as styled — both flat-text views render paragraph gaps as
layout, not whitespace.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6: Source dogfood end-to-end tests

**Files:**
- Create: `libs/markoff-source/tests/tst_source_dogfood_invariants.cpp`
- Modify: `libs/markoff-source/tests/CMakeLists.txt`

The four end-to-end slots that exist for styled (Enter at paragraph-end, Enter at document-end, mid-paragraph split, backspace-merge) should hold identically for source under WP unification. Mirror them.

- [ ] **Step 1: Create the test file**

Create `libs/markoff-source/tests/tst_source_dogfood_invariants.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QPlainTextEdit>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/source/Editor.h>

class TstSourceDogfoodInvariants : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_paragraph_end_creates_block_and_places_caret() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.plainTextEdit()->document();
        QTextCursor c(qdoc);
        c.setPosition(5);  // end of "Alpha"
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 3);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
        QCOMPARE(qdoc->toPlainText(), QStringLiteral("Alpha\n\nBravo"));
    }

    void enter_at_document_end_creates_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(5);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        QCOMPARE(int(doc.iterateBlocks().size()), 2);
        QCOMPARE(doc.blockText(doc.iterateBlocks()[1]), QByteArrayLiteral(""));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
        QCOMPARE(e.plainTextEdit()->document()->toPlainText(),
                 QStringLiteral("Alpha\n"));
    }

    void enter_mid_paragraph_splits_with_caret_at_new_block() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("AlphaBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(5);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Return);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Alpha"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Bravo"));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 6);
    }

    void backspace_at_block_start_merges_with_caret_at_join() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Alpha\n\nBravo"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // In WP unification widgetFlatView, "Alpha\n\nBravo" the file parses
        // to [Alpha, Bravo] and renders as "Alpha\nBravo" (single \n). Start
        // of "Bravo" is at sep-view pos 6.
        QTextCursor c(e.plainTextEdit()->document());
        c.setPosition(6);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Backspace);
        QTest::qWait(80);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 1);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("AlphaBravo"));
        QCOMPARE(e.plainTextEdit()->textCursor().position(), 5);
    }
};

QTEST_MAIN(TstSourceDogfoodInvariants)
#include "tst_source_dogfood_invariants.moc"
```

- [ ] **Step 2: Register in CMake**

In `libs/markoff-source/tests/CMakeLists.txt`, append (matching the existing test-target pattern from the file; copy from `tst_source_paragraph_margins` registration):

```cmake
add_executable(tst_source_dogfood_invariants tst_source_dogfood_invariants.cpp)
add_test(NAME tst_source_dogfood_invariants COMMAND tst_source_dogfood_invariants)
target_link_libraries(tst_source_dogfood_invariants
    PRIVATE Qt6::Test Qt6::Widgets markoff_source markoff_core)
set_tests_properties(tst_source_dogfood_invariants
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build + run; expect PASS**

Run: `cmake -S . -B build-dev >/dev/null && cmake --build build-dev --target tst_source_dogfood_invariants -j 8 && QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_dogfood_invariants`
Expected: PASS (4 slots). If any slot fails — most likely the caretResolved wire isn't routed into the source Editor's `setTextCursor` slot via the binding (the 2026-05-27 Task 4 work should already have it; verify by reading `libs/markoff-source/src/Editor.cpp` for the `caretResolved` connect added there).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-source/tests/tst_source_dogfood_invariants.cpp \
        libs/markoff-source/tests/CMakeLists.txt
git commit -m "test(source): dogfood invariants mirroring styled under WP unification

Source and styled are structurally identical under WP unification —
same applyInteractiveNewline behavior, same caret chokepoint, same
post-edit positions. These four slots are mechanical mirrors of the
styled dogfood invariants; they lock the source widget's structural
correctness against regression.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 7: Docs + guide updates

**Files:**
- Modify: `libs/markoff-core/CLAUDE.md`
- Modify: `docs/VIEW-IMPLEMENTORS-GUIDE.md`
- Modify: `libs/markoff-styled/CLAUDE.md`
- Modify: `libs/markoff-source/CLAUDE.md`
- Modify: `docs/queue.md`

- [ ] **Step 1: Add the `widgetFlatView` note in `markoff-core/CLAUDE.md`**

In `libs/markoff-core/CLAUDE.md`, in the "Canonical text egress — use `serializeForSave()`, not `toMarkdown()`" section (around the discussion of `flatView()`), append the following paragraph:

```markdown
**Runtime flat view — `widgetFlatView()`.** The flat-text view leaves
(`markoff-styled`, `markoff-source`) consume `MarkoffDocument::widgetFlatView()`
in their QTextDocument, NOT `flatView()`. `widgetFlatView()` joins blocks with
a single `\n` instead of `\n\n`; the visible paragraph gap comes from
`QTextBlockFormat::topMargin`/`bottomMargin` applied at the widget layer (WP
unification, spec `docs/specs/2026-05-28-flat-view-wp-unification-design.md`).
`flatView()` remains the canonical save/parse form and is used by
`serializeForSave` and the binding's reverse-path expected-string-build.
```

- [ ] **Step 2: Add §0.2 paradigm note and update §B.1 in the guide**

In `docs/VIEW-IMPLEMENTORS-GUIDE.md`, after §1 (the bimodal foundation section) and before §A.1, insert a new section:

```markdown
## §0.2 — Paragraph delineation is word-processor everywhere

Markoff treats paragraphs as **first-class structural objects**, not as runs
of bytes in a flat string. `markoff-live` lays out per-block QML delegates
with margins; `markoff-styled` and `markoff-source` consume
`MarkoffDocument::widgetFlatView()` (single-`\n` separator between
QTextBlocks) and apply `QTextBlockFormat::topMargin/bottomMargin` to produce
the visible inter-paragraph gap. Pressing Enter creates a *new model block*
(possibly empty, transient) via `applyInteractiveNewline`. The cursor cannot
land "in the gap" because the gap is layout space, not a byte position.

`markoff-source` is a *visual* sibling of `markoff-styled` — distinguished
only by not rendering inline markdown markers (`**`, `_`, `==`, etc. stay
visible as characters). At the structural level (blocks, Enter, backspace,
caret) it is identical to styled.

Authoritative spec:
`specs/2026-05-28-flat-view-wp-unification-design.md`.
```

In the existing §B.1 (Caret re-assertion after a structural edit), in the **Styled / source** paragraph, append a sentence after the existing prose:

```markdown
Under WP unification (2026-05-28) the *rendering* of the structural edit
changed from "literal `\n\n` per boundary" to "single `\n` + margins-driven
gap"; the caret-authority machinery is unchanged.
```

- [ ] **Step 3: Update the leaf CLAUDE.md files**

In `libs/markoff-styled/CLAUDE.md`, in the "v0.1 invariants" section, append:

```markdown
- **WP unification (2026-05-28).** The Editor's QTextDocument is seeded
  from `widgetFlatView()` (single-`\n` separator). `StyleApplier::baseBlockFormat`
  sets `topMargin`/`bottomMargin` = 5pt to provide the visible
  inter-paragraph gap. An empty model block renders as an empty
  QTextBlock whose margins contribute the "extra gap" signal of one
  Enter. Spec
  `../../docs/specs/2026-05-28-flat-view-wp-unification-design.md`.
```

In `libs/markoff-source/CLAUDE.md`, after the "Required reading" blockquote, add:

```markdown
## WP unification (2026-05-28)

Source view is structurally identical to styled — same `widgetFlatView()`,
same caret-authority chokepoint, same Enter/backspace semantics. It is
distinguished only by *not rendering* the inline markdown markers (the
KSyntaxHighlighting pass keeps them visible as characters; styled hides
them). Paragraph margins are applied via `Editor::applyParagraphMargins()`
on every `d2DocumentChanged`. Spec
`../../docs/specs/2026-05-28-flat-view-wp-unification-design.md`.
```

- [ ] **Step 4: Add Discipline Log entry in `docs/queue.md`**

Append to the Discipline Log section:

```markdown
- `libs/markoff-core/src/Detail/FlatBlockResolve.cpp` — `SEP_LEN`
  reduced 2 → 1 under WP unification (`docs/specs/2026-05-28-...`). The
  separator-zone branch in `findBlockAtSepByte` is now unreachable for
  valid inputs (one-byte separator has no interior position); kept as
  defensive code. A direct unit test on the boundary cases of
  `findBlockAtSepByte` remains worth adding as a small follow-up — same
  recommendation as the 2026-05-27 underflow find.
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/CLAUDE.md docs/VIEW-IMPLEMENTORS-GUIDE.md \
        libs/markoff-styled/CLAUDE.md libs/markoff-source/CLAUDE.md docs/queue.md
git commit -m "docs: WP unification — guide §0.2, widgetFlatView note, leaf CLAUDE.mds

Add §0.2 of the View Implementor's Guide naming the paradigm explicitly
(WP everywhere; source is a visual sibling of styled). Update §B.1 prose
to note the rendering-not-mechanism shift. Document widgetFlatView() in
markoff-core/CLAUDE.md as the runtime form; paragraph-margin policy in
both leaf CLAUDE.md files. Discipline Log entry for the SEP_LEN drop.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 8: Full-suite verification + dogfood handoff

**Files:** none (verification + optional handoff).

- [ ] **Step 1: Run the fast suite**

Run: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
Expected: all green except the three known pre-existing live-side failures (`tst_live_render_e2_nav_shift_extend`, `tst_live_render_focus_chokepoint_invariant`, `tst_live_render_cursor_typing_invariant`). Zero new failures.

- [ ] **Step 2: Confirm the new tests appear in the suite**

Run: `ctest --test-dir build-dev -R 'widget_flat_view|paragraph_margins|source_dogfood|interactive_newline|binding_caret|styled_dogfood' -N`
Expected: lists all the new and updated test binaries.

- [ ] **Step 3: Dogfood confirmation (user)**

The user dogfoods the styled (and ideally source) Editor and confirms: one Enter at end of paragraph = one extra margin-gap (not three blank lines); cursor lands in the new empty paragraph; backspace dissolves the boundary cleanly; pressing Enter at the start of an existing empty paragraph moves the cursor down one line (the empty above stays in place).

If anything is wrong, surface it; otherwise this spec is closed.

- [ ] **Step 4: Final push (only when the user asks)**

```bash
git push
```

---

## Self-review notes

- **Spec coverage:** §3.1 widgetFlatView → Task 1. §3.2 separator-width-1 → Task 3 (FlatBlockResolve + sepViewPosOf). §3.3 boundary refinement → Task 2. §3.4 styled margins → Task 4. §3.5 source margins → Task 5. §4 save (unchanged) → no task needed (it just stays). §5 tests → Tasks 1/2/3/4/5/6. §6 docs → Task 7. §7 v2 Shift+Enter — explicitly out of scope. §9 DoD → Task 8.
- **Type consistency:** `widgetFlatView()` → `QByteArray`, used by binding's three call sites in Task 3. `applyParagraphMargins()` → `void` slot in source Editor (Task 5). `baseBlockFormat()` in styled (Task 4) keeps its existing signature.
- **No placeholders:** every code step shows the code; every run step shows command + expected outcome. Task 5 step 1 asks the implementer to inspect existing wiring before adding the connect — that's deliberate (the binding/connect order is locally context-dependent), not a placeholder.
