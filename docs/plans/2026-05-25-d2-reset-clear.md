# D2 reset / reload clear before rebuild — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the D2 doubling-on-reset bug reported by Corbomite's
2026-05-25 steer. After this plan, `resetContent()` and
`loadFromMarkdown()` produce only the new content; no stale D2 blocks
survive into `serializeForSave()`.

**Architecture:** Add a non-emitting `local_clear()` primitive to
`CollabText::Crdt::IdList` (collabtext repo) and to
`Markoff::CausalLwwMap` (markoff-core, in-tree). Add
`MarkoffDocument::wipeD2State()` that uses both primitives plus
`.clear()` on the plain QHash/Set helpers, then call it unconditionally
from `resetContent()` and `loadFromMarkdown()` before
`buildD2FromBytes()`.

**Tech Stack:** Qt 6.8+, C++20, CMake 3.19+, collabtext (sibling
submodule), markoff-core (in-tree), QTest.

**Spec:** `docs/specs/2026-05-25-d2-reset-clear-design.md`. Read §2
(observable contract), §3 (architecture), §4 (acceptance tests) before
starting. Cite section numbers in commit messages where relevant.

**Driving consumer:** Corbomite
(`~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md`).

**Conventions:**
- Build (Markoff): `cmake --build build-dev --target <target> -j 8`
  (never bare `-j`).
- Build (collabtext): `cmake --build build-dev --target <target> -j 8`
  from `~/dev/collabtext`.
- Tests (Markoff): `scripts/run-tests.sh --bin <bin_name>` (defaults
  to `QT_QPA_PLATFORM=offscreen`).
- Tests (collabtext): `ctest --test-dir build-dev --output-on-failure
  -R <name>` from `~/dev/collabtext`.
- SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on every
  new file.
- TDD: failing test → implementation → green → commit. Frequent
  commits.
- **Falsifiability (INVARIANTS.md invariant 4):** each load-bearing
  test is run *before* its implementation to confirm it fails with
  the expected signature. Don't skip this step; it's the discipline
  that keeps tests honest.
- **Cross-repo discipline:** collabtext changes land on Codeberg
  first, then the Markoff submodule pin bump is its own commit, then
  Markoff feature work consumes the new API. No squashing across
  these boundaries — a future bisect needs to see them separately.

**Plan-time resolutions:**
- `IdList::local_clear()` zeros `m_entry_tree`, `m_undo_map`,
  `m_undo_stack`, `m_undo_cursor`, `m_deferred_queue`. Leaves
  `m_replica_id`, `m_clock`, `m_version`, `m_max_undo_depth`,
  `m_on_change`, `m_on_local_op` untouched. Does not fire any
  callback.
- `CausalLwwMap::local_clear()` zeros `m_entries`, `m_undoStack`,
  `m_redoStack`. Leaves `m_replicaId`, `m_localCounter`, `m_onChange`
  untouched. Does not fire `m_onChange`.
- `MarkoffDocument::wipeD2State()` is private, declared in
  `MarkoffDocument.h` next to the other private helpers; defined in
  `MarkoffDocument.cpp` next to `buildD2FromBytes()`.
- `bufferProxies` disposal uses `deleteLater()`, not `delete`. Each
  proxy is parented to `this` (MarkoffDocument); calling
  `deleteLater()` enqueues destruction so any in-flight signal
  emission unwinds first.

**File structure:**

| File | Repo | Change |
|------|------|--------|
| `libs/collabtext/src/crdt/IdList.h` | collabtext | add `local_clear()` declaration |
| `libs/collabtext/src/crdt/IdList.cpp` | collabtext | add `local_clear()` definition |
| `libs/collabtext/tests/tst_idlist.cpp` | collabtext | add `local_clear` test cases |
| `libs/collabtext` (submodule pin) | Markoff | bump to new collabtext tip |
| `libs/markoff-core/include/markoff/core/CausalLwwMap.h` | Markoff | add `local_clear()` template method |
| `libs/markoff-core/tests/d2/tst_causal_lww_map.cpp` | Markoff | new file — `local_clear` unit tests |
| `libs/markoff-core/tests/d2/CMakeLists.txt` | Markoff | wire new test binary |
| `libs/markoff-core/include/markoff/core/MarkoffDocument.h` | Markoff | declare `wipeD2State()` |
| `libs/markoff-core/src/MarkoffDocument.cpp` | Markoff | define `wipeD2State()`, wire into `resetContent`/`loadFromMarkdown`, remove caveat comments |
| `libs/markoff-core/tests/d2/tst_d2_reset_content.cpp` | Markoff | add acceptance tests; remove caveat comment |

