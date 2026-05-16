# Tier 3 — Kind-transition delegate architecture — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire the `kindOnlySwap`/`beginResetModel` workaround in
`LiveBlockModel::applyOps` and the `Connections`-based contentY
bandaid in `LiveView.qml`, replacing both with a stable-row-identity
architecture: kind becomes a row *property*, not part of row
*identity*. `BlockKey` carries `delegateClass` instead of `kind`;
paragraph↔heading↔blockquote↔list-item transitions become
`dataChanged({KindRole, …})` instead of model-reset. One new
`UnifiedInlineTextDelegate.qml` replaces four kind-specific delegates.

**Architecture:** A coarser dispatch key (`delegateClass`) buckets
kinds into 5 stable classes: `text-inline`, `code-block`, `math`,
`hr`, `image`. Within-bucket kind changes propagate via `dataChanged`
role hints; the same delegate instance — and its `TextEdit` — survive
the transition. Cross-bucket changes (rare: paragraph→hr, etc.) still
go through the pre-existing Delete+Insert path. Fixes the dogfood-
visible flicker on `#`/`-` and the cross-block paste header-styling
loss in one move.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest +
`LiveRealisticInputHarness` + `QmlIntegrationFixture`. Build/test via
`scripts/run-tests.sh` (offscreen by default — never `--direct`
without explicit per-task permission). Build cap: `-j 8`.

**Spec:** `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`
is authoritative; cite section numbers when in doubt.

**Reading order before starting:**
1. `docs/INVARIANTS.md` (invariants 1, 2, 3, 4, 5, 6, 7, 8)
2. `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md` (the tier-3 spec)
3. `docs/queue.md:63` — the discipline-log entry pre-authorizing this work
4. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` §5 (kind-transition detection — unchanged path)
5. `libs/markoff-live/include/markoff/live/BlockRecord.h` (BlockKey + BlockRecord shape)
6. `libs/markoff-live/include/markoff/live/LiveBlockModel.h` (Role enum)
7. `libs/markoff-live/src/LiveBlockModel.cpp` (`applyOps` lines 106–211 — to be partially retired)
8. `libs/markoff-live/src/LiveListModelBinding.cpp` lines 390–430 (key list assembly + kind-transition detection)
9. `libs/markoff-live/qml/LiveView.qml` (DelegateChooser, Connections bandaid)
10. `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`,
    `HeadingDelegate.qml`, `BlockquoteDelegate.qml`,
    `ListItemDelegate.qml` (the four to be merged)
11. `libs/markoff-live/tests/QmlIntegrationFixture.h` (`delegateTextEdit`, `listView`, etc.)

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration

# Default fast build (whole library + apps + tests):
cmake --build build-dev -j 8

# Build one target:
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target tst_live_render_kind_transition_invariant -j 8

# Run one test binary (offscreen):
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant

# Run a pattern (offscreen):
./scripts/run-tests.sh -R 'tst_live_render_'

# Full suite (offscreen, fast):
./scripts/run-tests.sh -E 'tst_realistic|tst_benchmark' --timeout 35
```

**Commit-message prefix convention:** `markoff-live: <slot summary>`
for code; `docs:` for spec/plan/queue updates.

---

## Baseline before this plan starts

- Branch: `exploration/new-foundation`.
- HEAD: `d92e1e2` (scroll bandaid) or a successor commit that lands
  the spec.
- Baseline failure count under offscreen: **11 of 201 tests fail**
  (3 timeouts on slow benchmarks; 8 pre-existing assertion failures
  in `tst_live_render_registry`, `tst_live_render_cursor`,
  `tst_live_render_structural`, `tst_live_render_setext_e2e`,
  `tst_live_render_e2_nav_*`, and 4 sub-failures in
  `tst_live_render_qml_integration`). None are caused by tier-3
  changes; the plan does not aim to reduce this count.

---

## Files touched

| File | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/BlockRecord.h` | `BlockKey` field rename: `kind` → `delegateClass`. Add `BlockRecord::delegateClass` (string, derived). |
| `libs/markoff-live/include/markoff/live/LiveBlockModel.h` | Add `DelegateClassRole` to the `Role` enum. |
| `libs/markoff-live/src/LiveBlockModel.cpp` | Register `delegateClass` role name; add `data()` switch case; retire `kindOnlySwap` detector + branch (lines 106–162); emit kind+attr role hints in the Equal-op `dataChanged`. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Populate `r.delegateClass = delegateClassFor(r.kind)`; `BlockKey { r.delegateClass, r.blockAnchor }`. |
| `libs/markoff-live/src/KindDispatch.h` | **New** — declares `delegateClassFor(const QString &kind) → QString`. |
| `libs/markoff-live/src/KindDispatch.cpp` | **New** — implementation + unit-test target. |
| `libs/markoff-live/qml/LiveView.qml` | Change `DelegateChooser` role to `"delegateClass"`; 5 choices; delete `Connections { … onModelReset }` + `_lastSavedContentY` + `_modelResetCount`. |
| `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml` | **New** — merges Paragraph, Heading, Blockquote, ListItem. |
| `libs/markoff-live/qml/delegates/qmldir` | Register `UnifiedInlineTextDelegate`; unregister the four superseded delegates. |
| `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` | **Delete.** |
| `libs/markoff-live/qml/delegates/HeadingDelegate.qml` | **Delete.** |
| `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml` | **Delete.** |
| `libs/markoff-live/qml/delegates/ListItemDelegate.qml` | **Delete.** |
| `libs/markoff-live/CMakeLists.txt` | Update QML module file list (add Unified, remove four). |
| `libs/markoff-live/tests/tst_kind_dispatch.cpp` | **New** — unit tests for `delegateClassFor`. |
| `libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp` | **New** — §6.1 + §6.2 slots. |
| `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` | Add §6.3 paste-styling slot. |
| `libs/markoff-live/tests/CMakeLists.txt` | Register two new test targets. |
| `docs/queue.md` | Discipline-log entries: close queue.md:63 (kindOnlySwap retired); record removal of Qt.callLater count from 2 → 1. Update `libs/markoff-live/CLAUDE.md` callLater count. |
| `libs/markoff-live/CLAUDE.md` | Update the "current count: 11 Qt.callLater sites" — actually 1 after this plan. |

---

## Task 1: Pre-flight checks

**Files:** none (verification only).

- [ ] **Step 1: Confirm worktree state.** From
  `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`:

```bash
git status --short
```

Expected: untracked-only output (`.superpowers/`, `selection.txt`,
`selection2.txt`, possibly `Testing/Temporary/LastTest.log`). If
anything tracked is modified, stop and report.

- [ ] **Step 2: Confirm branch + HEAD.**

```bash
git branch --show-current
git log --oneline -5
```

Expected: branch `exploration/new-foundation`. HEAD commit message
should mention the tier-3 spec landing or a successor; the bandaid
`d92e1e2` should be in the recent log.

- [ ] **Step 3: Confirm baseline test suite count.**

```bash
./scripts/run-tests.sh --timeout 35 2>&1 | tail -20
```

Expected: `11 tests failed out of 201`. Note the exact failures to a
scratch file — compared at Task 14.

- [ ] **Step 4: Confirm offscreen plugin still works.**

```bash
QT_QPA_PLATFORM=offscreen build-dev/bin/tst_live_render_qml_integration -functions 2>&1 | head -5
```

Expected: function list printed without segfault.

---

## Task 2: Add `delegateClassFor` helper + unit test

**Files:**
- Create: `libs/markoff-live/src/KindDispatch.h`
- Create: `libs/markoff-live/src/KindDispatch.cpp`
- Create: `libs/markoff-live/tests/tst_kind_dispatch.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt` (source list)
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (test target)

- [ ] **Step 1: Write the failing unit test.**

Create `libs/markoff-live/tests/tst_kind_dispatch.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "KindDispatch.h"

