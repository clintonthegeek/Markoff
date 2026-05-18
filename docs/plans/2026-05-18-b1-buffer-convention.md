# B1 buffer convention — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire the implicit "block buffers carry a trailing `\n` as delimiter" convention. Make block buffers content-only across all `BlockKind`s. Move structural-newline ownership to the serializer.

**Architecture:** Two-commit migration along the library boundary. Commit #1 retires the convention in `markoff-core` (load strip, `applyFlatEdit` cleanup, serializer rewrite, merge-cmd dead-code removal, test contract rewrites, new invariant binary). Commit #2 deletes the now-dead chop in `markoff-live` and unblocks the `QEXPECT_FAIL`'d soft-break test. Falsifiability proofs follow, then docs closeout.

**Tech Stack:** C++20, Qt 6.8+, QML, CTest, `scripts/run-tests.sh` for offscreen runs. Build cap `-j 8`.

**Working tree:** `.worktrees/foundation-exploration/` on branch `exploration/new-foundation`. All paths in this plan are relative to that worktree root.

**Spec:** `docs/specs/2026-05-18-b1-buffer-convention-design.md`.

---

## File map

**Create:**

- `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp` — Test 1 (no-load-terminator) + Test 2 (round-trip stability) per spec §5.
- `libs/markoff-live/tests/tst_block_buffer_interactive.cpp` — Test 3 (interactive contract via `QmlIntegrationFixture` + `LiveRealisticInputHarness`) per spec §5.

**Modify (C++ source):**

- `libs/markoff-core/src/MarkoffDocument.cpp` — load-time strip in `materializeBlocksFromParsedDoc` (line 1763); `interBlockSeparator()` (line 1889); new `finalDocumentTerminator()` helper; `serializeForSave` non-ListItem + ListItem branches; `applyFlatEdit` four `+ "\n"` removals (lines 1515, 1529, 1577, 1588).
- `libs/markoff-core/src/Cmd/D2.cpp` — `backspaceMerge` conditional strip removal (lines 75–80); `deleteMerge` conditional strip removal (lines 100–106).
- `libs/markoff-live/src/LiveListModelBinding.cpp` — delete the chop (line 404).

**Modify (test files):**

- `libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp` — rewrite `backspaceMerge_stripsTrailingNewlineAtBoundary` and `deleteMerge_stripsTrailingNewlineAtBoundary` to assert B1 contract.
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` — delete both `QEXPECT_FAIL(Continue)` markers in `shift_enter_creates_visible_newline` (lines 143, 150).

**Modify (build):**

- `libs/markoff-core/tests/CMakeLists.txt` — register `tst_block_buffer_invariant`.
- `libs/markoff-live/tests/CMakeLists.txt` — register `tst_block_buffer_interactive`.

**Modify (docs, closeout commit):**

- `docs/INVARIANTS.md` — new "Block buffer convention" section.
- `docs/queue.md` — close §#4.
- `docs/e-arc/e-arc-status.md` — Last-updated + recent-changes log.
- `libs/markoff-core/CLAUDE.md` — cross-reference the new INVARIANTS section.

---

## Phase 0: Pre-flight — establish baseline + write failing tests

### Task 0.1: Confirm baseline build + suite

**Files:** none

- [ ] **Step 1: Configure + build**

Run:
```
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```

Expected: configure done, build green.

- [ ] **Step 2: Run baseline suite (offscreen, fast)**

Run:
```
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: green except for pre-existing failures (the `shift_enter_creates_visible_newline` slot's two `QEXPECT_FAIL(Continue)`s are *expected* now and after — that test reports `XPASS` only if B1 prematurely lands; the surrounding suite is what we're verifying baseline-green).

Record any unexpected failures before proceeding. Per CLAUDE.md ("classify before fixing test failures"), distinguish drift / real-bug / redundant / in-flight.

### Task 0.2: Scaffold `tst_block_buffer_invariant`

**Files:**
- Create: `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file skeleton**

Write `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// B1 buffer convention invariant tests.
// Spec: docs/specs/2026-05-18-b1-buffer-convention-design.md

#include <QTest>
#include <QByteArray>
#include <QList>
#include <QRegularExpression>

#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

namespace {

struct Fixture {
    const char *name;
    QByteArray source;
};

// Representative corpus. Each fixture exercises a distinct parser shape.
const QList<Fixture> kCorpus = {
    {"single-block-no-eol",        "Heading"},
    {"single-block-with-eol",      "Heading\n"},
    {"two-paragraphs-with-eol",    "first\n\nsecond\n"},
    {"two-paragraphs-no-eol",      "first\n\nsecond"},
    {"setext-h1",                  "Heading\n========\n"},
    {"setext-h2",                  "Heading\n--------\n"},
    {"atx-heading",                "# Heading\n"},
    {"fenced-code-with-newlines",  "```\nline1\nline2\n```\n"},
    {"tight-list",                 "- one\n- two\n- three\n"},
    {"loose-list",                 "- one\n\n- two\n\n- three\n"},
    {"blockquote",                 "> quoted\n"},
    {"para-after-heading",         "# Heading\n\nbody\n"},
    {"para-before-list",           "intro\n\n- one\n- two\n"},
    {"hr",                         "before\n\n---\n\nafter\n"},
    {"frontmatter",                "---\ntitle: x\n---\n\nbody\n"},
};

// Normalize source for round-trip comparison: collapse runs of 2+ blank
// lines to one; ensure single trailing '\n'. Local to this test file.
QByteArray normalize(QByteArray s)
{
    // Collapse 3+ consecutive '\n' to exactly 2.
    static const QRegularExpression re("\\n{3,}");
    QString str = QString::fromUtf8(s);
    str.replace(re, "\n\n");
    QByteArray out = str.toUtf8();
    // Ensure single trailing '\n'.
    while (out.endsWith("\n\n")) out.chop(1);
    if (!out.endsWith('\n')) out += '\n';
    return out;
}

} // namespace

class TstBlockBufferInvariant : public QObject {
    Q_OBJECT
private slots:
    void no_load_terminator_data();
    void no_load_terminator();

    void roundtrip_stability_data();
    void roundtrip_stability();
};

QTEST_MAIN(TstBlockBufferInvariant)
#include "tst_block_buffer_invariant.moc"
```

- [ ] **Step 2: Register the binary in CMake**

Find the existing `qt_add_executable` block for `tst_d2_cmd_decomposition` in `libs/markoff-core/tests/CMakeLists.txt`. Add an analogous block:

```cmake
qt_add_executable(tst_block_buffer_invariant
    d2/tst_block_buffer_invariant.cpp)
