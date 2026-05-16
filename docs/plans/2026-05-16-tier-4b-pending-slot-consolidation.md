# Tier 4b — Pending-slot consolidation + auto-focus seam close-out — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close queue #2 concerns **#3** (overlapping `requestTextCaretAt*` APIs) and **#4** (two pending slots) and the 2026-05-16 discipline-log entry on the auto-focus gap by deleting the dead `m_pendingRow` mechanism, removing the two unused public APIs, and seeding initial focus through the chokepoint.

**Architecture:** The chokepoint (`establishFocus` + `m_pendingFocus`) is already the sole production pending mechanism. Tier 4b deletes the dead siblings (`m_pendingRow`, `PendingRow`, `requestTextCaretAtNewRow`, `requestTextCaretAtAnchor`, two resolvers, two slot handlers, two binding-side signals), migrates the four tests that exercise the dead path, and adds an explicit `requestTextCaretAtRow(0, 0)` in `LiveView.qml`'s `Component.onCompleted` so initial focus reaches the TextEdit descendant (not just the delegate root). One new invariant test slot + two falsifiability proofs.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest + `LiveRealisticInputHarness` + `QmlIntegrationFixture`. Build cap: `-j 8` always. Tests run via `scripts/run-tests.sh` (defaults to `QT_QPA_PLATFORM=offscreen`).

**Spec:** `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` is authoritative; cite section numbers when in doubt.

**Reading order before starting:**
1. `docs/INVARIANTS.md` (invariants 1, 2, 3, 4, 5, 8)
2. `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` (full)
3. `docs/specs/2026-05-11-focus-chokepoint-design.md` §5.1 (chokepoint mechanism)
4. `libs/markoff-live/include/markoff/live/LiveCursorState.h` (the surface being trimmed)
5. `libs/markoff-live/src/LiveCursorState.cpp` (the implementation being trimmed)
6. `libs/markoff-live/qml/LiveView.qml:97-101` (Component.onCompleted to extend)
7. `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` (slot template for the new initial-focus invariant)
8. `libs/markoff-live/tests/QmlIntegrationFixture.h` (fixture API)

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake --build build-dev -j 8

# Full fast suite (excludes the slow benchmark + realistic tests):
scripts/run-tests.sh -E 'realistic|benchmark'

# Single-target rebuild + test:
cmake --build build-dev --target tst_live_render_cursor -j 8
scripts/run-tests.sh --bin tst_live_render_cursor

cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant
```

**Commit-message prefix convention:** `markoff-live: <slot summary>` for code; `docs:` for spec/plan/queue updates (per recent commit history).

---

## Files touched

| File | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/LiveCursorState.h` | Delete declarations for `requestTextCaretAtNewRow`, `requestTextCaretAtAnchor`, `onStructuralRowsInserted`, `onStructuralRowRemoved`, `resolvePendingForRow`, `resolvePendingForAnchor`, `PendingRow` struct, `m_pendingRow` member. Update class-header docblock. |
| `libs/markoff-live/src/LiveCursorState.cpp` | Delete corresponding bodies. Delete the two `connect(binding, structuralRows...)` lines in the ctor. Replace `m_pendingRow.reset()` in `request()` with `m_pendingFocus.reset()`. |
| `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` | Delete `structuralRowsInserted` and `structuralRowRemoved` signal declarations. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Delete the two `Q_EMIT` lines in `onD2Changed`. |
| `libs/markoff-live/qml/LiveView.qml` | Extend `Component.onCompleted` with `binding.cursorState.requestTextCaretAtRow(0, 0)`. |
| `libs/markoff-live/tests/tst_live_render_cursor.cpp` | Migrate `requestTextCaretAtNewRow_landsAtQtPos0` to use `establishFocus`. Migrate two signal-spy tests (`structural_rows_inserted_emitted_on_new_block`, `structural_row_removed_emitted_on_block_removal`) to spy on `LiveBlockModel::rowsInserted` / `rowsRemoved`. |
| `libs/markoff-live/tests/tst_live_render_setext_e2e.cpp` | Comment touch-up (line 121). |
| `libs/markoff-live/tests/tst_live_render_structural_qml.cpp` | Comment touch-up (line 7). |
| `libs/markoff-live/tests/tst_live_render_cursor_qml.cpp` | Comment touch-up (lines 15-16). |
| `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` | **Add** `initial_focus_lands_on_textedit_not_delegate_root` slot. |
| `docs/queue.md` | Close discipline-log entry (2026-05-16, `UnifiedInlineTextDelegate.qml`). Update queue #2 banner — #3 + #4 closed. |
| `docs/e-arc/e-arc-status.md` | Recent-changes log entry. |

---

## Task 1: Pre-flight checks

**Files:** none (verification only).

- [ ] **Step 1:** Confirm worktree is on `exploration/new-foundation` and HEAD includes the tier-4b spec commit:

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
git branch --show-current
git log --oneline -5
```

Expected: branch `exploration/new-foundation`. Top commits include the spec at `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` (committed or staged).

- [ ] **Step 2:** Confirm working tree is clean except for known noise:

```bash
git status --short
```

Expected: untracked files only (`selection.txt`, `selection2.txt`, `Testing/...`, etc.). No tracked-and-modified files outside docs.

- [ ] **Step 3:** Confirm build is green:

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: no errors.

- [ ] **Step 4:** Record baseline test failures for regression-check at Task 22:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-baseline-failures.txt
wc -l /tmp/tier4b-baseline-failures.txt
cat /tmp/tier4b-baseline-failures.txt
```

Expected: a small number (the 2 named in `docs/e-arc/e-arc-status.md` recent-changes log under "Triage of baseline live-render failures": `shift_enter_creates_visible_newline` QEXPECT_FAIL-bracketed and `S1_setextDemote_lastUnderlineCharDeleted_keepsCursor`). Plus possibly a handful in `tst_live_render_*` that depend on real window-manager behaviour. Record the exact list.

- [ ] **Step 5:** Confirm the four targets of API deletion currently exist:

```bash
git grep -n 'requestTextCaretAtNewRow\|requestTextCaretAtAnchor' libs/markoff-live/
```

Expected: hits in `LiveCursorState.h`, `LiveCursorState.cpp`, and the four test files named in the spec §5.5 table.

- [ ] **Step 6:** Confirm zero production callsites for the doomed APIs:

```bash
git grep -n 'requestTextCaretAtNewRow\|requestTextCaretAtAnchor' libs/markoff-live/src/ libs/markoff-live/qml/ apps/
```

Expected: only the **definitions** in `LiveCursorState.cpp` (lines 129, 160). If any production caller appears, **stop** and revise the spec — the dead-code premise of tier 4b is wrong.

---

## Task 2: Migrate `requestTextCaretAtNewRow_landsAtQtPos0` test