class TestKindDispatch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void text_inline_kinds_share_a_class() {
        QCOMPARE(Markoff::Live::delegateClassFor("paragraph"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("heading"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("blockquote"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor("list-item"),
                 QStringLiteral("text-inline"));
    }

    void other_kinds_are_distinct() {
        QCOMPARE(Markoff::Live::delegateClassFor("code-block"),
                 QStringLiteral("code-block"));
        QCOMPARE(Markoff::Live::delegateClassFor("math"),
                 QStringLiteral("math"));
        QCOMPARE(Markoff::Live::delegateClassFor("hr"),
                 QStringLiteral("hr"));
        QCOMPARE(Markoff::Live::delegateClassFor("image"),
                 QStringLiteral("image"));
    }

    void unknown_kind_falls_back_to_text_inline() {
        QCOMPARE(Markoff::Live::delegateClassFor("plugin-future"),
                 QStringLiteral("text-inline"));
        QCOMPARE(Markoff::Live::delegateClassFor(""),
                 QStringLiteral("text-inline"));
    }
};

QTEST_MAIN(TestKindDispatch)
#include "tst_kind_dispatch.moc"
```

- [ ] **Step 2: Register the test target in CMake.**

Append to `libs/markoff-live/tests/CMakeLists.txt` (after the
`tst_live_render_inline_typing_perf` block):

```cmake
qt_add_executable(tst_kind_dispatch
    tst_kind_dispatch.cpp
)
target_include_directories(tst_kind_dispatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(tst_kind_dispatch PRIVATE
    Qt6::Core Qt6::Test markoff_live)
add_test(NAME tst_kind_dispatch COMMAND tst_kind_dispatch)
```

- [ ] **Step 3: Reconfigure + verify the test fails to build (header missing).**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build-dev --target tst_kind_dispatch -j 8 2>&1 | tail -10
```

Expected: build fails with "KindDispatch.h: No such file or directory".

- [ ] **Step 4: Write the header.**

Create `libs/markoff-live/src/KindDispatch.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::Live {

/// Coarser dispatch key over BlockKind: the `delegateClass` value that
/// `LiveView.qml`'s DelegateChooser uses to pick a delegate. Within-class
/// kind changes (e.g. paragraph→heading, both "text-inline") produce a
/// `dataChanged` on the kind role instead of a Delete+Insert pair, so the
/// same delegate instance survives the transition. Cross-class changes
/// (e.g. paragraph→hr) still go through Delete+Insert.
///
/// Spec §4.2 (`docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`).
QString delegateClassFor(const QString &kind);

}  // namespace Markoff::Live
```

- [ ] **Step 5: Write the implementation.**

Create `libs/markoff-live/src/KindDispatch.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindDispatch.h"

namespace Markoff::Live {

QString delegateClassFor(const QString &kind)
{
    if (kind == QStringLiteral("code-block")) return QStringLiteral("code-block");
    if (kind == QStringLiteral("math"))       return QStringLiteral("math");
    if (kind == QStringLiteral("hr"))         return QStringLiteral("hr");
    if (kind == QStringLiteral("image"))      return QStringLiteral("image");
    // paragraph, heading, blockquote, list-item, and unknown (future plugin
    // kinds) fall into the text-inline bucket.
    return QStringLiteral("text-inline");
}

}  // namespace Markoff::Live
```

- [ ] **Step 6: Register sources in CMake.**

Open `libs/markoff-live/CMakeLists.txt`, find the `qt_add_library(markoff_live …)` (or its source-list block), and append `src/KindDispatch.cpp` and `src/KindDispatch.h` to the source list. If the build uses a `set(MARKOFF_LIVE_SOURCES …)` variable, append both files there. The header is private, so it goes in the sources, not the public includes.

```bash
grep -n "src/Live" libs/markoff-live/CMakeLists.txt | head -3
```

Append the new files in the same style as a sibling entry like
`src/LiveListModelBinding.cpp` / `src/LiveListModelBinding.h`.

- [ ] **Step 7: Build + run the test.**

```bash
cmake --build build-dev --target tst_kind_dispatch -j 8 2>&1 | tail -5
./scripts/run-tests.sh --bin tst_kind_dispatch
```

Expected: 3 tests pass.

- [ ] **Step 8: Verify the rest of the library still builds.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
```

Expected: clean exit.

- [ ] **Step 9: Commit.**

```bash
git add libs/markoff-live/src/KindDispatch.h \
        libs/markoff-live/src/KindDispatch.cpp \
        libs/markoff-live/tests/tst_kind_dispatch.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: add delegateClassFor — kind → delegateClass bucket

Pure helper, no consumers yet. Buckets the eight block kinds into five
delegate-class values: text-inline (paragraph, heading, blockquote,
list-item), code-block, math, hr, image. Unknown kinds (future plugin
kinds) fall into text-inline.

Spec §4.2.
EOF
)"
```

---

## Task 3: Add `delegateClass` field to `BlockRecord` + `DelegateClassRole` to the model

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockRecord.h`
- Modify: `libs/markoff-live/include/markoff/live/LiveBlockModel.h`
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`

- [ ] **Step 1: Add the field to `BlockRecord`.**

Edit `libs/markoff-live/include/markoff/live/BlockRecord.h`. Locate
the `BlockRecord` struct (around line 20–40); add a new field right
after `kind`:

```cpp
QString kind;
QString delegateClass;  // derived from kind; see Markoff::Live::delegateClassFor.
```

- [ ] **Step 2: Add the role to the enum.**

Edit `libs/markoff-live/include/markoff/live/LiveBlockModel.h`.
Locate the `Role` enum and append:

```cpp
enum Role {
    KindRole         = Qt::UserRole + 1,
    TextRole,
    HeadingLevelRole,
    HeadingFormRole,
    CodeLanguageRole,
    BlockAnchorRole,
    BlockAttrsRole,
    MarkerStyleRole,
    MarkerNumberRole,
    IndentLevelRole,
    CheckedRole,
    LooseRunRole,
    InlineSpansRole,
    DelegateClassRole,  // new: see Markoff::Live::delegateClassFor.
};
```

- [ ] **Step 3: Register the role name.**

Edit `libs/markoff-live/src/LiveBlockModel.cpp`, locate the
`roleNames()` map (around line 27 — `{ KindRole, "kind" },`), and
append:

```cpp
{ DelegateClassRole, "delegateClass" },
```

- [ ] **Step 4: Add the `data()` switch case.**

Edit `libs/markoff-live/src/LiveBlockModel.cpp`, locate the switch
inside `data()` (around line 49 — `case KindRole: return r.kind;`),
and append:

```cpp
case DelegateClassRole: return r.delegateClass;
```

- [ ] **Step 5: Build + run the inline-related test sanity-check.**

```bash
cmake --build build-dev --target markoff_live tst_live_render_block_model -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_block_model
```

Expected: tests pass. (No new tests for this task — the field is just
added; it'll be populated in Task 4.)

- [ ] **Step 6: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/BlockRecord.h \
        libs/markoff-live/include/markoff/live/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp
git commit -m "$(cat <<'EOF'
markoff-live: add DelegateClassRole + BlockRecord.delegateClass field

Plumbing only — the field is populated in the next commit and the role
becomes load-bearing when LiveView.qml's DelegateChooser switches to
key on it. Spec §5.2.
EOF
)"
```

---

## Task 4: Populate `BlockRecord::delegateClass` from `LiveListModelBinding::onD2Changed`

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Locate the record assembly loop.**

```bash
grep -n "BlockRecord r;" libs/markoff-live/src/LiveListModelBinding.cpp
```

Expected: one hit around line 350.

- [ ] **Step 2: Include the dispatch header.**

At the top of `libs/markoff-live/src/LiveListModelBinding.cpp`, add
near the existing `#include` lines:

```cpp
#include "KindDispatch.h"
```

(Adjacent to includes like `#include "LiveBlockModel.h"`.)

- [ ] **Step 3: Populate the field during record assembly.**

In the record-construction block (around lines 350–360), after
`r.kind = blockKindToString(doc->blockKind(id));`, add:

```cpp
r.delegateClass = Markoff::Live::delegateClassFor(r.kind);
```

- [ ] **Step 4: Build + run the integration suite.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | tail -5
```

Expected: 19 pass / 4 fail (unchanged from baseline). The role is
populated but not yet consumed, so no behaviour change.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "$(cat <<'EOF'
markoff-live: populate BlockRecord.delegateClass during record assembly

LiveListModelBinding::onD2Changed now derives delegateClass per
record. No consumers yet — the dispatcher switches to consume it in a
subsequent commit. Spec §5.3.
EOF
)"
```

---

## Task 5: Write the failing §6.1 invariant test (3 slots, new binary)

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test file.**

Create `libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QQuickItem>
#include <QTest>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

namespace {

void typeAscii(QmlIntegrationFixture &fix, char c) {
    QTest::keyClick(fix.window(), c);
    QTest::qWait(30);
    QCoreApplication::processEvents();
}

}  // namespace

/// Spec §6.1: within-class kind transitions preserve the TextEdit
/// QQuickItem identity. The same QObject pointer survives
/// paragraph→heading, heading→paragraph, paragraph→list-item.
class TestLiveRenderKindTransitionInvariant : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void paragraph_to_heading_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("hello", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QQuickItem *textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);
        const qreal contentYBefore = fix.listView()->property("contentY").toReal();

        fix.placeCursorAtPos(0, 0);
        QCOMPARE(fix.delegateCursorPos(0), 0);

        typeAscii(fix, '#');
        typeAscii(fix, ' ');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("heading"), 2000));

        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore);  // SAME pointer.
        QCOMPARE(fix.delegateCursorPos(0), 2);

        const qreal contentYAfter = fix.listView()->property("contentY").toReal();
        QCOMPARE(contentYAfter, contentYBefore);
    }

    void heading_to_paragraph_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("# foo", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QQuickItem *textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);

        fix.placeCursorAtPos(0, 1);
        QCOMPARE(fix.delegateCursorPos(0), 1);

        QTest::keyClick(fix.window(), Qt::Key_Backspace);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        QVERIFY(fix.waitForKindAt(0, QStringLiteral("paragraph"), 2000));

        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore);
    }

    void paragraph_to_listitem_preserves_textedit_pointer() {
        QmlIntegrationFixture fix("x", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Clear "x" so we can promote a clean empty paragraph.
        fix.placeCursorAtPos(0, 0);
        QTest::keyClick(fix.window(), Qt::Key_Delete);
        QTest::qWait(30);
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        QQuickItem *textEditBefore = fix.delegateTextEdit(0);
        QVERIFY(textEditBefore);

        typeAscii(fix, '-');
        typeAscii(fix, ' ');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("list-item"), 2000));

        QQuickItem *textEditAfter = fix.delegateTextEdit(0);
        QVERIFY(textEditAfter);
        QCOMPARE(textEditAfter, textEditBefore);
    }
};

QTEST_MAIN(TestLiveRenderKindTransitionInvariant)
#include "tst_live_render_kind_transition_invariant.moc"
```

- [ ] **Step 2: Register the test target.**

Append to `libs/markoff-live/tests/CMakeLists.txt` (next to
`tst_live_render_qml_integration`):

```cmake
qt_add_executable(tst_live_render_kind_transition_invariant
    tst_live_render_kind_transition_invariant.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
    LiveRealisticInputHarness.h
    LiveRealisticInputHarness.cpp
)
target_link_libraries(tst_live_render_kind_transition_invariant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Test
    markoff_live markoff_core)
add_test(NAME tst_live_render_kind_transition_invariant
         COMMAND tst_live_render_kind_transition_invariant)
```

- [ ] **Step 3: Reconfigure + build.**

```bash
cmake -S . -B build-dev 2>&1 | tail -3
cmake --build build-dev --target tst_live_render_kind_transition_invariant -j 8 2>&1 | tail -3
```

- [ ] **Step 4: Run the test — expect ALL THREE slots to FAIL (RED).**

```bash
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -15
```

Expected: 3 of 3 slots fail. Failure mode: each `QCOMPARE(textEditAfter,
textEditBefore)` reports two distinct pointers. This proves the test
is correctly red against the current architecture (delegate is
destroyed+recreated on kind transition via `beginResetModel`).

If any slot passes: the test is too lenient — debug before continuing.
A common cause: the kind transition didn't actually fire (cursor not
at column 0). The slot's `QVERIFY(fix.waitForKindAt(...))` should
catch that earlier; if not, add diagnostics.

- [ ] **Step 5: Commit the failing test.**

```bash
git add libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: test (RED) — TextEdit identity preserved across kind transition

Invariant test for the tier-3 spec §6.1. All three slots currently
FAIL: under the kindOnlySwap / beginResetModel architecture, the
delegate (and its TextEdit) is destroyed and recreated on every
within-class kind transition, so the QQuickItem pointer changes.

Lands red here; turns green once Task 7 switches BlockKey to use
delegateClass instead of kind.
EOF
)"
```

---

## Task 6: Create `UnifiedInlineTextDelegate.qml`

**Files:**
- Create: `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/qmldir`
- Modify: `libs/markoff-live/CMakeLists.txt` (QML file list)

This is the largest single task in the plan. The unified delegate
merges Paragraph + Heading + Blockquote + ListItem rendering. The
shape:

- Persistent `TextEdit` with bindings reactive on `model.kind`.
- Conditional ornaments via `Loader { active: root.kind === "..." }`:
  - Blockquote bar (left rule)
  - List-item marker (Text with task-toggle MouseArea)
- Conditional theme-slot computation: TextDefault for paragraph;
  Heading1..6 for heading (per `model.headingLevel`); Blockquote for
  blockquote; ListItem for list-item (falls back to TextDefault if
  the slot doesn't exist in the theme).
- Conditional top/bottom padding (heading uses 6/2; others 4/4).
- Conditional `TextEdit.leftPadding` (list-item uses marker width +
  12; others 8).
- Conditional Keys bindings (heading adds Ctrl+Shift+0..6 level
  change; others share the same structural-key forwarding).

The bindings themselves are largely copied verbatim from the existing
delegates. The differences are gated on `root.kind` checks.

- [ ] **Step 1: Write `UnifiedInlineTextDelegate.qml`.**

Create `libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml`:

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

/// Unified delegate for the four text-inline block kinds: paragraph,
/// heading, blockquote, list-item. The `TextEdit` is persistent across
/// within-class kind transitions; ornaments (heading sizing,
/// blockquote bar, list-item marker) bind conditionally on
/// `model.kind`. Spec §5.1 of
/// `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string kind: model.kind
    readonly property int headingLevel: model.headingLevel || 0
    readonly property int indentLevel: model.indentLevel || 0
    readonly property string markerStyle: model.markerStyle || ""
    readonly property int markerNumber: model.markerNumber || 0
    readonly property bool checked: model.checked || false
    readonly property string blockText: model.text
    property var blockAnchor: undefined  // captured at Component.onCompleted

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    // True when this block is fully covered by a multi-block selection.
    // Drives marker-background paint (list-item only).
    property bool _fullySelected: false

    // -------- Theme slot dispatch --------
    readonly property int themeSlot: {
        if (kind === "heading") {
            // Slot enum: TextDefault=0, Heading1=1...Heading6=6.
            switch (headingLevel) {
                case 1: return 1
                case 2: return 2
                case 3: return 3
                case 4: return 4
                case 5: return 5
                default: return 6
            }
        }
        // Paragraph, blockquote, list-item all fall to TextDefault.
        // Blockquote-specific italic/color is theme-driven if the theme
        // chooses to override on a Blockquote slot in the future.
        return 0
    }

    readonly property int markerSlot: 0  // TextDefault, used for list marker

    // -------- List-item geometry --------
    readonly property int indentPx: 8 + indentLevel * 24
    readonly property string markerText: {
        if (markerStyle === "dot")   return markerNumber + "."
        if (markerStyle === "paren") return markerNumber + ")"
        if (markerStyle === "minus") return "-"
        if (markerStyle === "plus")  return "+"
        if (markerStyle === "star")  return "*"
        if (markerStyle === "task")  return checked ? "[x]" : "[ ]"
        return "•"
    }

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    // -------- Blockquote left bar (conditional) --------
    Rectangle {
        id: blockquoteBar
        visible: root.kind === "blockquote"
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            leftMargin: 4
        }
        width: 3
        color: palette.mid
    }

