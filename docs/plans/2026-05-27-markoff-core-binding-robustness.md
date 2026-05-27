# markoff-core single-document binding robustness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `markoff-core`'s single-document edit path robust — boundary-correct typing, canonical block structure after every flat edit, and incremental (non-destructive) reverse sync — so `markoff-styled`/`markoff-source` are first-class peers of the per-block (live) model.

**Architecture:** Three coordinated changes across two files. (1) Promote `markoff-source`'s sep-view↔block resolution helpers to a shared core header and rewrite `SourceTextDocumentBinding::onQtContentsChange` to resolve edits in separator-view and dispatch single-block edits directly to `d2ApplyBufferEdit` (boundary-correct) while routing structural/spanning edits to `applyFlatEdit`. (2) Make `applyFlatEdit` canonicalize structure (split on any newline-run, never create empty blocks). (3) Replace the reverse path's full-document `setPlainText` with a common-prefix/suffix text-diff applied via `QTextCursor`. The per-block (live) path is untouched.

**Tech Stack:** C++20, Qt6 6.8+, CMake 3.19+, `markoff-core` D2 CRDT (`d2ApplyBufferEdit`/`d2InsertBlock`/`d2RemoveBlock`), CollabText.

**Spec:** `docs/specs/2026-05-27-markoff-core-binding-robustness-design.md`.