---

## Phase A — collabtext: `IdList::local_clear()`

### Task A1: Falsifying test for `IdList::local_clear()`

**Files:**
- Test: `libs/collabtext/tests/tst_idlist.cpp` (modify — append cases)

- [ ] **Step 1: Add the failing tests**

Open `~/dev/collabtext/libs/collabtext/tests/tst_idlist.cpp`. The
existing pattern (note: class is `TestIdList`, slots are inline
`private slots:`, main macro is `QTEST_GUILESS_MAIN`, file already has
`using namespace CollabText::Crdt;` at top scope) is what to follow.

Add three inline slot methods to the `TestIdList` class body, anywhere
inside the existing `private slots:` block:

```cpp
    void local_clear_empties_the_list() {
        IdList list(1);
        list.insert_after(Anchor::min(), 100);
        list.insert_after(list.anchor_of(100, Bias::Right), 200);
        QCOMPARE(list.size(), 2u);

        list.local_clear();

        QCOMPARE(list.size(), 0u);
        QCOMPARE(list.tombstone_count(), size_t{0});
        QCOMPARE(list.entry_count(), size_t{0});
        QCOMPARE(list.undo_depth(), size_t{0});
    }

    void local_clear_preserves_clock_monotonicity() {
        IdList list(1);
        list.insert_after(Anchor::min(), 100);
        auto op1 = list.insert_after(list.anchor_of(100, Bias::Right), 200);
        const Lamport stampBefore = get_idlist_op_timestamp(op1);

        list.local_clear();

        auto op2 = list.insert_after(Anchor::min(), 300);
        const Lamport stampAfter = get_idlist_op_timestamp(op2);

        // Clock must not regress (Lamport has operator<=>).
        QVERIFY(stampBefore < stampAfter);
    }

    void local_clear_does_not_fire_callbacks() {
        IdList list(1);
        list.insert_after(Anchor::min(), 100);

        int changeCalls = 0;
        int localOpCalls = 0;
        list.set_on_change([&]{ ++changeCalls; });
        list.set_on_local_op([&](const IdListOperation &){ ++localOpCalls; });

        list.local_clear();

        QCOMPARE(changeCalls, 0);
        QCOMPARE(localOpCalls, 0);
    }
```

(The free function `CollabText::Crdt::get_idlist_op_timestamp(const
IdListOperation&)` lives in `<collabtext/IdListOperations.h>`, included
transitively via `crdt/IdList.h`.)

- [ ] **Step 2: Configure + build (will fail to compile)**

From `~/dev/collabtext`:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_idlist -j 8
```

Expected: compile error — `'local_clear' is not a member of 'CollabText::Crdt::IdList'`.

This is the falsification proof. Note the exact error in your scratch
notes — Step 4 will confirm the same test now compiles and passes.

### Task A2: Implement `IdList::local_clear()`

**Files:**
- Modify: `libs/collabtext/src/crdt/IdList.h` (add declaration after line 145)
- Modify: `libs/collabtext/src/crdt/IdList.cpp` (add definition near `set_entries`, around line 394)

- [ ] **Step 1: Add the declaration to `IdList.h`**

After the `apply_remote_op` declaration (around line 145, before the
`private:` section), insert:

```cpp
    /// Single-replica reset primitive. Drops all entries (visible +
    /// tombstones), the undo stack, and the deferred-op queue.
    /// Preserves replica_id, clock, version, max_undo_depth, and
    /// registered callbacks. Does NOT fire set_on_change or
    /// set_on_local_op. For use when the canonical content is
    /// replaced from outside the CRDT (file reload, revert-to-saved,
    /// programmatic content swap); calling on a connected collab
    /// session is allowed but remote peers will not see the clear —
    /// that's a higher-layer concern.
    void local_clear();