    // -------- List-item marker selection background (conditional) --------
    Rectangle {
        anchors.fill: markerLabel
        visible: root.kind === "list-item" && root._fullySelected
        color: palette.highlight
        z: -1
    }

    // -------- List-item marker (conditional) --------
    Text {
        id: markerLabel
        visible: root.kind === "list-item"
        anchors {
            left: parent.left
            top: parent.top
        }
        leftPadding: root.indentPx
        topPadding: 2
        text: root.markerText
        font.family: (root.liveBinding && root.liveBinding.theme)
                       ? root.liveBinding.themeFamilyFor(8 /* CodeBlock → Monospace */)
                       : "monospace"
        font.pixelSize: (root.liveBinding && root.liveBinding.theme
                          ? root.liveBinding.themePixelSizeFor(root.markerSlot)
                          : 14)
                        * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
        color: root._fullySelected ? palette.highlightedText : palette.text

        MouseArea {
            visible: root.markerStyle === "task"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!root.liveBinding || !root.liveBinding.document) return
                root.liveBinding.document.toggleListItemChecked(model.blockAnchor)
            }
        }
    }

    Connections {
        target: root.selectionView
        function onSelectionChanged() {
            if (root.kind !== "list-item") { root._fullySelected = false; return }
            const sv = root.selectionView
            if (!sv) { root._fullySelected = false; return }
            const r = sv.rangeForBlock(root.modelIndex)
            if (!r || r.x < 0) { root._fullySelected = false; return }
            root._fullySelected = (r.x === 0 && r.y >= edit.length)
        }
    }

    // -------- Persistent TextEdit --------
    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: root.kind === "list-item"
            ? markerLabel.implicitWidth + 12
            : (root.kind === "blockquote" ? 16 : 8)
        rightPadding: 8
        topPadding: root.kind === "heading" ? 6 : 4
        bottomPadding: root.kind === "heading" ? 2 : 4
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        readonly property var theme: root.liveBinding ? root.liveBinding.theme : null
        readonly property real fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0

        font.pixelSize: theme
            ? root.liveBinding.themePixelSizeFor(root.themeSlot) * fontScale
            : 14 * fontScale
        font.family: theme ? root.liveBinding.themeFamilyFor(root.themeSlot) : ""
        font.bold: theme
            ? root.liveBinding.themeIsBold(root.themeSlot)
            : (root.kind === "heading" && root.headingLevel <= 3)
        font.italic: theme ? root.liveBinding.themeIsItalic(root.themeSlot) : false
        color: palette.text
        selectByMouse: false
        persistentSelection: true

        onCursorPositionChanged: {
            const cs = root.liveBinding ? root.liveBinding.cursorState : null
            if (model.blockAnchor !== undefined && cs) {
                if (editBinding.isApplyingTextUpdate()
                        && cs.focusedAnchorRow === root.modelIndex) {
                    if (cs.focusedQtPos >= 0
                            && cs.focusedQtPos <= edit.length
                            && edit.cursorPosition !== cs.focusedQtPos) {
                        edit.cursorPosition = cs.focusedQtPos
                    }
                    return
                }
                cs.syncFromTextEdit(model.blockAnchor, edit.cursorPosition)
            }
        }

        InlineHighlighterAttached {
            target: edit.textDocument
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
            fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
            caretPosition: edit.activeFocus ? edit.cursorPosition : -1
            selectionStart: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                            ? edit.selectionStart : -1
            selectionEnd: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                          ? edit.selectionEnd : -1
        }

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.liveBinding) { event.accepted = false; return }

            const k = event.key
            const mods = event.modifiers
            const isStructural = (k === Qt.Key_Return || k === Qt.Key_Enter
                               || k === Qt.Key_Escape || k === Qt.Key_Backspace
                               || k === Qt.Key_Delete)
            const isNav = (k === Qt.Key_Up || k === Qt.Key_Down
                        || k === Qt.Key_Left || k === Qt.Key_Right
                        || k === Qt.Key_Home || k === Qt.Key_End
                        || k === Qt.Key_PageUp || k === Qt.Key_PageDown)
            const isHeadingLevelChange = root.kind === "heading"
                && (mods & Qt.ControlModifier) && (mods & Qt.ShiftModifier)
                && k >= Qt.Key_0 && k <= Qt.Key_6

            if (isHeadingLevelChange) {
                const sh = root.liveBinding.structuralKeyHandler
                if (sh) event.accepted = sh.tryHandle(k, mods, root.modelIndex,
                                                      edit.cursorPosition,
                                                      edit.selectionStart === edit.selectionEnd,
                                                      model.text)
                return
            }
            if (isStructural) {
                const sh = root.liveBinding.structuralKeyHandler
                if (!sh) return
                event.accepted = sh.tryHandle(k, mods, root.modelIndex,
                                               edit.cursorPosition,
                                               edit.selectionStart === edit.selectionEnd,
                                               model.text)
                return
            }
            if (isNav) {
                const nh = root.liveBinding.navigationController
                if (!nh) return
                event.accepted = (nh.tryHandle(k, mods, root.modelIndex,
                                                edit.cursorPosition,
                                                edit, model.text) === 1)
                return
            }
        }

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            const blockLen = length
            const start = Math.min(r.x, blockLen)
            const end   = Math.min(r.y, blockLen)
            if (start === end) {
                cursorPosition = start
                return
            }
            const myIdx = model.index
            const cursorAtEnd = (myIdx === sv.activeBlock())
                ? (sv.activeQtPos() === end)
                : (sv.activeBlock() > myIdx)
            const cursorPos = cursorAtEnd ? end   : start
            const otherPos  = cursorAtEnd ? start : end
            cursorPosition = otherPos
            moveCursorSelection(cursorPos, TextEdit.SelectCharacters)
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }
    }

    function positionAt(x, y) {
        return edit.positionAt(x - edit.leftPadding, y - edit.topPadding)
    }

    function takeFocus(qtPos: int) {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lineH = edit.font.pixelSize
                const targetY = (hint === 1) ? lineH * 0.5 : edit.contentHeight - lineH * 0.5
                edit.cursorPosition = edit.positionAt(desiredX - edit.leftPadding, targetY)
                edit.forceActiveFocus()
                return
            }
        }
        edit.cursorPosition = Math.min(Math.max(qtPos, 0), edit.length)
        edit.forceActiveFocus()
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        blockAnchor = model.blockAnchor
        if (cs) cs.delegateAvailable(blockAnchor, model.kind, root)
    }

    Component.onDestruction: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && blockAnchor !== undefined) cs.delegateGoingAway(blockAnchor, root)
    }
}
```

- [ ] **Step 2: Register in `qmldir`.**

Inspect `libs/markoff-live/qml/delegates/qmldir`:

```bash
cat libs/markoff-live/qml/delegates/qmldir
```

Append:

```
UnifiedInlineTextDelegate 1.0 UnifiedInlineTextDelegate.qml
```

(Do **not** remove the four superseded entries yet — Task 11 deletes
them after the dispatcher transition is proven.)

- [ ] **Step 3: Register in CMake QML module file list.**

Inspect `libs/markoff-live/CMakeLists.txt` for the QML module
`qt_add_qml_module(...)` call and find the `QML_FILES` list. Append
`qml/delegates/UnifiedInlineTextDelegate.qml` to that list.

- [ ] **Step 4: Build.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: clean build. The new delegate compiles but is not used.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/qml/delegates/UnifiedInlineTextDelegate.qml \
        libs/markoff-live/qml/delegates/qmldir \
        libs/markoff-live/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: add UnifiedInlineTextDelegate — paragraph/heading/blockquote/list-item

Single QML delegate covering the four text-inline block kinds. Holds
a persistent TextEdit; ornaments (blockquote bar, list-item marker)
bind conditionally on model.kind. Wired in qmldir; not consumed by
LiveView.qml yet.

Spec §5.1.
EOF
)"
```