Migrate the only direct caller of `requestTextCaretAtNewRow` (a test) to use `establishFocus`, which is the production equivalent. After this task, all of `requestTextCaretAtNewRow` / `requestTextCaretAtAnchor` callers are test-only and can be deleted.

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor.cpp:234-262`.

Per spec §5.5.

- [ ] **Step 1:** Read the existing test body. Current content (lines 234-262):

```cpp
void requestTextCaretAtNewRow_landsAtQtPos0() {
    // D2 version: use loadFromMarkdown + structureChanged to get model rows.
    // Then use Cmd::enterAtEnd to create a new block and verify the pending
    // cursor request resolves at the new row.
    Markoff::MarkoffDocument document(/*replicaId=*/1);

    LiveListModelBinding binding;
    binding.setDocument(&document);

    document.loadFromMarkdown("alpha");
    // loadFromMarkdown fires structureChanged synchronously → model rows are
    // already populated. Use QTRY_COMPARE as a safety net.
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    // Get the block anchor.
    const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

    // Schedule a pending request for "the row that's about to be born".
    binding.cursorState()->requestTextCaretAtNewRow(/*expectedRow=*/1, /*qtPos=*/0);

    // Create a new block after block0 using D2 API.
    Markoff::Cmd::enterAtEnd(document, block0);

    // The new row should arrive via structureChanged → onD2Changed → rowsInserted.
    // The pending cursor request resolves on rowsInserted.
    QTRY_COMPARE(binding.model()->rowCount(), 2);
    QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
    QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
}
```

- [ ] **Step 2:** Replace the body with the chokepoint equivalent. The new test stages the request via `establishFocus` *after* the structural edit creates the row — exercising the production path:

```cpp
void enterAtEnd_landsFocusOnNewRowViaChokepoint() {
    // Production path: structural-key handler calls Cmd::enterAtEnd, then
    // calls LiveCursorState::establishFocus on the newly-created BlockAnchor.
    // The chokepoint stages the pending and tryResolvePending picks the
    // correct delegate once it registers. Pre-tier-4b this lived under
    // requestTextCaretAtNewRow, which had its own pending slot (m_pendingRow)
    // resolved against binding-side structural signals; that path was deleted
    // in tier 4b. See docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md.
    Markoff::MarkoffDocument document(/*replicaId=*/1);

    LiveListModelBinding binding;
    binding.setDocument(&document);

    document.loadFromMarkdown("alpha");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

    // Create the new block first (structural edit completes).
    Markoff::Cmd::enterAtEnd(document, block0);
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    // Resolve the new row's BlockAnchor and stage focus through the chokepoint.
    const Markoff::BlockId block1 = binding.model()->recordAt(1).blockAnchor;
    binding.cursorState()->establishFocus(block1, /*qtPos=*/0);

    // No delegate is registered in this unit-test fixture (no QML view), so
    // the chokepoint holds the pending. The cursor state observable here is
    // the pending slot's contents — not the resolved cursor. To assert the
    // pending-side observable: hasPendingFocus() returns true.
    //
    // For the resolved-side assertion (focusedAnchorRow == 1), see
    // tst_live_render_focus_chokepoint_invariant — that file uses the QML
    // integration fixture which DOES register delegates. This unit-test slot
    // covers the chokepoint-staging step only.
    QVERIFY(binding.cursorState()->hasPendingFocus());
}
```

- [ ] **Step 3:** Rebuild + run the migrated slot:

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
scripts/run-tests.sh --bin tst_live_render_cursor -- -select enterAtEnd_landsFocusOnNewRowViaChokepoint
```

Expected: 1 pass.

- [ ] **Step 4:** Run the full `tst_live_render_cursor` binary to confirm no other slot regressed:

```bash
scripts/run-tests.sh --bin tst_live_render_cursor
```

Expected: same pass/fail counts as before, with the migrated slot now under its new name `enterAtEnd_landsFocusOnNewRowViaChokepoint`.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "markoff-live: migrate requestTextCaretAtNewRow test to chokepoint (tier-4b prep)"
```

---

## Task 3: Migrate signal-spy tests to model-side spies

`structural_rows_inserted_emitted_on_new_block` and `structural_row_removed_emitted_on_block_removal` spy on the soon-to-be-deleted binding-side signals. Migrate them to spy on `LiveBlockModel::rowsInserted` / `rowsRemoved` (standard Qt signals on the QAbstractItemModel) which observe the same underlying event.

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor.cpp:292-334`.

Per spec §5.5.

- [ ] **Step 1:** Locate lines 290-334 (`// ---- LiveListModelBinding: structural signals ----` and the two slots beneath).

- [ ] **Step 2:** Replace the section header comment + both slots with this:

```cpp
    // ---- LiveBlockModel: row-mutation signals ----

    void blockModel_emits_rowsInserted_on_new_block()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello");
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        QSignalSpy spy(binding.model(), &QAbstractItemModel::rowsInserted);

        // Insert a new block (Transaction commits on scope exit)
        auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2InsertBlock(ids.back(), Markoff::BlockKind::Paragraph, t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        // QAbstractItemModel::rowsInserted signature: (parent, first, last)
        QCOMPARE(spy[0][1].toInt(), 1);  // first
        QCOMPARE(spy[0][2].toInt(), 1);  // last
    }

    void blockModel_emits_rowsRemoved_on_block_removal()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n\nworld");
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        QSignalSpy spy(binding.model(), &QAbstractItemModel::rowsRemoved);

        auto ids = doc.iterateBlocks();
        QVERIFY(ids.size() >= 2);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2RemoveBlock(ids[1], t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QCOMPARE(spy[0][1].toInt(), 1);  // first
        QCOMPARE(spy[0][2].toInt(), 1);  // last
    }
```

The migrated slots observe the same underlying event (`applyOps` calls `beginInsertRows`/`endInsertRows` on the model, which fires Qt's standard `QAbstractItemModel::rowsInserted`). The argument layout differs (`rowsInserted(parent, first, last)` vs. binding's `structuralRowsInserted(first, last)`), so `spy[0][0]` → `spy[0][1]`.

- [ ] **Step 3:** Add the `<QAbstractItemModel>` include at the top of the file if not already present:

```bash
grep -n 'QAbstractItemModel\|LiveBlockModel\.h' libs/markoff-live/tests/tst_live_render_cursor.cpp | head -5
```

If `QAbstractItemModel` isn't already pulled in (it likely is via `LiveBlockModel.h`), add `#include <QAbstractItemModel>` to the includes.

- [ ] **Step 4:** Rebuild + run the migrated slots:

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
scripts/run-tests.sh --bin tst_live_render_cursor -- \
  -select blockModel_emits_rowsInserted_on_new_block \
  -select blockModel_emits_rowsRemoved_on_block_removal