target_link_libraries(tst_block_buffer_invariant PRIVATE
    Qt6::Core Qt6::Test markoff_core)
add_test(NAME tst_block_buffer_invariant
         COMMAND tst_block_buffer_invariant)
set_tests_properties(tst_block_buffer_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build the empty binary**

Run:
```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
```

Expected: builds. Test slots have no bodies yet so no assertions; binary runs but doesn't execute meaningful tests.

- [ ] **Step 4: Confirm CMake picked it up**

Run:
```
ctest --test-dir build-dev -N | grep tst_block_buffer_invariant
```

Expected: one line showing the test registered.

### Task 0.3: Implement Test 1 (no-load-terminator)

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`

- [ ] **Step 1: Add the data + slot bodies**

Inside the `private slots:` section, fill in:

```cpp
void TstBlockBufferInvariant::no_load_terminator_data()
{
    QTest::addColumn<QByteArray>("source");
    for (const auto &fx : kCorpus)
        QTest::newRow(fx.name) << fx.source;
}

void TstBlockBufferInvariant::no_load_terminator()
{
    QFETCH(QByteArray, source);

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    for (BlockId id : blocks) {
        const QByteArray text = doc.blockText(id);
        QVERIFY2(!text.endsWith('\n'),
                 qPrintable(QString("block %1 buffer ends with \\n: %2")
                     .arg(id.raw())
                     .arg(QString::fromUtf8(text))));
    }
}
```

- [ ] **Step 2: Build + run the test**

Run:
```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: **most fixtures FAIL** — the load convention today preserves tree-sitter's trailing `\n` for non-last blocks. Specifically, the `single-block-no-eol` fixture passes (last block doesn't have `\n` to begin with); the others fail. This is the predicted failure — Test 1 cannot pass until Task 1.1 lands.

### Task 0.4: Implement Test 2 (round-trip stability)

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`

- [ ] **Step 1: Add the data + slot bodies**

```cpp
void TstBlockBufferInvariant::roundtrip_stability_data()
{
    QTest::addColumn<QByteArray>("source");
    for (const auto &fx : kCorpus)
        QTest::newRow(fx.name) << fx.source;
}

void TstBlockBufferInvariant::roundtrip_stability()
{
    QFETCH(QByteArray, source);

    MarkoffDocument doc1(/*replicaId=*/1);
    doc1.loadFromMarkdown(source);
    QByteArray firstSave = doc1.serializeForSave();

    MarkoffDocument doc2(/*replicaId=*/1);
    doc2.loadFromMarkdown(firstSave);
    QByteArray secondSave = doc2.serializeForSave();

    // Fixed-point under round-trip: first save equals second save.
    QCOMPARE(secondSave, firstSave);

    // First save equals normalize(source): collapses blank-line runs +
    // ensures single trailing '\n'.
    QCOMPARE(firstSave, normalize(source));
}
```

- [ ] **Step 2: Build + run**

Run:
```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: Test 1 fixtures still fail as before; Test 2 fixtures fail on `firstSave == normalize(source)` for several cases because the serializer + load combination today doesn't normalize. This is the predicted failure.

- [ ] **Step 3: Commit the failing tests**

```
git add libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp libs/markoff-core/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(markoff-core): B1 buffer-invariant tests — failing under current code

Spec docs/specs/2026-05-18-b1-buffer-convention-design.md §5.

Two slots, data-driven over a 15-fixture corpus:

  * no_load_terminator — asserts blockText(id) never ends with '\n'
    after loadFromMarkdown. Currently fails on every fixture except
    single-block-no-eol because tree-sitter's byte range includes
    each block's own trailing '\n'.

  * roundtrip_stability — asserts that serialize(load(serialize(
    load(source)))) is a fixed point and equals normalize(source).
    Currently fails because the load + serialize combination
    doesn't normalize.

Both slots fail intentionally as the entry point for the B1
implementation. They will pass at the end of commit #1.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 1: markoff-core invariant landing (commit #1 target)

### Task 1.1: Load-time strip in `materializeBlocksFromParsedDoc`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:1763`

- [ ] **Step 1: Apply the strip**

Find the `QByteArray content = bodyUtf8.mid(...)` line in `materializeBlocksFromParsedDoc` (around line 1763). Replace with:

```cpp
// Buffer content: full source range in UTF-8 bytes, **then strip the
// trailing block-terminator '\n' if present**. Per B1 (spec
// 2026-05-18-b1-buffer-convention-design.md §1), block buffers hold
// content only — the structural '\n' separator belongs to the
// serializer.
//
// For FencedCodeBlock, full source is stored (fences preserved for
// round-trip). For ListItem, the parser's harvestListItem already
// strips trailing whitespace from the byte range, so this chop is
// idempotent for ListItem.
QByteArray content = bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart);
if (content.endsWith('\n'))
    content.chop(1);
```

- [ ] **Step 2: Build + run Test 1**

Run:
```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
scripts/run-tests.sh --bin tst_block_buffer_invariant -- -function no_load_terminator
```

Expected: Test 1 **PASSES** on every fixture in the corpus. Test 2 still fails (no serializer changes yet).

- [ ] **Step 3: Check for collateral damage in markoff-core**

Run:
```
scripts/run-tests.sh -R '^tst_d2_'
```

Expected: several D2 tests fail because the load strip exposes existing assumptions downstream (merge cmds, applyFlatEdit, the cmd-decomposition tests). Note the list; tasks 1.5–1.10 fix them. Do not commit yet.

### Task 1.2: `interBlockSeparator()` and `finalDocumentTerminator()`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:1889`

- [ ] **Step 1: Rewrite `interBlockSeparator()` + add helper**

Find `QByteArray interBlockSeparator()` in the anonymous namespace (around line 1889). Replace with:

```cpp
QByteArray interBlockSeparator()
{
    // Per B1 (spec 2026-05-18-b1-buffer-convention-design.md §1):
    // block buffers are content; the separator carries the full gap
    // between two block bodies — a line break ending the previous
    // block plus the blank line opening the next.
    return "\n\n";
}

QByteArray finalDocumentTerminator()
{
    // CommonMark-conventional: documents end with a single newline.
    // Emitted by serializeForSave after the block loop.
    return "\n";
}
```

- [ ] **Step 2: Build markoff-core**

Run:
```
cmake --build build-dev --target markoff_core -j 8
```

Expected: builds.