---

## Task 7: Switch DelegateChooser to delegateClass + change BlockKey

**Files:**
- Modify: `libs/markoff-live/qml/LiveView.qml`
- Modify: `libs/markoff-live/include/markoff/live/BlockRecord.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/src/AstBlockDiff.cpp` (if it references `BlockKey::kind`)

The dispatcher switch and the BlockKey change must land together: if
the dispatcher is keyed on `delegateClass` but `BlockKey` still uses
`kind`, the kindOnlySwap detector still fires (and tries to do a
model reset that the dispatcher now treats as a no-op-because-same-
delegateClass — risk of inconsistent state). If `BlockKey` uses
`delegateClass` but the dispatcher still keys on `kind`, the diff
treats paragraph↔heading as Equal but the chooser still has 8
hardcoded choices, and the kind change won't propagate to the
delegate type (incorrect rendering on cross-class).

Land them in one commit.

- [ ] **Step 1: Change `BlockKey` definition.**

Edit `libs/markoff-live/include/markoff/live/BlockRecord.h`. Find
`struct BlockKey` (line 55):

```cpp
struct MARKOFF_LIVE_EXPORT BlockKey {
    QString              kind;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};
```

Replace with:

```cpp
/// Diff identity key. Two blocks with the same delegateClass+anchor are
/// "the same row" across parses: within-class kind changes (paragraph→
/// heading) keep the row alive (delegate persists, TextEdit persists)
/// and propagate via dataChanged role hints. Cross-class kind changes
/// (paragraph→hr) produce Delete+Insert. See Markoff::Live::delegateClassFor.
struct MARKOFF_LIVE_EXPORT BlockKey {
    QString              delegateClass;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return delegateClass == o.delegateClass && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};
```