```

- [ ] **Step 2: Add the definition to `IdList.cpp`**

After the `set_entries` definition (around line 400), append:

```cpp
void IdList::local_clear() {
    m_entry_tree = IdListTree{};
    m_undo_map = UndoMap{};
    m_undo_stack.clear();
    m_undo_cursor = 0;
    m_deferred_queue = IdListOperationQueue{};
    // Intentionally preserved: m_replica_id, m_clock, m_version,
    // m_max_undo_depth, m_on_change, m_on_local_op.
    // Intentionally not fired: m_on_change, m_on_local_op.
}
```

- [ ] **Step 3: Build the test binary**

```bash
cmake --build build-dev --target tst_idlist -j 8
```

Expected: compiles clean.

- [ ] **Step 4: Run the new tests**

```bash
ctest --test-dir build-dev --output-on-failure -R 'tst_idlist'
```

Expected: PASS. All three new slots pass; existing slots still pass.

- [ ] **Step 5: Run the full collabtext suite for regressions**

```bash
ctest --test-dir build-dev --output-on-failure
```

Expected: no new failures vs baseline.

### Task A3: Commit + push collabtext

- [ ] **Step 1: Review the diff**

```bash
cd ~/dev/collabtext && git status && git diff
```

Expected: changes to `IdList.h`, `IdList.cpp`, `tests/tst_idlist.cpp`
only.

- [ ] **Step 2: Commit**

```bash
git add libs/collabtext/src/crdt/IdList.h \
        libs/collabtext/src/crdt/IdList.cpp \
        libs/collabtext/tests/tst_idlist.cpp
git commit -m "$(cat <<'EOF'
feat(IdList): add local_clear() single-replica reset primitive

Non-emitting reset for use when the canonical content is replaced from
outside the CRDT (file reload, revert-to-saved, programmatic content
swap). Drops entries (visible + tombstones), undo stack, and deferred-op
queue. Preserves replica_id, clock, version, callbacks. Does NOT fire
set_on_change or set_on_local_op.

Calling on a connected collab session is allowed but remote peers will
not see the clear — that's a higher-layer concern.

Caller: Markoff's MarkoffDocument::wipeD2State() (Markoff spec
docs/specs/2026-05-25-d2-reset-clear-design.md §3.1).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Push to Codeberg**

```bash
git push origin master
```

Expected: pushes to `git@codeberg.org:clintonthegeek/collabtext.git`.

- [ ] **Step 4: Record the collabtext tip SHA**

```bash
git rev-parse HEAD
```

Note the SHA — Task B1 needs it.

---

## Phase B — Markoff: bump submodule pin

### Task B1: Bump `libs/collabtext` submodule pin

**Files:**
- Modify: `libs/collabtext` (submodule pointer)

- [ ] **Step 1: From the Markoff repo, update the submodule**

From `~/dev/Markoff`:

```bash
git submodule update --init libs/collabtext  # ensures submodule is initialized
cd libs/collabtext
git fetch origin
git checkout master
git pull origin master
cd ../..
```

Now `libs/collabtext` HEAD matches the collabtext tip recorded in Task
A3 Step 4.

- [ ] **Step 2: Verify the pin advanced**

```bash
git status
```

Expected: `modified:   libs/collabtext (new commits)`.

```bash
git diff libs/collabtext
```

Expected: a one-line submodule SHA bump.

- [ ] **Step 3: Sanity-build Markoff against the new collabtext**

```bash
cmake --build build-dev -j 8
```

Expected: clean build (the new `local_clear` symbol isn't called yet,
so no link errors are expected).

- [ ] **Step 4: Commit the submodule bump**

Replace `<sha>` below with the collabtext SHA from Task A3 Step 4.

```bash
git add libs/collabtext
git commit -m "$(cat <<'EOF'
chore(submodule): bump collabtext to <sha> for IdList::local_clear

Pulls in CollabText::Crdt::IdList::local_clear() — non-emitting reset
primitive used by MarkoffDocument::wipeD2State() (next commit). Isolated
in its own commit so a future bisect can identify the collabtext API
change cleanly.

Spec: docs/specs/2026-05-25-d2-reset-clear-design.md §3.1, §7.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

Do **not** push yet — Phase C lands on top.

---

## Phase C — Markoff: `CausalLwwMap::local_clear()`

### Task C1: Falsifying test for `CausalLwwMap::local_clear()`

**Files:**
- Create: `libs/markoff-core/tests/d2/tst_causal_lww_map.cpp`
- Modify: `libs/markoff-core/tests/d2/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