**Canonical invariant (the contract):** after any `applyFlatEdit` returns — (1) no block buffer contains an internal `\n`; (2) no unintended empty blocks (newline-runs collapse to one boundary); (3) blocks separated by exactly one `"\n\n"` in `flatView()`. Enforced ONLY on the `applyFlatEdit` ingress, never on direct `d2ApplyBufferEdit`/`d2InsertBlock` (protects live's intentional empty paragraph).

**Build/test commands:**
```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'tst_d2_|tst_source_widget_|tst_styled_|tst_live_render_paste_kind_roundtrip'
scripts/run-tests.sh --bin <one-binary>
```
Offscreen platform is the default; never pass `--direct`/`--nested` without explicit user permission. `-j 8` cap always.

**Branch posture:** project works on `master` directly (single line of development). Each task is one atomic commit on master.

---

## File structure (target)

```
libs/markoff-core/
├── src/
│   ├── Detail/FlatBlockResolve.h      # NEW: shared sep-view↔block helpers
│   │                                  #   (promoted from markoff-source)
│   ├── MarkoffDocument.cpp            # applyFlatEdit: split-on-newline-run,
│   │                                  #   no empty blocks (normalize-on-edit)
│   └── SourceTextDocumentBinding.cpp  # onQtContentsChange: sep-view resolve +
│   │                                  #   dispatch; onD2DocumentChanged: text-diff
│   └── (tests/d2/) tst_d2_normalize_on_edit.cpp   # NEW core invariant test
├── ...
libs/markoff-source/src/Editor.cpp     # use shared helpers (delete local copies)
libs/markoff-styled/tests/             # integration guards (boundary, remote)
```

---

## Task 1: Promote sep-view↔block resolution helpers to a shared core header

**Goal:** One implementation of `findBlockAtSepByte` / `sliceByBlocks` (+ `BlockHit` / `BlockSlice`), shared by the binding and `markoff-source`. Pure refactor; no behavior change.

**Files:**
- Create: `libs/markoff-core/src/Detail/FlatBlockResolve.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (or a new `.cpp` for the helper if non-inline) — see note
- Modify: `libs/markoff-source/src/Editor.cpp` (delete local copies, include the shared header)
- Modify: `libs/markoff-core/CMakeLists.txt` if a new `.cpp` is added

- [ ] **Step 1: Read the donor code**

Read `libs/markoff-source/src/Editor.cpp:214-300` — the anonymous-namespace `BlockHit`, `findBlockAtSepByte`, `BlockSlice`, `sliceByBlocks`. These move verbatim into the shared header under `Markoff::Detail`.

- [ ] **Step 2: Create the shared header**

`libs/markoff-core/src/Detail/FlatBlockResolve.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <QByteArray>
#include <QList>
#include <markoff/core/BlockId.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Detail {

struct BlockHit {
    Markoff::BlockId blockId;
    quint32          byteInBlock;
    int              blockIndex;
};

/// Resolve a separator-view byte offset to (blockId, byteInBlock). When
/// sepOff lands exactly at a block boundary, biasForward=true picks the next
/// block's start; biasForward=false picks the previous block's end.
std::optional<BlockHit> findBlockAtSepByte(const Markoff::MarkoffDocument *doc,
                                           quint32 sepOff,
                                           bool biasForward);

struct BlockSlice {
    Markoff::BlockId blockId;
    quint32          byteLo;   // inclusive
    quint32          byteHi;   // exclusive
};

/// Slice a sep-view byte range [sepLo, sepHi) into per-block sub-ranges.
/// Empty ranges (sepLo == sepHi) yield no slices.
QList<BlockSlice> sliceByBlocks(const Markoff::MarkoffDocument *doc,
                                quint32 sepLo, quint32 sepHi);

}  // namespace Markoff::Detail
```

- [ ] **Step 3: Create the implementation**

Add `libs/markoff-core/src/Detail/FlatBlockResolve.cpp` with the two functions, bodies copied verbatim from `markoff-source/src/Editor.cpp` (the `findBlockAtSepByte` body at `:229-249` and the `sliceByBlocks` body at `:262-…`), changing the namespace to `Markoff::Detail` and qualifying `Markoff::BlockId`. Register it in `libs/markoff-core/CMakeLists.txt`'s source list (find the `add_library(markoff_core …)` block and add `src/Detail/FlatBlockResolve.cpp`).

(If the project prefers header-only, mark them `inline` and skip the `.cpp` + CMake edit. Check how other `src/Detail/*.h` helpers in markoff-core are done and match that convention.)

- [ ] **Step 4: Switch markoff-source to the shared helpers**

In `libs/markoff-source/src/Editor.cpp`: delete the anonymous-namespace `BlockHit`, `findBlockAtSepByte`, `BlockSlice`, `sliceByBlocks` definitions (`:217-300` region). Add `#include "Detail/FlatBlockResolve.h"` via the core include path (`#include <markoff/core/...>` won't work since it's an `src/` header — use the same mechanism other cross-lib `src/` includes use, OR if that's not available, this is the signal to put the header under `include/markoff/core/Detail/` instead). Replace call sites `findBlockAtSepByte(...)` → `Markoff::Detail::findBlockAtSepByte(...)` and `BlockHit`/`BlockSlice` → `Markoff::Detail::BlockHit`/`BlockSlice` (`:311`, `:457`, and the `sliceByBlocks` call sites).

**Decision point:** `src/`-private headers aren't visible across libraries. Since both `markoff-core` (binding) and `markoff-source` need this, put the header at `libs/markoff-core/include/markoff/core/Detail/FlatBlockResolve.h` (public-ish but `Detail`-namespaced, matching the `Markoff::Detail` convention in `markoff-core/CLAUDE.md`). Adjust Steps 2-4 paths accordingly.

- [ ] **Step 5: Build + run markoff-source regression**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_source_widget_'
```
Expected: all `tst_source_widget_*` pass — the promotion changed no behavior, just the definition location.

- [ ] **Step 6: Commit**
```bash
git add libs/markoff-core/include/markoff/core/Detail/FlatBlockResolve.h \
        libs/markoff-core/src/Detail/FlatBlockResolve.cpp \
        libs/markoff-core/CMakeLists.txt libs/markoff-source/src/Editor.cpp
git commit -m "refactor(core): promote findBlockAtSepByte/sliceByBlocks to shared Detail header

Sep-view->block resolution moves from markoff-source's Editor.cpp into
Markoff::Detail (markoff-core), so the binding and source share one
implementation. No behavior change; source format-op tests green."
```

---

## Task 2: Normalize-on-edit in `applyFlatEdit` — split on any newline-run, never create empty blocks

**Goal:** `applyFlatEdit` leaves canonical block structure. Replace the two `"\n\n"`-only split loops with one helper that splits on runs of ≥1 newline (collapsing runs), and never creates empty blocks.

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (`applyFlatEdit`, `:1525-1570` and `:1589-1625`)
- Create: `libs/markoff-core/tests/d2/tst_d2_normalize_on_edit.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt` (or wherever `tst_d2_*` are registered)

- [ ] **Step 1: Write the failing invariant test**

`libs/markoff-core/tests/d2/tst_d2_normalize_on_edit.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

namespace {
QByteArray flat(Markoff::MarkoffDocument &d) { return d.flatView(); }
int blockCount(Markoff::MarkoffDocument &d) {
    return int(d.iterateBlocks().size());
}
// Assert no block buffer contains an internal newline.
bool noInternalNewlines(Markoff::MarkoffDocument &d) {
    for (auto id : d.iterateBlocks())
        if (d.blockText(id).contains('\n')) return false;
    return true;
}
}  // namespace

class TstD2NormalizeOnEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void single_newline_insert_splits_block() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("alphabeta"));   // one block
        QCOMPARE(blockCount(d), 1);
        // Insert a single '\n' between "alpha" and "beta" (no-sep byte 5).
        d.applyFlatEdit(5, 5, QByteArrayLiteral("\n"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));            // no "alpha\nbeta" block
        QCOMPARE(blockCount(d), 2);                // split into two
        QCOMPARE(flat(d), QByteArrayLiteral("alpha\n\nbeta"));
    }

    void newline_run_collapses_no_empty_blocks() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("alphabeta"));
        // Insert a 4-newline run; must collapse to ONE boundary, not empties.
        d.applyFlatEdit(5, 5, QByteArrayLiteral("\n\n\n\n"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 2);                // two blocks, no empty middle
        QCOMPARE(flat(d), QByteArrayLiteral("alpha\n\nbeta"));
    }

    void multiline_paste_into_block() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("xy"));
        // Paste "a\nb\nc" at offset 1 → blocks: "xa","b","cy"
        d.applyFlatEdit(1, 1, QByteArrayLiteral("a\nb\nc"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 3);
        QCOMPARE(flat(d), QByteArrayLiteral("xa\n\nb\n\ncy"));
    }

    void canonical_input_is_identity() {
        Markoff::MarkoffDocument d(1);
        const QByteArray src = QByteArrayLiteral("# H\n\n- one\n- two\n\npara");
        d.loadFromMarkdown(src);
        // Identity edit: insert "" — flatView must be stable + canonical.
        d.applyFlatEdit(0, 0, QByteArray(), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
    }
};

QTEST_APPLESS_MAIN(TstD2NormalizeOnEdit)
#include "tst_d2_normalize_on_edit.moc"
```

(If `QTEST_APPLESS_MAIN` lacks a needed event loop for D2, use `QTEST_GUILESS_MAIN`. Match the other `tst_d2_*` mains.)

- [ ] **Step 2: Register + run to confirm failure**

Add to the `tst_d2_*` CMake registration (mirror an existing `tst_d2_*` entry):
```cmake
add_executable(tst_d2_normalize_on_edit d2/tst_d2_normalize_on_edit.cpp)
add_test(NAME tst_d2_normalize_on_edit COMMAND tst_d2_normalize_on_edit)
target_link_libraries(tst_d2_normalize_on_edit PRIVATE Qt6::Test markoff_core)
```
```bash
cmake --build build-dev --target tst_d2_normalize_on_edit -j 8
scripts/run-tests.sh --bin tst_d2_normalize_on_edit
```
Expected: FAIL — `single_newline_insert_splits_block` leaves `"alpha\nbeta"` in one block (current code splits only on `"\n\n"`); `newline_run_collapses_no_empty_blocks` produces empty blocks.

- [ ] **Step 3: Add a split-on-newline-run helper to MarkoffDocument.cpp**

In `libs/markoff-core/src/MarkoffDocument.cpp`, in the anonymous namespace near `applyFlatEdit`, add:
```cpp
// Split `text` into block-content parts at runs of one-or-more newlines.
// Collapses consecutive newlines so "a\n\n\n\nb" yields {"a","b"} (one
// boundary, no empty parts). Never returns an empty part (so callers never
// create empty blocks). An all-newline input yields {} (caller appends
// nothing new); empty input yields {""} so an identity edit is a no-op replace.
QList<QByteArray> splitOnNewlineRuns(const QByteArray &text) {
    QList<QByteArray> parts;
    int i = 0;
    const int n = text.size();
    int segStart = 0;
    bool sawAny = false;
    while (i < n) {
        if (text[i] == '\n') {
            if (i > segStart) { parts.append(text.mid(segStart, i - segStart)); sawAny = true; }
            while (i < n && text[i] == '\n') ++i;   // collapse the run
            segStart = i;
        } else {
            ++i;
        }
    }
    if (n == 0) { parts.append(QByteArray()); return parts; }   // identity replace
    if (segStart < n) { parts.append(text.mid(segStart)); sawAny = true; }
    if (!sawAny) parts.append(text);    // no newline at all → single part
    return parts;
}
```

- [ ] **Step 4: Use the helper in the intra-block-with-newlines branch**

Replace the `"\n\n"` split loop + the subsequent insert loop at `MarkoffDocument.cpp:1525-1570`. Read the current branch first (it computes `tail`, replaces the start block with `parts.front()`, then inserts a `d2InsertBlock` per additional part with an `if (!seed.isEmpty())` guard that still creates an empty block).

New branch (replaces the body from the `parts` declaration through the `return;`):
```cpp
    if (startIdx == endIdx) {
        const QByteArray currentText = blockText(blocks[startIdx]);
        const uint32_t removeLen = endWithin - startWithin;
        const QByteArray tail = currentText.mid(static_cast<int>(endWithin));

        const QList<QByteArray> parts = splitOnNewlineRuns(newText);

        // First part replaces [startWithin, endWithin)+tail in the current block.
        const QByteArray firstReplacement = parts.isEmpty() ? QByteArray() : parts.front();
        d2ApplyBufferEdit(blocks[startIdx], startWithin,
                          removeLen + static_cast<uint32_t>(tail.size()),
                          firstReplacement, t);

        // Each additional part becomes a new block; the LAST one carries the tail.
        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            QByteArray seed = parts[i];
            if (i == parts.size() - 1) seed += tail;
            // splitOnNewlineRuns never yields empty parts; only the tail can be
            // empty, which is fine (the block still has content from `seed`'s
            // part). If seed is somehow empty, skip creating an empty block.
            if (seed.isEmpty()) continue;
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            after = newBlk;
        }
        // Edge: parts had only the first element but `tail` is non-empty and
        // newText ended with a newline run (so tail belongs in a NEW block).
        // splitOnNewlineRuns drops the trailing run, so if newText ended in a
        // newline and tail is non-empty, emit the tail as its own block.
        if (parts.size() <= 1 && !tail.isEmpty()
            && !newText.isEmpty() && newText.endsWith('\n')) {
            BlockId newBlk = d2InsertBlock(blocks[startIdx], BlockKind::Paragraph, t);
            d2ApplyBufferEdit(newBlk, 0, 0, tail, t);
        }
        return;
    }
```

**Note:** the trailing-newline edge (Enter at end of block → `newText == "\n"`, `parts` becomes `{}` after collapse, but the block must split so the tail moves to a new block) is the subtle case. The test `single_newline_insert_splits_block` (tail empty, mid-block) and a new test for end-of-block Enter both guard it — add an `enter_at_end_of_block_creates_empty_aware_split` slot if the edge code above needs tuning against the real branch. **Verify against the test; the tests are the contract.**

- [ ] **Step 5: Use the helper in the cross-block branch**

Replace the `"\n\n"` split loop + insert loop at `MarkoffDocument.cpp:1589-1625` (the cross-block re-stitch) with `splitOnNewlineRuns(newText)` and the same "skip empty seeds, last part carries `endTail`" pattern. Read the current cross-block branch first; preserve its trim-start / remove-intermediate / remove-end structure, only swapping the split logic + empty-block suppression.

- [ ] **Step 6: Run the invariant test + paste regression**
```bash
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_d2_normalize_on_edit
scripts/run-tests.sh --bin tst_live_render_paste_kind_roundtrip
scripts/run-tests.sh -R '^tst_d2_'
```
Expected: `tst_d2_normalize_on_edit` PASS (all slots); `tst_live_render_paste_kind_roundtrip` still PASS (next-block bias untouched); all other `tst_d2_*` PASS. If a `tst_d2_*` slot that asserted old `"\n\n"`-split behavior fails, it was testing the pre-normalization shape — rename/adjust it to the canonical shape (per the project's test-evolution convention), don't retrofit the production code.

- [ ] **Step 7: Commit**
```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_normalize_on_edit.cpp \
        libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "feat(core): normalize-on-edit in applyFlatEdit (split newline-runs, no empty blocks)

applyFlatEdit now splits inserted text on runs of >=1 newline (collapsing
runs) instead of only '\n\n', and never creates empty blocks. Establishes
the canonical-structure invariant on the flat-edit ingress. Per-block path
(d2ApplyBufferEdit) untouched; next-block paste bias untouched
(tst_live_render_paste_kind_roundtrip green)."
```

---

## Task 3: Forward-path rewrite in `onQtContentsChange` — sep-view resolve + dispatch + separator-delete merge

**Goal:** Boundary-correct typing and separator-delete merge. Resolve edits in sep-view; dispatch single-block structure-neutral edits to `d2ApplyBufferEdit` (explicit block, no no-sep ambiguity); route structural/spanning edits (incl. separator deletes) to `applyFlatEdit`.

**Files:**
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp` (`onQtContentsChange`, `:353-381`; remove/repurpose `sepViewToNoSepByte` clamp)
- Create: `libs/markoff-core/tests/... tst_binding_forward.cpp` (binding-level, no widget) OR extend an existing binding test
- Modify: test CMake

- [ ] **Step 1: Write the failing test (binding-level, no widget needed)**

Use a bare `QTextDocument` + a `SourceTextDocumentBinding` to exercise the forward path without a QTextEdit. `tst_binding_forward.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace {
QByteArray flat(Markoff::MarkoffDocument &d) { return d.flatView(); }
}

class TstBindingForward : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_at_block_boundary_lands_in_previous_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QTextDocument qdoc;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);
        // qdoc now mirrors flatView: "alpha\n\nbeta". End of "alpha" = qtPos 5.
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("alpha\n\nbeta"));

        // Simulate typing a space at qtPos 5 (end of block 0, before "\n\n").
        QTextCursor c(&qdoc);
        c.setPosition(5);
        c.insertText(QStringLiteral(" "));   // fires contentsChange → onQtContentsChange

        // The space must land at the END of block 0, NOT the start of block 1.
        QCOMPARE(flat(doc), QByteArrayLiteral("alpha \n\nbeta"));
        // And the two representations agree (no drift → no reverse setPlainText).
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("alpha \n\nbeta"));
    }

    void backspace_over_separator_merges_blocks() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QTextDocument qdoc;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);

        // Select the "\n\n" (qtPos 5..7) and delete it — backspace-at-start-of-beta.
        QTextCursor c(&qdoc);
        c.setPosition(5);
        c.setPosition(7, QTextCursor::KeepAnchor);
        c.removeSelectedText();   // fires contentsChange (charsRemoved=2)

        QCOMPARE(int(doc.iterateBlocks().size()), 1);     // merged
        QCOMPARE(flat(doc), QByteArrayLiteral("alphabeta"));
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("alphabeta"));
    }

    void typing_mid_block_unaffected() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QTextDocument qdoc;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);

        QTextCursor c(&qdoc);
        c.setPosition(2);  // inside "alpha"
        c.insertText(QStringLiteral("X"));
        QCOMPARE(flat(doc), QByteArrayLiteral("alXpha\n\nbeta"));
    }
};

QTEST_GUILESS_MAIN(TstBindingForward)
#include "tst_binding_forward.moc"
```

- [ ] **Step 2: Register + confirm failure**
```bash
cmake --build build-dev --target tst_binding_forward -j 8
scripts/run-tests.sh --bin tst_binding_forward
```
Expected: FAIL — `typing_at_block_boundary` lands the space in block 1 (`"alpha\n\n beta"`); `backspace_over_separator` leaves 2 blocks (clamp gap).

- [ ] **Step 3: Rewrite `onQtContentsChange`**

Read the current `onQtContentsChange` (`:353-381`) and `sepViewToNoSepByte` (`:318-337`) first. Replace `onQtContentsChange`'s body with sep-view resolution + dispatch:

```cpp
void SourceTextDocumentBinding::onQtContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    if (m_applyingRemoteEdit) return;
    if (!m_markoffDocument || !m_textDocument) return;
    Markoff::MarkoffDocument *doc = m_markoffDocument;

    // Sep-view coordinates: the QTextDocument mirrors flatView() exactly, so
    // its plain text IS the separator-bearing view.
    const QByteArray preBytesSep = doc->flatView();
    const QString    preTextSep  = QString::fromUtf8(preBytesSep);
    const quint32 sepStart = qtPosToByteOffset(preTextSep, qtPos);
    const quint32 sepEnd   = qtPosToByteOffset(preTextSep, qtPos + charsRemoved);

    const QString postPlain    = m_textDocument->toPlainText();
    const QByteArray insertedUtf8 = postPlain.mid(qtPos, charsAdded).toUtf8();
    const bool insertedHasNewline = insertedUtf8.contains('\n');

    m_applyingLocalEdit = true;

    // Pure insertion (no removal), no embedded newline: resolve the block in
    // sep-view with previous-block bias (end-of-block typing lands in that
    // block, matching QTextEdit) and apply directly. This sidesteps the
    // no-separator boundary ambiguity entirely.
    if (charsRemoved == 0 && !insertedHasNewline) {
        const auto hit = Markoff::Detail::findBlockAtSepByte(
            doc, sepStart, /*biasForward=*/false);
        if (hit) {
            doc->d2ApplyBufferEdit(hit->blockId, hit->byteInBlock,
                                   /*removeBytes=*/0, insertedUtf8);
            m_applyingLocalEdit = false;
            return;
        }
        // hit == nullopt → empty document; fall through to applyFlatEdit which
        // auto-creates the first block.
    }

    // Everything else — removals (incl. separator-spanning deletes that must
    // merge), edits with embedded newlines (block splits), multi-block ranges
    // — goes through applyFlatEdit, which canonicalizes structure (Task 2).
    // applyFlatEdit consumes NO-SEPARATOR coordinates, so translate the sep-view
    // range. Translate by summing block sizes up to the resolved block + the
    // within-block offset (no clamping: a separator-spanning range yields a
    // cross-block no-sep range that applyFlatEdit merges).
    const quint32 noSepStart = sepViewToNoSepByteForEdit(doc, sepStart, /*biasForward=*/false);
    const quint32 noSepEnd   = sepViewToNoSepByteForEdit(doc, sepEnd,   /*biasForward=*/true);
    doc->applyFlatEdit(noSepStart, noSepEnd, insertedUtf8, Markoff::Origin::UserEdit);

    m_applyingLocalEdit = false;
}
```

- [ ] **Step 4: Replace `sepViewToNoSepByte` with a bias-aware translator**

The old `sepViewToNoSepByte` clamps separator-interior positions to a single no-sep byte (that's the merge-blocking bug). Replace it with `sepViewToNoSepByteForEdit(doc, sepOff, biasForward)` built on the shared `findBlockAtSepByte`:
```cpp
// Translate a sep-view byte offset to no-separator coordinates for
// applyFlatEdit. Uses findBlockAtSepByte so a boundary/in-separator position
// resolves to a real block edge (biasForward picks next-start vs prev-end),
// then accumulates block sizes. Unlike the old clamp, a range whose endpoints
// straddle a separator yields distinct no-sep offsets, so applyFlatEdit sees a
// genuine cross-block range and merges.
static quint32 sepViewToNoSepByteForEdit(const Markoff::MarkoffDocument *doc,
                                         quint32 sepOff, bool biasForward)
{
    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepOff, biasForward);
    const auto blocks = doc->iterateBlocks();
    if (!hit) {  // past end → total no-sep length
        quint32 total = 0;
        for (auto id : blocks) total += quint32(doc->blockText(id).size());
        return total;
    }
    quint32 noSep = 0;
    for (int i = 0; i < hit->blockIndex; ++i)
        noSep += quint32(doc->blockText(blocks[size_t(i)]).size());
    return noSep + hit->byteInBlock;
}
```
Delete the old `sepViewToNoSepByte` and its `:311-317` TODO comment (the gap it documented is now closed).

- [ ] **Step 5: Build + run**
```bash
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_forward
scripts/run-tests.sh -R '^tst_source_widget_|^tst_styled_|^tst_d2_'
```
Expected: `tst_binding_forward` all PASS; source + styled + d2 suites still green. Investigate any regression before proceeding (likely a no-sep translation edge — the binding-forward tests + the d2 invariant tests pin the behavior).

- [ ] **Step 6: Commit**
```bash
git add libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-core/tests/.../tst_binding_forward.cpp \
        libs/markoff-core/tests/.../CMakeLists.txt