- [ ] **Step 2: Update the only `BlockKey` constructor call.**

Edit `libs/markoff-live/src/LiveListModelBinding.cpp`. Find line 393:

```cpp
nextKeys.append(BlockKey{ r.kind, r.blockAnchor });
```

Replace with:

```cpp
nextKeys.append(BlockKey{ r.delegateClass, r.blockAnchor });
```

- [ ] **Step 3: Check `AstBlockDiff` for `BlockKey::kind` references.**

```bash
grep -n "\.kind" libs/markoff-live/src/AstBlockDiff.cpp libs/markoff-live/src/AstBlockDiff.h 2>&1 | head
```

The diff operates on `BlockKey::operator==`, not on the `.kind` field
directly. If grep returns no hits, no edit needed. If it returns hits
that reference `BlockKey::kind`, rename them to `delegateClass`.

- [ ] **Step 3b: Update `BlockKey` constructor calls in unit tests.**

Two unit-test files build `BlockKey` directly from `BlockRecord` fields
and will break otherwise:

```bash
grep -n "BlockKey{ r\.kind" libs/markoff-live/tests/tst_live_render_cursor.cpp \
                            libs/markoff-live/tests/tst_live_render_block_model.cpp
```

Expected: hits at `tst_live_render_cursor.cpp:41` and
`tst_live_render_block_model.cpp:30` inside a `keyOf(r)` helper.

In each file, edit the helper. Before:

```cpp
static BlockKey keyOf(const BlockRecord &r)
{
    return BlockKey{ r.kind, r.blockAnchor };
}
```

After:

```cpp
#include "KindDispatch.h"   // add to includes near top of file

static BlockKey keyOf(const BlockRecord &r)
{
    const QString cls = r.delegateClass.isEmpty()
        ? Markoff::Live::delegateClassFor(r.kind)
        : r.delegateClass;
    return BlockKey{ cls, r.blockAnchor };
}
```

The `isEmpty()` branch handles tests that build `BlockRecord` manually
without populating `delegateClass`. Production code (Task 4) always
populates it; the fallback is purely defensive for the unit tests.

Also update `tst_live_render_cursor.cpp`'s and
`tst_live_render_block_model.cpp`'s CMakeLists entries to add the
include path:

```bash
grep -n "tst_live_render_cursor\|tst_live_render_block_model" libs/markoff-live/tests/CMakeLists.txt | head -8
```

If either `add_executable` block lacks `target_include_directories(...
PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)`, add it. The
`tst_live_render_kind_transition` test already has this pattern as
a reference.

- [ ] **Step 4: Switch the DelegateChooser in `LiveView.qml`.**

Edit `libs/markoff-live/qml/LiveView.qml`. Find the `DelegateChooser`
block (around lines 27–37):

```qml
delegate: DelegateChooser {
    role: "kind"
    DelegateChoice { roleValue: "paragraph";  delegate: ParagraphDelegate  {} }
    DelegateChoice { roleValue: "heading";    delegate: HeadingDelegate    {} }
    DelegateChoice { roleValue: "code-block"; delegate: CodeBlockDelegate  {} }
    DelegateChoice { roleValue: "hr";         delegate: HorizontalRuleDelegate {} }
    DelegateChoice { roleValue: "image";      delegate: ImageDelegate      {} }
    DelegateChoice { roleValue: "list-item";  delegate: ListItemDelegate   {} }
    DelegateChoice { roleValue: "blockquote"; delegate: BlockquoteDelegate {} }
    DelegateChoice { roleValue: "math";       delegate: MathDelegate       {} }
}
```

Replace with:

```qml
delegate: DelegateChooser {
    role: "delegateClass"
    DelegateChoice { roleValue: "text-inline"; delegate: UnifiedInlineTextDelegate {} }
    DelegateChoice { roleValue: "code-block";  delegate: CodeBlockDelegate         {} }
    DelegateChoice { roleValue: "math";        delegate: MathDelegate              {} }
    DelegateChoice { roleValue: "hr";          delegate: HorizontalRuleDelegate    {} }
    DelegateChoice { roleValue: "image";       delegate: ImageDelegate             {} }
}
```

- [ ] **Step 5: Build.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 6: Run the §6.1 invariant test — expect GREEN.**

```bash
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -10
```

Expected: 3 of 3 slots PASS. The TextEdit pointer is now stable across
paragraph↔heading and paragraph↔list-item transitions because the
delegate isn't destroyed (same delegateClass for both endpoints).

If a slot fails: investigate. Likely causes:
- The `delegateClass` role isn't reaching the QML side (check that
  `LiveBlockModel::roleNames` includes `delegateClass` from Task 3).
- The chooser is picking a wrong delegate (check that `kind` is
  exposed AND `delegateClass` is exposed simultaneously).
- A timing issue — try increasing `QTest::qWait` to 100ms in
  `typeAscii`.

- [ ] **Step 7: Run the broader integration suite for regressions.**

```bash
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | grep -E "^FAIL|^Totals"
```

Expected: same 4 failing slots as baseline. If new failures appear
(especially around kind transitions), debug before continuing.

- [ ] **Step 8: Run the broader live-render suite.**

```bash
./scripts/run-tests.sh -R 'tst_live_render_' --timeout 35 2>&1 | tail -15
```

Expected: same 8 failing suites as baseline (plus 1 timeout on
`tst_live_render_structural` which takes ~30s).

- [ ] **Step 9: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/BlockRecord.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/qml/LiveView.qml \
        libs/markoff-live/src/AstBlockDiff.cpp libs/markoff-live/src/AstBlockDiff.h 2>/dev/null
# Above line: -cp may report "did not match" for AstBlockDiff if no
# edits were needed. That's fine.
git status --short
git commit -m "$(cat <<'EOF'
markoff-live: switch DelegateChooser to delegateClass; stable row identity

BlockKey carries delegateClass (text-inline | code-block | math | hr |
image) instead of kind. Within-class kind changes (paragraph→heading,
paragraph→list-item, etc.) now produce Equal ops in the diff; the
delegate and its TextEdit persist across the transition.