Create `libs/markoff-core/tests/d2/tst_causal_lww_map.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
//
// Unit tests for Markoff::CausalLwwMap::local_clear() — the
// non-emitting single-replica reset primitive used by
// MarkoffDocument::wipeD2State() to drop all entries from a sibling map
// when the canonical content is replaced from outside the CRDT.
//
// Spec: docs/specs/2026-05-25-d2-reset-clear-design.md §3.2.

#include <QTest>
#include <markoff/core/CausalLwwMap.h>

using namespace Markoff;

class TstCausalLwwMap : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void localClear_emptiesTheMap();
    void localClear_preservesClockMonotonicity();
    void localClear_doesNotFireChangeCallback();
    void localClear_clearsRedoStack();
};

void TstCausalLwwMap::localClear_emptiesTheMap()
{
    CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.setWithNextStamp("b", "2");
    QVERIFY(map.get("a").has_value());
    QVERIFY(map.get("b").has_value());

    map.local_clear();

    QVERIFY(!map.get("a").has_value());
    QVERIFY(!map.get("b").has_value());

    int liveCount = 0;
    map.forEachValue([&](const QByteArray &, const QByteArray &){ ++liveCount; });
    QCOMPARE(liveCount, 0);
}

void TstCausalLwwMap::localClear_preservesClockMonotonicity()
{
    CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    const CausalStamp before = map.currentStamp();

    map.local_clear();

    map.setWithNextStamp("a", "2");
    const CausalStamp after = map.currentStamp();

    QVERIFY(before < after);
}

void TstCausalLwwMap::localClear_doesNotFireChangeCallback()
{
    CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.setWithNextStamp("b", "2");

    int calls = 0;
    map.setOnChange([&](const QByteArray &, std::optional<QByteArray>,
                        std::optional<QByteArray>){ ++calls; });

    map.local_clear();

    QCOMPARE(calls, 0);
}

void TstCausalLwwMap::localClear_clearsRedoStack()
{
    CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.undo();
    // After undo, redo stack has one entry; calling redo would restore "a".

    map.local_clear();
    map.redo();  // must be a no-op; no crash, no resurrected entry.

    QVERIFY(!map.get("a").has_value());
}

QTEST_MAIN(TstCausalLwwMap)
#include "tst_causal_lww_map.moc"
```

- [ ] **Step 2: Wire the test in CMake**

Open `libs/markoff-core/tests/d2/CMakeLists.txt`, find the block where
other `d2/tst_*` binaries are declared (look for `tst_d2_reset_content`
or similar), and add the new binary using the same pattern.

The pattern is likely a single helper call (e.g.
`add_markoff_test(tst_causal_lww_map)`) or an `add_executable` +
`target_link_libraries` + `add_test` triple. Use whatever the
neighbouring binary uses — do not invent a new pattern.

- [ ] **Step 3: Configure + build (will fail to compile)**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_causal_lww_map -j 8
```

Expected: compile error — `'local_clear' is not a member of
'Markoff::CausalLwwMap<...>'`.

Falsification proof recorded.

### Task C2: Implement `CausalLwwMap::local_clear()`

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/CausalLwwMap.h`

- [ ] **Step 1: Add the method**

After the `applyRemote` method (around line 180, before the `private:`
section that begins around line 182), insert:

```cpp
    /// Single-replica reset primitive. Drops all entries (live +
    /// tombstoned), the undo stack, and the redo stack. Preserves
    /// replicaId, the local counter, and the registered onChange
    /// callback. Does NOT fire onChange. For use when the canonical
    /// content is replaced from outside the CRDT (file reload,
    /// revert-to-saved, programmatic content swap); calling on a
    /// connected collab session is allowed but remote peers will not
    /// see the clear.
    void local_clear() {
        m_entries.clear();
        m_undoStack.clear();
        m_redoStack.clear();
        // Intentionally preserved: m_replicaId, m_localCounter,
        // m_onChange. Intentionally not fired: m_onChange.
    }
```

- [ ] **Step 2: Build the test binary**

```bash
cmake --build build-dev --target tst_causal_lww_map -j 8
```

Expected: compiles clean.

- [ ] **Step 3: Run the new tests**

```bash
scripts/run-tests.sh --bin tst_causal_lww_map
```

Expected: PASS. All four new slots pass.

### Task C3: Commit `CausalLwwMap::local_clear()`

- [ ] **Step 1: Review the diff**

```bash
git status && git diff libs/markoff-core/include/markoff/core/CausalLwwMap.h \
                       libs/markoff-core/tests/d2/CMakeLists.txt
```

Plus the new file:

```bash
git diff --stat libs/markoff-core/tests/d2/tst_causal_lww_map.cpp
```

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-core/include/markoff/core/CausalLwwMap.h \
        libs/markoff-core/tests/d2/tst_causal_lww_map.cpp \
        libs/markoff-core/tests/d2/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(CausalLwwMap): add local_clear() single-replica reset primitive

Non-emitting reset for use when the canonical content is replaced from
outside the CRDT (file reload, revert-to-saved, programmatic content
swap). Drops entries (live + tombstoned), undo + redo stacks. Preserves
replicaId, local counter, onChange callback. Does NOT fire onChange.

Symmetric in intent with collabtext's IdList::local_clear() (landed in
the preceding submodule bump). Caller: MarkoffDocument::wipeD2State()
(next commit).