```

Expected: 2 passes.

- [ ] **Step 5:** Run the full file:

```bash
scripts/run-tests.sh --bin tst_live_render_cursor
```

Expected: same pass/fail counts as Task 2's Step 4.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "markoff-live: migrate structural-signal spies to QAbstractItemModel::rowsInserted/Removed (tier-4b prep)"
```

---

## Task 4: Comment touch-ups in unrelated test files

Three test files mention `requestTextCaretAtNewRow` / `requestTextCaretAtAnchor` in comments only. Update each to cite `establishFocus` instead. Pure documentation.

**Files:** Modify
- `libs/markoff-live/tests/tst_live_render_setext_e2e.cpp:121`
- `libs/markoff-live/tests/tst_live_render_structural_qml.cpp:7`
- `libs/markoff-live/tests/tst_live_render_cursor_qml.cpp:15-16`

Per spec §5.5.

- [ ] **Step 1:** Read the current comment in `tst_live_render_setext_e2e.cpp` around line 121:

```bash
sed -n '115,130p' libs/markoff-live/tests/tst_live_render_setext_e2e.cpp
```

- [ ] **Step 2:** Replace the `requestTextCaretAtAnchor` mention with `establishFocus`. Adapt the surrounding sentence so it remains accurate (the citation is *about* the chokepoint mechanism, not about the obsolete API).

- [ ] **Step 3:** Read the current comment in `tst_live_render_structural_qml.cpp` around line 7:

```bash
sed -n '1,15p' libs/markoff-live/tests/tst_live_render_structural_qml.cpp
```

- [ ] **Step 4:** Replace `requestTextCaretAtNewRow → chokepoint` with `establishFocus (chokepoint)` — both describe the same path post-tier-1; the former name is being deleted.

- [ ] **Step 5:** Read the current comment in `tst_live_render_cursor_qml.cpp` around lines 15-16:

```bash
sed -n '10,20p' libs/markoff-live/tests/tst_live_render_cursor_qml.cpp
```

- [ ] **Step 6:** Replace the two `requestTextCaretAtNewRow` references with the migrated test name `enterAtEnd_landsFocusOnNewRowViaChokepoint` (from Task 2). If the comment block's whole purpose was to defer to that slot, this is a name swap.

- [ ] **Step 7:** Rebuild affected targets (no behaviour change; just confirm comments don't break compilation):

```bash
cmake --build build-dev --target tst_live_render_setext_e2e tst_live_render_structural_qml tst_live_render_cursor_qml -j 8
```

Expected: builds clean.

- [ ] **Step 8:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_setext_e2e.cpp \
        libs/markoff-live/tests/tst_live_render_structural_qml.cpp \
        libs/markoff-live/tests/tst_live_render_cursor_qml.cpp
git commit -m "markoff-live: update test-file comments to cite establishFocus (tier-4b prep)"
```

---

## Task 5: Falsifiability Proof A — m_pendingRow inert

Per spec §5.6 Proof A. Stub every `m_pendingRow` write behind `if (false)` and confirm the full production suite still passes — proving no production path depends on the slot.

**Files:** Modify `libs/markoff-live/src/LiveCursorState.cpp`.

- [ ] **Step 1:** Identify every `m_pendingRow = ...`, `m_pendingRow.reset()`, and `m_pendingRow->...` write/read in `LiveCursorState.cpp`. Expected sites (from `grep -n 'm_pendingRow' libs/markoff-live/src/LiveCursorState.cpp`):

```
79:    m_pendingRow.reset();              // inside request()
140:   m_pendingRow = PendingRow{ ... };  // requestTextCaretAtNewRow
175:   m_pendingRow = std::move(p);       // requestTextCaretAtAnchor
180-207: onStructuralRowsInserted/Removed body
210-236: resolvePendingForAnchor body
238-254: resolvePendingForRow body (reads m_pendingRow->qtPos, calls reset)
```

- [ ] **Step 2:** Stub `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor` to early-return without writing the slot:

```cpp
void LiveCursorState::requestTextCaretAtNewRow(int /*expectedRow*/, int /*qtPos*/)
{
    // FALSIFIABILITY PROOF, REVERTS NEXT — m_pendingRow inert
    return;
}

void LiveCursorState::requestTextCaretAtAnchor(Markoff::BlockAnchor /*expectedAnchor*/,
                                               int /*qtPos*/)
{
    // FALSIFIABILITY PROOF, REVERTS NEXT — m_pendingRow inert
    return;
}
```

Leave the other read/write sites untouched — they're dominated by the two functions above; if no write happens, no read fires meaningfully.

- [ ] **Step 3:** Commit the stub:

```bash
git add libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: stub — m_pendingRow inert (FALSIFIABILITY PROOF, REVERTS NEXT)"
```

- [ ] **Step 4:** Rebuild + run the full live-render fast suite:

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-proof-a-failures.txt
diff /tmp/tier4b-baseline-failures.txt /tmp/tier4b-proof-a-failures.txt
```

Expected: `diff` produces no output (Proof A files match baseline). If new failures appear:
- If any failure is **not** in `{requestTextCaretAtNewRow_landsAtQtPos0, structural_rows_inserted_emitted_on_new_block, structural_row_removed_emitted_on_block_removal}` (already migrated in Tasks 2-3 so they shouldn't fail), **stop** and investigate — there is a production callsite the spec missed.
- If only the migrated slots fail, something is wrong with Task 2/3's migration; check those before proceeding.

- [ ] **Step 5:** Revert the stub:

```bash
git revert HEAD --no-edit
```

Expected: revert commit lands cleanly.

- [ ] **Step 6:** Confirm the revert restored the original file:

```bash
git grep -n 'm_pendingRow' libs/markoff-live/src/LiveCursorState.cpp | wc -l
```

Expected: count matches Step 1's enumeration (no fewer, no more).

---

## Task 6: Delete `request()`'s `m_pendingRow.reset()` — replace with `m_pendingFocus.reset()`

The line at `LiveCursorState.cpp:79` enforces "explicit request supersedes pending delivery." When `m_pendingRow` goes away, that semantic still applies to `m_pendingFocus` — preserve it.

**Files:** Modify `libs/markoff-live/src/LiveCursorState.cpp:73-80`.

- [ ] **Step 1:** Locate the existing block at lines 74-79:

```cpp
    // Explicit request supersedes any pending structural delivery. Without
    // this, a later structural signal could resolve a stale pending and clobber
    // the cursor we are trying to set right now (e.g. Enter-on-hole-at-EOB
    // commits the old hole AND opens a new one in immediate succession; the
    // commit's pending must not overwrite the new-hole request).
    m_pendingRow.reset();
```

- [ ] **Step 2:** Replace with:

```cpp
    // Explicit request supersedes any pending chokepoint delivery. Without
    // this, a later delegate-registration could resolve a stale pending and
    // clobber the cursor being set right now (e.g. Enter-on-hole-at-EOB
    // commits the old hole AND opens a new one in immediate succession; the
    // commit's pending must not overwrite the new-hole request). Note: when
    // tryResolvePending calls request() it has ALREADY reset m_pendingFocus
    // at that point, so this is a no-op for the resolution path; the line
    // exists for direct request() callers (LiveStructuralKeyHandler's
    // BlockInternalEdit/BlockSelected entries).
    m_pendingFocus.reset();
```

- [ ] **Step 3:** Build to confirm compilation:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Run the full live-render fast suite — must still match baseline:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-task6-failures.txt
diff /tmp/tier4b-baseline-failures.txt /tmp/tier4b-task6-failures.txt
```

Expected: no diff. If a test now fails, the supersession-semantics swap from `m_pendingRow` to `m_pendingFocus` broke something — investigate before continuing.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: request() supersedes m_pendingFocus (preserves m_pendingRow.reset semantic)"
```

---

## Task 7: Delete `requestTextCaretAtNewRow`

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h:118-126` (the docblock + declaration)
- `libs/markoff-live/src/LiveCursorState.cpp:129-141` (the body)

- [ ] **Step 1:** In the header, locate the `requestTextCaretAtNewRow` block:

```cpp
    /// Pure-pending variant: never resolves immediately even when
    /// `expectedRow` already exists. Use when the structural edit will
    /// INSERT a new row at this index (mid-block split, hole commit). The
    /// pending request resolves on the next `rowsInserted` whose range
    /// covers `expectedRow`. Distinguishes "row will be born here" from
    /// "row already exists, just move the cursor" — the latter must use
    /// requestTextCaretAtRow above.
    Q_INVOKABLE void requestTextCaretAtNewRow(int expectedRow, int qtPos);
```

Delete the entire block (8 lines: 7 comment + 1 declaration).

- [ ] **Step 2:** In the implementation, locate the body at lines 129-141:

```cpp
void LiveCursorState::requestTextCaretAtNewRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtNewRow row=" << expectedRow
                      << "qtPos=" << qtPos
                      << "(model.rowCount=" << m_model->rowCount() << ")";
    // Pure-pending: do NOT resolve against the current row at this index —
    // that would land the cursor on whatever block currently sits there
    // (the block that's about to be SHIFTED by the upcoming insertion).
    // Wait for the next structural signal whose range covers expectedRow.
    m_pendingRow = PendingRow{ expectedRow, qtPos, std::nullopt };
}
```

Delete the entire function body.

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean. (The tests that referenced the old name were migrated in Tasks 2-3.)

- [ ] **Step 4:** Confirm zero remaining references:

```bash
git grep -n 'requestTextCaretAtNewRow' libs/markoff-live/ apps/ docs/
```

Expected: only matches inside `docs/` (queue.md, prior specs/plans, e-arc-status — those are paper trail and stay). No `.cpp`/`.h`/`.qml` hits.

- [ ] **Step 5:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: delete requestTextCaretAtNewRow (tier-4b, queue #2 concern #3)"
```

---

## Task 8: Delete `requestTextCaretAtAnchor`

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h:141-152` (docblock + declaration)
- `libs/markoff-live/src/LiveCursorState.cpp:160-176` (the body)

- [ ] **Step 1:** In the header, locate the `requestTextCaretAtAnchor` block:

```cpp
    /// Anchor-keyed pure-pending variant. Use when the structural edit
    /// shifts an existing block (whose `BlockAnchor` we already know)
    /// rather than creating a brand-new block. The pending request
    /// resolves on the next `rowsInserted` event by searching the model
    /// for `expectedAnchor`'s row, NOT by indexing a row position.
    /// This is robust to any number of intervening Insert/Delete/Equal
    /// ops in the parse-back diff: the user's content's row index can
    /// shift unpredictably (anchor renumbering, multi-row diffs), but
    /// its BlockAnchor identity is stable. Bug 3 fix (Task 18 dogfood
    /// pass 2): start-of-paragraph Enter in mid-document context must
    /// land the cursor on the user's content, not on the row after it.
    void requestTextCaretAtAnchor(Markoff::BlockAnchor expectedAnchor, int qtPos);
```

Delete the entire block (11 comment lines + 1 declaration + blank line if present).

- [ ] **Step 2:** Also delete the cross-reference in the `syncFromTextEdit` comment block. Locate around line 136-139:

```cpp
    /// `(anchor, qtPos)`. Deliberately does NOT reset `m_pendingRow` — a
    /// pending structural-key request must survive incidental TextEdit
    /// cursor moves until its structural signal arrives.
    Q_INVOKABLE void syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos);
