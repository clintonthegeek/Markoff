# Round-Trip Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `editor.toPlainText()`, file save, and select-all + copy + paste reproduce the source `.md` byte-for-byte; render the exact blank-line layout of the source as cursor-editable empty paragraphs.

**Architecture:** Enforce a splitter invariant that every source byte is owned by some segment's `text`, with a single `"\n"` join separator between segments. Blank lines become real `QTextBlock`s inside text items, including in dedicated "spacer" text segments between otherwise-adjacent block items. Retire `leadSeparator` and `interItemNewlines` entirely.

**Tech Stack:** C++20, Qt6, tree-sitter (via `MarkoffParser`), QTest.

**Spec:** `docs/specs/2026-04-17-roundtrip-fidelity-design.md`.

**Build dir:** `build/` at the Corbomite root (legacy convention, not `build-dev`). Test binaries land in `build/bin/`.

**Common commands:**

Configure once (from `/home/clinton/dev/Markoff` root):
```bash
cmake -B build -DCORBOMITE_DEV_BUILD=ON
```

Build (from the Corbomite root):
```bash
cmake --build build --target markoff markoff-parser \
  tst_markoff_parser_splitter tst_markoff_scene_coordinator \
  tst_markoff_selection tst_markoff_folding_integration \
  tst_markoff_folding_model -j
```

Run a single test:
```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_parser_splitter
```

Run the full markoff/markoff-parser test suites:
```bash
cd build && ctest -R "markoff" --output-on-failure
```

---

## File Structure

### Files to modify

- `libs/markoff-parser/include/markoff-parser/MarkdownSplitter.h` — drop `MarkdownSegment::leadSeparator`.
- `libs/markoff-parser/src/MarkdownSplitter.cpp` — rewrite `split()` to satisfy the join-identity invariant.
- `libs/markoff-parser/tests/tst_splitter.cpp` — add join-identity tests, update any structure-assertion tests that diverge.
- `libs/markoff/src/SelectableItem.h` — drop `leadSeparator()` / `setLeadSeparator()` / `m_leadSeparator`.
- `libs/markoff/src/SceneCoordinator.h` — drop `interItemNewlines()` declaration.
- `libs/markoff/src/SceneCoordinator.cpp` — drop `setLeadSeparator` calls; rewrite `toMarkdown()` to pure `"\n"` join; replace `interItemNewlines` call sites in `globalPositionOf()`, `itemAtGlobalLine()`, `ensureHeadingMap()` with `+= 1`.
- `libs/markoff/src/SelectionManager.cpp` — rewrite `serializeAsMarkdown()` to pure `"\n"` join.
- `libs/markoff/tests/tst_scene_coordinator.cpp` — extend round-trip coverage; add blank-line editability + clipboard + heading-line tests.

### No new files.

---

## Task 1: Add failing parser-level join-identity tests

**Why first:** TDD. Pin down the new splitter contract as executable tests before changing any code. These tests will FAIL against the current splitter for two-block-with-gap cases; that's the trigger for Task 2.

**Files:**
- Modify: `libs/markoff-parser/tests/tst_splitter.cpp` (add slots + slot implementations)

- [ ] **Step 1: Add new slot declarations to the test class**

In `libs/markoff-parser/tests/tst_splitter.cpp`, add these slots after the existing declarations in the `private Q_SLOTS:` block (before the closing `};`):

```cpp
    void testJoinIdentity_empty();
    void testJoinIdentity_textOnly();
    void testJoinIdentity_blockOnly();
    void testJoinIdentity_blockAtStart();
    void testJoinIdentity_blockAtEnd();
    void testJoinIdentity_leadingBlanks();
    void testJoinIdentity_trailingNewline();
    void testJoinIdentity_trailingBlanks();
    void testJoinIdentity_twoBlocksAdjacent();
    void testJoinIdentity_twoBlocksOneBlank();
    void testJoinIdentity_twoBlocksEightBlanks();
    void testJoinIdentity_threeBlocksMixedGaps();
    void testJoinIdentity_textWithBlockAndBlanks();
```

- [ ] **Step 2: Add a join-identity helper at the top of the anonymous namespace**

At the top of `tst_splitter.cpp`, just below the `using namespace Markoff;` line, add:

```cpp
namespace {
    /// Concatenate every segment's text with a single '\n' between
    /// segments. Per the splitter invariant, the result must equal
    /// the source exactly.
    QString joinIdentity(const QList<MarkdownSegment> &segs)
    {
        QString out;
        for (int i = 0; i < segs.size(); ++i) {
            if (i > 0) out += QLatin1Char('\n');
            out += segs[i].text;
        }
        return out;
    }
}
```

- [ ] **Step 3: Write the new slot implementations at the bottom of the file (before `QTEST_MAIN`)**