### Task 1.3: `serializeForSave` non-ListItem branch — untouched normalize + final EOL

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` — the loop body for non-ListItem blocks (around lines 1956–1968), plus the after-loop region (~line 1973 before frontmatter close).

- [ ] **Step 1: Update the untouched-block branch**

In `serializeForSave`, find the non-ListItem block-emit branch (after the `if (kind == BlockKind::ListItem)` block). Replace the `if (!isBlockTouched(id))` branch with:

```cpp
QByteArray bytes;
if (!isBlockTouched(id)) {
    // Untouched: use original load-time bytes for byte-identical
    // content round-trip. Strip the load-time terminator so the
    // serializer owns separator placement (B1 §3).
    bytes = d->blockLoadTimeBytes.value(id);
    if (bytes.endsWith('\n'))
        bytes.chop(1);
} else {
    // Touched: re-serialize from CRDT state. Per-kind serializer is
    // contracted to emit body only — no terminator (B1 §4).
    auto fn = reg.get(kind);
    bytes = fn(kind, blockAttrs(id), blockText(id));
}
out += bytes;
if (i + 1 < blocks.size())
    out += interBlockSeparator();
```

- [ ] **Step 2: Append the final terminator after the block loop**

Find the `for (size_t i = 0; i < blocks.size(); ++i) { ... }` loop in `serializeForSave`. Immediately after its closing brace, before the `// 3. Link refs` comment, insert:

```cpp
// B1: serializer owns the document-final '\n'. CommonMark convention.
// (Footnote defs, if any, follow this and contribute their own '\n'-
// terminated lines; the trailing footnote line provides the final '\n'
// in that case. This unconditional append ensures the no-footnote case
// also terminates.)
if (!blocks.empty())
    out += finalDocumentTerminator();
```

This puts the terminator immediately after the last block's body and *before* the footnote-defs section. If footnotes are present, this produces `…lastBlock\n[^label]: …\n` (footnote starts on the line after the last block); without footnotes, the document ends `…lastBlock\n`. Per spec §8, the blank-line-before-footnotes question is a pre-existing concern, not B1-introduced — flagged here for visibility but out of scope.

- [ ] **Step 3: Build markoff-core**

```
cmake --build build-dev --target markoff_core -j 8
```

Expected: builds.

### Task 1.4: `serializeForSave` ListItem branch — separator ownership

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:1930–1953`

- [ ] **Step 1: Update the ListItem branch**

Find the `if (kind == BlockKind::ListItem)` branch. Replace the existing emission:

```cpp
// Emit: <indent><marker> <content>  (separator is added below)
out += indentBytes + marker + " " + content;

// Per B1 §3: the serializer owns inter-block separators. Loose runs
// emit a blank line between consecutive items; tight runs emit just
// the line break.
if (i + 1 < blocks.size())
    out += looseRun ? QByteArray("\n\n") : QByteArray("\n");
continue;
```

(Remove the existing `out += indentBytes + marker + " " + content + "\n";` line and the existing `if (looseRun && (i + 1 < blocks.size())) out += "\n";` block.)

- [ ] **Step 2: Build markoff-core**

```
cmake --build build-dev --target markoff_core -j 8
```

Expected: builds.

- [ ] **Step 3: Run the buffer-invariant suite**

```
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: Test 1 still passes. Test 2 (round-trip) **now passes for most fixtures**. The two-paragraphs and list-related fixtures should be green. Fixtures using `applyFlatEdit` may still fail until Task 1.5 lands; tracked.

### Task 1.5: `applyFlatEdit` — remove the four `+ "\n"` insertions

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` — lines 1515, 1529, 1577, 1588.

- [ ] **Step 1: Intra-block branch first-replacement (line 1515)**

Find:
```cpp
// First block ends with '\n' as its delimiter.
QByteArray firstReplacement = parts.front() + QByteArray("\n");
```

Replace with:
```cpp
// B1: block buffers are content. parts[0] is the new content for the
// portion of the current block before the split; the serializer
// reconstructs separators.
QByteArray firstReplacement = parts.front();
```

- [ ] **Step 2: Intra-block branch non-last seed (line 1529)**

Find (inside the `for (int i = 1; i < parts.size(); ++i)` loop):
```cpp
} else {
    seed += QByteArray("\n");  // delimiter for non-last blocks
}
```

Replace with:
```cpp
}
// (No delimiter append: B1 buffers are content-only.)
```

- [ ] **Step 3: Cross-block branch first-replacement (line 1577)**

Find:
```cpp
QByteArray firstReplacement = parts.front() + QByteArray("\n");
d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, firstReplacement, t);
```

Replace with:
```cpp
QByteArray firstReplacement = parts.front();
d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, firstReplacement, t);
```

- [ ] **Step 4: Cross-block branch non-last seed (line 1588)**

Find (inside the second `for (int i = 1; i < parts.size(); ++i)` loop):
```cpp
} else {
    seed += QByteArray("\n");
}
```

Replace with:
```cpp
}
// (No delimiter append: B1 buffers are content-only.)
```

- [ ] **Step 5: Build + check `tst_d4_apply_flat_edit`**

```
cmake --build build-dev --target markoff_core -j 8
scripts/run-tests.sh -R 'tst_d4_apply_flat_edit'
```

Expected: `tst_d4_apply_flat_edit` slots that asserted the old "+ \n" shape will fail. Note them; Task 1.7 handles test rewrites if needed. Tests that exercise *behaviour* (cursor lands here, content is X, block count is Y) should still pass because the B1 changes are internally consistent.

### Task 1.6: `Cmd::backspaceMerge` + `Cmd::deleteMerge` — remove conditional strip

**Files:**
- Modify: `libs/markoff-core/src/Cmd/D2.cpp:62–111`

- [ ] **Step 1: Update `backspaceMerge` (lines 62–87)**

Find:
```cpp
    BlockId prev = *(curIt - 1);
    QByteArray prevText = doc.blockText(prev);

    // A trailing '\n' in a block buffer is the structural inter-block delimiter.
    // When a merge destroys the boundary, that delimiter goes with it — replace
    // it with the next block's content rather than appending after it.
    uint32_t joinOffset = static_cast<uint32_t>(prevText.size());
    uint32_t removeLen  = 0;
    if (prevText.endsWith('\n')) {
        --joinOffset;
        removeLen = 1;
    }
```

Replace with:
```cpp
    BlockId prev = *(curIt - 1);
    QByteArray prevText = doc.blockText(prev);

    // B1: block buffers are content. Any trailing '\n' in prevText is
    // user-authored (e.g. a soft break) and must be preserved as
    // content of the merged block. Append cleanly.
    uint32_t joinOffset = static_cast<uint32_t>(prevText.size());
    uint32_t removeLen  = 0;