Spec: docs/specs/2026-05-25-d2-reset-clear-design.md §3.2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — Markoff: `wipeD2State()` + acceptance tests

### Task D1: Falsifying acceptance tests for the doubling bug

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_d2_reset_content.cpp`

- [ ] **Step 1: Add the new slot declarations**

Find the `private Q_SLOTS:` block (around line 30) and append:

```cpp
    void nonFreshReset_replacePlain_noResidue();
    void nonFreshReset_replaceHeader_noResidue();
    void nonFreshReset_replaceUnicode_noResidue();
    void nonFreshReset_externalReloadClean_noResidue();
    void nonFreshReset_externalReloadResolved_noResidue();
    void nonFreshReset_userRevertToSaved_noResidue();
    void nonFreshReset_firstOpen_noResidue();
    void loadFromMarkdown_calledTwice_replacesNotAppends();
    void reset_clearsFrontmatterFromPrior();
    void reset_clearsFootnotesFromPrior();
```

- [ ] **Step 2: Add the slot definitions**

Append at the end of the file (before `QTEST_MAIN`):

```cpp
void TstD2ResetContent::nonFreshReset_replacePlain_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_replaceHeader_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("# Note 1\n");
    doc.resetContent("# Modified Note 1\n", Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(), QByteArray("# Modified Note 1\n"));
}

void TstD2ResetContent::nonFreshReset_replaceUnicode_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QString::fromUtf8("日本語 café 🎉 résumé\n").toUtf8());
    doc.resetContent(
        QString::fromUtf8("日本語 café 🎉 résumé\n\nMore text\n").toUtf8(),
        Origin::TestFixture);
    QCOMPARE(doc.serializeForSave(),
             QString::fromUtf8("日本語 café 🎉 résumé\n\nMore text\n").toUtf8());
}

void TstD2ResetContent::nonFreshReset_externalReloadClean_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::ExternalReloadClean);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_externalReloadResolved_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::ExternalReloadResolved);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_userRevertToSaved_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::UserRevertToSaved);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::nonFreshReset_firstOpen_noResidue()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("original content\n");
    doc.resetContent("modified content\n", Origin::FirstOpen);
    QCOMPARE(doc.serializeForSave(), QByteArray("modified content\n"));
}

void TstD2ResetContent::loadFromMarkdown_calledTwice_replacesNotAppends()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("first\n");
    doc.loadFromMarkdown("second\n");
    QCOMPARE(doc.serializeForSave(), QByteArray("second\n"));
    QCOMPARE(doc.iterateBlocks().size(), size_t{1});
}

void TstD2ResetContent::reset_clearsFrontmatterFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("---\ntitle: A\n---\n\nBody A\n");
    doc.resetContent("Body B with no frontmatter\n",
                     Origin::ExternalReloadClean);
    QVERIFY(!doc.frontmatterValue("raw").has_value());
    QCOMPARE(doc.serializeForSave(),
             QByteArray("Body B with no frontmatter\n"));
}

void TstD2ResetContent::reset_clearsFootnotesFromPrior()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Text[^1]\n\n[^1]: footnote A\n");
    doc.resetContent("Plain text\n", Origin::ExternalReloadClean);
    // Stale footnote def must not survive into the serialized output.
    QCOMPARE(doc.serializeForSave(), QByteArray("Plain text\n"));
}
```

- [ ] **Step 3: Build the test binary**

```bash
cmake --build build-dev --target tst_d2_reset_content -j 8
```

Expected: compiles clean (no new APIs used yet).

- [ ] **Step 4: Run the new tests against unmodified production code**

```bash
scripts/run-tests.sh --bin tst_d2_reset_content
```

Expected: the **10 new slots FAIL** with the doubling signature, e.g.:

```
FAIL!  : TstD2ResetContent::nonFreshReset_replacePlain_noResidue()
   Compared values are not the same
      Actual:   "modified content\n\noriginal content\n"
      Expected: "modified content\n"
