# D4 Parser Scope Reduction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire the document-wide-incremental-parse pipeline (`ParsePool`, `IncrementalParseSession`, `parseUpdated`, `parseSequence`, `MarkoffEdit`, `applyLocalEdit`); migrate the source-widget to D2 primitives via a new `applyFlatEdit`; retire markoff-bench and view-qml live mode; delete the dead legacy `Cmd::*` family + `CommandFacade` + `ReplaceController`.

**Architecture:** After D4, every parse is one of two shapes: (1) cold load via `Markoff::Document::fromMarkdown`, (2) per-block on cache miss via `Markoff::inlineSpansFor`. Every edit is either per-block (`Cmd::*` D2 helpers, used by live-render) or buffer-global (new `applyFlatEdit`, used by source-widget). No worker thread for parse, no document-wide reparse on edit.

**Tech Stack:** C++20, Qt6.8+, CMake 3.19+, KF6::SyntaxHighlighting, tree-sitter (vendored in `libs/markoff-parser/src/vendor`), QTest.

**Spec:** `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` (read this first; the plan executes the spec).

---

## 0. Read first (orientation)

A fresh agent picking up D4 should read these in order. The plan assumes you've read them.

1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
2. `docs/d-arc/d-arc-status.md` — live status (D4 is `plan-approved`)
3. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items
4. `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` — the D4 spec
5. `CLAUDE.md` (worktree root) — branch posture and lib layout
6. `libs/markoff-foundation/CLAUDE.md` — foundation-internal guide
7. `libs/markoff-live-render/CLAUDE.md` — view leaf guide

---

## 1. Workspace and build/test cheatsheet

**Working tree:** `.worktrees/foundation-exploration/` (NOT the master tree).
**Branch:** `exploration/new-foundation`.

**Build (cap parallelism at -j 8 — never bare `-j` or higher; the user's CPU saturates):**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```

**Test:**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration/build-dev
ctest -j 8 --output-on-failure
```

**Fast inner loop (skip benches if they still exist; once Phase 5 lands they're gone):**

```bash
ctest -j 8 -E "tst_realistic|tst_benchmark"
```

**Run a single test:**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```

**Pre-commit baseline:** before starting any phase, run a full `cmake --build build-dev -j 8 && ctest -j 8` and confirm green. The tip of `exploration/new-foundation` (post-D3-correction) is 146/146.

---

## 2. Sequencing and phase map

Per spec §8, the order is bottom-up: build the new primitive, migrate consumers, retire dead libraries, delete the deprecated symbols last. Each phase ends with a green `ctest`. Each task ends with a `git commit`.

| Phase | What it does | Why this position |
|---|---|---|
| 1 | Build `applyFlatEdit` primitive (TDD) | New foundation API; no consumer yet |
| 2 | Migrate `SourceTextDocumentBinding` to D2 primitives | Gates `applyLocalEdit` deletion |
| 3 | Rewrite source-widget tests + update app | Confirms migration is complete |
| 4 | Delete dead legacy `Cmd::*` + `CommandFacade` + `ReplaceController` | Drops `MarkoffEdit` consumers (foundation-internal) |
| 5 | Retire `markoff-bench` | Drops `MarkoffEdit` and parse-pool consumers |
| 6 | Retire `markoff-view-qml` live mode | Drops the last `MarkoffEdit` and `parseUpdated` consumers |
| 7 | Delete deprecated foundation tests | They probe symbols about to be deleted |
| 8 | Excise `parsePool` from `MarkoffDocument` | Quietens the deprecated pipeline |
| 9 | Delete `parseUpdated` / `parseSequence` / `MarkoffEdit` / `applyLocalEdit` | Public surface deletion |
| 10 | Delete foundation-internal deprecated infrastructure files | `ParsePool*`, `IncrementalParseSession*`, `ParsePhases.h` |
| 11 | Parser-library deletions (`parseIncremental`, etc.) | Final deletion, no consumers left |
| 12 | Top-level CMake cleanup | Drop retired subdirs |
| 13 | Doc updates (post-execution) | Status board → complete; CLAUDE.md banner; etc. |
| 14 | Acceptance verification + dogfood | Final green build + grep + dogfood pass |

---

## 3. File map

### 3.1 Created in this plan

- `libs/markoff-foundation/src/ApplyFlatEdit.cpp` — the new flat-edit decomposition implementation
- `libs/markoff-foundation/include/markoff-foundation/ApplyFlatEdit.h` — header with helper types if any (most logic stays as a method on `MarkoffDocument`)
- `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp` — test file

### 3.2 Modified

- `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h` — add `applyFlatEdit` method declaration; later: remove `applyLocalEdit`, `parseUpdated`, `parseSequence`, `setRenderPhaseTaps`, includes of `MarkoffEdit.h`
- `libs/markoff-foundation/src/MarkoffDocument.cpp` — add `applyFlatEdit` definition; later: remove parsePool member usage, `applyLocalEdit` body, `parseSequence`, ctor relay
- `libs/markoff-foundation/src/MarkoffDocumentPrivate.h` — remove parsePool include + member; later: remove parseSequence, latestBlockAnchors
- `libs/markoff-foundation/src/SourceTextDocumentBinding.cpp` — migrate forward + reverse direction off `applyLocalEdit` / `MarkoffEdit`; subscribe to `d2DocumentChanged`
- `libs/markoff-foundation/include/markoff-foundation/SourceTextDocumentBinding.h` — corresponding declarations
- `libs/markoff-foundation/CMakeLists.txt` — add new source files; later: remove deleted files; remove tests for deleted families
- `libs/markoff-foundation/tests/CMakeLists.txt` — register new test; later: remove deleted tests
- `libs/markoff-foundation/CLAUDE.md` — remove `parseUpdated` / `parseSequence` / `applyLocalEdit` / `latestBlockAnchors` / `ParsePool` mentions; add `applyFlatEdit` description (Phase 13)
- `libs/markoff-source-widget/tests/tst_source_widget_editor.cpp` — drop `parseUpdated` `QSignalSpy`; reshape settle
- `libs/markoff-source-widget/tests/tst_source_widget_binding_roundtrip.cpp` — replace `applyLocalEdit` / `MarkoffEdit` with new entry point
- `libs/markoff-source-widget/tests/tst_source_widget_findbar.cpp` — same
- `libs/markoff-source-widget/app/main.cpp` — same
- `libs/markoff-live-render/tests/tst_live_render_paragraph_edit.cpp` — comment update only
- `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h` — remove deleted methods/structs; trim observability counters
- `libs/markoff-parser/src/TreeSitterParser.cpp` — remove implementations
- `libs/markoff-parser/include/markoff-parser/Document.h` — remove `Document::fromComponents`
- `libs/markoff-parser/src/Document.cpp` — remove implementation
- `libs/markoff-parser/CMakeLists.txt` — remove `tst_incremental_parse` registration
- `libs/markoff-parser/tests/CMakeLists.txt` — same
- `CMakeLists.txt` (root) — drop `add_subdirectory(libs/markoff-bench)`, drop `add_subdirectory(apps/bench)`
- `docs/d-arc/d-arc-status.md` — Phase 13: D4 → `complete`; recent-changes log entry
- `docs/d-arc/2026-05-04-d-arc-roadmap.md` — Phase 13: D4 status
- `CLAUDE.md` (worktree root) — Phase 13: banner update for D5 prep
- `docs/TODO.md` — Phase 13: any leftover TODOs

### 3.3 Deleted

**Foundation:**

- `libs/markoff-foundation/src/ParsePool.h`, `ParsePool.cpp`
- `libs/markoff-foundation/src/ParsePoolWorker.h`, `ParsePoolWorker.cpp`
- `libs/markoff-foundation/src/IncrementalParseSession.h`, `IncrementalParseSession.cpp`
- `libs/markoff-foundation/src/ParsePhases.h`
- `libs/markoff-foundation/include/markoff-foundation/MarkoffEdit.h`
- `libs/markoff-foundation/src/MarkoffEdit.cpp`
- `libs/markoff-foundation/include/markoff-foundation/Cmd/Block.h`, `src/Cmd/Block.cpp`
- `libs/markoff-foundation/include/markoff-foundation/Cmd/Insert.h`, `src/Cmd/Insert.cpp`
- `libs/markoff-foundation/include/markoff-foundation/Cmd/InlineFormat.h`, `src/Cmd/InlineFormat.cpp`
- `libs/markoff-foundation/src/Cmd/Helpers.cpp` (and any companion `Helpers.h`)
- `libs/markoff-foundation/include/markoff-foundation/CommandFacade.h`, `src/CommandFacade.cpp`
- `libs/markoff-foundation/include/markoff-foundation/ReplaceController.h`, `src/ReplaceController.cpp`
- `libs/markoff-foundation/include/markoff-foundation/Render/RenderPhaseTaps.h` (verify no surviving consumer at Phase 8)

**Foundation tests:**

- `libs/markoff-foundation/tests/tst_foundation_parse_pool.cpp`
- `libs/markoff-foundation/tests/tst_foundation_parse_phases.cpp`
- `libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp`
- `libs/markoff-foundation/tests/tst_foundation_parse_sequence.cpp`
- `libs/markoff-foundation/tests/tst_foundation_block_anchor.cpp`
- `libs/markoff-foundation/tests/tst_foundation_block_anchor_compute.cpp`
- `libs/markoff-foundation/tests/tst_foundation_block_anchor_perf.cpp`
- `libs/markoff-foundation/tests/tst_foundation_block_anchor_queries.cpp`
- `libs/markoff-foundation/tests/tst_foundation_block_anchor_stability.cpp`
- `libs/markoff-foundation/tests/tst_foundation_cmd_block.cpp`
- `libs/markoff-foundation/tests/tst_foundation_cmd_insert.cpp`
- `libs/markoff-foundation/tests/tst_foundation_cmd_inline_format.cpp`
- `libs/markoff-foundation/tests/tst_foundation_cmd_multi.cpp`
- `libs/markoff-foundation/tests/tst_foundation_command_facade.cpp`
- `libs/markoff-foundation/tests/tst_foundation_replace_controller.cpp`

**Parser:**

- `libs/markoff-parser/tests/tst_incremental_parse.cpp`

**markoff-bench (entire library + apps):**

- `libs/markoff-bench/` — whole directory
- `apps/bench/` — whole directory

**markoff-view-qml live mode:**

- All `LiveProjectionLayer.{h,cpp}`, `LiveStructuralKeyHandler.{h,cpp}`, `LiveClipboardController.{h,cpp}`, `InlineFormatHighlighter.{h,cpp}`, `ProjectionItem.{h,cpp}`, `LiveSpeculativeFenceController.{h,cpp}` and friends; live-mode QML files
- All `tst_view_qml_live_*` tests, `tst_view_qml_inline_format_highlighter`, `tst_view_qml_block_walker`, `tst_view_qml_ast_block_diff`, plus any non-`live_` test that exercises live mode (classified at task time)

(Exact file paths for the view-qml retirement are walked in Phase 6.)

### 3.4 Superseded by this plan

- `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md` — already redirects (header); leave the file as historical record.

---

# Phase 1 — `applyFlatEdit` primitive (TDD)

**Goal:** Add `MarkoffDocument::applyFlatEdit(uint32_t oldStart, uint32_t oldEnd, QByteArray newText, Origin origin)`. It opens an `UndoLog::Transaction`, decomposes the flat byte-range edit into the right sequence of per-block `d2ApplyBufferEdit` calls + structural ops, and commits.

**Why a method on `MarkoffDocument` (not a free `Cmd::applyFlatEdit`):** the decomposition needs `iterateBlocks()`, `blockText(BlockId)`, `d2ApplyBufferEdit`, `d2InsertBlock`, `d2RemoveBlock`, `d2UndoLog()` — all on the document. Putting it on the document keeps the call site clean (`doc.applyFlatEdit(...)`).

**Background — block-buffer convention:** every block's CRDT buffer holds its content; the inter-block delimiter is a trailing `\n` on each block except possibly the last. `Cmd::backspaceMerge` (read it for reference: `libs/markoff-foundation/src/Cmd/D2.cpp:62-87`) handles the trailing-`\n` special case when merging. `applyFlatEdit` follows the same convention.

**Task structure:** test cases first (each its own commit), then minimal implementation, then full implementation.

### Task 1.1: Test scaffold + first failing test (intra-block insert)

**Files:**
- Create: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`
- Modify: `libs/markoff-foundation/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Write the test scaffold and first failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff;

class TstD4ApplyFlatEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void intraBlockInsert_appendsBytesToTargetBlock();
};