Insert directly above the existing `QTEST_MAIN(TestSplitter)` line:

```cpp
void TestSplitter::testJoinIdentity_empty()
{
    TreeSitterParser parser;
    const QString src;
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_textOnly()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("line1\nline2\n\nline4\n\n\nline7");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockOnly()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockAtStart()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)\n\nafter");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockAtEnd()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("before\n\n\n![alt](img.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_leadingBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("\n\n![alt](img.png)\n\nafter");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_trailingNewline()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)\n");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_trailingBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("A\n\n\n\n");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksAdjacent()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![a](a.png)\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksOneBlank()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![a](a.png)\n\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksEightBlanks()
{
    // The banner case the user called out: 8 blank lines between blocks
    // in the source must survive the whole stack end-to-end.
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "![a](a.png)\n\n\n\n\n\n\n\n\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_threeBlocksMixedGaps()
{
    // a and b adjacent (single '\n'); b and c separated by three blanks.
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "![a](a.png)\n![b](b.png)\n\n\n\n![c](c.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_textWithBlockAndBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "# Title\n\n\nIntro paragraph.\n\n\n\n![a](a.png)\n\nOutro.");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}
```

- [ ] **Step 4: Build the failing test binary and run it**

```bash
cmake --build build --target tst_markoff_parser_splitter -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_parser_splitter
```

Expected: several of the new `testJoinIdentity_*` slots FAIL (specifically the ones involving images and non-standard whitespace). The existing slots (`testNoBlocks`, `testSingleTable`, etc.) still PASS. This proves the tests exercise the new invariant and the current splitter does not satisfy it.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-parser/tests/tst_splitter.cpp
git commit -m "$(cat <<'EOF'
Test: add failing join-identity tests for splitter

Pins down the new splitter invariant: concatenating every segment's
text with a single "\n" between segments must equal the source
byte-for-byte. Tests fail against the current splitter; Task 2
rewrites split() to satisfy them.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Rewrite `MarkdownSplitter::split()` to satisfy the invariant

**Why now:** The tests from Task 1 drive this change. We adopt the new strip rules from the spec's "Byte-accounting reference" section.

**Transitional compat note:** This task keeps `MarkdownSegment::leadSeparator` populated with `"\n"` (non-empty only on non-first segments), so `SceneCoordinator::toMarkdown()` — which still uses `leadSeparator` — continues to produce correct output. Task 3 will simplify that call site and Task 4 will delete the field.

**Files:**
- Modify: `libs/markoff-parser/src/MarkdownSplitter.cpp`

- [ ] **Step 1: Replace the body of `MarkdownSplitter::split()` with the new algorithm**

Open `libs/markoff-parser/src/MarkdownSplitter.cpp`. Replace the entire function body (lines 7–140) with:

```cpp
QList<MarkdownSegment> MarkdownSplitter::split(const QString &markdown,
                                                TreeSitterParser &parser)
{
    QList<MarkdownSegment> segments;

    // Trivial fall-through: empty input or parse failure -> one text segment.
    if (markdown.isEmpty()) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = 0;
        segments.append(seg);
        return segments;
    }
    if (!parser.parse(markdown)) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    // Collect only the boundaries we split on. Tables stay in text; the
    // editor converts them to QTextTable inside a text item.
    QList<TreeSitterParser::BlockBoundary> blockBoundaries;
    for (const auto &b : parser.findBlockBoundaries()) {
        if (b.type == TreeSitterParser::BlockBoundary::Table)
            continue;
        blockBoundaries.append(b);
    }

    if (blockBoundaries.isEmpty()) {
        // Whole document is one text segment.
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown;
        seg.sourceStart = 0;
        seg.sourceEnd = markdown.length();
        segments.append(seg);
        return segments;
    }

    auto emitText = [&](int runStart, int runEnd) {
        MarkdownSegment seg;
        seg.type = MarkdownSegment::Text;
        seg.text = markdown.mid(runStart, runEnd - runStart);
        seg.sourceStart = runStart;
        seg.sourceEnd = runEnd;
        if (!segments.isEmpty())
            seg.leadSeparator = QStringLiteral("\n");
        segments.append(seg);
    };

    auto emitBlock = [&](const TreeSitterParser::BlockBoundary &b) {
        MarkdownSegment seg;
        switch (b.type) {
        case TreeSitterParser::BlockBoundary::FencedCodeBlock:
            seg.type = MarkdownSegment::FencedCodeBlock;
            break;
        case TreeSitterParser::BlockBoundary::Image:
            seg.type = MarkdownSegment::Image;
            break;
        case TreeSitterParser::BlockBoundary::Table:
            Q_UNREACHABLE();
            break;
        }
        int start = b.startChar;
        int end = b.endChar;
        // Strip a trailing '\n' from the block's content (it is the join
        // separator to the next segment).
        if (end > start && markdown.at(end - 1) == QLatin1Char('\n'))
            end -= 1;
        seg.text = markdown.mid(start, end - start);
        seg.sourceStart = start;
        seg.sourceEnd = end;
        if (!segments.isEmpty())
            seg.leadSeparator = QStringLiteral("\n");
        segments.append(seg);
    };

    int cursor = 0;
    for (int k = 0; k < blockBoundaries.size(); ++k) {
        const auto &b = blockBoundaries[k];

        // --- Pre-block region: [runStart, runEnd) in source ---
        int runStart = cursor;
        int runEnd = b.startChar;
        const int rawLen = runEnd - runStart;

        if (segments.isEmpty()) {
            // k == 0 with no prior segment: strip at most one trailing '\n'
            // (join separator to the following block). No leading boundary.
            int strippedEnd = runEnd;
            if (strippedEnd > runStart
                && markdown.at(strippedEnd - 1) == QLatin1Char('\n'))
                strippedEnd -= 1;
            const bool contentNonEmpty = (strippedEnd > runStart);
            const bool emitForLeadingNewline = (rawLen >= 1);
            if (contentNonEmpty || emitForLeadingNewline)
                emitText(runStart, strippedEnd);
        } else {
            // k > 0: strip at most one leading '\n' AND one trailing '\n'.
            int strippedStart = runStart;
            int strippedEnd = runEnd;
            if (strippedStart < strippedEnd
                && markdown.at(strippedStart) == QLatin1Char('\n'))
                strippedStart += 1;
            if (strippedStart < strippedEnd
                && markdown.at(strippedEnd - 1) == QLatin1Char('\n'))
                strippedEnd -= 1;
            const bool contentNonEmpty = (strippedEnd > strippedStart);
            const bool emitForBlankLine = (rawLen >= 2);
            if (contentNonEmpty || emitForBlankLine)
                emitText(strippedStart, strippedEnd);
        }

        // --- Block segment ---
        emitBlock(b);
        cursor = b.endChar;
    }

    // --- Post-last-block region ---
    {
        const int runStart = cursor;
        const int runEnd = markdown.length();
        const int rawLen = runEnd - runStart;
        int strippedStart = runStart;
        if (strippedStart < runEnd
            && markdown.at(strippedStart) == QLatin1Char('\n'))
            strippedStart += 1;
        const bool contentNonEmpty = (runEnd > strippedStart);
        const bool emitForTrailingNewline = (rawLen >= 1);
        if (contentNonEmpty || emitForTrailingNewline)
            emitText(strippedStart, runEnd);
    }

    // Invariant: at least one segment always exists by this point
    // (we returned early for empty / no-boundary inputs). Assert for
    // safety.
    Q_ASSERT(!segments.isEmpty());
    return segments;
}
```

- [ ] **Step 2: Update existing splitter tests that inspect structure**

Most existing tests use tables or fenced code blocks that stay in text — they're unaffected. Two cases need review:

1. `testShowcaseFile` asserts `segments.size() >= 3` with one image in the file. Under the new splitter, leading/trailing text segments still surround the image, so `>= 3` still holds. Leave it.

Run the splitter binary to confirm all old tests still pass:

```bash
cmake --build build --target tst_markoff_parser_splitter -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_parser_splitter
```

Expected: all `testJoinIdentity_*` slots PASS; all pre-existing slots still PASS.

- [ ] **Step 3: Run the broader test suite to catch any downstream fallout**

```bash
cd build && ctest -R markoff --output-on-failure
```

Expected: all pass. The editor-side `tst_markoff_scene_coordinator` round-trip tests should still pass because `SceneCoordinator::toMarkdown()` now receives `leadSeparator = "\n"` on each non-first segment, which matches its current behavior for the cases covered (text-text and text-block boundaries).

**If any test fails, stop and investigate.** The most likely failure is a round-trip test that previously relied on the old splitter's trimming behavior. Review that test's source and expected output against the new invariant before proceeding.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-parser/src/MarkdownSplitter.cpp
git commit -m "$(cat <<'EOF'
Splitter: adopt join-identity invariant

Every source byte is owned by exactly one segment's text; consecutive
segments are separated by exactly one '\n' in the source. Pre-block
regions strip one leading '\n' (for k>0) and one trailing '\n'; block
segments strip one trailing '\n'; trailing regions strip one leading
'\n'. Spacer text segments are emitted whenever two blocks are
separated by >= 2 '\n's. leadSeparator is left set to "\n" on each
non-first segment for transitional compat with the editor's current
toMarkdown; Tasks 3-4 retire it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Simplify `SceneCoordinator::toMarkdown()` and `SelectionManager::serializeAsMarkdown()`