LiveView.qml's DelegateChooser keys on delegateClass; choices reduce
from 8 to 5. text-inline maps to UnifiedInlineTextDelegate (Task 6).

Invariant test tst_live_render_kind_transition_invariant goes from RED
to GREEN: the TextEdit QQuickItem pointer is now preserved across
paragraph↔heading and paragraph↔list-item.

Spec §3, §4, §5.3, §5.4.
EOF
)"
```

---

## Task 8: Retire `kindOnlySwap` + delete bandaid `Connections` block

**Files:**
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`
- Modify: `libs/markoff-live/qml/LiveView.qml`

After Task 7, the `kindOnlySwap` branch in `applyOps` is dead code:
- With `BlockKey { delegateClass, anchor }`, a within-class kind
  change produces an Equal op (not Delete+Insert) — the kindOnlySwap
  detector's primary condition (`ops.size() == nextRecords.size() + 1`)
  never holds.
- The bandaid `Connections` block in `LiveView.qml` only fires when
  the model emits `modelAboutToBeReset`/`modelReset`, which now never
  happens for kind changes.

Delete both. Verify nothing regresses.

- [ ] **Step 1: Delete `kindOnlySwap` branch in `LiveBlockModel::applyOps`.**

Edit `libs/markoff-live/src/LiveBlockModel.cpp`. Locate lines 106–162
(the detector + the `if (kindOnlySwap) { beginResetModel(); … }`
branch). Replace the entire block — from the long comment starting
with `// Detect kind-change-only ops` down to and including the
closing `}` of `if (kindOnlySwap)` — with nothing. The function's
first executable statement after the new deletion should be `int row
= 0;` (the start of the generic op loop, currently around line 159).

After deletion, `applyOps` opens directly with:

```cpp
void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords,
                              quint64 parseInputEditSeq)
{
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                // ...
```

- [ ] **Step 2: Extend the Equal op's `dataChanged` to emit kind role hints.**

In the Equal branch (around current lines 162–191, after the
deletion above), find:

```cpp
if (m_rows[row] != merged) {
    m_rows[row] = merged;
    Q_EMIT dataChanged(index(row), index(row));
}
```

Replace with:

```cpp
if (m_rows[row] != merged) {
    // Collect role hints for the QML side. Without explicit hints,
    // DelegateChooser doesn't re-evaluate per-row bindings as
    // promptly; with hints, the UnifiedInlineTextDelegate's
    // root.kind / themeSlot / leftPadding bindings re-evaluate
    // immediately.
    QList<int> roles;
    if (m_rows[row].kind          != merged.kind)          roles << KindRole << DelegateClassRole;
    if (m_rows[row].headingLevel  != merged.headingLevel)  roles << HeadingLevelRole;
    if (m_rows[row].headingForm   != merged.headingForm)   roles << HeadingFormRole;
    if (m_rows[row].codeLanguage  != merged.codeLanguage)  roles << CodeLanguageRole;
    if (m_rows[row].markerStyle   != merged.markerStyle)   roles << MarkerStyleRole;
    if (m_rows[row].markerNumber  != merged.markerNumber)  roles << MarkerNumberRole;
    if (m_rows[row].indentLevel   != merged.indentLevel)   roles << IndentLevelRole;
    if (m_rows[row].checked       != merged.checked)       roles << CheckedRole;
    if (m_rows[row].looseRun      != merged.looseRun)      roles << LooseRunRole;
    if (m_rows[row].text          != merged.text)          roles << TextRole;
    if (m_rows[row].attrs         != merged.attrs)         roles << BlockAttrsRole;
    m_rows[row] = merged;
    Q_EMIT dataChanged(index(row), index(row), roles);
}
```

(If a role's underlying field name differs from what's listed above,
follow the `BlockRecord` field names exactly. The `text` and
`inlineSpans` are handled in their own branches below — leave those
intact.)

- [ ] **Step 3: Delete the bandaid `Connections` block in `LiveView.qml`.**

Edit `libs/markoff-live/qml/LiveView.qml`. Find the block starting
with the comment:

```
// LiveBlockModel routes kind-only swaps (paragraph→heading on `# `)
```

Delete from that comment through the closing `}` of the `Connections
{ … onModelReset { … } }` block, plus the two diagnostic property
declarations (`property int _modelResetCount`; if `property real
_lastSavedContentY` still exists from a previous iteration, delete it
too).

After deletion, the `ListView` block runs straight from the
`ScrollBar.vertical: ScrollBar { … }` line into the `delegate:
DelegateChooser { … }` line that Task 7 just rewrote.

- [ ] **Step 4: Build.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 5: Run the §6.1 invariant test — still GREEN.**

```bash
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -10
```

Expected: 3 of 3 pass.

- [ ] **Step 6: Run the integration suite.**

```bash
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | grep -E "^FAIL|^Totals"
```

Expected: same 4 baseline failures. **My previous scroll-preservation
test (`typing_hash_preserves_scroll_position`) MUST still pass** —
the new architecture eliminates the jump-to-top by removing the
model reset entirely, so `contentY` is preserved naturally.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-live/src/LiveBlockModel.cpp \
        libs/markoff-live/qml/LiveView.qml
git commit -m "$(cat <<'EOF'
markoff-live: retire kindOnlySwap workaround and contentY bandaid

The kindOnlySwap detector + beginResetModel branch in
LiveBlockModel::applyOps becomes dead code under BlockKey-by-
delegateClass: within-class kind changes are Equal ops, not Delete+
Insert pairs. Delete the entire branch (lines ~106–162) and let kind
transitions flow through the standard Equal-op dataChanged emission.

The Equal-op dataChanged now ships a role-hint list (KindRole +
DelegateClassRole + per-attr roles) so the QML side's bindings re-
evaluate promptly.

LiveView.qml's Connections-on-modelReset block (introduced as the
scroll bandaid in d92e1e2) is also deleted: the model reset it
worked around no longer fires. The Qt.callLater count goes from
2 → 1 (per CLAUDE.md seam guidance).

Closes the open thread named in docs/queue.md:63. Closes the dogfood
"jump-to-top on `#`/`-`" and "throws me about" symptoms at the root.

Spec §3 (retirement table), §5.2, §5.4.
EOF
)"
```

---

## Task 9: Falsifiability proof (per invariant 4)

**Files:**
- Modify (temporarily): `libs/markoff-live/src/KindDispatch.cpp`

- [ ] **Step 1: Write the falsifying stub.**

Edit `libs/markoff-live/src/KindDispatch.cpp`. Replace the body of
`delegateClassFor` with a stub that defeats the bucketing — it
returns the kind unchanged, restoring the pre-tier-3 behaviour
(every kind in its own bucket; paragraph↔heading is a class change):

```cpp
QString delegateClassFor(const QString &kind)
{
    // FALSIFIABILITY PROOF — REVERTS NEXT.
    // Each kind in its own bucket means the diff treats kind changes as
    // class changes, i.e. Delete+Insert. The §6.1 invariant test must
    // FAIL under this stub (TextEdit pointer changes again).
    return kind.isEmpty() ? QStringLiteral("text-inline") : kind;
}
```

- [ ] **Step 2: Build + run §6.1 — expect all three slots to FAIL.**

```bash
cmake --build build-dev --target markoff_live tst_live_render_kind_transition_invariant -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -15
```

Expected: 3 of 3 slots FAIL. The failure mode: each slot's
`QCOMPARE(textEditAfter, textEditBefore)` reports distinct pointers.

If any slot passes under the stub: **STOP**. The test is too weak —
return to Task 5 and strengthen the assertion. A common cause: the
test isn't actually triggering the kind transition; verify
`fix.waitForKindAt(...)` reports the new kind before the pointer
comparison.

- [ ] **Step 3: Commit the stub.**

```bash
git add libs/markoff-live/src/KindDispatch.cpp
git commit -m "$(cat <<'EOF'
markoff-live: stub — delegateClassFor returns raw kind (FALSIFIABILITY PROOF, REVERTS NEXT)