namespace {
QByteArray fullText(MarkoffDocument &doc)
{
    QByteArray out;
    bool first = true;
    for (BlockId id : doc.iterateBlocks()) {
        if (!first) {} // block buffers carry their own trailing \n
        out += doc.blockText(id);
        first = false;
    }
    return out;
}
}

void TstD4ApplyFlatEdit::intraBlockInsert_appendsBytesToTargetBlock()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello world"), Origin::FirstOpen);

    // Insert "!" between "hello" and " world".
    doc.applyFlatEdit(/*oldStart=*/5, /*oldEnd=*/5,
                      /*newText=*/QByteArray("!"),
                      /*origin=*/Origin::UserKeystroke);

    QCOMPARE(fullText(doc), QByteArray("hello! world"));
}

QTEST_GUILESS_MAIN(TstD4ApplyFlatEdit)
#include "tst_d4_apply_flat_edit.moc"
```

- [ ] **Step 2: Register the test in CMake**

Edit `libs/markoff-foundation/tests/d2/CMakeLists.txt`. Find the existing pattern (e.g. `tst_d2_apply_block_edit`) and add a sibling entry:

```cmake
add_executable(tst_d4_apply_flat_edit tst_d4_apply_flat_edit.cpp)
target_link_libraries(tst_d4_apply_flat_edit PRIVATE Qt6::Test markoff-foundation)
add_test(NAME tst_d4_apply_flat_edit COMMAND tst_d4_apply_flat_edit)
```

(Match the exact pattern used by adjacent tests in this file — copy the closest neighbour and rename.)

- [ ] **Step 3: Verify the test fails to compile**

Run:
```bash
cmake --build build-dev -j 8 --target tst_d4_apply_flat_edit
```
Expected: compile error — `applyFlatEdit` is not a member of `MarkoffDocument` and `Origin::UserKeystroke` may need verification (check `libs/markoff-foundation/include/markoff-foundation/Origin.h` for the actual enum value names; if `UserKeystroke` doesn't exist, use whatever the existing convention is for "live edit from a flat-text view" — e.g. `Origin::Local`).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp \
        libs/markoff-foundation/tests/d2/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(d4): tst_d4_apply_flat_edit scaffold + intra-block-insert case

Failing baseline for the applyFlatEdit primitive (Phase 1). Compile
fails: applyFlatEdit not yet declared on MarkoffDocument.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.2: Add `applyFlatEdit` declaration + minimal stub

**Files:**
- Modify: `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Add the declaration**

In `MarkoffDocument.h`, locate the `// ===== D2 per-block edit API =====` section (around line 176). Add immediately after `applyStructural`:

```cpp
    /// Apply a buffer-global flat edit. Decomposes the edit into the
    /// appropriate sequence of per-block buffer edits + structural ops
    /// inside one UndoLog::Transaction. Used by flat-text views
    /// (e.g. source widget) that don't natively know block boundaries.
    ///
    /// Coordinates are global UTF-8 byte offsets across the concatenated
    /// block buffers (each block buffer carries its own trailing '\n'
    /// inter-block delimiter, per the per-block CRDT convention).
    /// `oldStart` and `oldEnd` must satisfy oldStart <= oldEnd <= total.
    void applyFlatEdit(uint32_t oldStart,
                       uint32_t oldEnd,
                       const QByteArray &newText,
                       Origin origin);
```

- [ ] **Step 2: Add a minimal stub definition**

In `MarkoffDocument.cpp`, add:

```cpp
void MarkoffDocument::applyFlatEdit(uint32_t /*oldStart*/,
                                    uint32_t /*oldEnd*/,
                                    const QByteArray & /*newText*/,
                                    Origin /*origin*/)
{
    // TODO Phase 1 Task 1.3+: implement decomposition.
    Q_ASSERT_X(false, "applyFlatEdit",
               "not yet implemented; covered by tst_d4_apply_flat_edit");
}
```

- [ ] **Step 3: Build to confirm declaration compiles**

```bash
cmake --build build-dev -j 8 --target markoff-foundation
```
Expected: success.

- [ ] **Step 4: Run the test to confirm it now fails at runtime (asserts)**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: FAIL with the assertion message.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): declare applyFlatEdit; stub asserts unimplemented

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.3: Implement intra-block insert path

**Files:**
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Replace the stub with the intra-block-only implementation**

Replace the body with:

```cpp
void MarkoffDocument::applyFlatEdit(uint32_t oldStart,
                                    uint32_t oldEnd,
                                    const QByteArray &newText,
                                    Origin origin)
{
    Q_UNUSED(origin);  // wired up later if/when Origin propagation matters
    Q_ASSERT(oldStart <= oldEnd);

    // Walk blocks and find the one containing the edit. Track the
    // running global-byte offset.
    const auto blocks = iterateBlocks();
    uint32_t globalStart = 0;
    for (BlockId blk : blocks) {
        const QByteArray text = blockText(blk);
        const uint32_t blockSize = static_cast<uint32_t>(text.size());
        const uint32_t globalEnd = globalStart + blockSize;
        if (oldStart >= globalStart && oldEnd <= globalEnd
            && newText.indexOf('\n') == -1) {
            // Intra-block, no embedded newline → single buffer edit.
            UndoLog::Transaction t(d2UndoLog());
            d2ApplyBufferEdit(blk,
                              oldStart - globalStart,
                              oldEnd - oldStart,
                              newText, t);
            return;
        }
        globalStart = globalEnd;
    }

    Q_ASSERT_X(false, "applyFlatEdit",
               "non-intra-block path not yet implemented");
}
```

- [ ] **Step 2: Run the test**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): applyFlatEdit intra-block insert path

Single buffer edit when the flat-edit range falls inside one block and
the inserted bytes contain no newline.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.4: Test + implement intra-block delete

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`

- [ ] **Step 1: Add the failing test**

Inside the class declaration, add to `private Q_SLOTS:`:

```cpp
    void intraBlockDelete_dropsRangeFromTargetBlock();
```

And implement after the previous test:

```cpp
void TstD4ApplyFlatEdit::intraBlockDelete_dropsRangeFromTargetBlock()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello world"), Origin::FirstOpen);

    doc.applyFlatEdit(5, 11, QByteArray(), Origin::UserKeystroke);
    QCOMPARE(fullText(doc), QByteArray("hello"));
}
```

- [ ] **Step 2: Run — should already pass with the intra-block path**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS (the intra-block branch handles deletes too — `oldEnd > oldStart, newText empty`).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp
git commit -m "$(cat <<'EOF'
test(d4): intra-block delete case

Confirms applyFlatEdit's intra-block path handles deletes (range with
empty newText) without further code change.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.5: Test + implement insert-newline = block split

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Add the failing test**

Add to the class:

```cpp
    void insertNewlineAtBlockEnd_splitsIntoTwoBlocks();
```

Implementation:

```cpp
void TstD4ApplyFlatEdit::insertNewlineAtBlockEnd_splitsIntoTwoBlocks()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello"), Origin::FirstOpen);
    QCOMPARE(doc.iterateBlocks().size(), 1u);

    // Insert "\n\n" at end of "hello": split into "hello\n" + "" with the
    // structural delimiter promoted to a new block.
    doc.applyFlatEdit(5, 5, QByteArray("\n\n"), Origin::UserKeystroke);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("hello\n"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray(""));
}
```

- [ ] **Step 2: Run — expect failure (split path not implemented)**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: FAIL with the "non-intra-block path not yet implemented" assertion.

- [ ] **Step 3: Implement the split path**

Update `applyFlatEdit` in `MarkoffDocument.cpp`:

```cpp
void MarkoffDocument::applyFlatEdit(uint32_t oldStart,
                                    uint32_t oldEnd,
                                    const QByteArray &newText,
                                    Origin origin)
{
    Q_UNUSED(origin);
    Q_ASSERT(oldStart <= oldEnd);

    const auto blocks = iterateBlocks();

    // Find the block containing oldStart and the block containing oldEnd.
    // (A range that lands exactly between blocks resolves into the block
    // whose end == oldStart, matching the trailing-\n delimiter convention.)
    uint32_t cursor = 0;
    int startIdx = -1;
    int endIdx = -1;
    uint32_t startWithin = 0;
    uint32_t endWithin = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const uint32_t sz = static_cast<uint32_t>(blockText(blocks[i]).size());
        const uint32_t blkEnd = cursor + sz;
        if (startIdx == -1 && oldStart <= blkEnd) {
            startIdx = static_cast<int>(i);
            startWithin = oldStart - cursor;
        }
        if (oldEnd <= blkEnd) {
            endIdx = static_cast<int>(i);
            endWithin = oldEnd - cursor;
            break;
        }
        cursor = blkEnd;
    }
    Q_ASSERT(startIdx >= 0 && endIdx >= 0);

    UndoLog::Transaction t(d2UndoLog());

    // Decompose newText into segments split by '\n'. Every '\n' in newText
    // becomes a structural break: a trailing '\n' on the current block plus
    // a new empty block created after it.
    if (startIdx == endIdx && newText.indexOf('\n') == -1) {
        // Pure intra-block edit, no structural change.
        d2ApplyBufferEdit(blocks[startIdx], startWithin,
                          endWithin - startWithin, newText, t);
        return;
    }

    if (startIdx == endIdx && !newText.isEmpty()) {
        // Intra-block edit replacing oldRange with text containing newlines:
        // first apply the buffer edit (with all newlines kept inline), then
        // walk the inserted segment to introduce structural breaks.
        // For simplicity in v1, decompose by inserting the first segment
        // up to the first '\n' (if any), splitting the block, and recursing
        // for the remainder.
        // [TODO Task 1.6+] expand this branch.
        Q_ASSERT_X(newText.count('\n') <= 2, "applyFlatEdit",
                   "complex multi-newline intra-block insert not yet supported");

        // Special-case "\n\n" appended at end-of-block: split into
        // (current + "\n", new empty block).
        if (startWithin == endWithin && startWithin == static_cast<uint32_t>(blockText(blocks[startIdx]).size())
            && newText == QByteArray("\n\n")) {
            d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, QByteArray("\n"), t);
            d2InsertBlock(blocks[startIdx], BlockKind::Paragraph, t);
            return;
        }

        // [TODO Task 1.6+] more cases.
        Q_ASSERT_X(false, "applyFlatEdit",
                   "intra-block insert with newlines not yet fully implemented");
        return;
    }

    // [TODO Task 1.7+] cross-block edits.
    Q_ASSERT_X(false, "applyFlatEdit",
               "cross-block edit not yet implemented");
}
```

- [ ] **Step 4: Run the test**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS for all three current tests.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp \
        libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): applyFlatEdit handles \\n\\n at block end → split

First structural-decomposition case: appending '\\n\\n' at end of a
block becomes (buffer-append '\\n' on current block + insert empty
paragraph after it) within one transaction.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.6: Test + implement intra-block split (newline mid-block)

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Add the failing test**

```cpp
    void insertSingleNewlineMidBlock_splitsBlock();