```

- [ ] **Step 2: Update `deleteMerge` (lines 89–111)**

Find:
```cpp
    BlockId next = *nextIt;
    QByteArray curText = doc.blockText(currentBlock);

    // Same as backspaceMerge: the trailing '\n' is structural; replace it.
    uint32_t joinOffset = static_cast<uint32_t>(curText.size());
    uint32_t removeLen  = 0;
    if (curText.endsWith('\n')) {
        --joinOffset;
        removeLen = 1;
    }
```

Replace with:
```cpp
    BlockId next = *nextIt;
    QByteArray curText = doc.blockText(currentBlock);

    // B1: see backspaceMerge — buffers are content; append cleanly.
    uint32_t joinOffset = static_cast<uint32_t>(curText.size());
    uint32_t removeLen  = 0;
```

- [ ] **Step 3: Build markoff-core**

```
cmake --build build-dev --target markoff_core -j 8
```

Expected: builds.

### Task 1.7: Rewrite merge-cmd test contracts

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp` — slots `backspaceMerge_stripsTrailingNewlineAtBoundary` (line 88) and `deleteMerge_stripsTrailingNewlineAtBoundary` (line 132).

- [ ] **Step 1: Rewrite `backspaceMerge_stripsTrailingNewlineAtBoundary`**

Replace the slot's body with:

```cpp
void TstD2Cmd::backspaceMerge_stripsTrailingNewlineAtBoundary()
{
    // Renamed-in-spirit: B1 (spec 2026-05-18-b1-buffer-convention-
    // design.md) eliminates the "trailing '\n' as delimiter" condition.
    // After loadFromMarkdown, block buffers are content-only; merge
    // appends cleanly.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello\n\nworld\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    BlockId blkA = blocks[0];
    BlockId blkB = blocks[1];

    // B1 precondition: buffer does not end with '\n'.
    QVERIFY(!doc.blockText(blkA).endsWith('\n'));

    auto result = Cmd::backspaceMerge(doc, blkB);
    QCOMPARE(result.mergedInto, blkA);
    QCOMPARE(result.cursorByteOffset, 5u);
    // Merged result is content-only.
    QCOMPARE(doc.blockText(blkA), QByteArray("helloworld"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}
```

Optionally rename the slot to `backspaceMerge_loadFromMarkdown_appendsCleanly` for clarity. Match the slot's declaration in `private slots:` (line 20). Touch CMake if test discovery is name-driven (it isn't for `private slots:`-style Qt tests; the binary discovers them at runtime).

- [ ] **Step 2: Rewrite `deleteMerge_stripsTrailingNewlineAtBoundary`**

Replace the slot's body with:

```cpp
void TstD2Cmd::deleteMerge_stripsTrailingNewlineAtBoundary()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello\n\nworld\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    BlockId blkA = blocks[0];

    QVERIFY(!doc.blockText(blkA).endsWith('\n'));

    Cmd::deleteMerge(doc, blkA);
    QCOMPARE(doc.blockText(blkA), QByteArray("helloworld"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}
```

- [ ] **Step 3: Build + run the merge-cmd tests**

```
cmake --build build-dev --target tst_d2_cmd_decomposition -j 8
scripts/run-tests.sh --bin tst_d2_cmd_decomposition
```

Expected: all six slots pass.

### Task 1.8: Per-kind serializer audit

**Files:** read-only inspection — files in `libs/markoff-core/src/Serializers/*.cpp` (or wherever `BuiltinBlockSerializerRegistry` populates).

- [ ] **Step 1: Locate the per-kind serializer functions**

Run:
```
grep -rn "BuiltinBlockSerializerRegistry\|registerBuiltins\|Slot::" libs/markoff-core/src/ --include='*.cpp' | head -30
```

Identify the serializer function for each `BlockKind`: Paragraph, Heading, BlockQuote, CodeBlock, HorizontalRule, Image, Math. (HtmlBlock and Table are D2-inert; skip.)

- [ ] **Step 2: Inspect each function for trailing `\n` emission**

For each serializer, read the source. Per spec §4 expected outputs:

| Kind | Expected last bytes |
|---|---|
| Paragraph | content's last byte (no trailing `\n`) |
| Heading (ATX) | content's last byte |
| Heading (setext) | last `=` or `-` of the underline |
| CodeBlock (fenced) | closing fence — e.g. `` ``` `` |
| CodeBlock (indented) | content's last byte (no trailing `\n`) |
| BlockQuote | last content byte after `> ` prefix |
| HorizontalRule | last `-`/`*`/`_` |
| Image / Math | content's last byte |

If any serializer ends its emitted bytes with `\n`, that's a violation of the B1 §4 contract. Fix in this task by removing the trailing `\n` append. Document the find in the task's commit if any are found.

- [ ] **Step 3: Run buffer-invariant + cmd-decomp suites**

```
scripts/run-tests.sh -R 'tst_block_buffer_invariant|tst_d2_cmd_decomposition'
```

Expected: both green. If a serializer fix was needed in Step 2, this run confirms it.

### Task 1.9: Full markoff-core suite

**Files:** none.

- [ ] **Step 1: Run all D2 tests**

```
scripts/run-tests.sh -R '^tst_d|tst_markoff_doc|tst_canonical_buffer|tst_block_buffer'
```

Expected: all green. Specifically:
- `tst_block_buffer_invariant` — Test 1 + Test 2 green.
- `tst_d2_cmd_decomposition` — all six slots green.
- `tst_d2_apply_block_edit`, `tst_d2_apply_structural`, `tst_d2_load`, `tst_d2_convergence`, `tst_d2_byterange_probe` — green.
- `tst_markoff_doc_apply_structured_paste` — green (this was a baseline failure in the 2026-05-16 attempt; under the full B1 plan it converges).
- `tst_d4_apply_flat_edit` — green.

If any failure surfaces, classify per CLAUDE.md ("classify before fixing test failures"). For tests that pinned the old convention (like the merge-cmd ones), update the test contract; for tests that exercise *behaviour*, fix the implementation.

### Task 1.10: Commit #1

**Files:** all markoff-core changes made in Phase 1 so far.

- [ ] **Step 1: Review the staged diff**

```
git diff --stat
```

Expected: ~5–7 files changed across `libs/markoff-core/src/` and `libs/markoff-core/tests/`.

- [ ] **Step 2: Commit**

```
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/Cmd/D2.cpp \
        libs/markoff-core/tests/d2/tst_d2_cmd_decomposition.cpp \
        libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp \
        libs/markoff-core/tests/CMakeLists.txt

# Add any serializer files touched in Task 1.8 if applicable.

git commit -m "$(cat <<'EOF'
feat(markoff-core): B1 buffer convention — content-only buffers

Per spec docs/specs/2026-05-18-b1-buffer-convention-design.md.

