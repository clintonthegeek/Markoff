# BlockAnchor Foundation — Implementation Plan

> **Status:** ✅ Complete (2026-04-30, commits `7f0bcad..5bb4491` on `exploration/new-foundation`). All 14 tasks landed; 96/96 tests pass; final whole-branch review approved. Two perf-gap follow-ups tracked in `docs/TODO.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the `Markoff::TextAnchor` + `Markoff::BlockAnchor` types and their supporting `MarkoffDocument` APIs per `docs/specs/2026-04-30-block-anchor-foundation-design.md`. After this plan, `parseUpdated` ships a parallel `QList<BlockAnchor>`, `Selection`'s anchors are TextAnchor-typed, and the foundation exposes `parseSequence()` / `editSequence()` so view-layer code never has to hold a `Crdt::Global`.

**Architecture:** Foundation-only spec; one new pair of types (`TextAnchor`, `BlockAnchor`) plus accessor surface on `MarkoffDocument`. BlockAnchor compute runs on the main thread in the lambda that relays the parse-pool's `parsed` to the public `parseUpdated`. The one architectural extension beyond the spec text: a foundation-internal `TopLevelBlockScanner` that enumerates top-level block byte ranges from a UTF-8 body, mirroring view-qml's `BlockWalker` algorithm (the parser does not currently expose top-level block byte ranges). Convergence with `BlockWalker` is tested via fixture corpus; full unification is deferred to the live-editing plan.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, Qt Test framework. Build with `-j 8` (no bare `-j`, which freezes the user's machine). All sources include `// SPDX-License-Identifier: GPL-3.0-or-later`.

**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`).

**Related docs:**
- Spec (load-bearing): [`docs/specs/2026-04-30-block-anchor-foundation-design.md`](../specs/2026-04-30-block-anchor-foundation-design.md)
- Driving consumer plan: [`docs/plans/2026-04-30-live-editing.md`](2026-04-30-live-editing.md)
- View-qml CLAUDE.md (cross-cutting invariants): [`libs/markoff-view-qml/CLAUDE.md`](../../libs/markoff-view-qml/CLAUDE.md)
- Existing `BlockWalker` (algorithm to mirror): `libs/markoff-view-qml/src/BlockWalker.cpp`
- CRDT `Anchor` type: `/home/clinton/dev/collabtext/libs/collabtext/src/crdt/Anchor.h`
- Anchor JSON precedent: `libs/markoff-core/include/markoff-foundation/AnchorJson.h`

**Out of scope:**
- Unifying view-qml's `BlockWalker` with the new foundation `TopLevelBlockScanner`. They run the same algorithm; convergence is tested. Unification is the live-editing plan's task 2.2 territory.
- Persisted serialisation of BlockAnchor (per spec §8 — they're ephemeral).
- Plugin/public docs for BlockAnchor consumers (per spec §8).
- Worker-thread BlockAnchor compute (alternative path; flagged in spec §7 as a future refactor if profiling demands).

---

## File Structure

### Files created

```
libs/markoff-core/
  include/markoff-foundation/
    TextAnchor.h
    BlockAnchor.h
  src/
    AnchorConversion.h
    AnchorConversion.cpp
    TopLevelBlockScanner.h
    TopLevelBlockScanner.cpp
    BlockAnchorComputation.h
    BlockAnchorComputation.cpp
  tests/
    tst_foundation_text_anchor.cpp
    tst_foundation_block_anchor.cpp
    tst_foundation_top_level_block_scanner.cpp
    tst_foundation_block_anchor_compute.cpp
    tst_foundation_block_anchor_stability.cpp
    tst_foundation_parse_sequence.cpp
    tst_foundation_edit_sequence.cpp
    fixtures/block_scanner_corpus/   (small markdown fixtures used by both
                                      scanner and convergence tests)
```

### Files modified

```
libs/markoff-core/
  include/markoff-foundation/
    MarkoffDocument.h         new public APIs (parseSequence, editSequence,
                              textAnchorAt overloads, resolveTextAnchor,
                              blockAt, offsetInBlock, blockAnchorAt,
                              blockByteRange); parseUpdated signal shape
                              change
    Selection.h               anchor/active become TextAnchor; drops
                              #include <crdt/Anchor.h>
  src/
    MarkoffDocumentPrivate.h  add parseSequence + editSequence counters,
                              latestBlockAnchors + latestBlockRanges
                              snapshots
    MarkoffDocument.cpp       implement new APIs; bump editSequence on
                              every state-change op; bump parseSequence
                              and compute BlockAnchors in the parse-relay
                              lambda
    Selection.cpp             update for TextAnchor field types
    CommandFacade.cpp         resolveAnchor → resolveTextAnchor
    SearchEngine.cpp          resolveAnchor → resolveTextAnchor
    SourceTextDocumentBinding.cpp resolveAnchor → resolveTextAnchor
    Cmd/Helpers.cpp           resolveAnchor → resolveTextAnchor
    Session.cpp               selection-equality compares TextAnchor fields
    CMakeLists.txt            new sources/tests
  tests/
    CMakeLists.txt            register new test executables
    tst_selection.cpp         update QCOMPARE calls to compare TextAnchor
                              instead of Crdt::Anchor (existing tests
                              behaviour-preserving; only types change)

libs/markoff-view-qml/
  src/
    EditorBackend.h           m_selectionAnchor / m_cursorAnchor become
                              TextAnchor-typed; drops <crdt/Anchor.h>
    EditorBackend.cpp         conversion at the QVariant boundary
  tests/
    tst_view_qml_editor_backend.cpp  update Anchor types in tests
```

No files deleted.

---

## Task 1: `TextAnchor` type

A view-layer-safe wrapper for a CRDT byte anchor. Header includes no CRDT types.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/TextAnchor.h`
- Create: `libs/markoff-core/tests/tst_foundation_text_anchor.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 1.1: Write failing test for TextAnchor equality**

`libs/markoff-core/tests/tst_foundation_text_anchor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/TextAnchor.h>

using namespace Markoff;

class TstFoundationTextAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_anchors_are_equal() {
        TextAnchor a;
        TextAnchor b;
        QVERIFY(a == b);
    }

    void anchors_with_same_fields_are_equal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 42, 0};
        QVERIFY(a == b);
    }

    void differing_replica_id_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{2, 42, 0};
        QVERIFY(!(a == b));
    }

    void differing_char_value_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 43, 0};
        QVERIFY(!(a == b));
    }

    void differing_bias_makes_unequal() {
        TextAnchor a{1, 42, 0};
        TextAnchor b{1, 42, 1};
        QVERIFY(!(a == b));
    }
};

QTEST_MAIN(TstFoundationTextAnchor)
#include "tst_foundation_text_anchor.moc"
```

- [x] **Step 1.2: Verify the test fails**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_foundation_text_anchor -j 8
```

Expected: build fails with "TextAnchor.h not found" (header doesn't exist yet) or the test target isn't registered.

- [x] **Step 1.3: Add the test target to `tests/CMakeLists.txt`**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_text_anchor tst_foundation_text_anchor.cpp)
add_test(NAME tst_foundation_text_anchor COMMAND tst_foundation_text_anchor)
target_link_libraries(tst_foundation_text_anchor PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_text_anchor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 1.4: Create `TextAnchor.h`**

`libs/markoff-core/include/markoff-foundation/TextAnchor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

/// Opaque view-layer-safe wrapper for a CRDT byte anchor. Same wire-
/// level identity (replicaId + charValue + bias) as
/// CollabText::Crdt::Anchor; conversion via foundation-internal
/// helpers in AnchorConversion.h. Consumers may hold and pass; must
/// NOT inspect, construct from raw fields, or compare except via
/// operator==. All translations go through MarkoffDocument APIs.
struct MARKOFF_FOUNDATION_EXPORT TextAnchor {
    quint16 replicaId = 0;
    quint32 charValue = 0;
    quint8  bias      = 0;   ///< 0 = Left, 1 = Right (matches Crdt::Bias enum order)

    bool operator==(const TextAnchor &) const = default;
};

}  // namespace Markoff
```

- [x] **Step 1.5: Build and run the test**

```bash
cmake --build build-dev --target tst_foundation_text_anchor -j 8
ctest --test-dir build-dev -R tst_foundation_text_anchor --output-on-failure
```

Expected: PASS — all five sub-tests green.

- [x] **Step 1.6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/TextAnchor.h \
        libs/markoff-core/tests/tst_foundation_text_anchor.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add TextAnchor type (no CRDT header dep)"
```

---

## Task 2: `AnchorConversion` internal helpers

Translate `Crdt::Anchor` ↔ `TextAnchor` for foundation-internal use. Adds round-trip tests as part of `tst_foundation_text_anchor`.

**Files:**
- Create: `libs/markoff-core/src/AnchorConversion.h`
- Create: `libs/markoff-core/src/AnchorConversion.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_text_anchor.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`

- [x] **Step 2.1: Write the failing round-trip test**

Append to `tst_foundation_text_anchor.cpp` (inside the class):

```cpp
    void roundtrip_basic_left_bias() {
        const CollabText::Crdt::Anchor a{7, 42, CollabText::Crdt::Bias::Left};
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        QCOMPARE(t.replicaId, quint16{7});
        QCOMPARE(t.charValue, quint32{42});
        QCOMPARE(t.bias, quint8{0});
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(back.replica_id, a.replica_id);
        QCOMPARE(back.char_value, a.char_value);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(a.bias));
    }

    void roundtrip_right_bias() {
        const CollabText::Crdt::Anchor a{99, 1234, CollabText::Crdt::Bias::Right};
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        QCOMPARE(t.bias, quint8{1});
        const CollabText::Crdt::Anchor back = Markoff::Detail::toCrdtAnchor(t);
        QCOMPARE(static_cast<int>(back.bias), static_cast<int>(CollabText::Crdt::Bias::Right));
    }

    void roundtrip_min_sentinel() {
        const auto a = CollabText::Crdt::Anchor::min();
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_min());
    }

    void roundtrip_max_sentinel() {
        const auto a = CollabText::Crdt::Anchor::max();
        const TextAnchor t = Markoff::Detail::toTextAnchor(a);
        const auto back = Markoff::Detail::toCrdtAnchor(t);
        QVERIFY(back.is_max());
    }