```

```cpp
void TstD4ApplyFlatEdit::insertSingleNewlineMidBlock_splitsBlock()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foobar"), Origin::FirstOpen);

    // Insert "\n\n" between "foo" and "bar": split into "foo\n" + "bar".
    doc.applyFlatEdit(3, 3, QByteArray("\n\n"), Origin::UserKeystroke);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo\n"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("bar"));
}
```

- [ ] **Step 2: Run — expect failure**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: FAIL with the "intra-block insert with newlines not yet fully implemented" assertion.

- [ ] **Step 3: Generalise the intra-block-with-newlines branch**

Replace the entire `if (startIdx == endIdx && !newText.isEmpty())` branch with the more general implementation:

```cpp
    if (startIdx == endIdx) {
        // Intra-block (possibly with embedded newlines).
        const QByteArray currentText = blockText(blocks[startIdx]);
        const uint32_t removeLen = endWithin - startWithin;

        // Tail content of the current block (after the deleted range).
        const QByteArray tail = currentText.mid(static_cast<int>(endWithin));

        // Replace [startWithin, endWithin) with the inserted text WITHOUT
        // newlines first — we'll handle the splits below.
        if (newText.indexOf('\n') == -1) {
            d2ApplyBufferEdit(blocks[startIdx], startWithin, removeLen, newText, t);
            return;
        }

        // newText contains '\n'. The convention for a markdown editor is:
        // a single '\n' inside a flat insert becomes a soft break (kept as
        // '\n' inside the same block); two consecutive '\n's becomes a
        // block split (one '\n' tails the current block, the rest seeds a
        // new sibling). We follow this convention so a flat-text view's
        // double-Enter behaviour matches the per-block view's Enter
        // behaviour (Cmd::enterAtEnd from Cmd/D2.cpp).
        //
        // Split newText on "\n\n" boundaries:
        //   parts = newText.split("\n\n")
        // The first part stays in the current block (replacing the old
        // range); each subsequent part seeds a new Paragraph block.
        //
        // Special-case: if newText ends with "\n\n", the last new block is
        // empty (intentional — caller is opening a fresh block).
        QList<QByteArray> parts;
        int cursor2 = 0;
        while (true) {
            const int nextDouble = newText.indexOf("\n\n", cursor2);
            if (nextDouble == -1) {
                parts.append(newText.mid(cursor2));
                break;
            }
            parts.append(newText.mid(cursor2, nextDouble - cursor2));
            cursor2 = nextDouble + 2;
        }
        Q_ASSERT(parts.size() >= 1);

        // Replace the old range with parts[0] + "\n" so the current block
        // gets its trailing delimiter, then strip the tail off into the
        // last new block.
        QByteArray firstReplacement = parts.front() + QByteArray("\n");
        d2ApplyBufferEdit(blocks[startIdx], startWithin, removeLen + static_cast<uint32_t>(tail.size()),
                          firstReplacement, t);

        // Insert one block per remaining part. The last block carries the
        // original tail content as its prefix.
        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            const bool isLast = (i == parts.size() - 1);
            QByteArray seed = parts[i];
            if (isLast) {
                seed += tail;
            } else {
                seed += QByteArray("\n");
            }
            if (!seed.isEmpty()) {
                d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            }
            after = newBlk;
        }
        return;
    }
```

(Remove the "\n\n at block end" special case from Task 1.5 — it's covered by the general code now.)

- [ ] **Step 4: Run all tests**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS for all four cases (the "\n\n at block end" case still works because the general code handles tail="" correctly).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp \
        libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): applyFlatEdit handles intra-block split

Splits the current block on every '\\n\\n' boundary in the inserted
text. Tail content moves to the last new block. Single '\\n' stays as
a soft break (kept inline within the block).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.7: Test + implement cross-block edit (delete spanning two blocks → merge)

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Add the failing test**

```cpp
    void deleteSpanningTwoBlocks_mergesIntoFirst();
```

```cpp
void TstD4ApplyFlatEdit::deleteSpanningTwoBlocks_mergesIntoFirst()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"), Origin::FirstOpen);
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo\n"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("bar"));

    // Delete "\nbar" — that's bytes [3, 8) in flat coords (the '\n' at the
    // end of block 0, then the entire block 1).
    // Convention: removing the inter-block '\n' merges block 1 into block 0.
    doc.applyFlatEdit(3, 8, QByteArray(), Origin::UserKeystroke);

    blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 1u);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo"));
}
```

(Note: the global byte coordinate uses each block buffer's actual size; block 0 = "foo\n" = 4 bytes, block 1 = "bar" = 3 bytes, total 7 bytes. So bytes [3, 7) covers the trailing '\n' of block 0 plus all of "bar". Adjust the test if `loadFromMarkdown` produces a different separator convention — verify by printing `blockText` sizes before writing the assertion. If the byte count is 7 not 8, use 7.)

- [ ] **Step 2: Run — expect failure**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: FAIL with the "cross-block edit not yet implemented" assertion.

- [ ] **Step 3: Implement the cross-block path**

Replace the final `Q_ASSERT_X(false, "applyFlatEdit", "cross-block edit not yet implemented");` with:

```cpp
    // Cross-block edit. Strategy:
    //   - Trim the start block: remove [startWithin, blockEnd] from blocks[startIdx].
    //   - Remove every middle block (blocks[startIdx+1 .. endIdx-1]) entirely.
    //   - Trim the end block: remove [0, endWithin) from blocks[endIdx].
    //   - Apply the inserted newText as if it were an intra-block insert
    //     at the end of the trimmed start block (recursively handling
    //     '\n' splits exactly as the intra-block branch above).
    //   - Merge the trimmed end block's remaining tail into the last block
    //     produced by the inserted-text expansion.
    //
    // Implementation note: instead of duplicating the segmenting code, we
    // first do all the structural trimming, then call back into the
    // intra-block branch logic for the inserted text + tail.

    const QByteArray startTextBefore = blockText(blocks[startIdx]);
    const QByteArray endTextBefore   = blockText(blocks[endIdx]);
    const QByteArray endTail = endTextBefore.mid(static_cast<int>(endWithin));

    // Trim the start block.
    d2ApplyBufferEdit(blocks[startIdx], startWithin,
                      static_cast<uint32_t>(startTextBefore.size()) - startWithin,
                      QByteArray(), t);
    // Remove the end block (its remaining tail is moved into start block below).
    // First, capture middle blocks for removal.
    for (int i = startIdx + 1; i < endIdx; ++i) {
        d2RemoveBlock(blocks[i], t);
    }
    d2RemoveBlock(blocks[endIdx], t);

    // Now apply newText + endTail to the start block at startWithin via the
    // same segmenting logic as the intra-block branch.
    const uint32_t insertAt = startWithin;

    // Build a "synthetic" intra-block insert: the inserted bytes are
    // newText, the tail content (placed back after the insert) is endTail.
    // Reuse the segmenting code by inlining it here.
    QList<QByteArray> parts;
    int cursor2 = 0;
    while (true) {
        const int nextDouble = newText.indexOf("\n\n", cursor2);
        if (nextDouble == -1) {
            parts.append(newText.mid(cursor2));
            break;
        }
        parts.append(newText.mid(cursor2, nextDouble - cursor2));
        cursor2 = nextDouble + 2;
    }
    Q_ASSERT(parts.size() >= 1);

    if (parts.size() == 1) {
        // No structural break in newText. Re-stitch endTail onto start block.
        QByteArray combined = parts.front() + endTail;
        d2ApplyBufferEdit(blocks[startIdx], insertAt, 0, combined, t);
    } else {
        // First part stays in start block, with its own trailing '\n' as
        // structural delimiter.
        QByteArray firstReplacement = parts.front() + QByteArray("\n");
        d2ApplyBufferEdit(blocks[startIdx], insertAt, 0, firstReplacement, t);

        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            const bool isLast = (i == parts.size() - 1);
            QByteArray seed = parts[i];
            if (isLast) {
                seed += endTail;
            } else {
                seed += QByteArray("\n");
            }
            if (!seed.isEmpty()) {
                d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            }
            after = newBlk;
        }
    }
```

- [ ] **Step 4: Run all tests**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS for all five cases.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp \
        libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): applyFlatEdit handles cross-block delete → merge

Trims start block, removes middle blocks, trims end block, re-stitches
the end-block tail into the start block (or into the last expanded
block if the edit also inserted text with structural breaks).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.8: Test + verify undo round-trip

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`

- [ ] **Step 1: Add the test**

```cpp
    void undo_restoresPreEditState_acrossSplitAndMerge();
```

```cpp
void TstD4ApplyFlatEdit::undo_restoresPreEditState_acrossSplitAndMerge()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"), Origin::FirstOpen);
    const QByteArray before = fullText(doc);

    // Two structural ops: split + merge.
    doc.applyFlatEdit(3, 3, QByteArray("\n\nzzz"), Origin::UserKeystroke);
    doc.applyFlatEdit(3, 12, QByteArray(), Origin::UserKeystroke);

    doc.undoD2();
    doc.undoD2();

    QCOMPARE(fullText(doc), before);
    QCOMPARE(doc.iterateBlocks().size(), 2u);
}
```

(If `undoD2` doesn't exist by that name, check `MarkoffDocument.h:188` for the actual method — it's `undoD2()`.)

- [ ] **Step 2: Run — should already pass since each `applyFlatEdit` opens its own transaction**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp
git commit -m "$(cat <<'EOF'
test(d4): applyFlatEdit undo round-trip across split + merge

Each applyFlatEdit opens its own UndoLog::Transaction; two undoD2()
calls restore pre-edit state.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.9: Test + verify round-trip identity (random fuzz-style)

**Files:**
- Modify: `libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp`

- [ ] **Step 1: Add a deterministic round-trip test**

```cpp
    void roundTrip_arbitraryEditMatchesFlatStringReplace();
```

```cpp
void TstD4ApplyFlatEdit::roundTrip_arbitraryEditMatchesFlatStringReplace()
{
    struct Case { QByteArray initial; uint32_t s, e; QByteArray ins; QByteArray expected; };
    const Case cases[] = {
        {"abc",        0, 3, "xyz",        "xyz"},
        {"abc\n\ndef", 0, 8, "",           ""},
        {"abc",        1, 1, "\n\n",       "a\n\nbc"},
        {"abc\n\ndef", 3, 5, "Q",          "abcQdef"},
        {"abc\n\ndef", 4, 4, "\n\n",       "abc\n\n\n\ndef"},  // inter-block split
    };
    for (const auto &c : cases) {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(c.initial, Origin::FirstOpen);
        doc.applyFlatEdit(c.s, c.e, c.ins, Origin::UserKeystroke);
        QCOMPARE(fullText(doc), c.expected);
    }
}
```