**Why:** Under the new splitter contract the join separator is always a single `"\n"`. Both serialization paths become pure joins.

**Files:**
- Modify: `libs/markoff/src/SceneCoordinator.cpp:405-418`
- Modify: `libs/markoff/src/SelectionManager.cpp:291-326`
- Modify: `libs/markoff/tests/tst_scene_coordinator.cpp` (add clipboard round-trip test)

- [ ] **Step 1: Add a failing clipboard round-trip test**

Open `libs/markoff/tests/tst_scene_coordinator.cpp`. Add this slot declaration to `private Q_SLOTS:` right after `roundTripPureText();`:

```cpp
    void clipboardRoundTripPreservesEightBlankLines();
```

Add this slot body immediately after `roundTripPureText()`'s implementation:

```cpp
void TstSceneCoordinator::clipboardRoundTripPreservesEightBlankLines()
{
    // select-all + the cross-boundary clipboard path must reproduce
    // the source byte-for-byte, including 8 blank lines between blocks.
    const QString src = QStringLiteral(
        "before\n\n\n\n\n\n\n\n\n![alt](img.png)\n\n\nafter");

    Editor editor;
    editor.resize(800, 400);
    editor.setPlainText(src);
    editor.show();
    QApplication::processEvents();

    auto *scene = static_cast<SelectionScene *>(editor.scene());
    QVERIFY(scene);
    auto *mgr = scene->selectionManager();
    QVERIFY(mgr);

    mgr->selectAll();
    QApplication::processEvents();

    QCOMPARE(mgr->serializeAsMarkdown(), src);
}
```

This test needs `#include "SelectionScene.h"` and `#include "SelectionManager.h"` at the top of the test file. Check if they're already present; if not, add them alongside the existing includes. The test's include directory already covers `libs/markoff/src/`, so these headers resolve directly.

Build and run the test:
```bash
cmake --build build --target tst_markoff_scene_coordinator -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_scene_coordinator \
  clipboardRoundTripPreservesEightBlankLines
```

Expected: FAIL. `serializeAsMarkdown()` returns the source with the 8 blank lines normalized to `"\n\n"` separators, so the comparison shows a diff.

- [ ] **Step 2: Rewrite `SceneCoordinator::toMarkdown()`**

Open `libs/markoff/src/SceneCoordinator.cpp`. Replace the `toMarkdown()` implementation (around lines 405–418):

```cpp
QString SceneCoordinator::toMarkdown() const
{
    QString result;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i > 0)
            result += QLatin1Char('\n');
        result += m_items[i]->toMarkdown();
    }
    return result;
}
```

- [ ] **Step 3: Rewrite `SelectionManager::serializeAsMarkdown()`**

Open `libs/markoff/src/SelectionManager.cpp`. Replace the loop body at lines 305–324 (inside `for (int i = lo; i <= hi; ++i)`). The corrected function:

```cpp
QString SelectionManager::serializeAsMarkdown() const
{
    if (!m_anchorItem || !m_currentItem)
        return {};

    int anchorIdx = m_items.indexOf(m_anchorItem);
    int currentIdx = m_items.indexOf(m_currentItem);
    if (anchorIdx < 0 || currentIdx < 0)
        return {};

    int lo = qMin(anchorIdx, currentIdx);
    int hi = qMax(anchorIdx, currentIdx);
    QString result;

    for (int i = lo; i <= hi; ++i) {
        if (i > lo)
            result += QLatin1Char('\n');
        SelectableItem *item = m_items[i];
        if (i == anchorIdx || i == currentIdx) {
            if (item->isTextItem())
                result += item->selectedMarkdown();
            else
                result += item->toMarkdown();
        } else {
            if (item->isTextItem())
                result += item->allMarkdown();
            else
                result += item->toMarkdown();
        }
    }
    return result;
}
```

- [ ] **Step 4: Run the scene-coordinator and selection tests**

```bash
cmake --build build --target tst_markoff_scene_coordinator tst_markoff_selection -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_scene_coordinator
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_selection
```

Expected: all pass, including the new `clipboardRoundTripPreservesEightBlankLines`.

- [ ] **Step 5: Run the full markoff test suite**