```

Also add to the includes section of the test file:

```cpp
#include <crdt/Anchor.h>
#include "AnchorConversion.h"  // tests reach into foundation src/ for internals
```

For the test to find `AnchorConversion.h`, add to its CMake registration:

```cmake
target_include_directories(tst_foundation_text_anchor PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/markoff-core/src)
```

(Place this after the existing `target_link_libraries` for the test.)

- [x] **Step 2.2: Verify test fails to build**

```bash
cmake -S . -B build-dev
cmake --build build-dev --target tst_foundation_text_anchor -j 8
```

Expected: build fails with "AnchorConversion.h not found" or "Markoff::Detail::toTextAnchor not declared".

- [x] **Step 2.3: Create `AnchorConversion.h`**

`libs/markoff-core/src/AnchorConversion.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <crdt/Anchor.h>
#include <markoff-foundation/TextAnchor.h>

namespace Markoff::Detail {

/// CollabText::Crdt::Anchor → Markoff::TextAnchor.
TextAnchor toTextAnchor(const CollabText::Crdt::Anchor &a) noexcept;

/// Markoff::TextAnchor → CollabText::Crdt::Anchor.
CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept;

}  // namespace Markoff::Detail
```

- [x] **Step 2.4: Create `AnchorConversion.cpp`**

`libs/markoff-core/src/AnchorConversion.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnchorConversion.h"

namespace Markoff::Detail {

TextAnchor toTextAnchor(const CollabText::Crdt::Anchor &a) noexcept
{
    TextAnchor t;
    t.replicaId = a.replica_id;
    t.charValue = a.char_value;
    t.bias      = (a.bias == CollabText::Crdt::Bias::Right) ? 1 : 0;
    return t;
}

CollabText::Crdt::Anchor toCrdtAnchor(const TextAnchor &t) noexcept
{
    using CollabText::Crdt::Bias;
    return CollabText::Crdt::Anchor{
        t.replicaId,
        t.charValue,
        t.bias == 1 ? Bias::Right : Bias::Left
    };
}

}  // namespace Markoff::Detail
```

- [x] **Step 2.5: Add to library `CMakeLists.txt`**

Locate the source-list block in `libs/markoff-core/CMakeLists.txt` (the one that lists `*.cpp` files for `markoff_core`) and append:

```cmake
    src/AnchorConversion.cpp
```

(Match existing alphabetical or topical grouping.)

- [x] **Step 2.6: Build and run the test**

```bash
cmake --build build-dev --target tst_foundation_text_anchor -j 8
ctest --test-dir build-dev -R tst_foundation_text_anchor --output-on-failure
```

Expected: all sub-tests PASS, including the four new round-trip tests.

- [x] **Step 2.7: Commit**

```bash
git add libs/markoff-core/src/AnchorConversion.h \
        libs/markoff-core/src/AnchorConversion.cpp \
        libs/markoff-core/tests/tst_foundation_text_anchor.cpp \
        libs/markoff-core/tests/CMakeLists.txt \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): AnchorConversion helpers (Crdt::Anchor <-> TextAnchor)"
```

---

## Task 3: `BlockAnchor` type

Trivial wrapper around `TextAnchor` to give block-identity its own type.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/BlockAnchor.h`
- Create: `libs/markoff-core/tests/tst_foundation_block_anchor.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 3.1: Write failing test**

`libs/markoff-core/tests/tst_foundation_block_anchor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/BlockAnchor.h>

using namespace Markoff;

class TstFoundationBlockAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_blocks_are_equal() {
        BlockAnchor a;
        BlockAnchor b;
        QVERIFY(a == b);
    }

    void blocks_with_same_first_byte_are_equal() {
        BlockAnchor a{TextAnchor{1, 42, 0}};
        BlockAnchor b{TextAnchor{1, 42, 0}};
        QVERIFY(a == b);
    }

    void blocks_with_different_first_byte_unequal() {
        BlockAnchor a{TextAnchor{1, 42, 0}};
        BlockAnchor b{TextAnchor{1, 43, 0}};
        QVERIFY(!(a == b));
    }

    void block_is_distinct_type_from_text_anchor() {
        // Static-assert at compile time that BlockAnchor and TextAnchor
        // are distinct types — protects against the "alias" alternative
        // we explicitly rejected in the spec §10 decision 1.
        static_assert(!std::is_same_v<BlockAnchor, TextAnchor>);
    }
};

QTEST_MAIN(TstFoundationBlockAnchor)
#include "tst_foundation_block_anchor.moc"
```

- [x] **Step 3.2: Add target to test CMakeLists**

Append:

```cmake
add_executable(tst_foundation_block_anchor tst_foundation_block_anchor.cpp)
add_test(NAME tst_foundation_block_anchor COMMAND tst_foundation_block_anchor)
target_link_libraries(tst_foundation_block_anchor PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_block_anchor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 3.3: Verify the test fails**

```bash
cmake --build build-dev --target tst_foundation_block_anchor -j 8
```

Expected: build fails — `BlockAnchor.h` does not exist.

- [x] **Step 3.4: Create `BlockAnchor.h`**

`libs/markoff-core/include/markoff-foundation/BlockAnchor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/TextAnchor.h>

namespace Markoff {

/// Stable identity for a top-level block in a parsed Markoff::Document.
/// Backed by a TextAnchor at the block's first byte (Left bias). Equal
/// iff the underlying first-byte TextAnchors refer to the same
/// character (i.e. share Lamport identity).
struct MARKOFF_FOUNDATION_EXPORT BlockAnchor {
    TextAnchor firstByte;

    bool operator==(const BlockAnchor &) const = default;
};

}  // namespace Markoff
```

- [x] **Step 3.5: Build and test**

```bash
cmake --build build-dev --target tst_foundation_block_anchor -j 8
ctest --test-dir build-dev -R tst_foundation_block_anchor --output-on-failure
```

Expected: PASS.

- [x] **Step 3.6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/BlockAnchor.h \
        libs/markoff-core/tests/tst_foundation_block_anchor.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add BlockAnchor wrapper struct"
```

---

## Task 4: `editSequence()` accessor

Locally-monotonic counter for "has the doc state changed since some prior point". Increments on every state-change op (`applyLocalEdit`, `undo`, `redo`, `applyRemoteOps`, `resetContent`).

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_edit_sequence.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 4.1: Write failing test**

`libs/markoff-core/tests/tst_foundation_edit_sequence.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationEditSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        MarkoffDocument d{1};
        QCOMPARE(d.editSequence(), quint64{0});
    }

    void apply_local_edit_increments() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        QVERIFY(d.editSequence() > seq0);
    }

    void undo_increments() {
        MarkoffDocument d{1};
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        const auto seq1 = d.editSequence();
        d.undo();
        QVERIFY(d.editSequence() > seq1);
    }

    void redo_increments() {
        MarkoffDocument d{1};
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        d.undo();
        const auto seq2 = d.editSequence();
        d.redo();
        QVERIFY(d.editSequence() > seq2);
    }

    void reset_content_increments() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        d.resetContent("hello", Origin::ExternalLoad);
        QVERIFY(d.editSequence() > seq0);
    }

    void monotonic_under_burst() {
        MarkoffDocument d{1};
        quint64 prev = d.editSequence();
        for (int i = 0; i < 20; ++i) {
            MarkoffEdit e;
            e.oldStart = static_cast<quint32>(i);
            e.oldEnd   = static_cast<quint32>(i);
            e.newText  = "a";
            d.applyLocalEdit({e});
            const auto cur = d.editSequence();
            QVERIFY(cur > prev);
            prev = cur;
        }
    }
};

QTEST_MAIN(TstFoundationEditSequence)
#include "tst_foundation_edit_sequence.moc"
```

- [x] **Step 4.2: Register the test executable**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_edit_sequence tst_foundation_edit_sequence.cpp)
add_test(NAME tst_foundation_edit_sequence COMMAND tst_foundation_edit_sequence)
target_link_libraries(tst_foundation_edit_sequence PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_edit_sequence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 4.3: Verify the test fails to build**

```bash
cmake --build build-dev --target tst_foundation_edit_sequence -j 8
```

Expected: build fails — `MarkoffDocument::editSequence` not declared.

- [x] **Step 4.4: Add public accessor declaration**

In `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`, add immediately before the `// ===== Local writes =====` line (around line 56-57):

```cpp
    // ===== Sequence accessors (CRDT-free, public-boundary friendly) =====
    /// Locally-monotonic edit-sequence number that increments on every
    /// state-change operation (applyLocalEdit, undo, redo, applyRemoteOps,
    /// resetContent). Used for dirty-tracking ("has the doc changed since
    /// the last save?") without holding a Crdt::Global. See spec §10
    /// decision 8.
    quint64 editSequence() const noexcept;
```

- [x] **Step 4.5: Add private state**

In `libs/markoff-core/src/MarkoffDocumentPrivate.h`, find the `Private` struct (or class) declaration. Add:

```cpp
    quint64 editSequence = 0;   ///< Bumps on every state-change op.
```

(Place it next to other counter-style fields if any, or just before `latestParse`.)

- [x] **Step 4.6: Implement the accessor and bump points**

In `libs/markoff-core/src/MarkoffDocument.cpp`:

Add (place near `version()`):

```cpp
quint64 MarkoffDocument::editSequence() const noexcept
{
    return d->editSequence;
}
```

In each of the following methods, **at the beginning of the body**, add `++d->editSequence;`:
- `applyLocalEdit`
- `undo` (only if it actually performs an undo — check for the `if (op_or_nullopt)` guard if present, and bump only when non-empty; if simpler, bump unconditionally and document that "no-op undo still counts as an op for dirty-tracking purposes" — pick whichever matches existing semantics)
- `redo` (same as undo)
- `applyRemoteOps`
- `resetContent`

Pseudocode of the simplest pattern:

```cpp
CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &edits)
{
    ++d->editSequence;
    // ... existing body unchanged ...
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    auto op = /* existing undo body */;
    if (op.has_value()) {
        ++d->editSequence;
    }
    return op;
}
```

For `undo`/`redo`: read the existing implementation in `MarkoffDocument.cpp` first; bump editSequence iff the call actually changed state.

- [x] **Step 4.7: Build and run**

```bash
cmake --build build-dev --target tst_foundation_edit_sequence -j 8
ctest --test-dir build-dev -R tst_foundation_edit_sequence --output-on-failure
```