git commit -m "fix(core): boundary-correct forward path + separator-delete merge

onQtContentsChange resolves edits in separator-view via findBlockAtSepByte
and dispatches single-block structure-neutral inserts straight to
d2ApplyBufferEdit with an explicit block (no no-sep boundary ambiguity);
structural/spanning edits route to applyFlatEdit. A bias-aware sep->no-sep
translator replaces the old clamp, so separator-spanning deletes now merge
blocks (closes the :311-317 backspace gap)."
```

---

## Task 4: Incremental reverse sync in `onD2DocumentChanged`

**Goal:** Remote edits update the QTextDocument via a minimal text-diff, preserving formatting/cursor/scroll instead of `setPlainText`.

**Files:**
- Modify: `libs/markoff-core/src/SourceTextDocumentBinding.cpp` (`onD2DocumentChanged`, `:383-401`)
- Create/extend: `libs/markoff-core/tests/.../tst_binding_reverse.cpp`

- [ ] **Step 1: Write the failing test**
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/SourceTextDocumentBinding.h>

class TstBindingReverse : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void remote_edit_preserves_formatting_outside_change() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QTextDocument qdoc;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);

        // Apply a distinctive char format to "beta" (simulating a styled view).
        {
            QTextCursor c(&qdoc);
            c.setPosition(7); c.setPosition(11, QTextCursor::KeepAnchor);
            QTextCharFormat f; f.setFontPointSize(22.0);
            c.mergeCharFormat(f);
        }
        // A remote edit changes ONLY "alpha" (prepend "X"). Use a second doc
        // mutation path that fires d2DocumentChanged WITHOUT the binding's
        // forward guard — applyFlatEdit directly on the doc simulates a remote
        // peer / programmatic edit.
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("X"), Markoff::Origin::UserEdit);

        // QTextDocument reflects the change.
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("Xalpha\n\nbeta"));
        // "beta"'s 22pt format survived (positions shifted by +1).
        QTextCursor probe(&qdoc);
        probe.setPosition(9); probe.setPosition(10, QTextCursor::KeepAnchor);
        QCOMPARE(probe.charFormat().fontPointSize(), 22.0);
    }

    void remote_edit_does_not_reset_cursor_to_end() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QTextDocument qdoc;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(&qdoc);
        b.setMarkoffDocument(&doc);

        // Remote edit far from a tracked cursor; verify the diff is targeted
        // (the change appears at the right place, not a full replace).
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("X"), Markoff::Origin::UserEdit);
        QCOMPARE(qdoc.toPlainText(), QStringLiteral("Xalpha\n\nbeta"));
        // Only one char differs at position 0 — assert blockCount unchanged
        // (no spurious structural churn).
        QCOMPARE(qdoc.blockCount(), 3);  // "Xalpha","","beta" as QTextBlocks
    }
};

QTEST_GUILESS_MAIN(TstBindingReverse)
#include "tst_binding_reverse.moc"
```