```bash
cd build && ctest -R markoff --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/SceneCoordinator.cpp libs/markoff/src/SelectionManager.cpp libs/markoff/tests/tst_scene_coordinator.cpp
git commit -m "$(cat <<'EOF'
Serialization: pure "\n" join in toMarkdown and serializeAsMarkdown

Under the splitter's join-identity invariant, the only separator
between items is a single '\n'. Both SceneCoordinator::toMarkdown
(file-save / toPlainText path) and SelectionManager::serializeAsMarkdown
(cross-boundary clipboard path) become pure joins. Adds a regression
test asserting clipboard copy of a document with 8 blank lines
between blocks round-trips byte-for-byte.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Retire `leadSeparator`

**Why:** No consumer uses it anymore. Delete the field and its plumbing.

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/MarkdownSplitter.h`
- Modify: `libs/markoff-parser/src/MarkdownSplitter.cpp` (drop the `seg.leadSeparator = ...` lines in the lambdas)
- Modify: `libs/markoff/src/SelectableItem.h`
- Modify: `libs/markoff/src/SceneCoordinator.cpp:182,230,678,709` (drop the `setLeadSeparator` calls)

- [ ] **Step 1: Remove `leadSeparator` from `MarkdownSegment`**

Open `libs/markoff-parser/include/markoff-parser/MarkdownSplitter.h`. Remove these lines from the `MarkdownSegment` struct (lines 19–23):

```cpp
    /// Whitespace (typically newlines) between the previous emitted
    /// segment's content end and this segment's content start. Empty for
    /// the first segment. Used to reconstruct the original inter-segment
    /// blank-line count on serialization.
    QString leadSeparator;
```

- [ ] **Step 2: Remove `leadSeparator` assignments in the splitter**

Open `libs/markoff-parser/src/MarkdownSplitter.cpp`. In both lambdas (`emitText` and `emitBlock`) added in Task 2, delete these two lines:

```cpp
        if (!segments.isEmpty())
            seg.leadSeparator = QStringLiteral("\n");
```

Each lambda has one such pair; remove both occurrences.

- [ ] **Step 3: Remove `leadSeparator()` / `setLeadSeparator()` / `m_leadSeparator` from `SelectableItem`**

Open `libs/markoff/src/SelectableItem.h`. Delete lines 40–51 (the doc block for `leadSeparator()`, the getter, the setter, the private member) and the now-empty `private:` block if unused. The resulting trailing portion of the class (starting right after the `virtual QString toMarkdown() const = 0;` line) should read:

```cpp
    virtual QString toMarkdown() const = 0;
};
```

If the `#include <QString>` on line 5 becomes unnecessary after this change (check other usages in the file), leave it — a `QString` still appears elsewhere via `selectedMarkdown()` / `allMarkdown()` / `toMarkdown()` return types. Don't touch the include.

- [ ] **Step 4: Remove the four `setLeadSeparator` calls in `SceneCoordinator.cpp`**

Open `libs/markoff/src/SceneCoordinator.cpp`. Delete each of these four lines:
- `item->setLeadSeparator(seg.leadSeparator);` at approx line 182 (inside `loadMarkdown`, Text branch)
- `item->setLeadSeparator(seg.leadSeparator);` at approx line 230 (inside `loadMarkdown`, Image branch)
- `item->setLeadSeparator(seg.leadSeparator);` at approx line 678 (inside `reparse`, Text branch)
- `item->setLeadSeparator(seg.leadSeparator);` at approx line 709 (inside `reparse`, Image branch)

Use grep to locate current line numbers; the exact positions may have shifted:
```bash
grep -n setLeadSeparator libs/markoff/src/SceneCoordinator.cpp
```

- [ ] **Step 5: Build everything**

```bash
cmake --build build --target markoff markoff-parser -j
```

Expected: clean build. Compiler errors here would mean a remaining consumer of `leadSeparator`; grep should have caught them all.

```bash
grep -rn leadSeparator libs/markoff libs/markoff-parser
```

Expected: zero matches in production code. Only references in `docs/` and spec files are permitted.

- [ ] **Step 6: Run the full markoff test suite**

```bash
cmake --build build --target \
  tst_markoff_parser_splitter tst_markoff_scene_coordinator \
  tst_markoff_selection -j
cd build && ctest -R markoff --output-on-failure
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-parser/include/markoff-parser/MarkdownSplitter.h \
        libs/markoff-parser/src/MarkdownSplitter.cpp \
        libs/markoff/src/SelectableItem.h \
        libs/markoff/src/SceneCoordinator.cpp
git commit -m "$(cat <<'EOF'
Retire leadSeparator

No consumer uses it anymore: serialization uses a pure "\n" join and
items carry their blank-line content in their own QTextDocuments.
Drops the field from MarkdownSegment and SelectableItem and the four
setLeadSeparator call sites in SceneCoordinator.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Remove `interItemNewlines` heuristic; simplify line math

**Why:** Under the new splitter invariant there is exactly one `\n` between consecutive items (the join separator). The `1 or 2` branch in `interItemNewlines` is dead code — or worse, actively wrong when a spacer text segment sits between two blocks, where the count should still be `1` (the join `\n`), but the previous code would return `2`.

**Files:**
- Modify: `libs/markoff/src/SceneCoordinator.h` (drop the `interItemNewlines` declaration)
- Modify: `libs/markoff/src/SceneCoordinator.cpp` (drop the definition and update three call sites: `globalPositionOf`, `itemAtGlobalLine`, `ensureHeadingMap`)
- Modify: `libs/markoff/tests/tst_scene_coordinator.cpp` (add a heading-line-math test)

- [ ] **Step 1: Add a failing heading-line-math test**

In `libs/markoff/tests/tst_scene_coordinator.cpp`, add this slot to `private Q_SLOTS:` after `clipboardRoundTripPreservesEightBlankLines()`:

```cpp
    void cursorLineAccountsForBlankLineGaps();