Expected: all six sub-tests PASS.

- [x] **Step 4.8: Run the full foundation test set to confirm no regression**

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_)' --output-on-failure -j 8
```

Expected: all green.

- [x] **Step 4.9: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_foundation_edit_sequence.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add editSequence() for public-boundary dirty-tracking"
```

---

## Task 5: `parseSequence()` accessor

Locally-monotonic counter for "this is the i-th parse to return on this MarkoffDocument instance". Used by view-layer code for parse ordering. Bumped in the lambda that relays the worker's `parsed` to the public `parseUpdated`. The signal-shape change (adding `parseSequence` and `QList<BlockAnchor>` to the signal) lands in Task 7; this task only adds the accessor and its underlying state.

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_parse_sequence.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 5.1: Write failing test**

`libs/markoff-core/tests/tst_foundation_parse_sequence.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParseSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        MarkoffDocument d{1};
        QCOMPARE(d.parseSequence(), quint64{0});
    }

    void parse_sequence_increments_after_first_parse() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);
        MarkoffEdit e; e.oldStart = 0; e.oldEnd = 0; e.newText = "hello";
        d.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
        QVERIFY(d.parseSequence() > 0);
    }

    void parse_sequence_strictly_monotonic_across_three_parses() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);
        for (int i = 0; i < 3; ++i) {
            MarkoffEdit e;
            e.oldStart = static_cast<quint32>(i);
            e.oldEnd   = static_cast<quint32>(i);
            e.newText  = "a";
            d.applyLocalEdit({e});
        }
        // Wait until at least three parseUpdated emissions arrived.
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, 3000);
        QVERIFY(d.parseSequence() >= 3);
    }
};

QTEST_MAIN(TstFoundationParseSequence)
#include "tst_foundation_parse_sequence.moc"
```

- [x] **Step 5.2: Register the test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_parse_sequence tst_foundation_parse_sequence.cpp)
add_test(NAME tst_foundation_parse_sequence COMMAND tst_foundation_parse_sequence)
target_link_libraries(tst_foundation_parse_sequence PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_parse_sequence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 5.3: Verify failure**

```bash
cmake --build build-dev --target tst_foundation_parse_sequence -j 8
```

Expected: build fails — `MarkoffDocument::parseSequence` undefined.

- [x] **Step 5.4: Add accessor declaration**

In `MarkoffDocument.h`, just below the `editSequence` declaration added in Task 4:

```cpp
    /// Locally-monotonic parse-sequence number for the most recent parse
    /// delivered via parseUpdated. View-layer code uses this for parse-
    /// ordering ("is this a newer parse than what I rendered?") without
    /// holding a Crdt::Global. Decoupled from the CRDT version vector.
    /// See spec §10 decision 3.
    quint64 parseSequence() const noexcept;
```

- [x] **Step 5.5: Add private state**

In `MarkoffDocumentPrivate.h`, alongside the `editSequence` field added in Task 4:

```cpp
    quint64 parseSequence = 0;  ///< Bumps each time parseUpdated is emitted.
```

- [x] **Step 5.6: Implement accessor + bump in the relay lambda**

In `libs/markoff-core/src/MarkoffDocument.cpp`, add the accessor near `editSequence`:

```cpp
quint64 MarkoffDocument::parseSequence() const noexcept
{
    return d->parseSequence;
}
```

Then update the constructor's parse-relay lambda. Currently it's:

```cpp
QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                 this, [this](const Markoff::Document *p) {
                     d->latestParse.reset(p);
                     Q_EMIT parseUpdated(p, version());
                 });
```

Change to:

```cpp
QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                 this, [this](const Markoff::Document *p) {
                     d->latestParse.reset(p);
                     ++d->parseSequence;
                     // Signal-shape change (parseSequence + QList<BlockAnchor>)
                     // lands in Task 7; for now keep the legacy emit and only
                     // bump parseSequence here.
                     Q_EMIT parseUpdated(p, version());
                 });
```

- [x] **Step 5.7: Build and run the new test**

```bash
cmake --build build-dev --target tst_foundation_parse_sequence -j 8
ctest --test-dir build-dev -R tst_foundation_parse_sequence --output-on-failure
```

Expected: PASS — sequence starts at 0, increments after each parse return.

- [x] **Step 5.8: Confirm full foundation test set still green**

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_)' --output-on-failure -j 8
```

- [x] **Step 5.9: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_foundation_parse_sequence.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): add parseSequence() (signal-shape change comes in T7)"
```

---

## Task 6: `textAnchorAt(byteOffset, rightBias)` + `resolveTextAnchor`

The TextAnchor-typed companions to the existing CRDT-typed `anchorAt` / `resolveAnchor`.

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_text_anchor.cpp`

- [x] **Step 6.1: Write the failing round-trip test**

Append to `tst_foundation_text_anchor.cpp` (inside the class):

```cpp
    void document_textAnchorAt_resolves_back_to_same_byte_with_left_bias() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::ExternalLoad);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ false);
        QCOMPARE(doc.resolveTextAnchor(t), quint32{6});
    }

    void document_textAnchorAt_resolves_back_to_same_byte_with_right_bias() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::ExternalLoad);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ true);
        QCOMPARE(doc.resolveTextAnchor(t), quint32{6});
    }

    void document_textAnchorAt_left_bias_survives_insert_before() {
        MarkoffDocument doc{1};
        doc.resetContent("hello world", Origin::ExternalLoad);
        const TextAnchor t = doc.textAnchorAt(6, /*rightBias*/ false);
        // Insert "X" at position 0 — anchor at byte 6 should now resolve to 7.
        MarkoffEdit e; e.oldStart = 0; e.oldEnd = 0; e.newText = "X";
        doc.applyLocalEdit({e});
        QCOMPARE(doc.resolveTextAnchor(t), quint32{7});
    }
```

Also add to includes if not already there:

```cpp
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
```

- [x] **Step 6.2: Verify failure**

```bash
cmake --build build-dev --target tst_foundation_text_anchor -j 8
```

Expected: build fails — `textAnchorAt`/`resolveTextAnchor` not declared on `MarkoffDocument`.

- [x] **Step 6.3: Add declarations**

In `MarkoffDocument.h`, find the existing `// ===== Anchors =====` section (around line 77-80) and add immediately after the existing `resolveAnchor` declaration:

```cpp
    /// TextAnchor-typed companion to anchorAt(quint32, Crdt::Bias). Same
    /// semantics; view-layer-friendly because it doesn't require including
    /// <crdt/Anchor.h>.
    TextAnchor textAnchorAt(quint32 byteOffset, bool rightBias) const;

    /// Resolve a TextAnchor to its current byte offset. Companion to
    /// resolveAnchor(const Crdt::Anchor &).
    quint32 resolveTextAnchor(const TextAnchor &) const;
```

Also add to the top-of-header `#include` block:

```cpp
#include <markoff-foundation/TextAnchor.h>
```

- [x] **Step 6.4: Implement in `MarkoffDocument.cpp`**

Place near the existing `anchorAt` / `resolveAnchor` implementations. Add at top of file:

```cpp
#include "AnchorConversion.h"
```

Then:

```cpp
TextAnchor MarkoffDocument::textAnchorAt(quint32 byteOffset, bool rightBias) const
{
    using CollabText::Crdt::Bias;
    return Detail::toTextAnchor(
        anchorAt(byteOffset, rightBias ? Bias::Right : Bias::Left));
}

quint32 MarkoffDocument::resolveTextAnchor(const TextAnchor &t) const
{
    return resolveAnchor(Detail::toCrdtAnchor(t));
}
```

- [x] **Step 6.5: Build and run**

```bash
cmake --build build-dev --target tst_foundation_text_anchor -j 8
ctest --test-dir build-dev -R tst_foundation_text_anchor --output-on-failure
```

Expected: all sub-tests PASS, including the three new round-trip tests.

- [x] **Step 6.6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_foundation_text_anchor.cpp
git commit -m "feat(foundation): add textAnchorAt + resolveTextAnchor on MarkoffDocument"
```

---

## Task 7: `TopLevelBlockScanner` + `BlockAnchorComputation` + `parseUpdated` shape change

The meat of the spec. Adds the foundation-internal scanner that enumerates top-level block byte ranges, the `BlockAnchorComputation` helper that maps those ranges to BlockAnchors, and changes the `parseUpdated` signal payload to ship the anchors plus a `quint64 parseSequence`.

**Files:**
- Create: `libs/markoff-core/src/TopLevelBlockScanner.h`
- Create: `libs/markoff-core/src/TopLevelBlockScanner.cpp`
- Create: `libs/markoff-core/src/BlockAnchorComputation.h`
- Create: `libs/markoff-core/src/BlockAnchorComputation.cpp`
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_top_level_block_scanner.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_block_anchor_compute.cpp`
- Create: `libs/markoff-core/tests/fixtures/block_scanner_corpus/` with several `.md` fixtures
- Modify: `libs/markoff-core/tests/CMakeLists.txt`
- Modify: `libs/markoff-core/CMakeLists.txt`

### Sub-task 7A: TopLevelBlockScanner

The scanner mirrors the algorithm in `libs/markoff-view-qml/src/BlockWalker.cpp` lines 60-160 (line-based scan over the source: blank-line separator; fenced code blocks captured as one block from open-fence through close-fence). Output is a list of half-open `[startByte, endByte)` ranges in body coordinates (caller adds frontmatter offset if present).

- [x] **Step 7A.1: Write failing test for TopLevelBlockScanner**