**Note on the test's remote-edit simulation:** calling `doc.applyFlatEdit(...)` directly fires `d2DocumentChanged`. The binding's `onD2DocumentChanged` runs with `m_applyingLocalEdit == false` (it wasn't a binding-forward edit), so the reverse path engages. If the binding's forward path happens to also be wired, guard the test by NOT routing through a QTextEdit (there's no widget here, so `onQtContentsChange` isn't triggered). Confirm the reverse path is what runs.

- [ ] **Step 2: Confirm failure**
```bash
cmake --build build-dev --target tst_binding_reverse -j 8
scripts/run-tests.sh --bin tst_binding_reverse
```
Expected: FAIL — current `setPlainText` wipes the 22pt format (`remote_edit_preserves_formatting` fails).

- [ ] **Step 3: Rewrite `onD2DocumentChanged` with common-prefix/suffix diff**
```cpp
void SourceTextDocumentBinding::onD2DocumentChanged()
{
    if (m_applyingLocalEdit) return;
    if (!m_textDocument || !m_subscribedDoc) return;

    const QString expected = QString::fromUtf8(m_subscribedDoc->flatView());
    const QString actual   = m_textDocument->toPlainText();
    if (actual == expected) return;   // in sync (common case for local edits)

    // Longest common prefix.
    int p = 0;
    const int minLen = std::min(actual.size(), expected.size());
    while (p < minLen && actual.at(p) == expected.at(p)) ++p;
    // Don't split a surrogate pair at the prefix boundary.
    if (p > 0 && p < actual.size() && actual.at(p - 1).isHighSurrogate()) --p;

    // Longest common suffix (not overlapping the prefix).
    int s = 0;
    const int maxS = minLen - p;
    while (s < maxS
           && actual.at(actual.size() - 1 - s) == expected.at(expected.size() - 1 - s))
        ++s;
    if (s > 0 && actual.at(actual.size() - s).isLowSurrogate()) --s;

    const int removeFrom = p;
    const int removeTo   = actual.size() - s;          // exclusive
    const QString middle = expected.mid(p, expected.size() - s - p);

    m_applyingRemoteEdit = true;
    QTextCursor c(m_textDocument);
    c.setPosition(removeFrom);
    c.setPosition(removeTo, QTextCursor::KeepAnchor);
    c.insertText(middle);
    m_applyingRemoteEdit = false;
}
```

Also update `syncQtDocumentFromMarkoff` (`:339-351`): it stays a full `setPlainText` ONLY for the initial population (when the QTextDocument is empty / first attach). That's correct — initial load has nothing to preserve. Leave it as-is.

- [ ] **Step 4: Build + run**
```bash
cmake --build build-dev -j 8
scripts/run-tests.sh --bin tst_binding_reverse
scripts/run-tests.sh -R '^tst_source_widget_|^tst_styled_'
```
Expected: `tst_binding_reverse` PASS; source + styled green. If a styled `tst_styled_d2_integration` slot assumed `setPlainText` semantics, rename/adjust it to the incremental shape (test-evolution convention).

- [ ] **Step 5: Commit**
```bash
git add libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-core/tests/.../tst_binding_reverse.cpp \
        libs/markoff-core/tests/.../CMakeLists.txt
git commit -m "fix(core): incremental reverse sync via common-prefix/suffix text-diff

onD2DocumentChanged replaces the full setPlainText with a minimal
contiguous diff applied through QTextCursor, preserving formatting,
cursor, and scroll outside the changed span. Composes with markoff-styled's
hash-gated restyle: remote edits no longer wipe formatting or jump the
caret. Initial-load full-sync (syncQtDocumentFromMarkoff) unchanged."
```

---

## Task 5: End-to-end integration guards (styled + source)

**Goal:** Prove the original dogfood symptoms are gone end-to-end, and source didn't regress.

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` (add boundary + remote slots)
- Modify: `libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp` (add boundary slot)

- [ ] **Step 1: Add styled end-to-end slots**

Append to `tst_styled_dogfood_invariants.cpp`:
```cpp
    void typing_at_boundary_does_not_wipe_or_leap() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# Heading\n\nbody one\n\nbody two"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Heading char is styled (>11pt).
        QTextDocument *qdoc = e.textEdit()->document();
        QTextCursor hc(qdoc); hc.setPosition(2); hc.setPosition(3, QTextCursor::KeepAnchor);
        QVERIFY(hc.charFormat().fontPointSize() > 11.0);

        // Type a space at the boundary between "# Heading" and "body one"
        // (qtPos = end of "# Heading" = 9).
        QTextCursor c(qdoc);
        c.setPosition(9);
        e.textEdit()->setTextCursor(c);
        QTest::keyClicks(e.textEdit(), QStringLiteral(" "));
        QTest::qWait(50);

        // Heading styling survived (no setPlainText wipe).
        QTextCursor hc2(qdoc); hc2.setPosition(2); hc2.setPosition(3, QTextCursor::KeepAnchor);
        QVERIFY(hc2.charFormat().fontPointSize() > 11.0);
        // Caret did not leap to end-of-document.
        QVERIFY(e.textEdit()->textCursor().position() < qdoc->characterCount() - 1);
    }