```

Replace the three comment lines above the declaration with:

```cpp
    /// `(anchor, qtPos)`. Deliberately does NOT reset `m_pendingFocus` — a
    /// pending chokepoint request must survive incidental TextEdit cursor
    /// moves until its delegate-registration event arrives.
    Q_INVOKABLE void syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos);
```

The change: `m_pendingRow` → `m_pendingFocus`, `structural-key request` → `chokepoint request`, `structural signal` → `delegate-registration event`.

- [ ] **Step 3:** Also look at the `requestTextCaretAtRow` docblock (lines 107-116 of the header) and confirm it does **not** mention `requestTextCaretAtAnchor`. If it does, update the cross-reference. Specifically, the docblock currently says:

```cpp
    /// ...Legitimate use requires that the row's TEXT is
    /// already stable at the time of the call — do not use immediately after
    /// a d2ApplyBufferEdit that changes the row content (use
    /// requestTextCaretAtAnchor instead). Spec §5.3 step 6.
```

This appears in the class-header docblock (around line 38-41 of `LiveCursorState.h` per the earlier read). The "use requestTextCaretAtAnchor instead" pointer is stale. Replace with:

```cpp
    /// ...Legitimate use requires that the row's TEXT is
    /// already stable at the time of the call — do not use immediately
    /// after a d2ApplyBufferEdit that changes the row content (resolve
    /// the BlockAnchor explicitly and call `establishFocus` instead).
    /// Spec §5.3 step 6.
```

- [ ] **Step 4:** In the implementation, locate the body at lines 160-176:

```cpp
void LiveCursorState::requestTextCaretAtAnchor(Markoff::BlockAnchor expectedAnchor,
                                               int qtPos)
{
    if (!m_model) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtAnchor qtPos=" << qtPos
                      << "(model.rowCount=" << m_model->rowCount() << ")";
    // Anchor-keyed pure-pending. Do NOT resolve immediately — the model
    // currently still reflects the PRE-edit state (the anchor sits at its
    // OLD row, which is about to be displaced by the upcoming insertion).
    // Wait for a structural signal to fire during parse-back applyOps; at that
    // point the anchor's CURRENT row is the right cursor target.
    PendingRow p;
    p.row = -1;
    p.qtPos = qtPos;
    p.anchor = expectedAnchor;
    m_pendingRow = std::move(p);
}
```

Delete the entire function body.

- [ ] **Step 5:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 6:** Confirm zero remaining production references:

```bash
git grep -n 'requestTextCaretAtAnchor' libs/markoff-live/ apps/
```

Expected: zero hits (matches in `docs/` are paper trail and OK).

- [ ] **Step 7:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff.

- [ ] **Step 8:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: delete requestTextCaretAtAnchor (tier-4b, queue #2 concern #3)"
```