Per invariant 4. Restores per-kind bucketing → within-class kind
changes again produce Delete+Insert → TextEdit pointer changes →
tst_live_render_kind_transition_invariant fails all 3 slots.

Reverts in the next commit.
EOF
)"
```

- [ ] **Step 4: Revert the stub.**

```bash
git revert --no-edit HEAD
```

This produces a `Revert "markoff-live: stub …"` commit that restores
the bucketing.

- [ ] **Step 5: Re-verify GREEN.**

```bash
cmake --build build-dev --target markoff_live tst_live_render_kind_transition_invariant -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -10
```

Expected: 3 of 3 pass.

The falsifiability lineage (stub commit + revert commit) is now in
the git history, matching the tier-2 pattern (`0aef9f3` / `6b32482`).

---

## Task 10: Add §6.2 cross-class transition test

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp`

- [ ] **Step 1: Add the slot.**

Edit `libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp`.
Append a new slot inside `class TestLiveRenderKindTransitionInvariant`:

```cpp
    /// Spec §6.2: cross-class kind transitions DO swap the delegate.
    /// Sanity check: paragraph→hr is a delegateClass change (text-inline
    /// → hr), so the standard Delete+Insert path runs and produces a
    /// different delegate.
    void paragraph_to_hr_swaps_delegate() {
        QmlIntegrationFixture fix("x", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.placeCursorAtPos(0, 0);
        QTest::keyClick(fix.window(), Qt::Key_Delete);  // clear "x"
        QTest::qWait(30);
        QCoreApplication::processEvents();

        QQuickItem *delegateBefore = fix.delegateAt(0);
        QVERIFY(delegateBefore);
        const QByteArray classBefore = delegateBefore->metaObject()->className();

        typeAscii(fix, '-');
        typeAscii(fix, '-');
        typeAscii(fix, '-');
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("hr"), 2000));

        // After paragraph→hr, the delegate must be a DIFFERENT instance.
        // The unified text-inline delegate is gone; HorizontalRuleDelegate
        // is in its place.
        QQuickItem *delegateAfter = fix.delegateAt(0);
        QVERIFY(delegateAfter);
        QVERIFY2(delegateAfter != delegateBefore,
                 "expected cross-class transition to swap delegate");
        const QByteArray classAfter = delegateAfter->metaObject()->className();
        QVERIFY2(classAfter.contains("HorizontalRule"),
                 qPrintable(QString("expected HorizontalRule delegate, got %1")
                            .arg(QString::fromUtf8(classAfter))));
        QVERIFY(classAfter != classBefore);
    }
```

- [ ] **Step 2: Build + run.**

```bash
cmake --build build-dev --target tst_live_render_kind_transition_invariant -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -10
```

Expected: 4 of 4 pass.

- [ ] **Step 3: Commit.**

```bash
git add libs/markoff-live/tests/tst_live_render_kind_transition_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cross-class kind transition still swaps delegate

Sanity-check companion to the within-class invariant. paragraph→hr
crosses delegateClass boundaries (text-inline → hr), so the standard
Delete+Insert path runs and the delegate is genuinely replaced. The
HorizontalRuleDelegate has always handled this path correctly; the
test pins it so the within-class refactor doesn't accidentally
extend its scope.

Spec §6.2.
EOF
)"
```

---

## Task 11: Add §6.3 paste-styling test

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`

- [ ] **Step 1: Inspect the existing paste tests for the pattern.**

```bash
grep -n "pasteText\|paste" libs/markoff-live/tests/tst_live_render_qml_integration.cpp | head -10
```

Read the surrounding context of the first hit to understand the
paste-test idiom used by the fixture.

- [ ] **Step 2: Add the slot.**

Edit `libs/markoff-live/tests/tst_live_render_qml_integration.cpp`.
Append a new slot inside `class TestLiveRenderQmlIntegration` (before
the closing `};` of the class):

```cpp
    /// Spec §6.3: pasting markdown that promotes the current paragraph
    /// to a heading renders the heading at heading-level-1 font size,
    /// not paragraph size. Closes the dogfood "cross-block paste loses
    /// header styling" regression.
    void paste_heading_into_paragraph_renders_as_heading() {
        QmlIntegrationFixture fix("", /*expectedRowCount=*/0);

        // Programmatically insert one empty paragraph so paste has a
        // landing block (mirrors the typing_preserves_insertion_order
        // setup pattern).
        {
            Markoff::UndoLog::Transaction t(fix.document()->d2UndoLog());
            fix.document()->d2InsertBlock(Markoff::BlockId{},
                                          Markoff::BlockKind::Paragraph, t);
        }
        QVERIFY(fix.waitForRowCount(1, 2000));
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        fix.clickOnBlock(0);
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Snapshot paragraph-size pixelSize for comparison.
        QQuickItem *te = fix.delegateTextEdit(0);
        QVERIFY(te);
        const int paragraphPx = te->property("font").value<QFont>().pixelSize();
        QVERIFY2(paragraphPx > 0,
                 qPrintable(QString("expected positive paragraph pixelSize, got %1")
                            .arg(paragraphPx)));

        fix.pasteText(QStringLiteral("# heading"));
        QVERIFY(fix.waitForKindAt(0, QStringLiteral("heading"), 2000));

        const int headingPx = fix.delegateTextEdit(0)
                                ->property("font").value<QFont>().pixelSize();
        QVERIFY2(headingPx > paragraphPx,
                 qPrintable(QString("expected heading pixelSize > paragraph; "
                                    "paragraph=%1 heading=%2")
                            .arg(paragraphPx).arg(headingPx)));
    }
```

- [ ] **Step 3: Build + run the integration suite.**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_qml_integration paste_heading_into_paragraph_renders_as_heading 2>&1 | tail -10
```

Expected: the new slot passes. If the slot fails, investigate the
paste pipeline — it may not actually trigger the kind transition
(some paste implementations bypass inferBlockKind). Adjust the test
to match the actual paste path's behaviour if needed.

- [ ] **Step 4: Re-run full integration suite to confirm no regressions.**

```bash
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | grep -E "^FAIL|^Totals"
```

Expected: same 4 baseline failures + 0 new failures + 1 new passing
slot (the paste-styling one).

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/tests/tst_live_render_qml_integration.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — paste-heading renders at heading font size

Closes the second dogfood regression named in
docs/handoff/2026-05-15-tier-2-completion.md. Pastes "# heading" into
an empty paragraph and asserts the resulting TextEdit's pixelSize is
larger than the paragraph baseline. Under tier-3's stable-row-identity
architecture, the kind role's dataChanged propagates to the unified
delegate's font binding without delegate replacement.

Spec §6.3.
EOF
)"
```

---

## Task 12: Delete superseded delegate files

**Files:**
- Delete: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`
- Delete: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Delete: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`
- Delete: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/qmldir`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Confirm no other code imports the superseded delegates by name.**

```bash
grep -rn "ParagraphDelegate\|HeadingDelegate\|BlockquoteDelegate\|ListItemDelegate" \
    libs/markoff-live/qml/ \
    libs/markoff-live/src/ \
    libs/markoff-live/include/ \
    libs/markoff-live/tests/ \
    libs/markoff-live/app/ 2>&1 | grep -v "qml/delegates/qmldir" | head
```

Expected: only the four files about to be deleted reference them
internally. Other references should be in test files that look up by
QML class name string (`metaObject()->className()`) — those are fine
since `UnifiedInlineTextDelegate` reports a different class name. If
any test asserts on the old class names (`"ParagraphDelegate"` etc.),
update those assertions to `"UnifiedInlineTextDelegate"`.

If any production code reference exists, **stop** — there's a
dependency the plan missed. Diagnose before continuing.

- [ ] **Step 2: Delete the four delegate files.**

```bash
rm libs/markoff-live/qml/delegates/ParagraphDelegate.qml
rm libs/markoff-live/qml/delegates/HeadingDelegate.qml
rm libs/markoff-live/qml/delegates/BlockquoteDelegate.qml
rm libs/markoff-live/qml/delegates/ListItemDelegate.qml
```

- [ ] **Step 3: Remove the qmldir entries.**