```

Existing slots still pass (`firstOpen_*`, `testFixture_*`, etc.).

This is the falsification proof for the production fix (Task D2).

### Task D2: Implement `MarkoffDocument::wipeD2State()` + wire it in

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Declare `wipeD2State()` in the header**

Open `libs/markoff-core/include/markoff/core/MarkoffDocument.h`, find
the private section near other private helpers (search for
`applyRemoteFootnoteDefMapOp`). Add the declaration before that block:

```cpp
    /// Reset all D2 in-memory state to the post-construction shape
    /// (block IdList, all sibling maps, per-block buffers, proxies,
    /// inline cache, blockLoadTimeBytes, edit-sequence tracking)
    /// without disturbing replica id, the legacy buffer, the undo log,
    /// or external signal connections. Used by resetContent() and
    /// loadFromMarkdown() before they rebuild D2 from new bytes; safe
    /// to call on a fresh document (every container is already empty).
    ///
    /// Single-replica only: this does NOT emit remote ops. A connected
    /// collab peer would not see the wipe. See spec
    /// docs/specs/2026-05-25-d2-reset-clear-design.md §6.2.
    void wipeD2State();
```

- [ ] **Step 2: Define `wipeD2State()` in the cpp**

Open `libs/markoff-core/src/MarkoffDocument.cpp`. Add the definition
just above `buildD2FromBytes()` (around line 1820):

```cpp
void MarkoffDocument::wipeD2State()
{
    // Dispose proxies via deleteLater so any in-flight signal
    // emission unwinds before destruction. Each BufferProxy is
    // parented to `this`; deleteLater() is the safe disposal.
    for (auto it = d->bufferProxies.cbegin();
         it != d->bufferProxies.cend(); ++it) {
        if (it.value()) it.value()->deleteLater();
    }
    d->bufferProxies.clear();

    // Plain Qt containers — drop in bulk.
    d->blockBuffers.clear();
    d->blockLoadTimeBytes.clear();
    d->blockEditSequences.clear();
    d->touchedSinceLoad.clear();
    d->structuralEditSequence = 0;
    if (d->inlineCache) d->inlineCache->clear();

    // CRDT structures — non-emitting clears.
    d->idList.local_clear();
    d->kindTagMap.local_clear();
    d->blockAttrsMap.local_clear();
    d->frontmatterMap.local_clear();
    d->linkRefMap.local_clear();
    d->footnoteDefMap.local_clear();

    // Intentionally preserved: d->replicaId, d->buffer (legacy),
    // d->undoLog, d->nextBlockId (so freshly-allocated BlockIds
    // remain globally unique across a document's lifetime).
}
```

- [ ] **Step 3: Wire `wipeD2State()` into `resetContent()`**

In `MarkoffDocument.cpp`, find the `resetContent()` method (around line
692). Locate the call to `buildD2FromBytes(newContent)` at line 747 and
**insert `wipeD2State();` immediately before it**. Also delete the
four-paragraph caveat comment at lines 734-746. The end-state of the
relevant block should read:

```cpp
    case Origin::UserEdit:
        Q_UNREACHABLE();
        break;
    }

    // Wipe D2 state before rebuilding so a reset on a non-fresh document
    // doesn't double its content. Safe on a fresh document (no-op-ish).
    wipeD2State();
    buildD2FromBytes(newContent);

    Q_EMIT documentReloaded();
    Q_EMIT documentChanged();
    scheduleD2Changed();
}
```

- [ ] **Step 4: Wire `wipeD2State()` into `loadFromMarkdown()`**

Find `loadFromMarkdown()` (around line 1851). Insert `wipeD2State();`
as the first statement:

```cpp
void MarkoffDocument::loadFromMarkdown(const QByteArray &src)
{
    wipeD2State();
    buildD2FromBytes(src);

    // documentChanged() fires synchronously here so connected views can update
    // their state in the same call stack as loadFromMarkdown(). d2DocumentChanged()
    // from scheduleD2Changed() is deferred one event-loop iteration (QTimer::singleShot(0));
    // consumers of both signals must not assume they arrive in the same call stack on load.
    Q_EMIT documentLoaded();
    Q_EMIT documentChanged();
    scheduleD2Changed();
}
```

- [ ] **Step 5: Remove the stale test caveat**

In `libs/markoff-core/tests/d2/tst_d2_reset_content.cpp`, delete lines
15-19 (the paragraph beginning "This test pins the 'also builds D2'
choice..." through "...tracked as a follow-up."). The follow-up is now
closed by this very plan.

- [ ] **Step 6: Build the test binary**

```bash
cmake --build build-dev --target tst_d2_reset_content -j 8
```

Expected: compiles clean.

- [ ] **Step 7: Run the acceptance tests — must now pass**

```bash
scripts/run-tests.sh --bin tst_d2_reset_content
```

Expected: all 10 new slots PASS. All pre-existing slots still PASS.

- [ ] **Step 8: Run the full test suite for regressions**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: pass count rises by 14 (10 new in `tst_d2_reset_content` +
4 new in `tst_causal_lww_map`); total count rises by the same 14.
The three known pre-existing failures named in the project CLAUDE.md
test-baseline section remain — `tst_live_render_e2_nav_shift_extend`,
`tst_live_render_focus_chokepoint_invariant`,
`tst_live_render_cursor_typing_invariant`. So baseline 235/238 → 249/252.

If any new failures appear beyond those three known ones, STOP and
investigate before committing — most likely something downstream of
`wipeD2State` (e.g. a view-layer test that expected stale state to
survive a reset) needs updating to match the new contract.

### Task D3: Commit the Markoff feature work

- [ ] **Step 1: Review the diff**

```bash
git status && git diff --stat
```

Expected: changes to
- `libs/markoff-core/include/markoff/core/MarkoffDocument.h`
- `libs/markoff-core/src/MarkoffDocument.cpp`
- `libs/markoff-core/tests/d2/tst_d2_reset_content.cpp`

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-core/include/markoff/core/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_reset_content.cpp
git commit -m "$(cat <<'EOF'
fix(d2): clear D2 state before rebuild on reset / reload

Closes the doubling-on-save bug reported by Corbomite's 2026-05-25 steer:
resetContent() and loadFromMarkdown() on a non-fresh document were
appending parsed blocks on top of any pre-existing D2 state, so
serializeForSave() emitted `new + old` and silently corrupted files on
ExternalReload / UserRevertToSaved / programmatic setMarkdown flows.

The fix adds MarkoffDocument::wipeD2State() — a private helper that
calls local_clear() on every D2 CRDT (idList, kindTagMap, blockAttrsMap,
frontmatterMap, linkRefMap, footnoteDefMap), clears the plain QHash/Set
sidecars, and disposes BufferProxy QObjects via deleteLater(). It does
NOT touch replica id, the legacy d->buffer, the undo log, or
nextBlockId. resetContent() and loadFromMarkdown() now call it
unconditionally before buildD2FromBytes(), making both methods
idempotent across N invocations on the same document instance.

The matching local_clear() primitives are CollabText::Crdt::IdList
(landed in the submodule bump two commits back) and
Markoff::CausalLwwMap (landed in the previous commit).

Acceptance tests cover all three reproduction shapes from the steer
(plain replace, header replace, unicode replace) parametrised across
ExternalReloadClean / ExternalReloadResolved / UserRevertToSaved /
FirstOpen / TestFixture, plus loadFromMarkdown-twice and sidecar-map
(frontmatter, footnote) wipe coverage. Falsification proven prior to
landing (Task D1 Step 4): the same tests fail with the doubling
signature against unmodified buildD2FromBytes.

Out of scope (see spec §6): D2-undo of revert, collab broadcast of
resets, tombstone compaction, Origin-enum simplification.

Spec:  docs/specs/2026-05-25-d2-reset-clear-design.md
Steer: ~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Task D4: Push Markoff

- [ ] **Step 1: Verify the commit history**

```bash
git log --oneline -5
```

Expected, in order (most recent first):
1. `fix(d2): clear D2 state before rebuild on reset / reload`
2. `feat(CausalLwwMap): add local_clear() single-replica reset primitive`
3. `chore(submodule): bump collabtext to <sha> for IdList::local_clear`
4. `spec(d2-reset-clear): correct CausalLwwMap location — markoff-core, not collabtext`
5. `spec: D2 reset / reload clear before rebuild (Corbomite steer)`

- [ ] **Step 2: Push**

```bash
git push origin master
```

Expected: pushes three commits (the submodule bump, the LWW primitive,
the feature fix) plus the two earlier spec commits.

### Task D5: Update project CLAUDE.md status banner

**Files:**
- Modify: `CLAUDE.md` (project root)

- [ ] **Step 1: Locate the active-workfront section**

In the project's `CLAUDE.md`, find the "Active workfront" section near
the top.

- [ ] **Step 2: Add a one-line entry under the resolved port-driven items**

In the list of resolved port-driven items (around the line documenting
the four previously-resolved items), append:

```markdown
> 5. ✅ **D2 reset/reload doubling** — closed 2026-05-25 by
>    `wipeD2State()` (Markoff `<sha>`) + IdList/CausalLwwMap
>    `local_clear()` primitives. Spec
>    `docs/specs/2026-05-25-d2-reset-clear-design.md`. Closes
>    the caveat previously documented at
>    `MarkoffDocument.cpp:741-746` and the
>    `tst_d2_reset_content.cpp` test docstring.
```

Replace `<sha>` with the short SHA of the Task D3 commit (`git rev-parse
--short HEAD`).

- [ ] **Step 3: Commit + push**

```bash
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
docs(CLAUDE.md): note D2 reset/reload doubling closed