- [ ] **Step 2: Run**

```bash
ctest -j 8 -R "tst_d4_apply_flat_edit" --output-on-failure
```
Expected: PASS. **If a case fails**, that's the boundary condition Phase 1 didn't cover. Diagnose by reading `blockText` for each block before/after, fix `applyFlatEdit`, repeat.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-foundation/tests/d2/tst_d4_apply_flat_edit.cpp
git commit -m "$(cat <<'EOF'
test(d4): applyFlatEdit round-trip parity for representative cases

Five flat-edit cases that span pure replace, full delete, intra-block
split, intra-block replace, and inter-block split. Each should match
what the equivalent QString::replace would produce on the flat text.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 1.10: Full-suite green check

- [ ] **Step 1: Run the full test suite**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS — `tst_d4_apply_flat_edit` plus everything pre-existing. If any pre-existing test broke, diagnose: `applyFlatEdit` is additive; nothing else should have changed.

- [ ] **Step 2: No commit (verification only)**

---

# Phase 2 — Migrate `SourceTextDocumentBinding` to D2 primitives

**Goal:** Reshape the source-widget binding so it never calls `applyLocalEdit` or constructs `MarkoffEdit`. Forward direction → `applyFlatEdit`. Reverse direction → subscribe to `d2DocumentChanged`.

**Background:** `SourceTextDocumentBinding` is the bridge between a flat-text widget (e.g. `QPlainTextEdit`) and `MarkoffDocument`. Today it intercepts `QPlainTextEdit::contentsChange(int qtPos, int charsRemoved, int charsAdded)`, builds a `MarkoffEdit{oldStart, oldEnd, newText}` in UTF-8 byte coords, and calls `MarkoffDocument::applyLocalEdit({...})`. Reverse direction listens to `MarkoffDocument::contentsChanged(QList<MarkoffEdit>)` and replays edits into the QPlainTextEdit, with re-entrance guarded.

Read `libs/markoff-foundation/src/SourceTextDocumentBinding.cpp` end-to-end before starting (~384 lines). The file's structure is the source of truth for the migration.

### Task 2.1: Audit current binding shape

- [ ] **Step 1: Read the current implementation**

```bash
sed -n '1,120p' libs/markoff-foundation/src/SourceTextDocumentBinding.cpp
sed -n '120,260p' libs/markoff-foundation/src/SourceTextDocumentBinding.cpp
sed -n '260,400p' libs/markoff-foundation/src/SourceTextDocumentBinding.cpp
```

(Reading is the work; no checkbox required for "I read it.") Identify and note:

- Where the forward signal is connected (e.g. `connect(&textDoc, &QTextDocument::contentsChange, ...)`)
- Where `MarkoffEdit` is constructed and `applyLocalEdit` is called
- Where the reverse subscription happens (likely a `connect(&markoffDoc, &MarkoffDocument::contentsChanged, ...)`)
- Re-entrance guard mechanism (likely a member bool flag set during the reverse-update path)
- Any byte-vs-char coordinate translation helpers (likely `Coordinates::qtPosToByte` or similar)

- [ ] **Step 2: Note the binding's public API**

Read `libs/markoff-foundation/include/markoff-foundation/SourceTextDocumentBinding.h`:

```bash
cat libs/markoff-foundation/include/markoff-foundation/SourceTextDocumentBinding.h
```