---

## Task 9: Delete `m_pendingRow`, `PendingRow`, the two resolvers, the two slot handlers

The two API functions that wrote `m_pendingRow` are gone (Tasks 7-8). Now delete the slot, struct, resolvers, and slot handlers.

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveCursorState.h` — declarations (`onStructuralRowsInserted`, `onStructuralRowRemoved`, `resolvePendingForRow`, `resolvePendingForAnchor`, `PendingRow` struct, `m_pendingRow` member)
- `libs/markoff-live/src/LiveCursorState.cpp` — bodies of the four functions

- [ ] **Step 1:** In the header, locate and delete:

```cpp
private:
    bool validateVariant(const Cursor &c) const;
    void onStructuralRowsInserted(int first, int last);
    void onStructuralRowRemoved(int row);
    void resolvePendingForRow(int row);
```

Delete the three `void on... / void resolve...` declarations (keeping `validateVariant`):

```cpp
private:
    bool validateVariant(const Cursor &c) const;
```

- [ ] **Step 2:** Further down in the header, locate the `PendingRow` struct block (around line 219-231):

```cpp
    struct PendingRow {
        int row;
        int qtPos;
        // If set, treat this pending request as anchor-keyed: ignore the
        // `row` field and resolve by searching the model for this
        // BlockAnchor on every structural signal event. Used by start-of-
        // paragraph Enter (marker insert before an existing block) where
        // the user's content's row index is not stable across the diff
        // but its BlockAnchor identity is.
        std::optional<Markoff::BlockAnchor> anchor;
    };
    std::optional<PendingRow> m_pendingRow;
    void resolvePendingForAnchor();
```

Delete the struct definition, the `m_pendingRow` member, and the `resolvePendingForAnchor()` declaration.

- [ ] **Step 3:** In the implementation, delete the four function bodies:
  - `onStructuralRowsInserted` (around lines 178-194)
  - `onStructuralRowRemoved` (around lines 196-208)
  - `resolvePendingForAnchor` (around lines 210-236)
  - `resolvePendingForRow` (around lines 238-268)

Use `grep -n` first to confirm exact line ranges before deletion:

```bash
grep -n '^void LiveCursorState::\(onStructuralRowsInserted\|onStructuralRowRemoved\|resolvePendingFor\)' libs/markoff-live/src/LiveCursorState.cpp
```

Delete from the `void LiveCursorState::onStructuralRowsInserted(...)` opening through the closing `}` of `resolvePendingForRow`. Confirm `requestTextCaretAtRowVisualX` (which starts at line 270 currently) is preserved.

- [ ] **Step 4:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean. If a compile error fires citing `m_pendingRow` or `PendingRow`, an unrelated read site was missed — `grep -n 'm_pendingRow\|PendingRow' libs/markoff-live/src/LiveCursorState.cpp` and clean it up. **At this point only the ctor `connect(...)` lines may still reference the deleted slot handlers** — those go in Task 10.

- [ ] **Step 5:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff (assuming Task 10 hasn't broken the ctor yet; if compile broke at Step 4, this step is gated on fixing that).

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: delete m_pendingRow + resolvers + slot handlers (tier-4b, queue #2 concern #4)"
```

---

## Task 10: Delete the ctor `connect(binding, structuralRows...)` lines

**Files:** Modify `libs/markoff-live/src/LiveCursorState.cpp:29-34`.

- [ ] **Step 1:** Locate the constructor block (lines 19-36):

```cpp
LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                 const LiveBlockModel    *model,
                                 LiveListModelBinding    *binding,
                                 QObject                 *parent)
    : QObject(parent)
    , m_cursor(NoCursor{})
    , m_registry(registry)
    , m_model(model)
    , m_binding(binding)
{
    if (binding) {
        connect(binding, &LiveListModelBinding::structuralRowsInserted,
                this, &LiveCursorState::onStructuralRowsInserted);
        connect(binding, &LiveListModelBinding::structuralRowRemoved,
                this, &LiveCursorState::onStructuralRowRemoved);
    }

}
```

- [ ] **Step 2:** Delete the `if (binding) { ... }` block. The result:

```cpp
LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                 const LiveBlockModel    *model,
                                 LiveListModelBinding    *binding,
                                 QObject                 *parent)
    : QObject(parent)
    , m_cursor(NoCursor{})
    , m_registry(registry)
    , m_model(model)
    , m_binding(binding)
{
}
```