Retires the implicit "block buffers carry a trailing '\n' as
delimiter" convention. All seven block kinds align with the ListItem
precedent (per 37661b5): buffers are content; the serializer owns
structural newlines.

Changes:

  * materializeBlocksFromParsedDoc: strip trailing '\n' from each
    block's slice after the parser hands it back. Idempotent for
    ListItem (parser-side strip already in place).

  * interBlockSeparator(): "\n" -> "\n\n" (the full inter-block gap
    since blocks no longer contribute their own terminator).
    finalDocumentTerminator() helper added.

  * serializeForSave: non-ListItem branch strips the load-time '\n'
    from blockLoadTimeBytes before emit. ListItem branch separator
    ownership moved into the inter-block conditional. Document-
    final '\n' appended after the block loop.

  * applyFlatEdit: four "+ QByteArray(\"\\n\")" insertions removed
    (lines 1515, 1529, 1577, 1588). Block parts are now content.

  * Cmd::backspaceMerge / Cmd::deleteMerge: conditional
    "endsWith('\\n')" strip removed. Under B1 the precondition is
    impossible — except for user-authored content '\n' (e.g. a soft
    break), which must be preserved through the merge.

  * Test contracts updated: backspaceMerge_stripsTrailingNewlineAt-
    Boundary + deleteMerge counterpart now assert the B1 result
    (merged buffer has no trailing '\n').

  * New invariant binary tst_block_buffer_invariant (15-fixture
    corpus, two slots: no_load_terminator + roundtrip_stability).

Save normalizes runs of 2+ blank lines to one and ensures a single
trailing '\n' (CommonMark-conventional).

markoff-live's read-side chop is now redundant; it is removed in
the next commit alongside the QEXPECT_FAIL marker removal for
shift_enter_creates_visible_newline.

Closes docs/queue.md §#4 (with the markoff-live follow-up).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 2: markoff-live (commit #2 target)

### Task 2.1: Delete the chop in `onD2Changed`

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp:404`

- [ ] **Step 1: Remove the chop**

Find the lines around 404:

```cpp
QByteArray raw = doc->blockText(id);
// Trim trailing newline. Per queue.md #4: this defensive chop is the
// symptom of the non-uniform buffer convention; see docs/queue.md §#4
// for the full investigation and the planned B1/A1 refactor.
if (raw.endsWith('\n'))
    raw.chop(1);
r.text = QString::fromUtf8(raw);
```

Replace with:

```cpp
// B1 (spec 2026-05-18-b1-buffer-convention-design.md): buffers are
// content; no chop needed.
r.text = QString::fromUtf8(doc->blockText(id));
```

- [ ] **Step 2: Build markoff-live**

```
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds.

- [ ] **Step 3: Run the live-render fast suite**

```
scripts/run-tests.sh -R '^tst_live_render_' -E 'tst_realistic|tst_benchmark'
```

Expected: the `shift_enter_creates_visible_newline` slot now `XPASS`es (since `QEXPECT_FAIL(Continue)` is still in place but the underlying behaviour is now correct). The `XPASS` will be reported as a test failure with QtTest — that's expected; Task 2.3 removes the markers. Other tests should be green.

If any test other than `shift_enter_creates_visible_newline` fails, classify per CLAUDE.md. The most likely surface for regression is QML callsites that read `model.text.endsWith('\n')` and rely on the chop's previous behaviour; the `QmlIntegrationFixture` JS-exception trap (commit `e8514eb`) should catch any TypeError from drift, and tests should fail loudly.

### Task 2.2: Scaffold `tst_block_buffer_interactive`

**Files:**
- Create: `libs/markoff-live/tests/tst_block_buffer_interactive.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

Write `libs/markoff-live/tests/tst_block_buffer_interactive.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// B1 buffer convention interactive contract test.
// Spec: docs/specs/2026-05-18-b1-buffer-convention-design.md §5 Test 3.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>

#include <QtTest/QtTest>
#include <QApplication>
#include <QClipboard>