```

Include at the top of the file (only if not already present):

```cpp
#include <markoff/Editor.h>
```

This should already be present. Add this slot body just before `QTEST_MAIN`:

```cpp
void TstSceneCoordinator::cursorLineAccountsForBlankLineGaps()
{
    // Heading on source line 11, counting every '\n' including blanks.
    // 5 blank lines between a heading and an image, then another heading.
    const QString src = QStringLiteral(
        "# First\n\n\n\n\n\n![alt](img.png)\n\n\n# Second\n");
    //     line 1: "# First"
    //     lines 2-6: blanks (5 of them)
    //     line 7: "![alt](img.png)"
    //     lines 8-9: blanks
    //     line 10: "# Second"
    //     line 11: trailing '\n' (empty last block of trailing text seg)

    Editor editor;
    editor.resize(800, 400);
    editor.setPlainText(src);
    editor.show();
    QApplication::processEvents();

    // Position cursor at the start of "# Second" in its item.
    auto *coord = editor.coordinatorForTesting();
    QVERIFY(coord);

    bool placed = false;
    for (auto *item : coord->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        const QString all = ti->allMarkdown();
        const int pos = all.indexOf(QStringLiteral("# Second"));
        if (pos >= 0) {
            QTextCursor c(ti->document());
            c.setPosition(pos);
            ti->textControl()->setTextCursor(c);
            ti->setFocus(Qt::MouseFocusReason);
            placed = true;
            break;
        }
    }
    QVERIFY2(placed, "Could not locate '# Second' in any text item");
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), 10);
}
```

Build and run:
```bash
cmake --build build --target tst_markoff_scene_coordinator -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_scene_coordinator \
  cursorLineAccountsForBlankLineGaps
```

Expected: FAIL (reported line is wrong — under-counts or over-counts because `interItemNewlines` assumes 2 \n's between text and block, missing the real 6-\n gap).

- [ ] **Step 2: Remove the `interItemNewlines` declaration**

Open `libs/markoff/src/SceneCoordinator.h`. Find and delete the declaration:

```cpp
    static int interItemNewlines(bool prevIsText, bool currIsText);
```

- [ ] **Step 3: Remove the `interItemNewlines` definition**

Open `libs/markoff/src/SceneCoordinator.cpp`. Delete the function definition at approx lines 243–246:

```cpp
int SceneCoordinator::interItemNewlines(bool prevIsText, bool currIsText)
{
    return (prevIsText && currIsText) ? 1 : 2;
}
```

- [ ] **Step 4: Update `globalPositionOf()` call site**

In the same file, find the loop at approx lines 283–324. Replace:

```cpp
        if (i > 0)
            line += interItemNewlines(m_items[i - 1]->isTextItem(),
                                      m_items[i]->isTextItem());
```

with:

```cpp
        if (i > 0)
            line += 1;
```

- [ ] **Step 5: Update `itemAtGlobalLine()` call site**

In the same file, find the loop at approx lines 331–380. Replace the same two-line `line += interItemNewlines(...)` call with:

```cpp
        if (i > 0)
            line += 1;
```

- [ ] **Step 6: Update `ensureHeadingMap()` call site**

Still in `SceneCoordinator.cpp`, find the loop at approx lines 841–906. Replace:

```cpp
        if (itemIdx > 0) {
            const bool prevBlock = !m_items[itemIdx - 1]->isTextItem();
            const bool currBlock = !m_items[itemIdx]->isTextItem();
            srcLine += (prevBlock || currBlock) ? 2 : 1;
        }
```

with:

```cpp
        if (itemIdx > 0)
            srcLine += 1;