Adds entry #5 to the resolved port-driven items list. Cross-references
the spec and the closed in-code caveats.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Phase E — Notify Corbomite

### Task E1: Write the closure handoff note

**Files:**
- Create: `docs/handoff/2026-05-25-d2-reset-clear-closed-reply.md`

- [ ] **Step 1: Draft the note**

Create `docs/handoff/2026-05-25-d2-reset-clear-closed-reply.md`:

```markdown
# Reply → Corbomite: D2 reset/reload doubling closed

**From:** Markoff
**Date:** 2026-05-25
**Re:** `~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md`

The steer landed. After the following commits the observable contract
from your steer holds: `resetContent(B)` / `loadFromMarkdown(B)` on a
document holding A reflect only B in `iterateBlocks()` and
`serializeForSave()` — regardless of whether the document was fresh.

## What landed

1. **collabtext** `<collabtext-sha>` — `CollabText::Crdt::IdList::local_clear()`
   non-emitting single-replica reset primitive.
2. **Markoff** `<bump-sha>` — submodule bump to pick up the IdList primitive.
3. **Markoff** `<lww-sha>` — `Markoff::CausalLwwMap::local_clear()` symmetric primitive.
4. **Markoff** `<fix-sha>` — `MarkoffDocument::wipeD2State()` called
   unconditionally from `resetContent()` and `loadFromMarkdown()` before
   `buildD2FromBytes()`. Caveat comment at the old
   `MarkoffDocument.cpp:741-746` removed (now resolved).

Spec: `docs/specs/2026-05-25-d2-reset-clear-design.md` (Markoff).

## Acceptance test coverage

All three reproduction shapes from your steer pinned in
`tst_d2_reset_content.cpp`, parametrised across `ExternalReloadClean`,
`ExternalReloadResolved`, `UserRevertToSaved`, `FirstOpen`,
`TestFixture`. Plus `loadFromMarkdown-twice` and sidecar-map
(frontmatter, footnote) wipe coverage. Falsification proven before
landing (tests failed with doubling signature against unmodified code,
then passed after `wipeD2State()` wired in).

## What you can re-pin to

Markoff tip after this plan: `<fix-sha>` (or any later commit on `master`).

## Acknowledged out-of-scope (per spec §6)

- **D2-undo of revert.** `UserRevertToSaved` is no longer undoable
  through D2 undo. Legacy buffer undo still reverses the revert via
  the mega-edit that's still recorded there. Matches typical editor
  semantics; flag if you need D2-undo of revert and it's a separate spec.
- **Collab broadcast of resets.** A reset on a connected peer does not
  propagate. Single-vault / single-replica assumption for now.

Happy to pair on the re-pin if anything regresses.
```