`libs/markoff-core/tests/tst_foundation_top_level_block_scanner.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "TopLevelBlockScanner.h"

using namespace Markoff::Detail;

class TstFoundationTopLevelBlockScanner : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_input_yields_no_blocks() {
        const auto ranges = scanTopLevelBlockRanges(QByteArray{});
        QCOMPARE(ranges.size(), 0);
    }

    void single_paragraph() {
        const QByteArray src = "hello world";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{11});
    }

    void two_paragraphs_separated_by_blank_line() {
        const QByteArray src = "p1\n\np2";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{2});  // "p1"
        QCOMPARE(ranges[1].startByte, quint32{4});
        QCOMPARE(ranges[1].endByte,   quint32{6});  // "p2"
    }

    void fenced_code_block_kept_as_single_block() {
        const QByteArray src = "```\nint x;\n```";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{14});
    }

    void heading_then_paragraph_two_blocks() {
        const QByteArray src = "# Heading\n\nparagraph";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 2);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{9});   // "# Heading"
        QCOMPARE(ranges[1].startByte, quint32{11});  // after "\n\n"
        QCOMPARE(ranges[1].endByte,   quint32{20});
    }

    void leading_blank_lines_skipped() {
        const QByteArray src = "\n\nhello";
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{2});
        QCOMPARE(ranges[0].endByte,   quint32{7});
    }

    void utf8_multibyte_paragraph() {
        // "héllo" — é is 2 bytes in UTF-8 (0xC3 0xA9). Total 6 bytes.
        const QByteArray src = QByteArray::fromRawData("h\xC3\xA9llo", 6);
        const auto ranges = scanTopLevelBlockRanges(src);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges[0].startByte, quint32{0});
        QCOMPARE(ranges[0].endByte,   quint32{6});
    }
};

QTEST_MAIN(TstFoundationTopLevelBlockScanner)
#include "tst_foundation_top_level_block_scanner.moc"
```

- [x] **Step 7A.2: Register the test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_top_level_block_scanner tst_foundation_top_level_block_scanner.cpp)
add_test(NAME tst_foundation_top_level_block_scanner COMMAND tst_foundation_top_level_block_scanner)
target_link_libraries(tst_foundation_top_level_block_scanner PRIVATE Qt6::Test markoff_core)
target_include_directories(tst_foundation_top_level_block_scanner PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/markoff-core/src)
set_tests_properties(tst_foundation_top_level_block_scanner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 7A.3: Verify build fails**

```bash
cmake --build build-dev --target tst_foundation_top_level_block_scanner -j 8
```

Expected: build fails — `TopLevelBlockScanner.h` not found.

- [x] **Step 7A.4: Create `TopLevelBlockScanner.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

namespace Markoff::Detail {

/// Half-open byte range [startByte, endByte) in UTF-8 body coordinates.
struct BlockByteRange {
    quint32 startByte = 0;
    quint32 endByte   = 0;
};

/// Enumerate top-level block byte ranges in a UTF-8 markdown body.
///
/// The algorithm mirrors libs/markoff-view-qml/src/BlockWalker.cpp:
///   - Skip leading blank lines.
///   - A fenced code block (line starting with ``` or ~~~ of three or
///     more) is one block, from the opening fence through the closing
///     fence (or to EOF if unclosed).
///   - Otherwise, a block is a run of contiguous non-blank lines,
///     terminated by a blank line or EOF.
///
/// The returned ranges are non-overlapping, in source order, and
/// strictly within `[0, body.size())`. They cover the *block content*
/// only; the inter-block blank-line separators are NOT included.
///
/// This scanner is foundation-internal. v0 maintains algorithmic
/// parity with view-qml's BlockWalker by convention; convergence is
/// asserted via tst_foundation_top_level_block_scanner_convergence
/// (Task 7A.7). Future unification (factoring the scanner into a
/// shared library) is tracked in the live-editing plan.
QList<BlockByteRange> scanTopLevelBlockRanges(const QByteArray &body);

}  // namespace Markoff::Detail
```

- [x] **Step 7A.5: Create `TopLevelBlockScanner.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TopLevelBlockScanner.h"

namespace Markoff::Detail {

namespace {

bool isBlankLine(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    for (qsizetype i = lineStart; i < lineEnd; ++i) {
        const char c = body[i];
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

/// Returns true if the line at [lineStart, lineEnd) opens a fenced
/// code block — i.e. starts with three or more backticks or tildes
/// (after up to three leading spaces).
bool isFenceOpenOrClose(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    qsizetype i = lineStart;
    int leadingSpaces = 0;
    while (i < lineEnd && body[i] == ' ' && leadingSpaces < 3) {
        ++i; ++leadingSpaces;
    }
    if (i >= lineEnd) return false;
    const char c = body[i];
    if (c != '`' && c != '~') return false;
    int run = 0;
    while (i < lineEnd && body[i] == c) { ++i; ++run; }
    return run >= 3;
}

/// Scan from `cursor` to the start of the next line (one past '\n', or end).
/// Returns the line-end (exclusive of the '\n'), and advances `cursor`
/// to one past the '\n' (or to body.size() if EOF).
qsizetype findLineEnd(const QByteArray &body, qsizetype lineStart, qsizetype &cursor)
{
    qsizetype i = lineStart;
    while (i < body.size() && body[i] != '\n') ++i;
    cursor = (i < body.size()) ? (i + 1) : body.size();
    return i;
}

}  // namespace

QList<BlockByteRange> scanTopLevelBlockRanges(const QByteArray &body)
{
    QList<BlockByteRange> result;
    qsizetype cursor = 0;

    while (cursor < body.size()) {
        // Skip blank lines.
        qsizetype lineStart = cursor;
        qsizetype lineEnd   = findLineEnd(body, lineStart, cursor);
        while (isBlankLine(body, lineStart, lineEnd) && cursor < body.size()) {
            lineStart = cursor;
            lineEnd   = findLineEnd(body, lineStart, cursor);
        }
        if (lineStart >= body.size()) break;
        if (isBlankLine(body, lineStart, lineEnd)) break;  // trailing blank, EOF

        BlockByteRange range;
        range.startByte = static_cast<quint32>(lineStart);

        if (isFenceOpenOrClose(body, lineStart, lineEnd)) {
            // Fenced code block. Read through close-fence or EOF.
            qsizetype lastIncludedEnd = lineEnd;
            while (cursor < body.size()) {
                const qsizetype nextStart = cursor;
                const qsizetype nextEnd   = findLineEnd(body, nextStart, cursor);
                lastIncludedEnd = nextEnd;
                if (isFenceOpenOrClose(body, nextStart, nextEnd)) break;
            }
            range.endByte = static_cast<quint32>(lastIncludedEnd);
            result.append(range);
            continue;
        }

        // Non-fence: collect lines until next blank line or EOF.
        qsizetype lastIncludedEnd = lineEnd;
        while (cursor < body.size()) {
            const qsizetype nextStart = cursor;
            const qsizetype nextEnd   = findLineEnd(body, nextStart, cursor);
            if (isBlankLine(body, nextStart, nextEnd)) break;
            lastIncludedEnd = nextEnd;
        }
        range.endByte = static_cast<quint32>(lastIncludedEnd);
        result.append(range);
    }

    return result;
}

}  // namespace Markoff::Detail
```

- [x] **Step 7A.6: Add to library `CMakeLists.txt`**

In `libs/markoff-core/CMakeLists.txt`, append `src/TopLevelBlockScanner.cpp` to the source list.

- [x] **Step 7A.7: Build and run the scanner test**

```bash
cmake --build build-dev --target tst_foundation_top_level_block_scanner -j 8
ctest --test-dir build-dev -R tst_foundation_top_level_block_scanner --output-on-failure
```

Expected: all seven sub-tests PASS.

- [x] **Step 7A.8: Commit the scanner**

```bash
git add libs/markoff-core/src/TopLevelBlockScanner.h \
        libs/markoff-core/src/TopLevelBlockScanner.cpp \
        libs/markoff-core/tests/tst_foundation_top_level_block_scanner.cpp \
        libs/markoff-core/tests/CMakeLists.txt \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): add TopLevelBlockScanner (BlockWalker-mirror)"
```

### Sub-task 7B: BlockAnchorComputation

A thin helper: given the parsed `Markoff::Document` and the `MarkoffDocument`, produce a `QList<BlockAnchor>` and a parallel `QList<BlockByteRange>`.

- [x] **Step 7B.1: Write failing test**

`libs/markoff-core/tests/tst_foundation_block_anchor_compute.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationBlockAnchorCompute : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_updated_payload_carries_block_anchors_for_three_paragraphs() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("a\n\nb\n\nc", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));

        // The new signal signature is (Document*, quint64, QList<BlockAnchor>).
        // Extract the last emission's BlockAnchor list and assert.
        const auto &args = spy.last();
        QCOMPARE(args.size(), 3);
        const auto anchors = args.at(2).value<QList<BlockAnchor>>();
        QCOMPARE(anchors.size(), 3);
        // Each anchor's firstByte resolves to the corresponding block's
        // start byte (0, 3, 6 in the body "a\n\nb\n\nc").
        QCOMPARE(doc.resolveTextAnchor(anchors[0].firstByte), quint32{0});
        QCOMPARE(doc.resolveTextAnchor(anchors[1].firstByte), quint32{3});
        QCOMPARE(doc.resolveTextAnchor(anchors[2].firstByte), quint32{6});
    }

    void edit_within_block_keeps_block_anchor_identity() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("first\n\nsecond", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto firstAnchors = spy.last().at(2).value<QList<BlockAnchor>>();
        QCOMPARE(firstAnchors.size(), 2);
        const BlockAnchor secondBlockBefore = firstAnchors[1];

        // Insert a character in the *first* block. Second block's identity
        // should be preserved.
        spy.clear();
        MarkoffEdit e; e.oldStart = 5; e.oldEnd = 5; e.newText = "X";
        doc.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
        const auto afterAnchors = spy.last().at(2).value<QList<BlockAnchor>>();
        QCOMPARE(afterAnchors.size(), 2);
        QCOMPARE(afterAnchors[1], secondBlockBefore);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorCompute)
#include "tst_foundation_block_anchor_compute.moc"
```

- [x] **Step 7B.2: Register the test**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_block_anchor_compute tst_foundation_block_anchor_compute.cpp)
add_test(NAME tst_foundation_block_anchor_compute COMMAND tst_foundation_block_anchor_compute)
target_link_libraries(tst_foundation_block_anchor_compute PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_block_anchor_compute PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 7B.3: Verify build fails**

```bash
cmake --build build-dev --target tst_foundation_block_anchor_compute -j 8
```

Expected: build fails — `parseUpdated` signature mismatch (still has `Crdt::Global atVersion`, not `quint64 + QList<BlockAnchor>`); `BlockAnchor` not registered as Qt metatype; `resolveTextAnchor` of `BlockAnchor::firstByte` is fine but the QVariant retrieval of `QList<BlockAnchor>` fails.

- [x] **Step 7B.4: Add `Q_DECLARE_METATYPE(Markoff::BlockAnchor)`**

In `libs/markoff-core/include/markoff-foundation/BlockAnchor.h`, after the namespace closes:

```cpp
Q_DECLARE_METATYPE(Markoff::BlockAnchor)
```

Add `#include <QMetaType>` to the header.