The migration must keep the public API unchanged so callers (`Markoff::Source::Widget::Editor`, view-qml's `SourceTextDocumentBindingForeign`) compile without churn. The internal forward/reverse path is what changes.

### Task 2.2: Migrate forward direction (QPlainTextEdit → MarkoffDocument)

**Files:**
- Modify: `libs/markoff-foundation/src/SourceTextDocumentBinding.cpp`

- [ ] **Step 1: Locate and replace the forward call site**

Find the function that runs on `QTextDocument::contentsChange` (likely a slot or lambda named `onContentsChange` / `applyContentsChange` / similar). Inside, replace the `applyLocalEdit({MarkoffEdit{...}})` call with `applyFlatEdit(...)`.

Concrete shape (paraphrased — adjust to match the actual variable names you find in the file):

```cpp
// BEFORE (paraphrased):
//   const auto bytes = qtPosToBytes(qtPos);
//   const auto removeBytes = qtRemovedToBytes(qtPos, removed);
//   const auto inserted = ... compute UTF-8 of inserted chars ...;
//   Markoff::MarkoffEdit ed;
//   ed.oldStart = bytes;
//   ed.oldEnd   = bytes + removeBytes;
//   ed.newText  = inserted;
//   m_doc->applyLocalEdit({ed});

// AFTER:
const auto bytes = qtPosToBytes(qtPos);
const auto removeBytes = qtRemovedToBytes(qtPos, removed);
const auto inserted = /* ... compute UTF-8 of inserted chars ... */;
m_doc->applyFlatEdit(bytes, bytes + removeBytes, inserted,
                     Markoff::Origin::UserKeystroke);
```

Drop the `#include <markoff-foundation/MarkoffEdit.h>` line at the top of `SourceTextDocumentBinding.cpp` — only do this once the reverse direction migration in Task 2.3 is done, otherwise the reverse path's `MarkoffEdit` includes break.

- [ ] **Step 2: Build (forward direction only is now D2)**

```bash
cmake --build build-dev -j 8 --target markoff-foundation
```
Expected: success. Any compile error means a stray `MarkoffEdit` reference in the forward path remains; fix.

- [ ] **Step 3: Run source-widget tests (they will probably break — that's expected, fix in Phase 3)**

```bash
ctest -j 8 -R "tst_source_widget_" --output-on-failure
```
Expected: some failures. Note them (especially `tst_source_widget_binding_roundtrip`); they'll be fixed when their setup is migrated in Phase 3.

- [ ] **Step 4: Run full suite to confirm nothing else regressed**

```bash
ctest -j 8 -E "tst_source_widget_" --output-on-failure
```
Expected: PASS (all non-source-widget tests).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/src/SourceTextDocumentBinding.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): SourceTextDocumentBinding forward path → applyFlatEdit

QPlainTextEdit::contentsChange now dispatches to
MarkoffDocument::applyFlatEdit instead of constructing a MarkoffEdit
and calling applyLocalEdit. Source-widget tests will be rewritten in
Phase 3 to match the new shape.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 2.3: Migrate reverse direction (MarkoffDocument → QPlainTextEdit)

**Files:**
- Modify: `libs/markoff-foundation/src/SourceTextDocumentBinding.cpp`
- Modify: `libs/markoff-foundation/include/markoff-foundation/SourceTextDocumentBinding.h` (if any private slot/method declarations change)

- [ ] **Step 1: Replace the reverse subscription**

Find the `connect(&markoffDoc, &MarkoffDocument::contentsChanged, ...)` (or `parseUpdated`) subscription. Replace with a subscription to `d2DocumentChanged`.

```cpp
// BEFORE (paraphrased):
//   connect(m_doc, &MarkoffDocument::contentsChanged,
//           this, &SourceTextDocumentBinding::onContentsChanged);
// (where onContentsChanged took QList<MarkoffEdit>)

// AFTER:
connect(m_doc, &MarkoffDocument::d2DocumentChanged,
        this, &SourceTextDocumentBinding::onD2DocumentChanged);
```

Replace the slot body. Reverse direction is now: re-derive the QPlainTextEdit text from the document's per-block buffers (concatenate `blockText(id)` over `iterateBlocks()`), and overwrite the QPlainTextEdit's text only if it differs. Re-entrance guard stays exactly as before — the forward path must not re-fire while the reverse update is in flight.

```cpp
void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_inReverseUpdate) return;
    QByteArray expected;
    for (Markoff::BlockId id : m_doc->iterateBlocks()) {
        expected += m_doc->blockText(id);
    }
    const QString expectedStr = QString::fromUtf8(expected);
    if (m_textDoc->toPlainText() == expectedStr) return;
    m_inReverseUpdate = true;
    m_textDoc->setPlainText(expectedStr);
    m_inReverseUpdate = false;
}
```

(Adjust types/names to match the file's existing style; `m_textDoc` may be a `QTextDocument *` or a `QPlainTextEdit *`. Keep the existing flag-name convention.)

- [ ] **Step 2: Drop the MarkoffEdit include**

Remove `#include <markoff-foundation/MarkoffEdit.h>` from `SourceTextDocumentBinding.cpp` (and the header if present). Build to find any straggler references.

- [ ] **Step 3: Build**

```bash
cmake --build build-dev -j 8 --target markoff-foundation
```
Expected: success. If an error mentions `MarkoffEdit` or `QList<MarkoffEdit>`, find the straggler and fix.

- [ ] **Step 4: Build the source-widget too (to surface upstream breakage)**

```bash
cmake --build build-dev -j 8 --target markoff-source-widget
```
Expected: success.

- [ ] **Step 5: Run all non-source-widget tests**

```bash
ctest -j 8 -E "tst_source_widget_" --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-foundation/src/SourceTextDocumentBinding.cpp \
        libs/markoff-foundation/include/markoff-foundation/SourceTextDocumentBinding.h
git commit -m "$(cat <<'EOF'
foundation(d4): SourceTextDocumentBinding reverse path → d2DocumentChanged

Reverse direction now subscribes to d2DocumentChanged and re-derives
the QPlainTextEdit text from per-block CRDT buffers via iterateBlocks +
blockText. Re-entrance guard preserved.

Drops the binding's last MarkoffEdit dependency.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 3 — Rewrite source-widget tests + update app

### Task 3.1: Rewrite `tst_source_widget_binding_roundtrip`

**Files:**
- Modify: `libs/markoff-source-widget/tests/tst_source_widget_binding_roundtrip.cpp`

- [ ] **Step 1: Read the current test**

```bash
cat libs/markoff-source-widget/tests/tst_source_widget_binding_roundtrip.cpp
```

Identify every spot that constructs `MarkoffEdit` or calls `applyLocalEdit`. Most likely the test issues a sequence of edits via `applyLocalEdit({MarkoffEdit{...}})` to set up scenarios; replace those with calls to `m_doc->applyFlatEdit(start, end, text, Origin::Local)` (or `UserKeystroke` — whichever Origin enum value is the canonical "test fixture" choice; check `Origin.h`).

- [ ] **Step 2: Rewrite each test method**

Concrete pattern:

```cpp
// BEFORE:
//   Markoff::MarkoffEdit ed;
//   ed.oldStart = 0; ed.oldEnd = 0; ed.newText = "hello";
//   doc.applyLocalEdit({ed});

// AFTER:
doc.applyFlatEdit(0, 0, QByteArray("hello"), Markoff::Origin::Local);
```

Drop `#include <markoff-foundation/MarkoffEdit.h>`. Add `#include <markoff-foundation/Origin.h>` if needed.

- [ ] **Step 3: Run the test**

```bash
ctest -j 8 -R "tst_source_widget_binding_roundtrip" --output-on-failure
```
Expected: PASS. If it fails, diagnose: most likely a coord-translation or settle-timing issue. The test may need a `QCoreApplication::processEvents()` after edits to let `d2DocumentChanged` fire and the reverse path settle.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-source-widget/tests/tst_source_widget_binding_roundtrip.cpp
git commit -m "$(cat <<'EOF'
test(source-widget): roundtrip uses applyFlatEdit, not MarkoffEdit

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 3.2: Rewrite `tst_source_widget_findbar`

**Files:**
- Modify: `libs/markoff-source-widget/tests/tst_source_widget_findbar.cpp`

- [ ] **Step 1: Read and rewrite by the same pattern as Task 3.1**

```bash
cat libs/markoff-source-widget/tests/tst_source_widget_findbar.cpp
```

Replace `MarkoffEdit` + `applyLocalEdit` with `applyFlatEdit`. Drop the `MarkoffEdit.h` include.

- [ ] **Step 2: Run**

```bash
ctest -j 8 -R "tst_source_widget_findbar" --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-source-widget/tests/tst_source_widget_findbar.cpp
git commit -m "$(cat <<'EOF'
test(source-widget): findbar uses applyFlatEdit, not MarkoffEdit

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 3.3: Touch `tst_source_widget_editor` — drop `parseUpdated` spy

**Files:**
- Modify: `libs/markoff-source-widget/tests/tst_source_widget_editor.cpp`

- [ ] **Step 1: Open the file and find the `parseUpdated` spy**

In `setDocument_attaches_and_seed_text_appears`, drop the `QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);` line. The `qWait(50)` settle remains; if the test passes locally, leave it. If it flakes, replace with:

```cpp
QSignalSpy d2Spy(&doc, &Markoff::MarkoffDocument::d2DocumentChanged);
e.setDocument(&doc);
// Wait for at most 1s for d2DocumentChanged to fire (or accept that it
// already fired synchronously inside setDocument, in which case the spy
// is already non-empty).
if (d2Spy.isEmpty()) d2Spy.wait(1000);
```

- [ ] **Step 2: Run**

```bash
ctest -j 8 -R "tst_source_widget_editor" --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-source-widget/tests/tst_source_widget_editor.cpp
git commit -m "$(cat <<'EOF'
test(source-widget): drop parseUpdated spy in editor test

Use d2DocumentChanged as the settle signal where needed. parseUpdated
is being deleted in Phase 9.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 3.4: Update `libs/markoff-source-widget/app/main.cpp`

**Files:**
- Modify: `libs/markoff-source-widget/app/main.cpp`

- [ ] **Step 1: Replace any `applyLocalEdit` calls or `MarkoffEdit` construction with `applyFlatEdit`**

```bash
grep -n "applyLocalEdit\|MarkoffEdit" libs/markoff-source-widget/app/main.cpp
```

Replace each occurrence by the Task 3.1 pattern. Drop the `#include <markoff-foundation/MarkoffEdit.h>`.

- [ ] **Step 2: Build**

```bash
cmake --build build-dev -j 8 --target markoff-source-widget-app
```
(If the target name differs, check `libs/markoff-source-widget/app/CMakeLists.txt`. Use the actual target name.)

Expected: success.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-source-widget/app/main.cpp
git commit -m "$(cat <<'EOF'
source-widget(app): use applyFlatEdit, drop MarkoffEdit dependency

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 3.5: Full suite green check

- [ ] **Step 1: Run**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS — all source-widget + foundation + parser + live-render tests. Tests for the deprecated machinery (parse_pool, parse_phases, block_anchor_*, cmd_*, command_facade, replace_controller) still pass — they probe behaviour we're about to delete in Phase 7+.

- [ ] **Step 2: No commit**

---

# Phase 4 — Delete dead `Cmd::*` legacy + `CommandFacade` + `ReplaceController`

These have zero external code consumers (verified at plan time: `grep -r "CommandFacade\|ReplaceController" libs/ apps/ --include="*.cpp" --include="*.h"` outside foundation returns empty).

### Task 4.1: Delete `Cmd::Block` family

**Files:**
- Delete: `libs/markoff-foundation/include/markoff-foundation/Cmd/Block.h`
- Delete: `libs/markoff-foundation/src/Cmd/Block.cpp`
- Delete: `libs/markoff-foundation/tests/tst_foundation_cmd_block.cpp`
- Modify: `libs/markoff-foundation/CMakeLists.txt`
- Modify: `libs/markoff-foundation/tests/CMakeLists.txt`

- [ ] **Step 1: Remove the source files**

```bash
git rm libs/markoff-foundation/include/markoff-foundation/Cmd/Block.h
git rm libs/markoff-foundation/src/Cmd/Block.cpp
git rm libs/markoff-foundation/tests/tst_foundation_cmd_block.cpp
```

- [ ] **Step 2: Update CMake**

In `libs/markoff-foundation/CMakeLists.txt`, find and remove the `src/Cmd/Block.cpp` source line. In `libs/markoff-foundation/tests/CMakeLists.txt`, find and remove the `tst_foundation_cmd_block` target registration (executable + add_test).

- [ ] **Step 3: Build**

```bash
cmake --build build-dev -j 8 --target markoff-foundation
```
Expected: failures naming `Cmd::setHeading`, `Cmd::toggleCheckbox`, `Cmd::blockQuote`, etc., from `CommandFacade.cpp`. That's expected — `CommandFacade` consumes them. We delete CommandFacade in Task 4.4. For now, comment out those lines in `CommandFacade.cpp` to get a clean build, then delete the file in Task 4.4.

Actually — don't comment out. Combine Tasks 4.1–4.4 into a single transactional commit so the build never breaks. Re-do step 1 to add CommandFacade deletion now. (Restart from Step 1 below.)

**Restart Step 1: delete all of Block + Insert + InlineFormat + Helpers + CommandFacade + ReplaceController + their tests in one commit.**

- [ ] **Step 1 (revised): Bulk delete**

```bash
git rm libs/markoff-foundation/include/markoff-foundation/Cmd/Block.h
git rm libs/markoff-foundation/include/markoff-foundation/Cmd/Insert.h
git rm libs/markoff-foundation/include/markoff-foundation/Cmd/InlineFormat.h
git rm libs/markoff-foundation/src/Cmd/Block.cpp
git rm libs/markoff-foundation/src/Cmd/Insert.cpp
git rm libs/markoff-foundation/src/Cmd/InlineFormat.cpp
git rm libs/markoff-foundation/src/Cmd/Helpers.cpp
git rm libs/markoff-foundation/include/markoff-foundation/CommandFacade.h
git rm libs/markoff-foundation/src/CommandFacade.cpp
git rm libs/markoff-foundation/include/markoff-foundation/ReplaceController.h
git rm libs/markoff-foundation/src/ReplaceController.cpp
git rm libs/markoff-foundation/tests/tst_foundation_cmd_block.cpp
git rm libs/markoff-foundation/tests/tst_foundation_cmd_insert.cpp
git rm libs/markoff-foundation/tests/tst_foundation_cmd_inline_format.cpp
git rm libs/markoff-foundation/tests/tst_foundation_cmd_multi.cpp
git rm libs/markoff-foundation/tests/tst_foundation_command_facade.cpp
git rm libs/markoff-foundation/tests/tst_foundation_replace_controller.cpp
```

If `Cmd/Helpers.h` exists, also remove it:
```bash
[ -f libs/markoff-foundation/include/markoff-foundation/Cmd/Helpers.h ] && \
    git rm libs/markoff-foundation/include/markoff-foundation/Cmd/Helpers.h
```

- [ ] **Step 2: Update foundation CMake**

Edit `libs/markoff-foundation/CMakeLists.txt`. Remove these source-list lines (search for each):

- `src/Cmd/Block.cpp`
- `src/Cmd/Insert.cpp`
- `src/Cmd/InlineFormat.cpp`
- `src/Cmd/Helpers.cpp`
- `src/CommandFacade.cpp`
- `src/ReplaceController.cpp`

Edit `libs/markoff-foundation/tests/CMakeLists.txt`. Remove these target-registration blocks:

- `tst_foundation_cmd_block`
- `tst_foundation_cmd_insert`
- `tst_foundation_cmd_inline_format`
- `tst_foundation_cmd_multi`
- `tst_foundation_command_facade`
- `tst_foundation_replace_controller`

- [ ] **Step 3: Update `Cmd.h` umbrella header**

```bash
cat libs/markoff-foundation/include/markoff-foundation/Cmd.h
```

Remove `#include "Cmd/Block.h"`, `#include "Cmd/Insert.h"`, `#include "Cmd/InlineFormat.h"` lines. Keep `Cmd/D2.h` and `Cmd/Edit.h` includes.

- [ ] **Step 4: Build**

```bash
cmake --build build-dev -j 8
```
Expected: success — no straggler consumers (we verified at plan time). If an error appears, search the codebase for the missing symbol; if it's only used by another deleted file, also delete that. If it's a live consumer we missed, stop and surface it before continuing.

- [ ] **Step 5: Run tests**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-foundation/CMakeLists.txt \
        libs/markoff-foundation/tests/CMakeLists.txt \
        libs/markoff-foundation/include/markoff-foundation/Cmd.h
git commit -m "$(cat <<'EOF'
foundation(d4): delete dead legacy Cmd::* family + CommandFacade + ReplaceController

Zero external code consumers (only internal tests + foundation-internal
references). All produced MarkoffEdit and called applyLocalEdit, which
D4 retires.

Deleted:
  - Cmd/Block.{h,cpp} (setHeading, toggleCheckbox, blockQuote)
  - Cmd/Insert.{h,cpp} (insertTable, insertLink, insertImage,
    insertHorizontalRule)
  - Cmd/InlineFormat.{h,cpp} (toggleBold, toggleItalic, toggleStrikethrough,
    toggleInlineCode)
  - Cmd/Helpers.cpp
  - CommandFacade.{h,cpp}
  - ReplaceController.{h,cpp}
  - their dedicated tests (cmd_block, cmd_insert, cmd_inline_format,
    cmd_multi, command_facade, replace_controller)

D2-native Cmd::* helpers (Cmd/D2.h, Cmd/Edit.h: insertCharacter,
insertSoftBreak, insertListItemAfter/Before, renumberRunStartingAt,
changeKind, undo, redo, etc.) survive unchanged.

A D2-native search-and-replace controller can be re-introduced later
when search-and-replace UI is needed.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 5 — Retire markoff-bench

**Goal:** Delete `libs/markoff-bench/` and `apps/bench/` entirely. They benchmark Tier 1 (`IncrementalParseSession`) and Tier 1b (`MarkoffDocument` + `ParsePool` + `parseUpdated`); neither exists post-D4.

### Task 5.1: Delete the markoff-bench library and bench apps

**Files:**
- Delete: `libs/markoff-bench/` (whole tree)
- Delete: `apps/bench/` (whole tree)
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 1: Remove the directories**

```bash
git rm -r libs/markoff-bench
git rm -r apps/bench
```

- [ ] **Step 2: Remove root CMake references**

Edit the root `CMakeLists.txt`. Find and remove `add_subdirectory(libs/markoff-bench)` and `add_subdirectory(apps/bench)` (or however they're listed — `apps/` may be conditional behind `if(BUILD_APPS)`).

- [ ] **Step 3: Build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```
(Reconfigure because we removed subdirs.) Expected: success.

- [ ] **Step 4: Run tests**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS — and the previously-slow `tst_benchmark` / `tst_realistic` are gone.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "$(cat <<'EOF'
bench(d4): retire markoff-bench library + apps/bench

The library benchmarks the Tier 1 (IncrementalParseSession) and Tier 1b
(MarkoffDocument + ParsePool + parseUpdated) hot paths; neither exists
post-D4. A fresh bench harness for the D2 hot path (per-block buffer
edit latency, inline parse cache miss, cold load) is out of scope for
D4 and may be re-introduced separately later.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 6 — Retire markoff-view-qml live mode

**Goal:** Delete view-qml's live-mode plumbing while keeping source mode. CLAUDE.md says live mode retires when live-render reaches dogfood-stable; D2 was signed off 2026-05-05 and D3 landed 2026-05-06. D4 is the moment.

### Task 6.1: Classify each view-qml file

- [ ] **Step 1: Inventory live-mode-only files**

```bash
ls libs/markoff-view-qml/src/ libs/markoff-view-qml/include/markoff/view/qml/ libs/markoff-view-qml/qml/ libs/markoff-view-qml/tests/
```

The live-mode files (verified at plan time):
- `LiveProjectionLayer.{h,cpp}`
- `LiveStructuralKeyHandler.{h,cpp}`
- `LiveClipboardController.{h,cpp}`
- `InlineFormatHighlighter.{h,cpp}`
- `ProjectionItem.{h,cpp}` (and `ProjectionItem.qml` if it exists)
- `LiveSpeculativeFenceController.{h,cpp}` (if present)
- `EditorBackend.{h,cpp}` — partially live; the `parseUpdated → parseUpdatedAt` relay is live-only

The source-mode files to keep:
- `SearchBackend.{h,cpp}` — source-mode search
- `SourceTextDocumentBindingForeign.h` — Qt foreign-type wrapper for source-mode QML
- Source-mode QML view files

The tests to delete (live-mode-only, by name pattern):
- All `tst_view_qml_live_*.cpp`
- `tst_view_qml_inline_format_highlighter.cpp`
- `tst_view_qml_block_walker.cpp` (block-walker is live-only)
- `tst_view_qml_ast_block_diff.cpp` (live-only)
- Any other test that imports `LiveProjectionLayer` / `InlineFormatHighlighter` / etc.

The tests to inspect (mode unclear by name):
- `tst_view_qml_app_smoke.cpp`
- `tst_view_qml_integration.cpp`
- `tst_view_qml_search_backend.cpp` — uses `applyLocalEdit` for setup; if it's source-mode, rewrite to `applyFlatEdit`; if it's live-mode, delete
- `tst_view_qml_editor_backend.cpp` — likely live-only

- [ ] **Step 2: Read each unclear test (one at a time)**

```bash
head -60 libs/markoff-view-qml/tests/tst_view_qml_app_smoke.cpp
head -60 libs/markoff-view-qml/tests/tst_view_qml_integration.cpp
head -60 libs/markoff-view-qml/tests/tst_view_qml_search_backend.cpp
head -60 libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp
```

For each, identify whether it instantiates a live-mode component (e.g. `MarkoffEditor` with live mode; `LiveProjectionLayer`; `EditorBackend` with `parseUpdated`) or only source-mode components. Decision rule: **if the test's behaviour depends on live mode in any way, delete it. If the test exercises source mode (or pure foundation), keep and rewrite as needed.**

Common case: `tst_view_qml_search_backend` uses `applyLocalEdit` only as test setup to mutate the document; SearchBackend itself is mode-agnostic. **Keep this test, rewrite the setup to `applyFlatEdit`**.

`tst_view_qml_app_smoke` and `tst_view_qml_integration` both load `MarkoffEditor.qml` and probably exercise live mode. **Delete both** unless inspection shows they only test source mode.

`tst_view_qml_editor_backend` directly tests `EditorBackend` and its `parseUpdated` relay. **Delete**.

### Task 6.2: Delete view-qml live-mode source files

**Files:**
- Delete: live-mode source/header/QML files identified in Task 6.1

- [ ] **Step 1: Bulk delete the live-mode files**

```bash
git rm libs/markoff-view-qml/src/LiveProjectionLayer.cpp
git rm libs/markoff-view-qml/include/markoff/view/qml/LiveProjectionLayer.h
git rm libs/markoff-view-qml/src/LiveStructuralKeyHandler.cpp
git rm libs/markoff-view-qml/include/markoff/view/qml/LiveStructuralKeyHandler.h
git rm libs/markoff-view-qml/src/LiveClipboardController.cpp
git rm libs/markoff-view-qml/include/markoff/view/qml/LiveClipboardController.h
git rm libs/markoff-view-qml/src/InlineFormatHighlighter.cpp
git rm libs/markoff-view-qml/include/markoff/view/qml/InlineFormatHighlighter.h
git rm libs/markoff-view-qml/src/ProjectionItem.cpp
git rm libs/markoff-view-qml/include/markoff/view/qml/ProjectionItem.h
```

For each, if `git rm` reports "not in index" the file doesn't exist — skip. For `LiveSpeculativeFenceController`, `LiveEditBinding`, `LiveSelectionView`, `LiveBlockModel` (if present), do the same:

```bash
for f in LiveSpeculativeFenceController LiveEditBinding LiveSelectionView LiveBlockModel ProjectionLayer; do
  [ -f "libs/markoff-view-qml/src/${f}.cpp" ] && git rm "libs/markoff-view-qml/src/${f}.cpp"
  [ -f "libs/markoff-view-qml/include/markoff/view/qml/${f}.h" ] && git rm "libs/markoff-view-qml/include/markoff/view/qml/${f}.h"
done
```

QML files:
```bash
ls libs/markoff-view-qml/qml/
```

For each `.qml` file that's the live-mode editor root or its delegates, `git rm` it. Source-mode QML stays. (If unclear, open the file and look for `LiveProjectionLayer` / `ProjectionItem` imports — those are live-mode markers.)

- [ ] **Step 2: Trim `EditorBackend`**

Edit `libs/markoff-view-qml/src/EditorBackend.cpp`. Delete the `parseUpdated → parseUpdatedAt` relay block (lines ~18-45 per the plan-time grep). Edit the matching header to remove the `parseUpdatedAt` signal declaration.

- [ ] **Step 3: Update view-qml CMake**

Edit `libs/markoff-view-qml/CMakeLists.txt`. Remove every source line corresponding to a deleted `.cpp`. Remove every `qt_add_qml_module`/`set_source_files_properties` line that names a deleted QML file.

- [ ] **Step 4: Build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8 --target markoff-view-qml
```
Expected: success. Any compile error mentions a deleted symbol; if it's referenced from source-mode code, that file may need its include trimmed too.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-view-qml/CMakeLists.txt \
        libs/markoff-view-qml/src/EditorBackend.cpp \
        libs/markoff-view-qml/include/markoff/view/qml/EditorBackend.h
git commit -m "$(cat <<'EOF'
view-qml(d4): retire live mode (source mode stays)

Deleted live-mode plumbing: LiveProjectionLayer, LiveStructuralKeyHandler,
LiveClipboardController, InlineFormatHighlighter, ProjectionItem, et al.
EditorBackend's parseUpdated → parseUpdatedAt relay removed.

Source mode (QPlainTextEdit-style view, SearchBackend, source QML)
preserved.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 6.3: Delete or rewrite view-qml tests

**Files:**
- Delete: live-mode-only tests
- Modify: `tst_view_qml_search_backend.cpp` (rewrite setup if it stays)
- Modify: `libs/markoff-view-qml/tests/CMakeLists.txt`

- [ ] **Step 1: Bulk delete live-mode tests**

```bash
git rm libs/markoff-view-qml/tests/tst_view_qml_live_*.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_inline_format_highlighter.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_inline_format_highlighter.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_block_walker.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_block_walker.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_ast_block_diff.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_app_smoke.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_app_smoke.cpp
[ -f libs/markoff-view-qml/tests/tst_view_qml_integration.cpp ] && \
    git rm libs/markoff-view-qml/tests/tst_view_qml_integration.cpp
```

(If `tst_view_qml_app_smoke` or `tst_view_qml_integration` turn out to be source-mode-only after Task 6.1's inspection, **don't** delete them — rewrite their setup to use `applyFlatEdit`.)

- [ ] **Step 2: Rewrite `tst_view_qml_search_backend` setup**

Edit `libs/markoff-view-qml/tests/tst_view_qml_search_backend.cpp`. Replace each `MarkoffEdit ed; ...; doc.applyLocalEdit({ed});` block with `doc.applyFlatEdit(ed.oldStart, ed.oldEnd, ed.newText, Markoff::Origin::Local);` (translating coords directly). Drop the `MarkoffEdit.h` include.

- [ ] **Step 3: Update tests CMake**

Edit `libs/markoff-view-qml/tests/CMakeLists.txt`. Remove every `add_executable` / `add_test` block for a deleted test.

- [ ] **Step 4: Build and run**

```bash
cmake --build build-dev -j 8
ctest -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-view-qml/tests/tst_view_qml_search_backend.cpp \
        libs/markoff-view-qml/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
view-qml(d4): delete live-mode tests; rewrite search_backend setup

Live-mode tests (tst_view_qml_live_*, inline_format_highlighter,
block_walker, ast_block_diff, editor_backend, app_smoke, integration if
live-only) deleted. tst_view_qml_search_backend rewritten to use
applyFlatEdit for test setup.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 7 — Delete deprecated foundation tests

**Files:**
- Delete: tests for parse-pipeline machinery + block-anchor reparse-survival

### Task 7.1: Bulk delete deprecated tests

- [ ] **Step 1: Delete files**

```bash
git rm libs/markoff-foundation/tests/tst_foundation_parse_pool.cpp
git rm libs/markoff-foundation/tests/tst_foundation_parse_phases.cpp
git rm libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp
git rm libs/markoff-foundation/tests/tst_foundation_parse_sequence.cpp
git rm libs/markoff-foundation/tests/tst_foundation_block_anchor.cpp
git rm libs/markoff-foundation/tests/tst_foundation_block_anchor_compute.cpp
git rm libs/markoff-foundation/tests/tst_foundation_block_anchor_perf.cpp
git rm libs/markoff-foundation/tests/tst_foundation_block_anchor_queries.cpp
git rm libs/markoff-foundation/tests/tst_foundation_block_anchor_stability.cpp
```

- [ ] **Step 2: Update tests CMake**

Edit `libs/markoff-foundation/tests/CMakeLists.txt`. Remove every block registering a deleted test target.

- [ ] **Step 3: Build and run remaining tests**

```bash
cmake --build build-dev -j 8
ctest -j 8 --output-on-failure
```
Expected: PASS — only D2-native foundation tests + survivor leaf tests remain.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-foundation/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(foundation/d4): delete tests for deprecated parse pipeline

Deleted:
  - tst_foundation_parse_pool, parse_phases, parse_input_edit_seq,
    parse_sequence
  - tst_foundation_block_anchor, _compute, _perf, _queries, _stability

These probe parseUpdated/applyLocalEdit reparse-survival, which has no
analogue in the D2 per-block CRDT model. BlockAnchor itself stays as a
public anchor primitive.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 8 — Excise parsePool from MarkoffDocument

**Goal:** Quieten the deprecated pipeline. Remove the `parsePool` member, the `parseReady → parseUpdated` relay, and every `parsePool.schedule(...)` / `parsePool.scheduleReset(...)` call. The signal and accessor still exist (deletion happens in Phase 9), but they no longer fire.

### Task 8.1: Remove parsePool member, ctor relay, and schedule calls

**Files:**
- Modify: `libs/markoff-foundation/src/MarkoffDocumentPrivate.h`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

- [ ] **Step 1: Drop the parsePool member from `MarkoffDocumentPrivate.h`**

Delete the `#include "ParsePool.h"` line at the top, the comment block describing it, and the `Markoff::Parse::Detail::ParsePool parsePool;` member.

- [ ] **Step 2: Drop the ctor relay in `MarkoffDocument.cpp`**

Find the `QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady, ...)` block (around lines 100-125). Delete the whole block including the suppression comment.

- [ ] **Step 3: Drop the schedule calls**

Find every `d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);` line in `applyLocalEdit`, `undo`, `redo`, `applyRemoteOps`. Delete each line.

Find `d->parsePool.scheduleReset(toMarkdownUtf8(), d->editSequence);` in `resetContent`. Delete it.

Find `d->parsePool.isPending()` in `parsePending()` (or wherever — search). Replace its return with `false` for now (the accessor itself goes in Phase 9).

Find `d->parsePool.setRenderPhaseTaps(taps);` in `setRenderPhaseTaps`. Delete the body so the function is a no-op (or delete the whole method — it's also going in Phase 9, but other code may call it; mark it `Q_UNUSED(taps)` and leave the body empty).

- [ ] **Step 4: Build**

```bash
cmake --build build-dev -j 8 --target markoff-foundation
```
Expected: success.

- [ ] **Step 5: Run tests**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS. The `parseUpdated` signal never fires now, but no surviving test waits on it (those waits were excised in Phases 3 and 7).

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-foundation/src/MarkoffDocumentPrivate.h \
        libs/markoff-foundation/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation(d4): excise parsePool from MarkoffDocument runtime

ParsePool is no longer constructed inside the document; applyLocalEdit
/ undo / redo / applyRemoteOps / resetContent no longer schedule
document-wide reparses. parseReady → parseUpdated relay removed.

parseUpdated, parseSequence(), and isPending() still compile (the
accessors are removed in Phase 9 as the public-surface deletion); they
return zero/false and never fire.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 9 — Delete `parseUpdated` / `parseSequence` / `MarkoffEdit` / `applyLocalEdit`

### Task 9.1: Delete the public-surface symbols

**Files:**
- Modify: `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-foundation/src/MarkoffDocumentPrivate.h`
- Delete: `libs/markoff-foundation/include/markoff-foundation/MarkoffEdit.h`
- Delete: `libs/markoff-foundation/src/MarkoffEdit.cpp`

- [ ] **Step 1: From `MarkoffDocument.h`, delete:**

- The `parseSequence()` accessor declaration (around line 91)
- The `applyLocalEdit(const QList<MarkoffEdit> &edits)` method declaration (around line 100)
- The `parseUpdated` signal declaration (in the signals: section, around line 328)
- The `setRenderPhaseTaps` declaration (around line 174) and its `Markoff::Render::RenderPhaseTaps` forward declaration / include
- Any `#include <markoff-foundation/MarkoffEdit.h>` line (top of file)
- Any `Q_DECLARE_METATYPE(...)` lines for `MarkoffEdit` and `QList<MarkoffEdit>` (bottom of file or in the metatype header)

- [ ] **Step 2: From `MarkoffDocument.cpp`, delete:**

- The `parseSequence()` definition
- The `applyLocalEdit(...)` definition (now an empty husk after Phase 8; the entire method goes)
- The `setRenderPhaseTaps` definition
- The `qRegisterMetaType<QList<Markoff::BlockAnchor>>()` line stays (BlockAnchor survives) but `qRegisterMetaType<QList<Markoff::MarkoffEdit>>()` if present, goes

- [ ] **Step 3: From `MarkoffDocumentPrivate.h`, delete:**

- The `quint64 parseSequence = 0;` member
- The `QList<Markoff::BlockAnchor> latestBlockAnchors;` member (stays only if D2 anchor APIs use it; verify by grepping `latestBlockAnchors` — if no surviving consumer, delete)

- [ ] **Step 4: Delete `MarkoffEdit.h` and `MarkoffEdit.cpp`**

```bash
git rm libs/markoff-foundation/include/markoff-foundation/MarkoffEdit.h
git rm libs/markoff-foundation/src/MarkoffEdit.cpp
```

- [ ] **Step 5: Update CMake**

In `libs/markoff-foundation/CMakeLists.txt`, remove the `src/MarkoffEdit.cpp` source line.

- [ ] **Step 6: Build**

```bash
cmake --build build-dev -j 8
```
Expected: success — at this point no surviving consumer references `MarkoffEdit`, `applyLocalEdit`, `parseUpdated`, or `parseSequence`. If a compile error appears, find the consumer and migrate it (it should have been caught in earlier phases; this is the safety net).

- [ ] **Step 7: Run tests**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-foundation/src/MarkoffDocument.cpp \
        libs/markoff-foundation/src/MarkoffDocumentPrivate.h \
        libs/markoff-foundation/CMakeLists.txt
git commit -m "$(cat <<'EOF'
foundation(d4): delete parseUpdated / parseSequence / MarkoffEdit / applyLocalEdit

Public-surface deletion of the deprecated pipeline. Every consumer was
migrated to D2 primitives (Cmd::*, applyBlockEdit, applyStructural,
applyFlatEdit) or retired (markoff-bench, view-qml live mode, dead
legacy Cmd::*).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 10 — Delete foundation-internal deprecated infrastructure files

### Task 10.1: Delete ParsePool / IncrementalParseSession / ParsePhases

**Files:**
- Delete: `libs/markoff-foundation/src/ParsePool.h`, `ParsePool.cpp`
- Delete: `libs/markoff-foundation/src/ParsePoolWorker.h`, `ParsePoolWorker.cpp`
- Delete: `libs/markoff-foundation/src/IncrementalParseSession.h`, `IncrementalParseSession.cpp`
- Delete: `libs/markoff-foundation/src/ParsePhases.h`
- Delete: `libs/markoff-foundation/include/markoff-foundation/Render/RenderPhaseTaps.h` (if no surviving consumer; verify)

- [ ] **Step 1: Delete files**

```bash
git rm libs/markoff-foundation/src/ParsePool.h
git rm libs/markoff-foundation/src/ParsePool.cpp
git rm libs/markoff-foundation/src/ParsePoolWorker.h
git rm libs/markoff-foundation/src/ParsePoolWorker.cpp
git rm libs/markoff-foundation/src/IncrementalParseSession.h
git rm libs/markoff-foundation/src/IncrementalParseSession.cpp
git rm libs/markoff-foundation/src/ParsePhases.h
```

For `RenderPhaseTaps.h`, first verify no consumer:
```bash
grep -r "RenderPhaseTaps\|Markoff::Render::" libs/ apps/ --include="*.cpp" --include="*.h"
```
If empty, delete:
```bash
git rm libs/markoff-foundation/include/markoff-foundation/Render/RenderPhaseTaps.h
[ -d libs/markoff-foundation/include/markoff-foundation/Render ] && \
    rmdir libs/markoff-foundation/include/markoff-foundation/Render 2>/dev/null
```

- [ ] **Step 2: Update CMake**

In `libs/markoff-foundation/CMakeLists.txt`, remove the source-list lines for:
- `src/ParsePool.cpp`
- `src/ParsePoolWorker.cpp`
- `src/IncrementalParseSession.cpp`

(`ParsePhases.h` and `ParsePool.h` are headers; they may not appear in the source list. Headers that ARE listed go too.)

- [ ] **Step 3: Build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```
Expected: success.

- [ ] **Step 4: Run tests**

```bash
ctest -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-foundation/CMakeLists.txt
git commit -m "$(cat <<'EOF'
foundation(d4): delete ParsePool / ParsePoolWorker / IncrementalParseSession / ParsePhases

Foundation-internal infrastructure files that backed the deprecated
parse pipeline. RenderPhaseTaps deleted iff no surviving consumer.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 11 — Parser-library deletions

### Task 11.1: Trim `TreeSitterParser` public surface

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`

- [ ] **Step 1: From `TreeSitterParser.h`, delete:**

- The `ByteEdit` struct
- The `parseIncremental(const QList<ByteEdit> &, const QByteArray &)` method declaration
- The `buildDocumentQueries(const DocumentQueryResult &prior, const QList<ByteEdit> &edits)` overload declaration
- The `findBlockBoundaries()` declaration AND the nested `BlockBoundary` struct
- The `buildSpanMap()` public method declaration (move it to the `private:` section so it's still callable internally)
- The observability counters: `inlineTreeReuseCount()`, `blockChangedByteCount()`, `lastParseBlockNs()`, `lastParseInlineNs()` declarations
- The nested `ByteRange` struct
- The private members backing the deleted features: `m_inlineTrees`, `m_inlineRanges`, `m_lastChangedRanges`, `m_lastInlineReuseCount`, `m_lastBlockChangedBytes`, `m_lastParseBlockNs`, `m_lastParseInlineNs`

- [ ] **Step 2: From `TreeSitterParser.cpp`, delete the corresponding implementations**

- `parseIncremental(...)` body (likely large — multi-hundred lines)
- `buildDocumentQueries(prior, edits)` body
- `findBlockBoundaries()` body
- Any internal helpers that are only called by the deleted methods (search for static functions / file-local helpers and verify by grep)
- The reset of observability counters inside `parse()` and `parseIncremental()` if they're still in `parse()`'s body (they should drop with the deletion — clean up)

The `parse()` (full parse), `buildSpanMap()` (now private), `buildDocumentQueries()` (no-arg), constructor / destructor, `hasTree()`, `utf8Source()`, `utf8ToCharOffset()`, and the free `inlineSpansFor(QByteArray)` survive.

- [ ] **Step 3: From `Document.h` and `Document.cpp`, delete `Document::fromComponents(...)`**

- Header: delete the static method declaration and the `ExtractedSource` definition's surplus fields (`frontmatterBlockStart`, `frontmatterBlockEnd`, `frontmatterEofClose`) **if** no surviving consumer needs them. Verify by:

```bash
grep -rn "frontmatterBlockStart\|frontmatterBlockEnd\|frontmatterEofClose" libs/ apps/ --include="*.cpp" --include="*.h"
```

If matches are only inside the parser library (i.e. they're set but never read externally), delete them.

- Source: delete the implementation of `fromComponents`.

- [ ] **Step 4: Build the parser library**

```bash
cmake --build build-dev -j 8 --target markoff-parser
```
Expected: success. If an internal helper is now unused (file-local function declared but never called), the compiler may warn — delete it.

- [ ] **Step 5: Run parser tests**

```bash
ctest -j 8 -R "tst_document\|tst_frontmatter\|tst_linktext\|tst_parser_\|tst_splitter\|tst_table" --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-parser/include/markoff-parser/TreeSitterParser.h \
        libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/include/markoff-parser/Document.h \
        libs/markoff-parser/src/Document.cpp
git commit -m "$(cat <<'EOF'
parser(d4): trim public surface to fromMarkdown + inlineSpansFor

Deleted:
  TreeSitterParser::parseIncremental(...)
  TreeSitterParser::buildDocumentQueries(prior, edits) overload
  TreeSitterParser::findBlockBoundaries() + BlockBoundary struct
  TreeSitterParser::buildSpanMap() public method (moved to private)
  Observability counters (inlineTreeReuseCount, blockChangedByteCount,
    lastParseBlockNs, lastParseInlineNs) and their backing members
  ByteEdit struct, ByteRange struct
  Document::fromComponents and dead ExtractedSource fields

Survivors: TreeSitterParser::parse(QString), buildDocumentQueries() (no-arg),
inlineSpansFor(QByteArray) free function, Document::fromMarkdown,
Document::extract, all Document accessors.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

### Task 11.2: Delete `tst_incremental_parse`

**Files:**
- Delete: `libs/markoff-parser/tests/tst_incremental_parse.cpp`
- Modify: `libs/markoff-parser/tests/CMakeLists.txt`

- [ ] **Step 1: Delete and update CMake**

```bash
git rm libs/markoff-parser/tests/tst_incremental_parse.cpp
```

Edit `libs/markoff-parser/tests/CMakeLists.txt` to remove the `tst_incremental_parse` registration block.

- [ ] **Step 2: Build and run**

```bash
cmake --build build-dev -j 8
ctest -j 8 --output-on-failure
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-parser/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(parser/d4): delete tst_incremental_parse

The incremental-parse path it tested no longer exists.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 12 — Top-level CMake cleanup

### Task 12.1: Verify no stragglers + clean root

**Files:**
- Modify: `CMakeLists.txt` (root, only if straggler references exist)

- [ ] **Step 1: Confirm zero references to deleted libraries / apps in CMake**

```bash
grep -n "markoff-bench\|apps/bench" CMakeLists.txt libs/*/CMakeLists.txt apps/*/CMakeLists.txt 2>/dev/null
```
Expected: empty output. If any line surfaces, delete it.

- [ ] **Step 2: Confirm zero references to deleted symbols across the tree**

```bash
grep -rn "parseIncremental\|IncrementalParseSession\|ParsePool\|parseUpdated\|parseSequence\|MarkoffEdit\|applyLocalEdit\|fromComponents\|findBlockBoundaries" libs/ apps/ --include="*.cpp" --include="*.h" --include="*.qml" 2>/dev/null
```

Expected: empty output. Allowed exceptions: tree-sitter vendored sources under `libs/markoff-parser/src/vendor/` (may contain unrelated `parseIncremental` from the C grammar runtime — those don't count) and any historical or archive `.md` documentation. **Code-side hits must be zero.**

- [ ] **Step 3: Clean reconfigure to confirm**

```bash
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
ctest -j 8 --output-on-failure
```
Expected: full build green, full ctest green.

- [ ] **Step 4: Commit only if any straggler was fixed**

If steps 1-3 ran clean, no commit needed. If a straggler was fixed, commit the fix:

```bash
git add <fixed files>
git commit -m "$(cat <<'EOF'
build(d4): cleanup straggler reference to retired library/symbol

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 13 — Doc updates (post-execution)

### Task 13.1: Status board → complete

**Files:**
- Modify: `docs/d-arc/d-arc-status.md`
- Modify: `docs/d-arc/2026-05-04-d-arc-roadmap.md`
- Modify: `CLAUDE.md` (worktree root)
- Modify: `libs/markoff-foundation/CLAUDE.md`

- [ ] **Step 1: Status board**

Edit `docs/d-arc/d-arc-status.md`:

- Update header `Last updated:` to today's date with a short summary ("D4 complete; all phases shipped").
- Update `Active phase:` to **D5** (collab activation) — substantive design pending.
- Update the TL;DR to point at the D5 stub for the next step.
- Update the Phase board row for D4 from `plan-approved` to `complete`. Note the final commit short SHA in the Notes column.
- Append entries to the recent-changes log: one entry per phase landing commit (or one bundled entry per logical group), ending with the D4-complete entry. Cite each commit's short SHA.

- [ ] **Step 2: Roadmap**

Edit `docs/d-arc/2026-05-04-d-arc-roadmap.md`:

- Update the D4 row in the Phase summary table from `🟢 plan-approved` to `✅ done`.
- Update the "Pick up D4" / "Pick up D5" lines in §2 (Where to start by purpose).
- Update the date at the top.

- [ ] **Step 3: Worktree CLAUDE.md banner**

Edit the root-of-worktree `CLAUDE.md` banner block. Replace the "Active phase: D4" message with "D4 complete; D5 (collab activation) pending substantive design." Adjust the read-first list. Do NOT delete the historical D-arc orientation links — keep them for fresh agents.

- [ ] **Step 4: Foundation CLAUDE.md**

Edit `libs/markoff-foundation/CLAUDE.md`:

- Remove the "Status (2026-05-06)" block's references to corrective work that has shipped; restate the current state (D2/D3/D4 complete).
- Remove the `parseUpdated` signal section; remove the `parseSequence()` accessor mention; remove `applyLocalEdit` mention; remove `latestBlockAnchors`/`ParsePool` mentions.
- Add a short section describing `applyFlatEdit` — one paragraph naming its role (buffer-global edit decomposition for flat-text views) and its position relative to `applyBlockEdit` / `applyStructural` / `Cmd::*` D2 helpers.
- Remove deprecated public API warnings that no longer apply.

- [ ] **Step 5: TODO.md**

```bash
cat docs/TODO.md
```

Remove any line that names a D4-deletion target as a TODO. Add any newly-noticed TODO that emerged during execution (e.g. "D2-native search-and-replace controller for future search/replace UI").

- [ ] **Step 6: Commit**

```bash
git add docs/d-arc/d-arc-status.md \
        docs/d-arc/2026-05-04-d-arc-roadmap.md \
        CLAUDE.md \
        libs/markoff-foundation/CLAUDE.md \
        docs/TODO.md
git commit -m "$(cat <<'EOF'
docs(d4): mark D4 complete; advance orienting docs to D5

Status board, roadmap, worktree CLAUDE.md banner, and foundation
CLAUDE.md updated to reflect D4 completion. D5 (collab activation) is
the next substantive design step.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

# Phase 14 — Acceptance verification + dogfood

### Task 14.1: Hard acceptance criteria

- [ ] **Step 1: Clean build from scratch**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```
Expected: zero warnings related to D4 deletions, zero errors.

- [ ] **Step 2: Full ctest**

```bash
cd build-dev
ctest -j 8 --output-on-failure
```
Expected: all tests PASS. Record the count.

- [ ] **Step 3: Final grep — no straggling references to retired symbols anywhere in code**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
grep -rn "parseIncremental\|IncrementalParseSession\|\bParsePool\b\|parseUpdated\|parseSequence\|\bMarkoffEdit\b\|applyLocalEdit\|fromComponents\|findBlockBoundaries\|InlineFormatHighlighter\|LiveProjectionLayer\|LiveStructuralKeyHandler\|LiveClipboardController" \
    libs/ apps/ --include="*.cpp" --include="*.h" --include="*.qml" 2>/dev/null
```

Expected: empty output (excluding tree-sitter vendored sources, which the grep doesn't reach because we didn't include `vendor/`).

If any hit appears in code, fix it before declaring D4 complete.

### Task 14.2: Source-widget dogfood

- [ ] **Step 1: Launch the source-widget app**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
./build-dev/bin/markoff-source-widget-app /tmp/d4-dogfood.md
```

(If the app's binary name differs, find it: `find build-dev/bin -name '*source-widget*' -o -name '*source*'`. Use that.)

- [ ] **Step 2: Manual exercise**

Type some content. Save (Ctrl+S). Edit, delete, replace. Use undo (Ctrl+Z) and redo (Ctrl+Shift+Z). Toggle find bar; do a find. Close the app.

Verify behaviour matches what a Markdown source editor should do: text persists across save, undo/redo work, no crashes, no text corruption.

- [ ] **Step 3: Open the saved file in another tool to confirm content is correct**

```bash
cat /tmp/d4-dogfood.md
```

### Task 14.3: Live-render dogfood

- [ ] **Step 1: Launch the live-render-hosting app**

If `markoff-live-render` ships its own demo app, launch it with a markdown file. Otherwise, the test app under `apps/` (whichever survives) is the harness:

```bash
find build-dev/bin -name '*live-render*' -o -name '*editor*'
./build-dev/bin/<actual-binary> /tmp/d4-dogfood.md
```

- [ ] **Step 2: Manual exercise**

Type, edit, delete, replace, undo, redo. Add a heading. Add a list. Add a fenced code block. Type at block boundaries (Enter to split, Backspace to merge). Save. Reload.

Verify the live-preview rendering matches your edits. No crashes.

### Task 14.4: User sign-off + status board → complete

- [ ] **Step 1: Show the user the dogfood report**

State explicitly:
- "Source-widget dogfood: edited / saved / undo / redo. No issues."
- "Live-render dogfood: edited / saved / undo / redo / structural ops. No issues."
- "Build green; ctest <N>/<N> passing; grep clean for retired symbols."

Wait for user sign-off.

- [ ] **Step 2: Final commit (if status board had a TL;DR for "in progress" that needs flipping)**

If Phase 13 already set D4 to `complete`, no further commit. If a final status update is needed, edit the status board and commit:

```bash
git add docs/d-arc/d-arc-status.md
git commit -m "$(cat <<'EOF'
docs(d4): D4 → complete after dogfood sign-off

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

D4 done.

---

# Appendix A — When things go wrong

**A test fails after a deletion.** It probably probed a deleted symbol. If the test is in the "delete" list, complete the delete; if not, the test exposes a real regression — diagnose. Bisect with `git bisect` if the broken phase isn't obvious.

**A clangd diagnostic complains about a missing include.** `compile_commands.json` may be stale. Re-run the configure step: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. The `.clangd` file points at `build-dev`.

**`applyFlatEdit` decomposition produces a wrong block layout.** Add a focused test (Phase 1 Task 1.9 style) that names the failing input + expected output. Diagnose by adding `qDebug() << blockText(id)` inside the function or by stepping through `iterateBlocks()` manually. The convention is rigid: each block buffer except possibly the last carries a trailing `\n`; structural splits / merges must preserve that invariant.

**A view-qml test you weren't sure about is keeping you red.** Re-classify it: open the file, look at imports / `MarkoffEditor` instantiation. Live-mode imports are `LiveProjectionLayer`, `InlineFormatHighlighter`, `ProjectionItem`. Source-mode is everything else. Delete or rewrite per the classification.

**A clean build fails with "unknown target markoff-bench" or similar.** A CMake reference to the deleted lib survives. Re-run `grep -n "markoff-bench" CMakeLists.txt libs/*/CMakeLists.txt`.

# Appendix B — Read-only reference (no execution)

These are reference points the executor may need to reread:

- `libs/markoff-foundation/src/Cmd/D2.cpp:38-87` — pattern for opening a transaction and using `d2ApplyBufferEdit` + `d2InsertBlock` + `d2RemoveBlock`. The model for `applyFlatEdit`'s implementation.
- `libs/markoff-foundation/include/markoff-foundation/UndoLog.h:48-110` — `UndoLog::Transaction` lifetime + coalescing API.
- `libs/markoff-foundation/include/markoff-foundation/StructuralOp.h` — what `applyStructural` consumes.
- `libs/markoff-foundation/src/MarkoffDocument.cpp:175-220` — the legacy `applyLocalEdit` implementation; useful only as historical context.
- `libs/markoff-view-qml/src/EditorBackend.cpp:18-45` — the `parseUpdated → parseUpdatedAt` relay being deleted.
- `libs/markoff-source-widget/src/Editor.cpp` — the source-widget Editor that holds a `SourceTextDocumentBinding`. Don't change the Editor itself; only the binding.