(The `binding` parameter stays — it's stored in `m_binding` via the initializer list and used elsewhere.)

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff.

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/src/LiveCursorState.cpp
git commit -m "markoff-live: drop ctor connections to deleted structuralRows signals (tier-4b)"
```

---

## Task 11: Delete `structuralRowsInserted` / `structuralRowRemoved` signals from `LiveListModelBinding`

**Files:** Modify
- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h:146,150` (signal declarations)
- `libs/markoff-live/src/LiveListModelBinding.cpp:644-651` (the emit loop in `onD2Changed`)

- [ ] **Step 1:** In the header, locate the `Q_SIGNALS:` block containing the two signals. Use `grep -n 'structuralRows' libs/markoff-live/include/markoff/live/LiveListModelBinding.h` to find exact lines, then delete both declarations:

```cpp
    void structuralRowsInserted(int first, int last);
    // (the comment line above this declaration, if any)
    void structuralRowRemoved(int row);
    // (the comment line above this declaration, if any)
```

Both go; keep the surrounding `Q_SIGNALS:` other entries intact.

- [ ] **Step 2:** In the implementation, locate the emit loop at lines 644-651:

```cpp
    // Emit structural signals so LiveCursorState can resolve pending cursors
    // without going through the parse-cycle path.
    for (const auto &op : ops) {
        if (op.kind == AstBlockDiff::OpKind::Insert) {
            Q_EMIT structuralRowsInserted(op.nextIndex, op.nextIndex);
        } else if (op.kind == AstBlockDiff::OpKind::Delete) {
            Q_EMIT structuralRowRemoved(op.prevIndex);
        }
    }
```

Delete the entire block (comment + 7-line loop).

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean. If something fails to compile citing the deleted signals, `grep -rn 'structuralRowsInserted\|structuralRowRemoved' libs/markoff-live/` to find the holdout.

- [ ] **Step 4:** Confirm zero remaining production references:

```bash
git grep -n 'structuralRowsInserted\|structuralRowRemoved' libs/markoff-live/ apps/
```

Expected: zero hits in `.cpp`/`.h`/`.qml`. Test-file references should also be zero (Task 3 migrated them). Comment-only hits in tests would already have been touched in Task 4.

- [ ] **Step 5:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff.

- [ ] **Step 6:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "markoff-live: delete structuralRowsInserted/Removed signals (tier-4b)"
```

---

## Task 12: Update `LiveCursorState` class-header docblock

The class-header docblock still references the deleted `requestTextCaretAtRow` "deterministic-pending variant" framing. Correct it.

**Files:** Modify `libs/markoff-live/include/markoff/live/LiveCursorState.h:21-41` (the docblock).

- [ ] **Step 1:** Locate the current docblock at lines 21-41:

```cpp
/// Owns the canonical cursor value for **structural events** (kind transitions,
/// cross-block navigation, `BlockSelected`, `BlockInternalEdit`). For
/// **in-block caret position during typing**, `QQuickTextEdit::cursorPosition`
/// is canonical; `m_cursor` mirrors it via `syncFromTextEdit`, called from each
/// text-bearing delegate's `onCursorPositionChanged` and from
/// `LiveEditBinding::onContentsChange` after each buffer edit. The authority
/// split is documented in `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`
/// §3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
///
/// `requestTextCaretAtRow` is the deterministic-pending variant used by
/// the structural-key handler. When the row already exists in the model
/// it resolves immediately; when a structural edit has not yet propagated
/// through the CRDT→model pipeline the request is held until
/// `rowsInserted` fires. Legitimate use requires that the row's TEXT is
/// already stable at the time of the call — do not use immediately after
/// a d2ApplyBufferEdit that changes the row content (use
/// requestTextCaretAtAnchor instead). Spec §5.3 step 6.
```

- [ ] **Step 2:** Replace the third paragraph (the `requestTextCaretAtRow` block) with the post-tier-4b version:

```cpp
/// `requestTextCaretAtRow` is a row-keyed convenience wrapper over
/// `establishFocus`: it flushes any pending document changes (via
/// `LiveListModelBinding::flushPendingDocumentChanges`), resolves the
/// row to a `BlockAnchor` via `recordAt(row).blockAnchor`, and stages
/// the chokepoint pending. Out-of-range rows are rejected synchronously.
/// `establishFocus` is the canonical entry for callers that already
/// hold a `BlockAnchor` (e.g. `LiveStructuralKeyHandler` consuming
/// `Cmd::*` return values). Spec
/// `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md` §3.
```

- [ ] **Step 3:** Build:

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h
git commit -m "markoff-live: refresh LiveCursorState docblock post-tier-4b"
```

---

## Task 13: Add `initial_focus_lands_on_textedit_not_delegate_root` test (RED)

Per spec §5.6 Proof B. Write the test FIRST — it must fail without the seed (Task 14 adds it, Task 15 verifies the test then passes, Task 16 proves falsifiability).

**Files:** Modify `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` (add one slot).

- [ ] **Step 1:** Open the test file. Skim existing slots to identify the fixture pattern (likely `QmlIntegrationFixture`-based). Pick a slot near the end and use it as a template.

- [ ] **Step 2:** Add the new slot after the last existing one. Body:

```cpp
void initial_focus_lands_on_textedit_not_delegate_root()
{
    // Spec: tier-4b §4.4 auto-focus gap close-out. After the production
    // QML view finishes Component.onCompleted with a non-empty model, the
    // focused QQuickItem must be the TextEdit descendant of the first
    // text-bearing delegate, NOT the delegate root.
    //
    // Without the explicit `establishFocus` seed in LiveView.qml, this
    // test fails: ListView.focus = true delivers focus to the delegate
    // root (which has no Keys.onPressed) and the TextEdit child never
    // gains activeFocus. See discipline-log entry 2026-05-16 (closed by
    // this task).
    QmlIntegrationFixture fx;
    fx.loadMarkdown("alpha\n\nbeta");
    fx.waitForRows(2);

    // After the first paint cycle, find the focused QQuickItem.
    QQuickItem *focused = fx.window()->activeFocusItem();
    QVERIFY2(focused != nullptr, "no active focus item");

    // The TextEdit descendant of UnifiedInlineTextDelegate has
    // objectName "textEdit" (see UnifiedInlineTextDelegate.qml).
    const QString name = focused->objectName();
    QCOMPARE(name, QStringLiteral("textEdit"));

    // And it must belong to row 0.
    const QVariant rowVar = focused->property("modelIndex");
    if (!rowVar.isValid()) {
        // Walk up the parent chain to find the delegate root with modelIndex.
        QQuickItem *p = focused->parentItem();
        while (p && !p->property("modelIndex").isValid())
            p = p->parentItem();
        QVERIFY2(p != nullptr, "no parent delegate with modelIndex");
        QCOMPARE(p->property("modelIndex").toInt(), 0);
    } else {
        QCOMPARE(rowVar.toInt(), 0);
    }
}
```

The `QmlIntegrationFixture::window()` method and `loadMarkdown`/`waitForRows` helpers are inherited from the existing fixture; confirm by reading `libs/markoff-live/tests/QmlIntegrationFixture.h`.

- [ ] **Step 3:** Build the test target:

```bash
cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
```

Expected: builds clean. If `activeFocusItem()` is unavailable or `window()` isn't exposed, adapt to the fixture's actual API (read `QmlIntegrationFixture.h` for the correct accessor).

- [ ] **Step 4:** Run the new slot (expecting FAIL — the seed isn't there yet):

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant -- \
  -select initial_focus_lands_on_textedit_not_delegate_root
```

Expected outcome: **FAIL**. The failure mode is either (a) `focused == nullptr` (nothing has focus), (b) `name != "textEdit"` (delegate root has focus), or (c) the slot passes (meaning `ListView.focus = true` *did* deliver focus to the TextEdit in the offscreen test env — see open question below).

If (c) — the test passes without the seed:
- The offscreen test environment may behave differently than production. Investigate by adding `qDebug() << focused << name;` and inspecting. If the offscreen path resolves TextEdit focus correctly but the production discipline-log entry stands (two production tests had to migrate from auto-focus), then this test is the wrong shape for the invariant — the gap is environment-specific. **Stop and report.** Possible alternative: make the test more aggressive (programmatically destroy/recreate the delegate to simulate the kind-transition path that exposed the gap).
- If you confirm (a) or (b), proceed to Task 14.

- [ ] **Step 5:** Do NOT commit yet (the test is RED; the seed comes next).

---

## Task 14: Add `establishFocus` seed to `LiveView.qml`

**Files:** Modify `libs/markoff-live/qml/LiveView.qml:97-101` (extending `Component.onCompleted`).

Per spec §4.4 + §5.4.

- [ ] **Step 1:** Locate the current `Component.onCompleted` block:

```qml
    // ---- Wire navigationController.setListView on startup ----
    Component.onCompleted: {
        if (binding && binding.navigationController)
            binding.navigationController.setListView(root)
    }
```

- [ ] **Step 2:** Extend the body:

```qml
    // ---- Wire navigationController.setListView + seed initial focus on startup ----
    Component.onCompleted: {
        if (binding && binding.navigationController)
            binding.navigationController.setListView(root)
        // Seed initial focus through the chokepoint. Without this,
        // ListView.focus = true delivers focus to the delegate root but not
        // the text-bearing TextEdit descendant — the path every other event
        // recovers via establishFocus/takeFocus, but startup didn't.
        // Discipline-log entry 2026-05-16 (closed by tier-4b).
        // requestTextCaretAtRow early-returns for out-of-range rows, so an
        // empty model is a safe no-op.
        if (binding && binding.cursorState)
            binding.cursorState.requestTextCaretAtRow(0, 0)
    }
```

The `requestTextCaretAtRow` call is the row-keyed chokepoint entry — it resolves row 0 to a `BlockAnchor` (the variant-aware chokepoint stages `TextCaret` for text-bearing kinds, `BlockSelected` for HR/Image per registry).

- [ ] **Step 3:** Build (QML changes are picked up at runtime; rebuilding ensures the QML resource bundle is current):

```bash
cmake --build build-dev --target markoff-live-app-internal markoff-live-app -j 8
```

Expected: builds clean.

- [ ] **Step 4:** Run the new test from Task 13 — should now PASS:

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant -- \
  -select initial_focus_lands_on_textedit_not_delegate_root
```

Expected: **PASS**.

- [ ] **Step 5:** Run the full chokepoint invariant suite:

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant
```

Expected: all existing slots still pass, plus the new one.

- [ ] **Step 6:** Run the full live-render fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort | diff /tmp/tier4b-baseline-failures.txt -
```

Expected: no diff (no new failures).

- [ ] **Step 7:** Commit (Task 13's RED test + Task 14's GREEN seed are bundled together because they describe one feature):

```bash
git add libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp \
        libs/markoff-live/qml/LiveView.qml
git commit -m "markoff-live: seed initial focus via chokepoint (closes 2026-05-16 auto-focus gap)"
```

---

## Task 15: Falsifiability Proof B — initial-focus seed disabled

Per spec §5.6 Proof B. Comment out the new seed and confirm the test fails — proving the test is load-bearing.

**Files:** Modify `libs/markoff-live/qml/LiveView.qml` (temporarily).

- [ ] **Step 1:** Comment out the new seed:

```qml
    Component.onCompleted: {
        if (binding && binding.navigationController)
            binding.navigationController.setListView(root)
        // FALSIFIABILITY PROOF, REVERTS NEXT — seed disabled
        // if (binding && binding.cursorState)
        //     binding.cursorState.requestTextCaretAtRow(0, 0)
    }
```

- [ ] **Step 2:** Commit the stub:

```bash
git add libs/markoff-live/qml/LiveView.qml
git commit -m "markoff-live: stub — Component.onCompleted skips establishFocus seed (FALSIFIABILITY PROOF, REVERTS NEXT)"
```

- [ ] **Step 3:** Rebuild + run the new test:

```bash
cmake --build build-dev --target markoff-live-app-internal markoff-live-app -j 8
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant -- \
  -select initial_focus_lands_on_textedit_not_delegate_root
```

Expected: **FAIL**. The same failure mode observed in Task 13 Step 4.

If the test still passes:
- The test isn't actually testing what it claims; investigate before reverting (the seed isn't load-bearing for the test, but the underlying gap might still exist in production — the test needs tightening).

- [ ] **Step 4:** Revert the stub:

```bash
git revert HEAD --no-edit
```

Expected: revert lands cleanly. The seed returns; the test passes again.

- [ ] **Step 5:** Confirm test now passes:

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant -- \
  -select initial_focus_lands_on_textedit_not_delegate_root
```

Expected: **PASS**.

---

## Task 16: Full-suite regression check

**Files:** none (verification).

- [ ] **Step 1:** Clean rebuild:

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: no errors.

- [ ] **Step 2:** Run the full fast suite:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-final-failures.txt
diff /tmp/tier4b-baseline-failures.txt /tmp/tier4b-final-failures.txt
```

Expected: empty diff. Pre-existing baseline failures unchanged; no new failures from tier-4b.

- [ ] **Step 3:** Run the chokepoint invariant suite specifically (the protective fixture for tier-4b):

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant
```

Expected: all slots pass, including the new `initial_focus_lands_on_textedit_not_delegate_root`.

- [ ] **Step 4:** Run the realistic/benchmark suite once (slow; record any new failures vs. the recent-changes log's 12-failure count):

```bash
scripts/run-tests.sh -R 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-realistic-failures.txt
wc -l /tmp/tier4b-realistic-failures.txt
```

Expected: count is ≤ baseline-realistic count (currently ≤12 per `docs/e-arc/e-arc-status.md`'s recent-changes log). If higher, identify which test regressed and investigate before proceeding.

- [ ] **Step 5:** Grep-based invariant checks:

```bash
# No Qt.callLater introduced (the seam guidance caps current count at 1).
git grep -c 'Qt\.callLater' libs/markoff-live/qml/

# No new re-entrance guards.
git grep -nE 'm_applying|isApplying' libs/markoff-live/src/ libs/markoff-live/include/

# Confirm dead symbols all gone.
git grep -nE 'requestTextCaretAtNewRow|requestTextCaretAtAnchor|m_pendingRow|PendingRow|resolvePendingFor|onStructuralRow|structuralRowsInserted|structuralRowRemoved' \
    libs/markoff-live/src/ libs/markoff-live/include/ libs/markoff-live/qml/ apps/
```

Expected:
- `Qt.callLater` count: 1 (`MathDelegate.qml:113` from existing inventory).
- Re-entrance guards: same set as baseline (`m_applyingTextUpdate`, `m_applyingSessionSelection`); no new ones.
- Dead-symbols grep: **zero hits** outside `docs/`. If any production file matches, **stop** and investigate.

---

## Task 17: Update `docs/queue.md`

Close the 2026-05-16 discipline-log entry. Update the queue #2 banner.

**Files:** Modify `docs/queue.md`.

- [ ] **Step 1:** Locate the discipline-log entry from 2026-05-16 mentioning `UnifiedInlineTextDelegate.qml`. The current entry:

```
- 2026-05-16 `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — inv #1 — `ListView.focus = true` puts the unified delegate's root Item in the focus chain (`d->hasActiveFocus()` returns true) but the TextEdit child does NOT gain `activeFocus` — ...
```

- [ ] **Step 2:** Prepend `~~` and append `→ fixed in <commit-sha>`. The commit SHA is whichever commit landed Task 14's `LiveView.qml` seed (find with `git log --oneline | grep "seed initial focus"`):

```
- ~~2026-05-16 `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` — inv #1 — `ListView.focus = true` puts the unified delegate's root Item in the focus chain (`d->hasActiveFocus()` returns true) but the TextEdit child does NOT gain `activeFocus` — ...~~ → fixed in <SHA> (tier-4b). Initial focus now routed through `LiveView.qml`'s `Component.onCompleted` → `cursorState.requestTextCaretAtRow(0, 0)`. The two test slots that previously migrated from auto-focus to `requestCursor` stay that way — explicit chokepoint routing is the new normal.
```

- [ ] **Step 3:** Locate the queue #2 banner section. The current state shows tier-4a closeout (concerns #5, #9, #12 closed) and lists remaining concerns #3, #4, #10. Update the top banner to reflect tier-4b:

```
> **2026-05-16 — Tier 4b implemented.** Concerns **#3** (three
> overlapping `requestTextCaretAt*` APIs — two unused variants
> `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor` deleted;
> `requestTextCaretAtRow` retained as row-keyed convenience over
> `establishFocus`) and **#4** (the second pending slot `m_pendingRow`
> and its resolvers / slot handlers / signal connections deleted)
> both closed. Spec
> `docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md`;
> plan `docs/plans/2026-05-16-tier-4b-pending-slot-consolidation.md`.
> Falsifiability proofs A (m_pendingRow inert; full suite still
> green) and B (initial-focus seed disabled; new
> `initial_focus_lands_on_textedit_not_delegate_root` slot fails) both
> committed and reverted in-history. Remaining concern: **#10**
> (`LiveSelectionView` / `LiveCursorState` dual canonical stores) →
> tier 4c.
```

- [ ] **Step 4:** Commit:

```bash
git add docs/queue.md
git commit -m "docs: queue — close 2026-05-16 auto-focus gap; tier-4b complete (concerns #3, #4)"
```

---

## Task 18: Update `docs/e-arc/e-arc-status.md`

Append the tier-4b recent-changes entry.

**Files:** Modify `docs/e-arc/e-arc-status.md`.

- [ ] **Step 1:** Locate the recent-changes log table near the top of the file (under "## Recent-changes log").

- [ ] **Step 2:** Add a new row at the top of the table (most-recent-first):

```
| 2026-05-16 | (pending) | **Tier-4b cursor cleanup: queue #2 concerns #3 + #4 closed; 2026-05-16 auto-focus discipline-log entry closed.** Deleted dead-in-production `requestTextCaretAtNewRow`, `requestTextCaretAtAnchor`, `m_pendingRow`, `PendingRow`, `onStructuralRowsInserted`, `onStructuralRowRemoved`, `resolvePendingForRow`, `resolvePendingForAnchor`, and the two binding-side signals `structuralRowsInserted` / `structuralRowRemoved`. `request()`'s explicit-supersedes-pending semantic preserved (now resets `m_pendingFocus` instead). Initial focus now routed through chokepoint via `LiveView.qml` `Component.onCompleted` seed. Test migrations: `requestTextCaretAtNewRow_landsAtQtPos0` → `enterAtEnd_landsFocusOnNewRowViaChokepoint`; two structural-signal spies → `QAbstractItemModel::rowsInserted/Removed` spies. New invariant slot `initial_focus_lands_on_textedit_not_delegate_root` in `tst_live_render_focus_chokepoint_invariant`. Two falsifiability proofs committed + reverted in-history. Only #10 (selection/cursor unification) remains from queue #2 → tier 4c. |
```

- [ ] **Step 3:** Update the "Last updated" line at the top:

```
**Last updated:** 2026-05-16 (tier-4b complete — pending-slot consolidation; only queue #2 concern #10 remains).
```

- [ ] **Step 4:** Update the "Active phase" line:

```
**Active phase:** **dogfood pending** — E2.5 (S1/S2/S3 fixes) + E2.6 (theme + zoom) both implemented; tier-4b cursor cleanup complete (concerns #3, #4 closed). Tags held until user runs the interactive checklist. Next executable work (if dogfood unavailable): queue #2 concern #10 (tier 4c) or queue #4 (buffer-`\n` invariant) — see queue.md for ordering.
```

- [ ] **Step 5:** Commit:

```bash
git add docs/e-arc/e-arc-status.md
git commit -m "docs: e-arc-status — record tier-4b session (concerns #3, #4 closed)"
```

---

## Task 19: Final verification

Re-run every guardrail to catch any drift introduced by docs edits in Tasks 17-18.

**Files:** none (verification).

- [ ] **Step 1:** Full fast suite vs. baseline:

```bash
scripts/run-tests.sh -E 'realistic|benchmark' 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier4b-final2-failures.txt
diff /tmp/tier4b-baseline-failures.txt /tmp/tier4b-final2-failures.txt
```

Expected: empty diff.

- [ ] **Step 2:** Confirm the new slot still passes:

```bash
scripts/run-tests.sh --bin tst_live_render_focus_chokepoint_invariant -- \
  -select initial_focus_lands_on_textedit_not_delegate_root
```

Expected: PASS.

- [ ] **Step 3:** Confirm `git log --oneline` shows the expected commit chain (10–13 commits including the two falsifiability stubs + their reverts):

```bash
git log --oneline | head -20
```

- [ ] **Step 4:** Hand off to the user with a one-paragraph summary citing:
  - Concerns #3 + #4 closed.
  - Discipline-log entry closed.
  - Falsifiability proofs A + B in history (point to the stub commits).
  - Remaining open work: queue #2 concern #10 → tier 4c, queue #4 → after tier 4c.
  - Tags untouched (tier-4b lands no production-behavior change other than the initial-focus seed, which a follow-up dogfood pass should sign off on lightly per spec §9).

---

## Spec coverage check

- §2.1 in-scope items map to: §#3 → Tasks 7-8, §#4 → Tasks 9-11, auto-focus gap → Tasks 13-15, test migrations → Tasks 2-4.
- §2.2 non-goals: respected (no `LiveSelectionView` touched; no buffer-`\n` work; no `requestTextCaretAtRow` rename).
- §3 L4 decision: enforced (one pending slot left; invariant 3 satisfied — old store retired in the same plan as the new authority is canonised).
- §4.3 deletion table: covered by Tasks 7-11.
- §4.4 initial-focus seed: Task 14.
- §5.5 test inventory: Tasks 2-4.
- §5.6 falsifiability proofs A + B: Tasks 5 + 15.
- §8 definition-of-done checklist: every item maps to a task above (verify by line-by-line cross-check after plan execution).
- §11 open questions: deferred — the plan does not change L4 decisions or retirement choices. Open question on first text-bearing row resolved by using `requestTextCaretAtRow(0, 0)` (chokepoint stages variant per registry; block-only first row gets `BlockSelected`).