```

- [ ] **Step 7: Re-run the new test and the full suite**

```bash
cmake --build build --target tst_markoff_scene_coordinator tst_markoff_folding_integration tst_markoff_folding_model -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_scene_coordinator
cd build && ctest -R markoff --output-on-failure
```

Expected: `cursorLineAccountsForBlankLineGaps` now passes; all pre-existing tests still pass (folding, heading-map, global-coordinates).

**If folding or heading-map tests regress**, the most likely cause is a test whose source string happened to conform to the old heuristic's assumption. Inspect the failing test's source and recompute the expected line under the new model: every `\n` in the source maps to exactly one line boundary. Update the test's expectation if it was encoding the old buggy behavior; do NOT revert the code change.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff/src/SceneCoordinator.h libs/markoff/src/SceneCoordinator.cpp libs/markoff/tests/tst_scene_coordinator.cpp
git commit -m "$(cat <<'EOF'
Line math: drop interItemNewlines heuristic

Under the splitter's join-identity invariant there is exactly one '\n'
between consecutive items. The 1-or-2 heuristic in interItemNewlines
was wrong whenever a spacer text segment sat between two blocks, or
whenever a text item absorbed blank lines adjacent to a block — both
of which are now the normal case. Replaces the three call sites with
'+= 1' and removes the helper. Adds a regression test asserting
cursorLine() reports the correct source line across a 5-blank-line gap.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Editor-level coverage — blank-line editability and extended round-trips

**Why:** The spec requires blank lines to be cursor-reachable editable paragraphs. We verify that directly (type a character into a blank-line region, confirm `toMarkdown()` reflects it). Also add a handful of extended round-trip cases so future regressions are caught at the editor layer, not just the splitter layer.

**Files:**
- Modify: `libs/markoff/tests/tst_scene_coordinator.cpp` (add tests)

- [ ] **Step 1: Add test slot declarations**

In `libs/markoff/tests/tst_scene_coordinator.cpp`, add these to `private Q_SLOTS:`:

```cpp
    void blankLinesAreEditableParagraphs();
    void roundTripLeadingBlankLines();
    void roundTripSingleTrailingNewline();
    void roundTripTwoAdjacentImagesWithGap();
    void roundTripEightBlankLinesBeforeImage();
```

- [ ] **Step 2: Add slot implementations**

Add these just before `QTEST_MAIN`:

```cpp
void TstSceneCoordinator::blankLinesAreEditableParagraphs()
{
    // 3 blank lines between a paragraph and an image; the "spacer" must
    // be a text item with 3 empty paragraphs, and typing into one of
    // them must reflect in toMarkdown().
    const QString src = QStringLiteral("A\n\n\n\n![alt](img.png)");
    //     line 1: "A", lines 2-4: blanks, line 5: image
    //     text segment content between A and image: "A\n\n\n"
    //     (4 blocks: "A", "", "", "")

    Editor editor;
    editor.resize(800, 400);
    editor.setPlainText(src);
    editor.show();
    QApplication::processEvents();

    // Find the text item whose content contains "A".
    auto *coord = editor.coordinatorForTesting();
    QVERIFY(coord);

    MarkdownTextItem *target = nullptr;
    for (auto *item : coord->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        if (ti->allMarkdown().contains(QLatin1Char('A'))) {
            target = ti;
            break;
        }
    }
    QVERIFY(target);

    // Expect 4 blocks in the QTextDocument: "A", "", "", "".
    QTextDocument *doc = target->document();
    int blockCount = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        ++blockCount;
    QCOMPARE(blockCount, 4);

    // Position cursor at the start of the 2nd block (the first blank
    // paragraph between A and the image), type 'X'.
    QTextBlock secondBlock = doc->findBlockByNumber(1);
    QVERIFY(secondBlock.isValid());
    QTextCursor c(doc);
    c.setPosition(secondBlock.position());
    target->textControl()->setTextCursor(c);
    target->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();

    c.insertText(QStringLiteral("X"));
    QApplication::processEvents();

    // toMarkdown now reflects the edit; the X lives on its own line
    // between A and the remaining blanks.
    QCOMPARE(editor.toPlainText(),
             QStringLiteral("A\nX\n\n\n![alt](img.png)"));
}