- [x] **Step 7B.5: Create `BlockAnchorComputation.h`**

`libs/markoff-core/src/BlockAnchorComputation.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff-foundation/BlockAnchor.h>
#include "TopLevelBlockScanner.h"

namespace Markoff {
class MarkoffDocument;
}  // namespace Markoff

namespace Markoff::Detail {

/// For each top-level block in `body`, produce a BlockAnchor at its
/// first byte (Left bias), using `doc` as the CRDT-buffer source for
/// anchor lookup. Ranges are returned in parallel for use by
/// MarkoffDocument's blockAt / offsetInBlock / blockByteRange APIs.
struct BlockAnchorBundle {
    QList<BlockAnchor>    anchors;
    QList<BlockByteRange> ranges;
};

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                       const QByteArray &body);

}  // namespace Markoff::Detail
```

- [x] **Step 7B.6: Create `BlockAnchorComputation.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockAnchorComputation.h"

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::Detail {

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                       const QByteArray &body)
{
    BlockAnchorBundle bundle;
    const QList<BlockByteRange> ranges = scanTopLevelBlockRanges(body);
    bundle.anchors.reserve(ranges.size());
    bundle.ranges = ranges;
    for (const BlockByteRange &r : ranges) {
        const TextAnchor t = doc.textAnchorAt(r.startByte, /*rightBias*/ false);
        bundle.anchors.append(BlockAnchor{t});
    }
    return bundle;
}

}  // namespace Markoff::Detail
```

- [x] **Step 7B.7: Add to library CMakeLists**

Append `src/BlockAnchorComputation.cpp` to the source list in `libs/markoff-core/CMakeLists.txt`.

- [x] **Step 7B.8: Update private state**

In `MarkoffDocumentPrivate.h`, add:

```cpp
    QList<Markoff::BlockAnchor>           latestBlockAnchors;
    QList<Markoff::Detail::BlockByteRange> latestBlockRanges;
```

(Add the include `#include "BlockAnchorComputation.h"` at the top of the private header.)

- [x] **Step 7B.9: Change `parseUpdated` signal signature**

In `MarkoffDocument.h`, replace:

```cpp
    void parseUpdated(const Markoff::Document *parsed, CollabText::Crdt::Global atVersion);
```

with:

```cpp
    void parseUpdated(const Markoff::Document *parsed,
                      quint64 parseSequence,
                      QList<Markoff::BlockAnchor> blockAnchors);
```

Add `#include <markoff-foundation/BlockAnchor.h>` near the top of the header (it's already implied via TextAnchor.h but make it explicit for the signal type).

- [x] **Step 7B.10: Update the relay lambda**

In `MarkoffDocument.cpp`, the constructor lambda (modified in Task 5) becomes:

```cpp
MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
{
    // Runtime registration so QList<BlockAnchor> survives any cross-thread
    // QueuedConnection slot. Q_DECLARE_METATYPE on its own is compile-time
    // only; queued connections need this.
    qRegisterMetaType<Markoff::BlockAnchor>("Markoff::BlockAnchor");
    qRegisterMetaType<QList<Markoff::BlockAnchor>>("QList<Markoff::BlockAnchor>");

    QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                     this, [this](const Markoff::Document *p) {
                         d->latestParse.reset(p);
                         ++d->parseSequence;

                         // Compute BlockAnchors against the CURRENT CRDT buffer.
                         // One-cycle staleness is acceptable per spec §3.
                         const QByteArray body = toMarkdownUtf8();
                         auto bundle = Markoff::Detail::computeBlockAnchors(*this, body);
                         d->latestBlockAnchors = bundle.anchors;
                         d->latestBlockRanges  = std::move(bundle.ranges);

                         Q_EMIT parseUpdated(p, d->parseSequence, d->latestBlockAnchors);
                     });
}
```

Add `#include "BlockAnchorComputation.h"` near the existing include of `MarkoffDocumentPrivate.h`.

- [x] **Step 7B.11: Update existing parse-relay subscribers in foundation src/**

Search for foundation-internal subscribers of `parseUpdated` whose signal-slot connections need updating:

```bash
grep -rn "parseUpdated\|onParseUpdated" libs/markoff-core/src/ libs/markoff-core/include/
```

For each match, update slots that take `(const Markoff::Document *, CollabText::Crdt::Global)` to take `(const Markoff::Document *, quint64, QList<Markoff::BlockAnchor>)`. Foundation-internal subscribers typically don't care about the new arguments — they just ignore them, e.g.:

```cpp
void Foo::onParseUpdated(const Markoff::Document *parsed,
                         quint64 /*parseSequence*/,
                         const QList<Markoff::BlockAnchor> & /*blockAnchors*/);
```

(Pass `QList<Markoff::BlockAnchor>` by value or const-ref to match Qt signal/slot conventions; Qt copies on QueuedConnection regardless.)

If an existing test (like the existing `tst_foundation_markoff_document` or `tst_foundation_parse_pool`) connects to `parseUpdated`, update its lambda to take three arguments instead of two.

- [x] **Step 7B.12: Build, fixing any signature-mismatch errors**

```bash
cmake --build build-dev -j 8 2>&1 | tail -50
```

Expected: build succeeds after slot signatures align. If there are errors, fix the call sites and re-run.

- [x] **Step 7B.13: Run the BlockAnchor compute tests**

```bash
ctest --test-dir build-dev -R tst_foundation_block_anchor_compute --output-on-failure
```

Expected: PASS — both sub-tests green.

- [x] **Step 7B.14: Run the parse-sequence test**

```bash
ctest --test-dir build-dev -R tst_foundation_parse_sequence --output-on-failure
```

Expected: still PASS (it doesn't access the new args, just verifies parseSequence increments).

- [x] **Step 7B.15: Run the full foundation test set**

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_)' --output-on-failure -j 8
```

Expected: all green. If any pre-existing test connected to `parseUpdated` with the old signature, the slot signature update in 7B.11 should have fixed it; if not, fix any remaining call sites and re-run.

- [x] **Step 7B.16: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/BlockAnchor.h \
        libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/BlockAnchorComputation.h \
        libs/markoff-core/src/BlockAnchorComputation.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/tst_foundation_block_anchor_compute.cpp \
        libs/markoff-core/tests/CMakeLists.txt
# (also any other foundation files updated in 7B.11)
git commit -m "feat(foundation): parseUpdated ships BlockAnchor list + parseSequence"
```

---

## Task 8: Block-aware queries on `MarkoffDocument`

`blockAt`, `offsetInBlock`, `textAnchorAt(BlockAnchor, offset, bias)`, `blockAnchorAt(int)`, `blockByteRange`. All read from the cached `latestBlockAnchors` / `latestBlockRanges` snapshot.

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_block_anchor_queries.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 8.1: Write failing test**

`libs/markoff-core/tests/tst_foundation_block_anchor_queries.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

namespace {

void waitForParse(MarkoffDocument &doc, QSignalSpy &spy)
{
    QVERIFY(spy.wait(2000));
}

}  // namespace

class TstFoundationBlockAnchorQueries : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void blockAnchorAt_returns_anchor_for_each_top_level_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("p1\n\np2\n\np3", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));

        const auto a0 = doc.blockAnchorAt(0);
        const auto a1 = doc.blockAnchorAt(1);
        const auto a2 = doc.blockAnchorAt(2);
        QVERIFY(a0.has_value());
        QVERIFY(a1.has_value());
        QVERIFY(a2.has_value());
        QCOMPARE(doc.resolveTextAnchor(a0->firstByte), quint32{0});
        QCOMPARE(doc.resolveTextAnchor(a1->firstByte), quint32{4});
        QCOMPARE(doc.resolveTextAnchor(a2->firstByte), quint32{8});
    }

    void blockAnchorAt_out_of_range_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("only", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        QVERIFY(!doc.blockAnchorAt(1).has_value());
        QVERIFY(!doc.blockAnchorAt(-1).has_value());
    }

    void blockByteRange_returns_range_for_known_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("hello\n\nworld", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto block0 = doc.blockAnchorAt(0).value();
        const auto rng = doc.blockByteRange(block0);
        QVERIFY(rng.has_value());
        QCOMPARE(rng->first,  quint32{0});
        QCOMPARE(rng->second, quint32{5});  // "hello"
    }

    void blockAt_inside_first_block_returns_first_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const TextAnchor mid = doc.textAnchorAt(2, /*rightBias*/ false);
        const auto block = doc.blockAt(mid);
        QVERIFY(block.has_value());
        QCOMPARE(*block, doc.blockAnchorAt(0).value());
    }

    void blockAt_inside_separator_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aa\n\nbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        // Byte 2 is the first '\n' — which is one past the end of block 0.
        // The blank-line region is [2, 4). blockAt of byte 3 should be nullopt.
        const TextAnchor inSeparator = doc.textAnchorAt(3, /*rightBias*/ false);
        QVERIFY(!doc.blockAt(inSeparator).has_value());
    }

    void blockAt_past_last_block_returns_nullopt() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("only\n", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const TextAnchor pastEnd = doc.textAnchorAt(5, /*rightBias*/ true);
        QVERIFY(!doc.blockAt(pastEnd).has_value());
    }

    void offsetInBlock_returns_byte_offset_within_block() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor mid = doc.textAnchorAt(8, /*rightBias*/ false);  // "bb|bb"
        QCOMPARE(doc.offsetInBlock(block1, mid), 2);
    }

    void offsetInBlock_clamps_below_block_to_zero() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor before = doc.textAnchorAt(0, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block1, before), 0);
    }

    void offsetInBlock_clamps_past_block_to_block_length() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto block0 = doc.blockAnchorAt(0).value();
        const TextAnchor pastBlock = doc.textAnchorAt(8, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block0, pastBlock), 4);
    }

    void block_local_textAnchorAt_round_trips_via_offsetInBlock() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto block1 = doc.blockAnchorAt(1).value();
        const TextAnchor t = doc.textAnchorAt(block1, /*offset*/ 3, /*rightBias*/ false);
        QCOMPARE(doc.offsetInBlock(block1, t), 3);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorQueries)