(Replace the `<...-sha>` placeholders with the actual SHAs once you've
pushed.)

- [ ] **Step 2: Commit + push the handoff note**

```bash
git add docs/handoff/2026-05-25-d2-reset-clear-closed-reply.md
git commit -m "$(cat <<'EOF'
docs(handoff): closure reply to Corbomite D2-reset-clear steer

Confirms the observable contract from Corbomite's 2026-05-25 steer is
met, lists the four landing commits, points to the spec, and notes the
two acknowledged out-of-scope items (D2-undo of revert; collab
broadcast).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
git push origin master
```

---

## Self-review checklist (executor)

Before declaring done, confirm:

- [ ] Spec §2 contract verified end-to-end: all 10 new slots in
  `tst_d2_reset_content` PASS, including the unicode case and the
  `loadFromMarkdown-twice` case.
- [ ] Spec §4.5 falsifiability: D1 Step 4 actually saw 10 FAIL outputs
  against unmodified code (not skipped).
- [ ] Spec §4.4 collabtext local_clear tests PASS (A2 Step 4) and the
  symmetric Markoff `tst_causal_lww_map` tests PASS (C2 Step 3).
- [ ] Stale caveats removed: `MarkoffDocument.cpp` no longer contains
  the "requires IdList clear semantics" paragraph; `tst_d2_reset_content.cpp`
  no longer contains the "non-fresh… tracked as a follow-up" paragraph.
- [ ] No regressions vs the baseline named in `CLAUDE.md`'s test-baseline
  section.
- [ ] All five commits pushed in order (collabtext × 1, Markoff × 4).
- [ ] Corbomite handoff note in place with real SHAs (not `<...>`
  placeholders).
- [ ] Project `CLAUDE.md` updated with the closure entry.