void TstSceneCoordinator::roundTripLeadingBlankLines()
{
    const QString src = QStringLiteral("\n\n\n![alt](img.png)\n\nafter");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripSingleTrailingNewline()
{
    const QString src = QStringLiteral("A\n\n![alt](img.png)\n");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripTwoAdjacentImagesWithGap()
{
    const QString src = QStringLiteral(
        "![a](a.png)\n\n\n\n![b](b.png)");
    QCOMPARE(roundTrip(src), src);
}

void TstSceneCoordinator::roundTripEightBlankLinesBeforeImage()
{
    const QString src = QStringLiteral(
        "# Heading\n\n\n\n\n\n\n\n\n![alt](img.png)\n\nbottom");
    QCOMPARE(roundTrip(src), src);
}
```

- [ ] **Step 3: Ensure `coordinatorForTesting()->items()` is available**

This accessor is used in Task 5 too; it already exists per the earlier reparse test. If the compiler complains about `items()`, check the header:

```bash
grep -n "items()" libs/markoff/src/SceneCoordinator.h
```

If there's only `const QList<SelectableItem*> &items() const`, you're fine.

- [ ] **Step 4: Build and run**

```bash
cmake --build build --target tst_markoff_scene_coordinator -j
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_scene_coordinator
```

Expected: all new tests PASS alongside the existing ones.

- [ ] **Step 5: Run the full test suite one last time**

```bash
cd build && ctest -R markoff --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/tests/tst_scene_coordinator.cpp
git commit -m "$(cat <<'EOF'
Test: blank-line editability and extended round-trip coverage

Verifies the user-visible contract directly: blank lines are real,
cursor-reachable QTextBlocks; typing into one reflects in toMarkdown().
Plus four extended round-trip cases covering leading blanks, single
trailing newlines, two-image block-to-block gaps, and the 8-blank-line
"banner" case.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Update TODO and architecture docs

**Why:** `docs/TODO.md` lists a "Round-trip fidelity" section under the 2026-04-17 partial fix. `docs/architecture.md` has a "future work" bullet for this. Both should reflect what's shipped.

**Files:**
- Modify: `libs/markoff/docs/TODO.md`
- Modify: `libs/markoff/docs/architecture.md`

- [ ] **Step 1: Update the TODO entry**

Open `libs/markoff/docs/TODO.md`. Find the "Round-trip fidelity: blank lines around block items" entry (approx line 294). Replace the whole entry with:

```markdown
- **Round-trip fidelity: full stack** (2026-04-17):
  `MarkdownSplitter::split()` now guarantees that concatenating every
  segment's `text` with a single `"\n"` between segments reproduces
  the source byte-for-byte. Blank lines live inside text segments as
  real empty `QTextBlock`s (editable, cursor-reachable). The
  `leadSeparator` field and the `interItemNewlines` 1/2 heuristic are
  gone; every line-counting site uses `+= 1` for the join separator.
  `SelectionManager::serializeAsMarkdown()` (the cross-boundary
  clipboard path) and `SceneCoordinator::toMarkdown()` (file save)
  both use pure `"\n"` joins. New tests cover: splitter join-identity
  (parser-level, 13 slots), clipboard round-trip of 8 blank lines
  between blocks, blank-line editability (typing into a spacer
  paragraph), heading line math across blank-line gaps, and extended
  round-trip cases (leading blanks, single trailing newline, two
  adjacent images with gap).
  Spec: `docs/specs/2026-04-17-roundtrip-fidelity-design.md`.
  Plan: `docs/plans/2026-04-17-roundtrip-fidelity.md`.
```

- [ ] **Step 2: Update the architecture doc**

Open `libs/markoff/docs/architecture.md`. Find the "Round-trip fidelity" future-work bullet (approx line 527):

```markdown
4. **Round-trip fidelity** — blank lines between blocks are normalized
   by `SceneCoordinator::toMarkdown()`; affects line-number precision.
```

Delete that bullet entirely. In the "Shipped" list further down, add under the existing shipped bullets (approx line 548):

```markdown
- Round-trip fidelity: byte-for-byte source preservation across
  `toPlainText()`, file save, and select-all + clipboard copy. Blank
  lines render as real editable paragraphs (2026-04-17).
```

- [ ] **Step 3: Commit**

```bash
git add libs/markoff/docs/TODO.md libs/markoff/docs/architecture.md
git commit -m "$(cat <<'EOF'
Docs: record round-trip fidelity as shipped

Updates TODO.md and architecture.md to reflect the full-stack
fidelity work: splitter join-identity invariant, editable
blank-line paragraphs, retired leadSeparator and interItemNewlines.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review checklist (for the executing agent)

After Task 7, verify:

- [ ] `grep -rn leadSeparator libs/markoff libs/markoff-parser` returns only hits in `docs/`.
- [ ] `grep -rn interItemNewlines libs/markoff libs/markoff-parser` returns zero hits in source or tests.
- [ ] `cd build && ctest -R markoff --output-on-failure` is fully green.
- [ ] The new `testJoinIdentity_*` slots are present in `tst_splitter.cpp` and all pass.
- [ ] The new `clipboardRoundTripPreservesEightBlankLines`, `cursorLineAccountsForBlankLineGaps`, `blankLinesAreEditableParagraphs`, and the four extended round-trip slots are present in `tst_scene_coordinator.cpp` and all pass.
- [ ] `docs/TODO.md` and `docs/architecture.md` reflect the shipped work.

If all checks pass, the plan is complete.