#include "tst_foundation_block_anchor_queries.moc"
```

- [x] **Step 8.2: Register the test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_block_anchor_queries tst_foundation_block_anchor_queries.cpp)
add_test(NAME tst_foundation_block_anchor_queries COMMAND tst_foundation_block_anchor_queries)
target_link_libraries(tst_foundation_block_anchor_queries PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_block_anchor_queries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 8.3: Verify build fails**

```bash
cmake --build build-dev --target tst_foundation_block_anchor_queries -j 8
```

Expected: build fails — `blockAnchorAt`, `blockByteRange`, `blockAt`, `offsetInBlock`, `textAnchorAt(BlockAnchor, ...)` not declared.

- [x] **Step 8.4: Add declarations to `MarkoffDocument.h`**

After the `resolveTextAnchor` declaration added in Task 6, add:

```cpp
    // ===== Block-aware queries =====
    /// Returns the BlockAnchor for the top-level block at index `i` in
    /// the most-recent parse. Out-of-range returns std::nullopt.
    std::optional<BlockAnchor> blockAnchorAt(int blockIndex) const;

    /// Returns the byte range [start, end) of the BlockAnchor's block in
    /// the current parse. End is exclusive of any structural separator
    /// to the next block (parser/scanner-reported AST node range).
    std::optional<std::pair<quint32, quint32>>
        blockByteRange(const BlockAnchor &) const;

    /// Returns the BlockAnchor for the top-level block containing the
    /// given TextAnchor's resolved byte position. Returns std::nullopt
    /// if the resolved byte falls outside any top-level block (e.g.
    /// inside an inter-block separator, before the first block, or
    /// past the last block).
    std::optional<BlockAnchor> blockAt(const TextAnchor &) const;

    /// Returns the offset (in UTF-8 bytes) of the given TextAnchor's
    /// resolved byte relative to the BlockAnchor's first byte. Clamps:
    /// resolved-byte below block-start returns 0; resolved-byte past
    /// block-end returns block byte length.
    int offsetInBlock(const BlockAnchor &, const TextAnchor &) const;

    /// Returns a TextAnchor at `offset` UTF-8 bytes from the
    /// BlockAnchor's first-byte position. Block-local companion to
    /// textAnchorAt(quint32, bool).
    TextAnchor textAnchorAt(const BlockAnchor &, int offset, bool rightBias) const;
```

Also add `#include <optional>` if not present, and `#include <utility>` for `std::pair`.

- [x] **Step 8.5: Implement in `MarkoffDocument.cpp`**

```cpp
std::optional<BlockAnchor> MarkoffDocument::blockAnchorAt(int blockIndex) const
{
    if (blockIndex < 0) return std::nullopt;
    if (blockIndex >= d->latestBlockAnchors.size()) return std::nullopt;
    return d->latestBlockAnchors.at(blockIndex);
}

std::optional<std::pair<quint32, quint32>>
MarkoffDocument::blockByteRange(const BlockAnchor &b) const
{
    for (int i = 0; i < d->latestBlockAnchors.size(); ++i) {
        if (d->latestBlockAnchors[i] == b) {
            const auto &r = d->latestBlockRanges[i];
            return std::make_pair(r.startByte, r.endByte);
        }
    }
    return std::nullopt;
}

std::optional<BlockAnchor> MarkoffDocument::blockAt(const TextAnchor &t) const
{
    const quint32 byte = resolveTextAnchor(t);
    for (int i = 0; i < d->latestBlockRanges.size(); ++i) {
        const auto &r = d->latestBlockRanges[i];
        if (byte >= r.startByte && byte < r.endByte) {
            return d->latestBlockAnchors[i];
        }
    }
    return std::nullopt;
}

int MarkoffDocument::offsetInBlock(const BlockAnchor &b, const TextAnchor &t) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return 0;
    const quint32 byte = resolveTextAnchor(t);
    if (byte <= rng->first)  return 0;
    if (byte >= rng->second) return static_cast<int>(rng->second - rng->first);
    return static_cast<int>(byte - rng->first);
}

TextAnchor MarkoffDocument::textAnchorAt(const BlockAnchor &b,
                                         int offset,
                                         bool rightBias) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return TextAnchor{};
    const int clamped = std::max(0, std::min(offset,
        static_cast<int>(rng->second - rng->first)));
    return textAnchorAt(rng->first + static_cast<quint32>(clamped), rightBias);
}
```

- [x] **Step 8.6: Build and run**

```bash
cmake --build build-dev --target tst_foundation_block_anchor_queries -j 8
ctest --test-dir build-dev -R tst_foundation_block_anchor_queries --output-on-failure
```

Expected: all ten sub-tests PASS.

- [x] **Step 8.7: Run the full foundation test suite**

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_)' --output-on-failure -j 8
```

Expected: all green.

- [x] **Step 8.8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_foundation_block_anchor_queries.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): block-aware queries on MarkoffDocument"
```

---

## Task 9: BlockAnchor stability tests (spec §4 table)

Exercise each of the §4 mutations and assert the right anchors stable / new / orphaned.

**Files:**
- Create: `libs/markoff-core/tests/tst_foundation_block_anchor_stability.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 9.1: Write the test cases**

`libs/markoff-core/tests/tst_foundation_block_anchor_stability.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationBlockAnchorStability : public QObject {
    Q_OBJECT

    /// Returns the QList<BlockAnchor> from the most recent parseUpdated.
    static QList<BlockAnchor> latestAnchors(QSignalSpy &spy)
    {
        return spy.last().at(2).value<QList<BlockAnchor>>();
    }

    /// Apply an edit and wait for the parse to return.
    static void apply(MarkoffDocument &doc, QSignalSpy &spy,
                      quint32 oldStart, quint32 oldEnd, const QByteArray &newText)
    {
        spy.clear();
        MarkoffEdit e; e.oldStart = oldStart; e.oldEnd = oldEnd; e.newText = newText;
        doc.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
    }

    /// Set up a doc with three paragraphs: "p1\n\np2\n\np3".
    static void setupThreeParagraphs(MarkoffDocument &doc, QSignalSpy &spy)
    {
        doc.resetContent("p1\n\np2\n\np3", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
    }

private Q_SLOTS:
    void edit_within_block_preserves_all_anchors() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        setupThreeParagraphs(doc, spy);
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 3);

        // Insert "X" inside p2 (byte 5 is between "p" and "2").
        apply(doc, spy, 5, 5, "X");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[1]);
        QCOMPARE(after[2], before[2]);
    }

    void split_preserves_upper_half_introduces_new_lower() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 1);
        const BlockAnchor upper = before[0];

        // Split "aaaa" → "aa\n\naa" by inserting "\n\n" at byte 2.
        apply(doc, spy, 2, 2, "\n\n");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], upper);
        QVERIFY(after[1] != upper);
    }

    void merge_preserves_surviving_block_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aa\n\nbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);
        const BlockAnchor first = before[0];

        // Merge: delete the "\n\n" separator (bytes 2..4).
        apply(doc, spy, 2, 4, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after[0], first);
    }

    void add_block_at_top_preserves_existing() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("middle\n\nlast", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);

        apply(doc, spy, 0, 0, "first\n\n");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[1], before[0]);
        QCOMPARE(after[2], before[1]);
    }

    void add_block_at_bottom_preserves_existing() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("first\n\nmiddle", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);

        apply(doc, spy, 13, 13, "\n\nlast");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[1]);
    }

    void delete_entire_block_preserves_neighbours() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        setupThreeParagraphs(doc, spy);
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 3);

        // Delete p2 plus its trailing separator (bytes 4..8 = "p2\n\n").
        apply(doc, spy, 4, 8, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[2]);
    }

    void delete_first_byte_of_block_orphans_old_anchor() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent("aaaa\n\nbbbb", Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));
        const auto before = latestAnchors(spy);
        QCOMPARE(before.size(), 2);
        const BlockAnchor secondBlockOrig = before[1];

        // Delete the first 'b' of block 2 (byte 6).
        apply(doc, spy, 6, 7, "");
        const auto after = latestAnchors(spy);
        QCOMPARE(after.size(), 2);
        QCOMPARE(after[0], before[0]);
        // The old anchor for the second block was at the deleted char's
        // position; the new anchor for the second block is at a new
        // (still-present) char. They should differ.
        QVERIFY(after[1] != secondBlockOrig);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorStability)
#include "tst_foundation_block_anchor_stability.moc"
```

- [x] **Step 9.2: Register test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_block_anchor_stability tst_foundation_block_anchor_stability.cpp)
add_test(NAME tst_foundation_block_anchor_stability COMMAND tst_foundation_block_anchor_stability)
target_link_libraries(tst_foundation_block_anchor_stability PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_block_anchor_stability PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 9.3: Build and run**

```bash
cmake --build build-dev --target tst_foundation_block_anchor_stability -j 8
ctest --test-dir build-dev -R tst_foundation_block_anchor_stability --output-on-failure
```

Expected: all seven sub-tests PASS.

If any fail, that's a real defect either in the scanner (Task 7A) or the BlockAnchor compute (Task 7B). Diagnose by examining which case fails and whether the byte ranges or the anchor identities are at fault. Do **not** rewrite the test to match buggy behaviour — fix the production code per the spec §4 invariants.

- [x] **Step 9.4: Commit**

```bash
git add libs/markoff-core/tests/tst_foundation_block_anchor_stability.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "test(foundation): BlockAnchor stability across §4 mutation cases"
```

---

## Task 10: `Selection` field type change

`Selection.anchor` and `.active` become `TextAnchor`. Header drops `<crdt/Anchor.h>`.

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/Selection.h`
- Modify: `libs/markoff-core/src/Selection.cpp`
- Modify: `libs/markoff-core/tests/tst_selection.cpp`

- [x] **Step 10.1: Update `Selection.h`**

In `libs/markoff-core/include/markoff-foundation/Selection.h`, replace:

```cpp
#include <crdt/Anchor.h>
```

with:

```cpp
#include <markoff-foundation/TextAnchor.h>
```

And replace:

```cpp
    CollabText::Crdt::Anchor anchor;
    CollabText::Crdt::Anchor active;
```

with:

```cpp
    TextAnchor anchor;
    TextAnchor active;
```

(Update the `///` comment line above the fields if it mentions `CollabText::Crdt::Anchor`.)

- [x] **Step 10.2: Update `Selection.cpp`**