Edit `libs/markoff-live/qml/delegates/qmldir`. Remove the four lines
that name `ParagraphDelegate`, `HeadingDelegate`, `BlockquoteDelegate`,
`ListItemDelegate`. Keep all other entries (including
`UnifiedInlineTextDelegate`).

- [ ] **Step 4: Remove from CMake QML_FILES list.**

Edit `libs/markoff-live/CMakeLists.txt`. Find the `QML_FILES` list in
the `qt_add_qml_module` block. Remove the four lines naming the
superseded delegates.

- [ ] **Step 5: Build.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 6: Run the integration + invariant suites.**

```bash
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | grep -E "^FAIL|^Totals"
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -5
```

Expected: 4 baseline failures + invariant suite all green.

- [ ] **Step 7: Commit.**

```bash
git add -A libs/markoff-live/qml/delegates/ \
        libs/markoff-live/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: delete superseded paragraph/heading/blockquote/listitem delegates

The four text-inline delegates are subsumed by
UnifiedInlineTextDelegate (Task 6); under the new dispatch they were
unreachable from LiveView.qml since Task 7. Delete the QML files,
qmldir entries, and CMake QML_FILES list entries.

Spec §5.6.
EOF
)"
```

---

## Task 13: Update seam-guidance documentation

**Files:**
- Modify: `libs/markoff-live/CLAUDE.md`
- Modify: `docs/queue.md`

- [ ] **Step 1: Update Qt.callLater count in `libs/markoff-live/CLAUDE.md`.**

Find the line:

```
You will encounter `Qt.callLater` and re-entrance guards in
this directory (current count: 11 `Qt.callLater` sites across 8
delegate files; ...).
```

Replace with:

```
You will encounter `Qt.callLater` and re-entrance guards in
this directory (current count: 1 `Qt.callLater` site —
`MathDelegate.qml:113`, focus deferral during BlockInternalEdit;
re-entrance guards `m_applyingTextUpdate` in `LiveEditBinding` and
`m_applyingSessionSelection` in `LiveSelectionView`).
```

(The 11-site count was stale; the 2026-05-15 survey found 2 sites,
and tier-3 retired one of them.)

- [ ] **Step 2: Append a Discipline Log entry closing queue.md:63.**

Edit `docs/queue.md`. Locate the Discipline Log section (after the
existing line 63 entry). Append a new entry:

```
- 2026-05-15 `libs/markoff-live/src/LiveBlockModel.cpp:applyOps` —
  closes 2026-05-11 entry above. The kindOnlySwap detector +
  beginResetModel branch retired in tier 3 (commit hash TBD). Block
  kind now flows through `delegateClass` bucketing per spec
  `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`.
  Within-class kind transitions (paragraph↔heading, paragraph↔list-item,
  etc.) are dataChanged events on the same delegate; cross-class
  transitions still produce Delete+Insert.
```

(Replace `TBD` with the actual hash of the Task 8 commit once the
plan finishes — do this in Task 14's final commit.)

- [ ] **Step 3: Build + a final sanity check.**

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
./scripts/run-tests.sh --bin tst_live_render_kind_transition_invariant 2>&1 | tail -5
./scripts/run-tests.sh --bin tst_live_render_qml_integration 2>&1 | grep -E "^Totals"
```

Expected: clean build, invariant green, integration suite at baseline.

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/CLAUDE.md docs/queue.md
git commit -m "$(cat <<'EOF'
docs: update seam guidance — Qt.callLater count 11 → 1; close queue.md:63

CLAUDE.md's claim of "11 Qt.callLater sites" was stale (the 2026-05-15
survey found 2 actual sites). Tier 3 retired one (the LiveView.qml
contentY bandaid); MathDelegate.qml:113 remains.

Discipline log entry at queue.md closes the 2026-05-11 entry naming
the kindOnlySwap workaround as a candidate for retirement.
EOF
)"
```

---

## Task 14: Final regression run + dogfood

**Files:** none (verification + a manual dogfood pass).

- [ ] **Step 1: Run the full live-render suite.**

```bash
./scripts/run-tests.sh -R 'tst_live_render_|tst_kind_dispatch' --timeout 35 2>&1 | tail -25
```

Expected counts (per the baseline at Task 1):
- `tst_kind_dispatch` — 3 pass.
- `tst_live_render_kind_transition_invariant` — 4 pass.
- All other live-render binaries unchanged from baseline (8 binaries
  with pre-existing failures still fail with the same assertions).

- [ ] **Step 2: Run the full suite under offscreen.**

```bash
./scripts/run-tests.sh --timeout 35 2>&1 | tail -20
```

Expected: 11 of 201 fail (same baseline as Task 1, possibly +1 new
target if `tst_kind_dispatch` is counted in the total — confirm the
total is 202 or 203, not 200).

- [ ] **Step 3: Visual dogfood (user-driven).**

Ask the user to launch `markoff-live-app` and exercise the symptoms:

```bash
./build-dev/bin/markoff-live-app /tmp/scratch.md
```

The user verifies:

- Typing `#` at the start of a line: no flicker, no scroll jump.
- Typing `# ` to promote paragraph to heading: text resizes in place;
  cursor preserved.
- Typing `-` then space: paragraph → list-item, no flicker, marker
  appears.
- Rapid typing of `#`/`-`/`>`: no "throws me about" behaviour.
- Copy a section that includes a heading from one part of the
  document; paste elsewhere: pasted heading renders as heading.
- Scroll behaviour generally feels stable across edits.

If the user reports new visual regressions or any of the above
symptoms persist, the plan has missed something. Capture the symptom
verbatim and return to systematic-debugging.

- [ ] **Step 4: Fill in the TBD commit hash in queue.md.**

```bash
git log --oneline | grep "retire kindOnlySwap" | awk '{print $1}'
```

Note the hash. Edit `docs/queue.md` and replace the literal text
`(commit hash TBD)` from Task 13 Step 2 with `(commit <hash>)`.

```bash
git add docs/queue.md
git commit -m "docs: queue.md — record commit hash for kindOnlySwap retirement"
```

- [ ] **Step 5: Update the project memory.**

Update `/home/clinton/.claude/projects/-home-clinton-dev-Markoff/memory/MEMORY.md`
to add an entry pointing at the new spec + plan. Add a new memory
file `project_tier3_completion.md` describing what landed. (This is
the standard handoff cadence; see `project_tier2_completion.md` as
the template.)

This step is bookkeeping for future agents; not a code change.

---

## Done

When all of Task 1–14 are checked off:

- The flicker on `#`/`-` typing is gone (model reset retired).
- Cross-block paste with a heading renders as a heading (same path).
- One Qt.callLater site retires (LiveView.qml's contentY bandaid).
- Four delegate files retire (Paragraph, Heading, Blockquote,
  ListItem) → one (UnifiedInlineTextDelegate).
- queue.md:63's open thread closes.
- Invariant test pins the new architecture (with a falsifiability
  proof in the commit lineage).
- Baseline failure count unchanged at 11/201 under offscreen.

The full work is captured in commits:

| Task | Commit theme |
|---|---|
| 2 | `add delegateClassFor` |
| 3 | `add DelegateClassRole + BlockRecord field` |
| 4 | `populate delegateClass during record assembly` |
| 5 | `test (RED) — TextEdit identity preserved` |
| 6 | `add UnifiedInlineTextDelegate` |
| 7 | `switch DelegateChooser; stable row identity` |
| 8 | `retire kindOnlySwap workaround and contentY bandaid` |
| 9a | `stub — FALSIFIABILITY PROOF, REVERTS NEXT` |
| 9b | `Revert "stub — FALSIFIABILITY PROOF, REVERTS NEXT"` |
| 10 | `test — cross-class kind transition swaps delegate` |
| 11 | `test — paste-heading renders at heading font size` |
| 12 | `delete superseded delegates` |
| 13 | `docs: update seam guidance` |
| 14 | `docs: queue.md — record commit hash` |

Plus one handoff doc + memory update for tier-4 / future agents.