```

- [ ] **Step 2: Add source boundary slot**

Append to `tst_source_widget_binding_roundtrip.cpp` a slot that loads `"alpha\n\nbeta"`, positions the cursor at the boundary via `plainTextEdit()`, types a space, and asserts the doc's `flatView()` is `"alpha \n\nbeta"` (space in block 0). Mirror the existing slots' setup.

- [ ] **Step 3: Build + full relevant suite**
```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_|^tst_source_widget_|^tst_d2_|tst_live_render_paste_kind_roundtrip'
```
Expected: all green.

- [ ] **Step 4: Commit**
```bash
git add libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp \
        libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp
git commit -m "test(core): end-to-end boundary + remote-edit guards (styled + source)

Styled: typing at a block boundary no longer wipes formatting or leaps the
caret. Source: boundary typing lands in the correct block. The end-to-end
regression guards for the 2026-05-27 dogfood report."
```

---

## Task 6: Documentation — invariant, gap closure, Discipline Log

**Files:**
- Modify: `libs/markoff-core/CLAUDE.md`
- Modify: `libs/markoff-styled/CLAUDE.md`
- Modify: `docs/queue.md`

- [ ] **Step 1: Record the canonical invariant in `markoff-core/CLAUDE.md`**

Add a section documenting: the single-document binding's forward path resolves in sep-view and dispatches single-block edits to `d2ApplyBufferEdit`; `applyFlatEdit` guarantees the canonical-structure invariant (no internal `\n`, no unintended empty blocks, single `\n\n` separators); the reverse path is an incremental text-diff, not `setPlainText`; normalization is scoped to `applyFlatEdit` only (per-block path untouched). Note that `Markoff::Detail::findBlockAtSepByte`/`sliceByBlocks` (in `include/markoff/core/Detail/FlatBlockResolve.h`) are the shared sep-view↔block resolution helpers used by the binding and `markoff-source`.

- [ ] **Step 2: Update `markoff-styled/CLAUDE.md`**

In the "v0.1 invariants" / known-gaps area, note that the boundary-drift + setPlainText-wipe class of bug is resolved at the core binding level (reference this spec). Leave the `blockAt`/`blockByteRange` D2-broken caveat (still true).

- [ ] **Step 3: Discipline Log entry in `docs/queue.md`**

Append: the `:311-317` backspace-merge TODO in `SourceTextDocumentBinding.cpp` is CLOSED (separator-spanning deletes now merge via the bias-aware translator + applyFlatEdit cross-block branch); reference this spec + the `tst_binding_forward::backspace_over_separator_merges_blocks` guard.

- [ ] **Step 4: Commit**
```bash
git add libs/markoff-core/CLAUDE.md libs/markoff-styled/CLAUDE.md docs/queue.md
git commit -m "docs(core): canonical-structure invariant + closed backspace-merge gap

markoff-core CLAUDE.md documents the single-document binding's forward
dispatch + applyFlatEdit canonical invariant + incremental reverse sync.
Discipline Log records the :311-317 separator-delete-merge gap as closed."
```

---

## Self-review checklist (run after all tasks)

- [ ] `scripts/run-tests.sh -R '^tst_d2_|^tst_source_widget_|^tst_styled_|tst_live_render_paste_kind_roundtrip'` all green.
- [ ] `tst_live_render_paste_kind_roundtrip` green — next-block paste bias untouched.
- [ ] No internal `\n` in any block after any flat edit (the §9.1 invariant test).
- [ ] Dogfood by hand: `markoff-styled-app` on a real doc — type at boundaries, press Enter, backspace at start of a block, scroll; no wipe, no caret leap, blocks split/merge cleanly. (Ask the user; don't spawn windows unprompted.)
- [ ] No new failures in `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` baseline.
- [ ] Per-block (live) path untouched: `tst_live_render_*` baseline unchanged (modulo the 3 documented pre-existing failures).

If anything fails, fix in a follow-up commit on the same branch — do not amend.