namespace Markoff::Live::Test {

class TstBlockBufferInteractive : public QObject {
    Q_OBJECT
private slots:
    void soft_break_and_split_preserves_content_newline();
    void paste_multi_block_does_not_synthesize_terminator();
};

void TstBlockBufferInteractive::soft_break_and_split_preserves_content_newline()
{
    QmlIntegrationFixture fx("Heading", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    const auto blocks = fx.document()->iterateBlocks();
    QCOMPARE(blocks.size(), 1u);
    const Markoff::BlockId blk0 = blocks[0];

    // Step 1: buffer 0 = "Heading"
    QCOMPARE(fx.bufferText(blk0), QByteArray("Heading"));

    // Step 2: cursor at pos 7, Shift+Enter
    fx.placeCursorAtPos(0, 7);
    QTRY_COMPARE_WITH_TIMEOUT(fx.delegateCursorPos(0), 7, 2000);
    fx.harness().keyClick(Qt::Key_Return, Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.bufferText(blk0), QByteArray("Heading\n"), 2000);

    // Step 3: type '='
    fx.harness().keyClick(Qt::Key_Equal);
    QTRY_COMPARE_WITH_TIMEOUT(fx.bufferText(blk0), QByteArray("Heading\n="), 2000);

    // Step 4: cursor at pos 8 (between \n and =), press Enter to split.
    fx.placeCursorAtPos(0, 8);
    QTRY_COMPARE_WITH_TIMEOUT(fx.delegateCursorPos(0), 8, 2000);
    fx.harness().keyClick(Qt::Key_Return);

    QVERIFY(fx.waitForRowCount(2, 2000));
    const auto split = fx.document()->iterateBlocks();
    QCOMPARE(split.size(), 2u);
    // Block 0 preserves the user-authored '\n' as content.
    QCOMPARE(fx.bufferText(split[0]), QByteArray("Heading\n"));
    // Block 1 has the '=' tail, no terminator.
    QCOMPARE(fx.bufferText(split[1]), QByteArray("="));
    QVERIFY(!fx.bufferText(split[1]).endsWith('\n'));

    // Step 5: cursor at start of block 1, Backspace (merge).
    fx.placeCursorAtPos(1, 0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.delegateCursorPos(1), 0, 2000);
    fx.harness().keyClick(Qt::Key_Backspace);

    QVERIFY(fx.waitForRowCount(1, 2000));
    const auto merged = fx.document()->iterateBlocks();
    QCOMPARE(merged.size(), 1u);
    // The soft-break '\n' survives the merge as content; the '=' is
    // appended after it. The merged buffer is "Heading\n=" — the merge
    // cmd no longer strips the user's content '\n'.
    QCOMPARE(fx.bufferText(merged[0]), QByteArray("Heading\n="));
}

void TstBlockBufferInteractive::paste_multi_block_does_not_synthesize_terminator()
{
    // Empty document.
    QmlIntegrationFixture fx("", /*expectedRowCount=*/0);

    // Place clipboard with multi-block markdown.
    QApplication::clipboard()->setText(QStringLiteral("a\n\nb\n\nc"));
    QTest::qWait(20);

    // Paste via Ctrl+V.
    fx.harness().keyClick(Qt::Key_V, Qt::ControlModifier);

    QVERIFY(fx.waitForRowCount(3, 2000));
    const auto blocks = fx.document()->iterateBlocks();
    QCOMPARE(blocks.size(), 3u);

    QCOMPARE(fx.bufferText(blocks[0]), QByteArray("a"));
    QCOMPARE(fx.bufferText(blocks[1]), QByteArray("b"));
    QCOMPARE(fx.bufferText(blocks[2]), QByteArray("c"));

    for (auto id : blocks)
        QVERIFY2(!fx.bufferText(id).endsWith('\n'),
                 qPrintable(QString("block %1 ends with \\n").arg(id.raw())));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TstBlockBufferInteractive)
#include "tst_block_buffer_interactive.moc"
```

- [ ] **Step 2: Register in CMake**

Find the `qt_add_executable` for `tst_live_render_qml_integration` in `libs/markoff-live/tests/CMakeLists.txt`. Add an analogous block:

```cmake
qt_add_executable(tst_block_buffer_interactive
    tst_block_buffer_interactive.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_block_buffer_interactive PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_block_buffer_interactive
         COMMAND tst_block_buffer_interactive)
set_tests_properties(tst_block_buffer_interactive
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Reconfigure + build**

```
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_block_buffer_interactive -j 8
```

Expected: configures + builds.

- [ ] **Step 4: Run the new test**

```
scripts/run-tests.sh --bin tst_block_buffer_interactive
```

Expected: both slots **pass**. If `soft_break_and_split_preserves_content_newline` fails at "split" step (cursor placement / structural-key-handler not handling mid-buffer Enter), investigate. The expected production path is `Cmd::splitParagraph` or equivalent — check `LiveStructuralKeyHandler`.

If split-at-mid-buffer is unsupported, the test slot may need to construct the split via a direct cmd call instead (less ideal per invariant 5 — but acceptable with a documented gap-flag for a follow-up to wire the gesture in QML).

### Task 2.3: Remove `QEXPECT_FAIL` markers in `shift_enter_creates_visible_newline`

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp:143, 150`

- [ ] **Step 1: Delete both markers**

Find the two `QEXPECT_FAIL(...)` lines in `shift_enter_creates_visible_newline` (around lines 143 and 150). Delete them. The block becomes:

```cpp
const QString dt = fix.delegateText(0);
QVERIFY2(dt.contains(QLatin1Char('\n')),
         qPrintable(QString("delegate text missing \\n: %1").arg(dt)));

// Cursor at position 8 succeeds under B1: the model text is "Heading\n"
// (8 chars), TextEdit can park the cursor at pos 8.
QCOMPARE(fix.delegateCursorPos(0), 8);
```

- [ ] **Step 2: Build + run**

```
cmake --build build-dev --target tst_live_render_qml_integration -j 8
scripts/run-tests.sh --bin tst_live_render_qml_integration -- shift_enter_creates_visible_newline
```

Expected: the slot **passes** without `QEXPECT_FAIL` markers. The 5-day-old known regression is closed.

### Task 2.4: Full markoff-live suite

**Files:** none.

- [ ] **Step 1: Run the live-render fast suite**

```
scripts/run-tests.sh -R '^tst_live_render_|^tst_block_buffer_' -E 'tst_realistic|tst_benchmark'
```

Expected: all green. The QML JS-exception trap (commit `e8514eb`) may catch a QML callsite that relied on the chop's behaviour — if so, that surfaces as a TypeError failure and is fixed in this task (most likely candidate: a delegate that read `model.text.endsWith('\n')` or used `model.text.length` as if the chop were still active).

### Task 2.5: Commit #2

**Files:** markoff-live changes from Phase 2.

- [ ] **Step 1: Review staged**

```
git diff --stat
```

Expected: 3 files changed across `libs/markoff-live/src/` and `libs/markoff-live/tests/`.

- [ ] **Step 2: Commit**

```
git add libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_block_buffer_interactive.cpp \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp \
        libs/markoff-live/tests/CMakeLists.txt

git commit -m "$(cat <<'EOF'
fix(markoff-live): retire onD2Changed chop, close 5-day-old soft-break regression

With markoff-core landing B1 (previous commit), block buffers are now
content. The defensive chop in LiveListModelBinding::onD2Changed
(line 404, "if (raw.endsWith('\\n')) raw.chop(1)") is dead code; its
last live job was hiding the buffer-convention bug from the view
layer.

  * Chop removed.

  * Both QEXPECT_FAIL markers removed from shift_enter_creates_visible_-
    newline (tst_live_render_qml_integration.cpp:143, 150). The
    slot has been XPASSing since 2026-05-13 and is now green
    end-to-end.

  * New invariant binary tst_block_buffer_interactive with two slots:

      - soft_break_and_split_preserves_content_newline: drives
        Shift+Enter + '=' typing + mid-buffer Enter + Backspace-merge
        through the realistic-input harness; asserts that the
        user-authored '\n' survives the merge as content (the
        chop + conditional-strip combination previously destroyed it).

      - paste_multi_block_does_not_synthesize_terminator: paste-via-
        clipboard of "a\n\nb\n\nc" produces three content-only blocks.

Closes docs/queue.md §#4.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 3: Falsifiability proofs (per spec §5)

These four stub-and-revert cycles prove the tests aren't vacuous. Each cycle: commit the stub, run the suite, observe the predicted failure, commit the revert. Proof commits stay in history; reverts immediately follow.

### Task 3.1: Stub — remove load-time strip

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (the strip added in Task 1.1)

- [ ] **Step 1: Comment out the strip**

Find the load-strip block added in Task 1.1. Comment out the `chop(1)`:

```cpp
QByteArray content = bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart);
// FALSIFIABILITY PROOF: B1 load-strip disabled
// if (content.endsWith('\n'))
//     content.chop(1);
```

- [ ] **Step 2: Build + run Test 1**

```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: **Test 1 FAILS on most fixtures**. Test 2 likely also fails.

- [ ] **Step 3: Commit the stub**

```
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "proof: B1 load-strip removed -> tst_block_buffer_invariant fails"
```

- [ ] **Step 4: Revert immediately**

```
git revert HEAD --no-edit
```

- [ ] **Step 5: Verify suite green again**

```
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: green.

### Task 3.2: Stub — re-introduce one `+ "\n"` in `applyFlatEdit`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (line 1515-equivalent post-Task-1.5)

- [ ] **Step 1: Restore the synthetic terminator**

Find the intra-block first-replacement (modified in Task 1.5 Step 1). Change `parts.front()` back to `parts.front() + QByteArray("\n")`:

```cpp
// FALSIFIABILITY PROOF: B1 applyFlatEdit "+ \n" restored
QByteArray firstReplacement = parts.front() + QByteArray("\n");
```

- [ ] **Step 2: Build + run interactive test**

```
cmake --build build-dev --target tst_block_buffer_interactive -j 8
scripts/run-tests.sh --bin tst_block_buffer_interactive
```

Expected: `paste_multi_block_does_not_synthesize_terminator` **FAILS** — pasted block 0 will end with `\n`.

- [ ] **Step 3: Commit + revert**

```
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "proof: applyFlatEdit \"+ \\n\" restored -> tst_block_buffer_interactive paste slot fails"
git revert HEAD --no-edit
```

- [ ] **Step 4: Verify green**

```
scripts/run-tests.sh --bin tst_block_buffer_interactive
```

Expected: both slots green.

### Task 3.3: Stub — restore the chop in `onD2Changed`

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp` (the location of the deleted chop)

- [ ] **Step 1: Re-insert the chop**

Find the line modified in Task 2.1. Restore the conditional chop:

```cpp
// FALSIFIABILITY PROOF: B1 chop restored
QByteArray raw = doc->blockText(id);
if (raw.endsWith('\n'))
    raw.chop(1);
r.text = QString::fromUtf8(raw);
```

- [ ] **Step 2: Build + run interactive test**

```
cmake --build build-dev --target tst_block_buffer_interactive -j 8
scripts/run-tests.sh --bin tst_block_buffer_interactive
```

Expected: `soft_break_and_split_preserves_content_newline` **FAILS** at the post-Shift+Enter assertion (the chop strips the user's content `\n` from the model, so subsequent cursor placement / split behaviour diverges).

- [ ] **Step 3: Commit + revert**

```
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "proof: onD2Changed chop restored -> tst_block_buffer_interactive soft-break slot fails"
git revert HEAD --no-edit
```

- [ ] **Step 4: Verify green**

```
scripts/run-tests.sh --bin tst_block_buffer_interactive
```

Expected: both slots green.

### Task 3.4: Stub — `interBlockSeparator()` returns `"\n"`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (the function modified in Task 1.2)

- [ ] **Step 1: Restore single-`\n` separator**

Change `interBlockSeparator()` to:

```cpp
QByteArray interBlockSeparator()
{
    // FALSIFIABILITY PROOF: B1 separator reverted to "\n"
    return "\n";
}
```

- [ ] **Step 2: Build + run round-trip test**

```
cmake --build build-dev --target tst_block_buffer_invariant -j 8
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: `roundtrip_stability` **FAILS on every multi-block fixture** — `firstSave` will under-separate, `secondSave` won't match because the re-loaded under-separated text parses differently.

- [ ] **Step 3: Commit + revert**

```
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "proof: interBlockSeparator reverted to \"\\n\" -> roundtrip slot fails on multi-block fixtures"
git revert HEAD --no-edit
```

- [ ] **Step 4: Verify green**

```
scripts/run-tests.sh --bin tst_block_buffer_invariant
```

Expected: green.

---

## Phase 4: Documentation closeout

### Task 4.1: Update `docs/INVARIANTS.md`

**Files:**
- Modify: `docs/INVARIANTS.md`

- [ ] **Step 1: Add the new section**

Find the existing invariants list. After invariant 8 (Discipline Log), add a new top-level section:

```markdown
## Project-wide invariants beyond the seam

### Block buffer convention (2026-05-18, B1)

Block buffers in `MarkoffDocument` hold **content only**. They carry
no trailing structural delimiter. `blockText(id).endsWith('\n')` is
legitimate only when the user has authored a soft break or pasted
content containing one — in that case the `\n` is content, not a
protocol bit.

Structural newlines (block separators, document terminator) are the
serializer's responsibility:

  * `interBlockSeparator()` returns `"\n\n"` — the full gap between
    two block bodies.
  * `finalDocumentTerminator()` returns `"\n"` — appended after the
    block loop in `serializeForSave`.

Save normalizes runs of 2+ blank lines to one and ensures a single
trailing `\n`.

Spec: `docs/specs/2026-05-18-b1-buffer-convention-design.md`.

Falsifiable test: `tst_block_buffer_invariant` (markoff-core) +
`tst_block_buffer_interactive` (markoff-live). Falsifiability proofs
landed and reverted in the commit chain for the B1 implementation.

This invariant applies to every `BlockKind`. ListItem was the first to
comply (per `37661b5`, 2026-05-06); paragraph/heading/blockquote/code-
block/HR/image/math joined under the B1 spec.
```

- [ ] **Step 2: Cross-reference from `libs/markoff-core/CLAUDE.md`**

Open `libs/markoff-core/CLAUDE.md` and add a paragraph near the top, after the existing "Status" or "Conventions" section:

```markdown
### Block buffer convention

Per `docs/INVARIANTS.md` "Block buffer convention" + spec
`docs/specs/2026-05-18-b1-buffer-convention-design.md`: block buffers
hold content only. No trailing structural `\n`. Separators are the
serializer's responsibility (`interBlockSeparator() == "\n\n"`,
`finalDocumentTerminator() == "\n"`).
```

### Task 4.2: Close `docs/queue.md` §#4

**Files:**
- Modify: `docs/queue.md`

- [ ] **Step 1: Get the implementing commit SHAs**

Run:
```
git log --oneline | head -10
```

Identify the two feature-commit SHAs from Tasks 1.10 and 2.5.

- [ ] **Step 2: Update the §#4 header**

Find `## #4 — Chop-trailing-`\n` investigation + fix`. Change the header to:

```markdown
## ~~#4 — Chop-trailing-`\n` investigation + fix~~ ✅ COMPLETE 2026-05-18 (B1 spec)

**Status:** complete. Landed in commits `<COMMIT-SHA-1>` (markoff-core: B1 buffer convention) + `<COMMIT-SHA-2>` (markoff-live: retire onD2Changed chop). Spec: `docs/specs/2026-05-18-b1-buffer-convention-design.md`. Plan: `docs/plans/2026-05-18-b1-buffer-convention.md`. Closes the long-standing soft-break regression in `shift_enter_creates_visible_newline`.
```

Substitute the actual SHAs.

- [ ] **Step 3: Add a Discipline Log entry**

Find the Discipline Log section at the top of `docs/queue.md`. Append:

```markdown
- 2026-05-18 `libs/markoff-core/src/MarkoffDocument.cpp:1763` + chain — inv #3 — the trailing-`\n` convention was never invariant (load was variable, `d2InsertBlock` produced unterminated buffers, `applyFlatEdit` synthesized terminators post-hoc, merge cmds and the chop guarded conditionally). Three commits across 2026-05-04..2026-05-05 each made a local decision that fit the local code; collectively they produced a non-convention that took six dogfood passes to write down and one B1 spec to retire. → fixed by spec 2026-05-18-b1-buffer-convention-design.md + plan 2026-05-18-b1-buffer-convention.md in commits `<COMMIT-SHA-1>..<COMMIT-SHA-2>`.
```

### Task 4.3: Update `docs/e-arc/e-arc-status.md`

**Files:**
- Modify: `docs/e-arc/e-arc-status.md`

- [ ] **Step 1: Update Last-updated + Active-phase**

Replace the existing Last-updated and Active-phase lines with:

```markdown
**Last updated:** 2026-05-18 (B1 buffer convention landed — block buffers are content; serializer owns structural newlines. Closes queue.md §#4 and the 5-day-old soft-break regression in `shift_enter_creates_visible_newline`).
**Working tree:** `.worktrees/foundation-exploration/`
**Branch:** `exploration/new-foundation`
**Active phase:** queue.md is now down to the in-flight cursor concerns (queue #2 remaining items) and E2.7 (speculative paths). E2.5, E2.6, and B1 all tagged or closed. Pick next item from `docs/queue.md` per the user's call.
```

- [ ] **Step 2: Add a recent-changes-log entry**

Find the recent-changes-log table. Add a new top row:

```markdown
| 2026-05-18 | `<COMMIT-SHA-1>..<COMMIT-SHA-2>` | **B1 buffer convention landed.** Spec `docs/specs/2026-05-18-b1-buffer-convention-design.md`; plan `docs/plans/2026-05-18-b1-buffer-convention.md`. Block buffers now content-only across all `BlockKind`s; `interBlockSeparator()` carries the full inter-block gap (`"\n\n"`); save normalizes runs of 2+ blank lines and ensures a single trailing `\n`. Two commits along the lib boundary, four falsifiability proofs in history. Closes queue.md §#4; closes `shift_enter_creates_visible_newline` (`QEXPECT_FAIL` markers removed). `tst_block_buffer_invariant` (markoff-core, 15-fixture corpus) + `tst_block_buffer_interactive` (markoff-live, 2 harness-driven slots) pin the contract going forward. |
```

Substitute the actual commit SHAs.

### Task 4.4: Commit the docs closeout

**Files:** all docs changes from Phase 4.

- [ ] **Step 1: Review staged**

```
git diff --stat
```

Expected: 4 files changed across `docs/`.

- [ ] **Step 2: Commit**

```
git add docs/INVARIANTS.md docs/queue.md docs/e-arc/e-arc-status.md libs/markoff-core/CLAUDE.md

git commit -m "$(cat <<'EOF'
docs: B1 buffer convention landed — INVARIANTS, queue, status board

  * docs/INVARIANTS.md: new "Block buffer convention" section. Applies
    project-wide. Cross-referenced from libs/markoff-core/CLAUDE.md.

  * docs/queue.md: §#4 closed with implementing commit SHAs. Discipline
    Log entry added documenting the developmental history of the
    fossil convention.

  * docs/e-arc/e-arc-status.md: Last-updated + recent-changes log
    updated. Active-phase moves on from queue #4.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task 4.5: Final verification — full suite green

**Files:** none.

- [ ] **Step 1: Run the full fast suite**

```
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: all green. The known pre-B1 baseline failure (`shift_enter_creates_visible_newline`) is now green. No new failures.

- [ ] **Step 2: Run the optional slow suite (manual gate)**

```
scripts/run-tests.sh
```

Expected: `tst_realistic` + `tst_benchmark` complete without regression. Times match the recent baseline (`tst_realistic` ~70 s, `tst_benchmark` ~340 s). If either surfaces a regression, classify per CLAUDE.md and investigate before declaring done.

- [ ] **Step 3: Confirm branch history**

```
git log --oneline | head -15
```

Expected sequence (newest first), in order:

```
<sha> docs: B1 buffer convention landed — INVARIANTS, queue, status board
<sha> Revert "proof: interBlockSeparator reverted to \"\\n\" ..."
<sha> proof: interBlockSeparator reverted to "\n" -> roundtrip slot fails on multi-block fixtures
<sha> Revert "proof: onD2Changed chop restored ..."
<sha> proof: onD2Changed chop restored -> tst_block_buffer_interactive soft-break slot fails
<sha> Revert "proof: applyFlatEdit \"+ \\n\" restored ..."
<sha> proof: applyFlatEdit "+ \n" restored -> tst_block_buffer_interactive paste slot fails
<sha> Revert "proof: B1 load-strip removed ..."
<sha> proof: B1 load-strip removed -> tst_block_buffer_invariant fails
<sha> fix(markoff-live): retire onD2Changed chop, close 5-day-old soft-break regression
<sha> feat(markoff-core): B1 buffer convention — content-only buffers
<sha> test(markoff-core): B1 buffer-invariant tests — failing under current code
<sha> spec: B1 buffer convention — block buffers are content
...
```

12 new commits total (spec + tests + two feature + four proof + four revert + closeout). HEAD = closeout. Full suite green at HEAD.

---

## Risk register (from spec §7)

| Risk | Mitigation built into the plan |
|---|---|
| A per-kind serializer secretly emits a trailing `\n` | Task 1.8 (per-kind audit) + Task 1.9 (full suite). Test 2 (round-trip) catches any regression on the relevant fixture. |
| `applyFlatEdit`'s decomposer has a corner case missed by fixtures | Task 2.2 Step 4 paste slot exercises it. Existing `tst_d4_apply_flat_edit` rerun in Task 1.9. |
| Untouched-block normalization corrupts a round-trip | Test 2's fixed-point comparison catches drift across two saves regardless of input. |
| QML JS-exception trap fires because a QML callsite reads `model.text.endsWith('\n')` | Desired surfacing. Task 2.4 catches; classify and fix in Phase 2. |
| Mid-buffer Enter (Test 3 step 4) isn't a supported gesture in the structural-key-handler | Task 2.2 Step 4 notes the fallback (direct cmd call) and documents the gap. |
| Performance regression from extra `chop(1)` per block at load | One byte-trim per block per load is sub-microsecond. Task 4.5 Step 2 optionally runs the benchmark suite to confirm. |