In `libs/markoff-core/src/Selection.cpp`, the `toJson` and `fromJson` implementations currently call `anchorToJson` / `anchorFromJson` from `AnchorJson.h`, which take `CollabText::Crdt::Anchor`. We need to convert to/from `TextAnchor` at the JSON boundary.

Add at top of file:

```cpp
#include "AnchorConversion.h"
```

For each call site like:

```cpp
s.anchor = anchorFromJson(obj.value("anchor").toObject());
```

change to:

```cpp
s.anchor = Detail::toTextAnchor(anchorFromJson(obj.value("anchor").toObject()));
```

For the `toJson` direction, find calls like:

```cpp
obj.insert("anchor", anchorToJson(anchor));
```

change to:

```cpp
obj.insert("anchor", anchorToJson(Detail::toCrdtAnchor(anchor)));
```

Also update `isEmpty()` / `isReversed()` if they read `.anchor.replica_id` etc. — those become `.anchor.replicaId` (note camelCase change). Search for `replica_id\|char_value\|\.bias\b` in `Selection.cpp` and rename to `replicaId\|charValue\|.bias` (no rename for `.bias`, just the fields prefixed with snake_case).

Run a grep to confirm:

```bash
grep -n "replica_id\|char_value" libs/markoff-core/src/Selection.cpp
```

Expected after changes: empty output.

- [x] **Step 10.3: Update `tst_selection.cpp`**

Search for usages of `Crdt::Anchor` / `replica_id` / `char_value` in `libs/markoff-core/tests/tst_selection.cpp` and update:

```bash
grep -n "Crdt::Anchor\|replica_id\|char_value" libs/markoff-core/tests/tst_selection.cpp
```

For each, change `Crdt::Anchor a{1, 42, Bias::Left}` to construct via `Detail::toTextAnchor` if Crdt::Anchor is needed elsewhere, or directly `TextAnchor{1, 42, 0}` if the test only cares about the wire identity.

If the test relies on JSON round-trip behaviour, ensure the round-trip still works after the field-type change (it should — `Detail::toTextAnchor` is the boundary).

- [x] **Step 10.4: Build the foundation library**

```bash
cmake --build build-dev --target markoff_core -j 8 2>&1 | tail -50
```

Expected: build fails at multiple foundation src/ call sites that pass `sel.anchor` / `sel.active` to `resolveAnchor(Crdt::Anchor)`. **Don't fix these yet** — Task 11 covers them.

**Important:** If the foundation library itself fails to compile because `Selection.cpp` references something that hasn't been fixed up, narrow the failures to call-site files (CommandFacade, SearchEngine, etc.). The Selection.h / Selection.cpp changes themselves should be self-consistent.

- [x] **Step 10.5: Build the selection test in isolation if possible**

```bash
cmake --build build-dev --target tst_selection -j 8 2>&1 | tail -30
```

If `markoff_core` itself doesn't build because of call-site failures, this won't compile either. In that case skip ahead to Task 11 and run `tst_selection` after that task.

- [x] **Step 10.6: Note partial state — DO NOT commit yet**

The build is broken at this point; intentionally. Task 11 fixes the call sites. Commit happens at the end of Task 11 as one logical unit ("Selection: switch fields to TextAnchor + update foundation call sites"). Hold the changes uncommitted.

(No `git commit` here.)

---

## Task 11: Foundation-internal call-site updates

The places that read `sel.anchor` / `sel.active` and pass them to `resolveAnchor(Crdt::Anchor)` switch to `resolveTextAnchor(TextAnchor)`.

**Files (each modified):**
- `libs/markoff-core/src/CommandFacade.cpp` (~5 sites)
- `libs/markoff-core/src/SearchEngine.cpp` (~2 sites)
- `libs/markoff-core/src/SourceTextDocumentBinding.cpp` (~3 sites)
- `libs/markoff-core/src/Cmd/Helpers.cpp` (~2 sites)
- `libs/markoff-core/src/Session.cpp` (~1 selection-equality block)

- [x] **Step 11.1: Inventory call sites**

```bash
grep -n "resolveAnchor\|sel\.anchor\|sel\.active\|primarySelection().anchor\|primarySelection().active" \
    libs/markoff-core/src/*.cpp libs/markoff-core/src/Cmd/*.cpp 2>&1
```

Confirm the inventory matches the list in the spec §5 (~10–15 sites).

- [x] **Step 11.2: Update `CommandFacade.cpp`**

For each line where `m_sess->primarySelection().anchor` or `.active` is passed to a function taking `Crdt::Anchor`, the receiver function may itself accept `Crdt::Anchor`. Look at the receiver:
- If the receiver is an internal foundation helper, change *its* parameter to `TextAnchor` and update its body to call `resolveTextAnchor` instead of `resolveAnchor`.
- If the receiver is a public foundation API still typed as `Crdt::Anchor`, convert at the call site: `Detail::toCrdtAnchor(sel.anchor)`.

Pattern: prefer pushing `TextAnchor` deeper rather than wrapping at the call site. Receivers that exist purely to be called from `Selection`-driven code can switch their signatures to `TextAnchor`.

For each touched line, also remove `#include <crdt/Anchor.h>` from the file if it's no longer needed; add `#include <markoff-foundation/TextAnchor.h>` and `#include "AnchorConversion.h"` if not present.

- [x] **Step 11.3: Update `SearchEngine.cpp`**

The two sites are:

```cpp
const quint32 cur = doc->resolveAnchor(sess->primarySelection().active);
```

becomes:

```cpp
const quint32 cur = doc->resolveTextAnchor(sess->primarySelection().active);
```

Same for the `.anchor` site.

- [x] **Step 11.4: Update `SourceTextDocumentBinding.cpp`**

Three sites: similar pattern. The local variables `anchor`, `anchorA`, `anchorB` may need to be retyped from `Crdt::Anchor` to `TextAnchor`. Where the binding currently calls `m_markoffDocument->anchorAt(...)` (CRDT-typed) and assigns the result to `sel.anchor` (now TextAnchor-typed), switch the call to `m_markoffDocument->textAnchorAt(...)`. The two `resolveAnchor` calls in this file become `resolveTextAnchor`.

- [x] **Step 11.5: Update `Cmd/Helpers.cpp`**

Two sites resolve anchors in one helper function; switch both to `resolveTextAnchor`.

- [x] **Step 11.6: Update `Session.cpp`**

The selection-equality compare currently is field-by-field on `Crdt::Anchor`:

```cpp
if (cur.anchor.replica_id == sel.anchor.replica_id
    && cur.anchor.char_value == sel.anchor.char_value
    && cur.anchor.bias       == sel.anchor.bias
    && cur.active.replica_id == sel.active.replica_id
    && cur.active.char_value == sel.active.char_value
    && cur.active.bias       == sel.active.bias
    /* + other fields */)
```

Replace with:

```cpp
if (cur.anchor == sel.anchor
    && cur.active == sel.active
    /* + other fields */)
```

(`TextAnchor`'s `operator==` is defaulted, covers all fields.)

- [x] **Step 11.7: Build the foundation library**

```bash
cmake --build build-dev --target markoff_core -j 8 2>&1 | tail -20
```

Expected: `markoff_core` builds clean. If errors remain, they likely point at a call site missed in steps 11.2–11.6; fix and rebuild.

- [x] **Step 11.8: Run all foundation tests**

```bash
ctest --test-dir build-dev -R '^tst_(markoff_edit|anchor_json|selection|fold_ref|foundation_)' --output-on-failure -j 8
```

Expected: all green. If `tst_selection`, `tst_foundation_command_facade`, `tst_foundation_cmd_*`, or `tst_foundation_replace_controller` fail, the failure is most likely a missed call site or a JSON-round-trip semantic divergence. Diagnose and fix.

- [x] **Step 11.9: Commit Tasks 10 + 11 together**

```bash
git add libs/markoff-core/include/markoff-foundation/Selection.h \
        libs/markoff-core/src/Selection.cpp \
        libs/markoff-core/src/CommandFacade.cpp \
        libs/markoff-core/src/SearchEngine.cpp \
        libs/markoff-core/src/SourceTextDocumentBinding.cpp \
        libs/markoff-core/src/Cmd/Helpers.cpp \
        libs/markoff-core/src/Session.cpp \
        libs/markoff-core/tests/tst_selection.cpp
git commit -m "refactor(foundation): Selection fields use TextAnchor; resolveTextAnchor at call sites"
```

---

## Task 12: View-qml call-site updates

`EditorBackend.cpp`'s `m_selectionAnchor` / `m_cursorAnchor` were `CollabText::Crdt::Anchor`; they become `Markoff::TextAnchor`. The qml editor-backend test updates accordingly.

**Files:**
- Modify: `libs/markoff-view-qml/src/EditorBackend.h`
- Modify: `libs/markoff-view-qml/src/EditorBackend.cpp`
- Modify: `libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp`

- [x] **Step 12.1: Inventory the call sites**

```bash
grep -n "Crdt::Anchor\|m_selectionAnchor\|m_cursorAnchor\|m_selectionActive" \
    libs/markoff-view-qml/src/EditorBackend.{h,cpp} \
    libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp
```

- [x] **Step 12.2: Update `EditorBackend.h`**

Replace:

```cpp
#include <crdt/Anchor.h>
```

with:

```cpp
#include <markoff-foundation/TextAnchor.h>
```

If a `Q_DECLARE_METATYPE(CollabText::Crdt::Anchor)` exists at file scope (per the view-qml CLAUDE.md), replace it with:

```cpp
Q_DECLARE_METATYPE(Markoff::TextAnchor)
```

Change member field types `CollabText::Crdt::Anchor m_selectionAnchor` → `Markoff::TextAnchor m_selectionAnchor`, etc. (similarly for `m_selectionActive`, `m_cursorAnchor`).

If properties exposed to QML reference the anchor type — e.g. `Q_PROPERTY(CollabText::Crdt::Anchor selectionAnchor ...)` — they become `Q_PROPERTY(Markoff::TextAnchor selectionAnchor ...)`. Update getter/setter signatures to match.

- [x] **Step 12.3: Update `EditorBackend.cpp`**

Wherever the file reads `sel.anchor` (now TextAnchor) and assigns to `m_selectionAnchor`, the assignment is now type-clean. Wherever it calls `m_doc->anchorAt(...)` to construct an anchor for the session, switch to `m_doc->textAnchorAt(...)`. Wherever it calls `m_doc->resolveAnchor(...)`, switch to `m_doc->resolveTextAnchor(...)`.

Add `#include <markoff-foundation/MarkoffDocument.h>` if not present (probably already included).

- [x] **Step 12.4: Update the test**

In `tst_view_qml_editor_backend.cpp`, the existing tests construct `Crdt::Anchor a3, a8` etc. via `m_doc->anchorAt(3, Bias::Left)`. Change to `m_doc->textAnchorAt(3, /*rightBias*/ false)`. The `QCOMPARE(sel.anchor, a3)` style asserts continue to work because both sides are now `TextAnchor` and have a defaulted `operator==`.

- [x] **Step 12.5: Build the view-qml library + tests**

```bash
cmake --build build-dev --target markoff_view_qml -j 8 2>&1 | tail -30
cmake --build build-dev --target tst_view_qml_editor_backend -j 8 2>&1 | tail -30
```

Expected: both build clean.

- [x] **Step 12.6: Run the editor-backend test**

```bash
ctest --test-dir build-dev -R tst_view_qml_editor_backend --output-on-failure
```

Expected: PASS.

- [x] **Step 12.7: Run all view-qml tests**

```bash
ctest --test-dir build-dev -R '^tst_view_qml_' --output-on-failure -j 8
```

Expected: all PASS. The walking-skeleton tests (`tst_view_qml_live_view_qml`, `tst_view_qml_ast_block_diff`, etc.) shouldn't depend on EditorBackend's selection types, but if any do, fix them analogously (TextAnchor).

If `tst_view_qml_source_binding` or `tst_view_qml_search_backend` fail, the failure is most likely a slot signature that hadn't been updated for the new `parseUpdated` shape from Task 7B.11, or an EditorBackend property that's now TextAnchor-typed where the test expected Crdt::Anchor.

- [x] **Step 12.8: Commit**

```bash
git add libs/markoff-view-qml/src/EditorBackend.h \
        libs/markoff-view-qml/src/EditorBackend.cpp \
        libs/markoff-view-qml/tests/tst_view_qml_editor_backend.cpp
git commit -m "refactor(view-qml): EditorBackend selection state uses TextAnchor"
```

---

## Task 13: Performance test

Verify per-parse BlockAnchor compute stays under 1 ms wall-time on a 50 KB / 100-block doc.

**Files:**
- Create: `libs/markoff-core/tests/tst_foundation_block_anchor_perf.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [x] **Step 13.1: Write the perf test**

`libs/markoff-core/tests/tst_foundation_block_anchor_perf.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QElapsedTimer>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include "TopLevelBlockScanner.h"
#include "BlockAnchorComputation.h"

using namespace Markoff;

class TstFoundationBlockAnchorPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void scanner_under_1_ms_for_50KB_100_block_doc() {
        QByteArray src;
        src.reserve(50 * 1024);
        for (int i = 0; i < 100; ++i) {
            src.append("Block ").append(QByteArray::number(i))
               .append(" content. Lorem ipsum dolor sit amet, consectetur "
                       "adipiscing elit. Sed do eiusmod tempor incididunt "
                       "ut labore et dolore magna aliqua. Ut enim ad minim.")
               .append("\n\n");
        }
        // Verify we have ~100 blocks at ~50 KB.
        QVERIFY(src.size() > 40 * 1024);
        QVERIFY(src.size() < 70 * 1024);

        // Warmup.
        (void)Markoff::Detail::scanTopLevelBlockRanges(src);

        QElapsedTimer t; t.start();
        const int iterations = 100;
        for (int i = 0; i < iterations; ++i) {
            (void)Markoff::Detail::scanTopLevelBlockRanges(src);
        }
        const qint64 nsTotal = t.nsecsElapsed();
        const double msPerIter = (nsTotal / 1e6) / iterations;
        qInfo() << "Scanner avg wall:" << msPerIter << "ms per call";
        QVERIFY(msPerIter < 1.0);
    }

    void anchor_compute_per_parse_under_2ms_on_50KB_doc() {
        MarkoffDocument doc{1};
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        QByteArray src;
        for (int i = 0; i < 100; ++i) {
            src.append("Block ").append(QByteArray::number(i))
               .append(" content. Lorem ipsum dolor sit amet.\n\n");
        }
        doc.resetContent(src, Origin::ExternalLoad);
        QVERIFY(spy.wait(2000));

        QElapsedTimer t; t.start();
        auto bundle = Markoff::Detail::computeBlockAnchors(doc, doc.toMarkdownUtf8());
        const qint64 ns = t.nsecsElapsed();
        const double ms = ns / 1e6;
        qInfo() << "computeBlockAnchors wall:" << ms << "ms"
                << "blocks:" << bundle.anchors.size();
        QVERIFY(ms < 2.0);
    }
};

QTEST_MAIN(TstFoundationBlockAnchorPerf)
#include "tst_foundation_block_anchor_perf.moc"
```

- [x] **Step 13.2: Register the perf test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_block_anchor_perf tst_foundation_block_anchor_perf.cpp)
add_test(NAME tst_foundation_block_anchor_perf COMMAND tst_foundation_block_anchor_perf)
target_link_libraries(tst_foundation_block_anchor_perf PRIVATE Qt6::Test markoff_core)
target_include_directories(tst_foundation_block_anchor_perf PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/markoff-core/src)
set_tests_properties(tst_foundation_block_anchor_perf PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 13.3: Build and run**

```bash
cmake --build build-dev --target tst_foundation_block_anchor_perf -j 8
ctest --test-dir build-dev -R tst_foundation_block_anchor_perf --output-on-failure -V
```

Expected: PASS, with `qInfo` output showing scanner under 1 ms and compute under 2 ms.

If the threshold is exceeded on slow machines, raise it modestly (3 ms / 5 ms) but don't disable the test — it's a regression guardrail more than a strict bar.

- [x] **Step 13.4: Commit**

```bash
git add libs/markoff-core/tests/tst_foundation_block_anchor_perf.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "test(foundation): per-parse BlockAnchor compute perf guardrail"
```

---

## Task 14: Final integration sweep

Run the entire test suite to confirm nothing in the wider tree regressed. Update relevant CLAUDE.md sections if any architectural invariants need new prose.

**Files:**
- Modify (probably): `libs/markoff-core/CLAUDE.md` — note new public-API surface (TextAnchor, BlockAnchor, parseSequence, editSequence) and the parseUpdated signal-shape change.

- [x] **Step 14.1: Run the full non-slow test suite**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -E 'tst_realistic|tst_benchmark' --output-on-failure -j 8
```

Expected: all green. If anything regressed (e.g. a markoff-parser test that connected to `parseUpdated` somehow), fix it.

- [x] **Step 14.2: Run the slow tests**

```bash
ctest --test-dir build-dev -R 'tst_realistic' --output-on-failure
```

Expected: PASS, no perf regression. (If the realistic test takes ~90 s and passes, that's the bar.)

- [x] **Step 14.3: Smoke-test the test app**

```bash
cmake --build build-dev --target markoff-view-qml-app -j 8
./build-dev/bin/markoff-view-qml-app --live tests/fixtures/empty.md &
APP_PID=$!
sleep 3
kill $APP_PID 2>/dev/null
```

Expected: the app launches without crashing under the new `parseUpdated` signature. The Live view will not be functional for editing yet (that's the live-editing plan), but it must not crash on launch.

- [x] **Step 14.4: Update foundation CLAUDE.md if it exists**

```bash
ls libs/markoff-core/CLAUDE.md 2>&1
```

If it exists, add a short "Public-boundary types" section noting:
- `TextAnchor` and `BlockAnchor` are the view-layer-safe handle types (no CRDT header dep).
- `parseSequence()` / `editSequence()` are the public-boundary version accessors.
- `parseUpdated(parsed, parseSequence, blockAnchors)` is the new signal shape.

If it doesn't exist, skip — we won't create one purely for this work.

- [x] **Step 14.5: Final verification grep**

Verify no view-layer or app-layer code includes `<crdt/Anchor.h>` or `<crdt/Clock.h>` outside foundation src/:

```bash
grep -rn "crdt/Anchor.h\|crdt/Clock.h" libs/markoff-view-qml/ libs/markoff-source/ 2>&1
```

Expected: empty. If non-empty, the offending file was missed in Tasks 11/12; fix and re-test.

- [x] **Step 14.6: Commit any CLAUDE.md update**

```bash
git add libs/markoff-core/CLAUDE.md  # if updated
git commit -m "docs(foundation): note BlockAnchor public-boundary surface"
```

(Skip if no CLAUDE.md update.)

---

## Verification gates

Before declaring the BlockAnchor foundation work complete:

1. **All seven new test executables pass:**
   - `tst_foundation_text_anchor`
   - `tst_foundation_block_anchor`
   - `tst_foundation_top_level_block_scanner`
   - `tst_foundation_block_anchor_compute`
   - `tst_foundation_block_anchor_queries`
   - `tst_foundation_block_anchor_stability`
   - `tst_foundation_parse_sequence`
   - `tst_foundation_edit_sequence`
   - `tst_foundation_block_anchor_perf`

2. **All pre-existing foundation + view-qml tests pass** with the new signal shape and Selection field types (no test rewrites that change behaviour — only retypings).

3. **`tst_realistic` passes** with no perf regression beyond ±5 %.

4. **No view-layer / app-layer file includes `<crdt/Anchor.h>` or `<crdt/Clock.h>`.**
   ```bash
   grep -rn "crdt/Anchor.h\|crdt/Clock.h" libs/markoff-view-qml/ libs/markoff-source/
   ```
   Returns empty.

5. **The `parseUpdated` signal signature is** `(const Markoff::Document *, quint64, QList<Markoff::BlockAnchor>)`. No call sites still expect `Crdt::Global atVersion`.

6. **The live-editing plan's Task 1.1 grep precondition passes:**
   ```bash
   grep -E "BlockAnchor|TextAnchor|parseSequence|editSequence" libs/markoff-core/include/markoff-foundation/MarkoffDocument.h
   grep -E "blockAt|offsetInBlock|textAnchorAt.*BlockAnchor" libs/markoff-core/include/markoff-foundation/MarkoffDocument.h
   ```
   At least one match for each grep.

Once all six gates pass, the live-editing plan's hard precondition is satisfied and that plan can begin.
