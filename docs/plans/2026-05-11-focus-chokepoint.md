# Focus-chokepoint refactor — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `LiveCursorState` the single owner of focus delivery on structural events, retiring the scattered focus logic across delegates, `LiveView.qml`, and `LiveListModelBinding`. Fixes two reported bugs (focus-loss after Enter at paragraph end; focus-loss after heading→paragraph kind change via `#` deletion) as consequences, not bandages.

**Architecture:** `LiveCursorState` gains `establishFocus(BlockAnchor, qtPos)`, a pending-focus queue, cascade brackets called from `onD2Changed`, and a stale-registration check that validates registered delegates against the model's current kind. Each text-bearing QML delegate exposes one `takeFocus(qtPos)` method and registers itself on `Component.onCompleted`. Typing-led cursor sync (`syncFromTextEdit`) is unchanged.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest + `LiveRealisticInputHarness`. Build cap: **`-j 8` always** (per project memory).

**Spec:** `docs/specs/2026-05-11-focus-chokepoint-design.md` is authoritative for every design decision in this plan. Cite by section number when in doubt.

**Reading order for executor before starting:**
1. `docs/INVARIANTS.md` (especially invariants 1–5 and 8)
2. `docs/specs/2026-05-11-focus-chokepoint-design.md` (full)
3. `libs/markoff-live/include/markoff/live/LiveCursorState.h` and `.cpp` (current API)
4. `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` (representative of the per-delegate pattern to retire)
5. `libs/markoff-live/src/LiveListModelBinding.cpp:380–600` (the twin re-anchor blocks at 441–452 and 556–569; the duplicated clamp at 405–406, 522–523)

**Naming note:** Spec uses `BlockId`; live code uses `BlockAnchor` (== `BlockId` in D2). This plan uses `BlockAnchor` to match the codebase. Rename to `BlockId` is tier-3 work, out of scope.

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

**Commit-message prefix convention:** `markoff-live: <slot summary>` per the recent commit history pattern (see `git log --oneline` examples like `8c18cec markoff-live: slot — Enter at paragraph-end migrates focus`).

---

## Files touched

| File | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/LiveCursorState.h` | Add new API (§5.1) + internal types |
| `libs/markoff-live/src/LiveCursorState.cpp` | Implement new methods + `tryResolvePending` with stale check |
| `libs/markoff-live/include/markoff/live/LiveBlockModel.h` | Possibly add `kindFor(BlockAnchor) -> QString` accessor if missing |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Cascade brackets in `onD2Changed`; retire twin re-anchor blocks (lines 441–452, 556–569); fold duplicated clamp (405–406, 522–523) |
| `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` | Migrate `requestTextCaretAtNewRow` etc. → `establishFocus` |
| `libs/markoff-live/qml/LiveView.qml` | Retire `Connections { onCursorChanged }` (lines 110–127); replace MouseArea `focusEditAt` (302–305, 331–333) with `establishFocus` |
| `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` | Add `takeFocus`, registration; retire local focus block + `onCursorChanged` |
| `libs/markoff-live/qml/delegates/HeadingDelegate.qml` | (same) |
| `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml` | (same) |
| `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` | (same) |
| `libs/markoff-live/qml/delegates/ListItemDelegate.qml` | (same) |
| `libs/markoff-live/qml/delegates/MathDelegate.qml` | (same; `latexEdit.forceActiveFocus` in `Qt.callLater` at line 118 stays — `BlockInternalEdit` sub-cursor, out of scope per spec §2.2) |
| `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` | **New** — parameterised invariant test (§8.1) |
| `libs/markoff-live/tests/tst_live_render_focus_after_enter_at_paragraph_end.cpp` | **New** — Bug A regression (§8.2) |
| `libs/markoff-live/tests/tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp` | **New** — Bug B regression (§8.2) |
| `libs/markoff-live/tests/tst_live_cursor_state_chokepoint.cpp` | **New** — six edge-case unit tests (§8.4 + §5.1.1) |
| `libs/markoff-live/tests/tst_live_render_focus_path_shape.cpp` | **New** — retirement-evidence shape tests (§8.5) |
| `libs/markoff-live/tests/CMakeLists.txt` | Wire the four new test executables |
| `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md` | **New** — dogfood checklist (§8.6) |
| `docs/queue.md` §#2 | Mark concerns #1 (partial), #7, #8, #11 as resolved-by-tier-1 with cross-reference to this work |

---

## Task 1: Pre-flight — confirm assumptions, no commit

**Files:** None modified. Read-only inspection.

- [ ] **Step 1: Verify model exposes `kind` and `blockAnchor` roles to QML**

Run:
```bash
grep -n "{ KindRole\|{ BlockAnchorRole" libs/markoff-live/src/LiveBlockModel.cpp
```
Expected: both role-to-name mappings present (`kind` and `blockAnchor`). If missing, **stop and flag** — spec §5.2's `model.kind` / `model.blockAnchor` references depend on these.

- [ ] **Step 2: Check whether `LiveBlockModel::kindFor(BlockAnchor)` accessor exists**

Run:
```bash
grep -n "kindFor\|QString kindFor" libs/markoff-live/include/markoff/live/LiveBlockModel.h
```
If absent (likely): note that Task 5 will add it. Implementation pattern:
```cpp
QString LiveBlockModel::kindFor(Markoff::BlockAnchor anchor) const {
    for (const auto &r : m_rows) if (r.blockAnchor == anchor) return r.kind;
    return {};
}
```

- [ ] **Step 3: Read the three existing `requestTextCaretAt*` declarations**

Read: `libs/markoff-live/include/markoff/live/LiveCursorState.h:81–136`.

Confirm: `requestTextCaretAtRow`, `requestTextCaretAtNewRow`, `requestTextCaretAtRowVisualX`, `requestTextCaretAtAnchor` are the four current entry points. These remain callable through tier 1 as thin wrappers; tier 3 removes them (per spec §2.2).

- [ ] **Step 4: Read the twin re-anchor blocks**

Read: `libs/markoff-live/src/LiveListModelBinding.cpp:380–600`. Note the demote block (~441–452), the promote block (~556–569), and the duplicated `qtPos` clamp (~405–406 and ~522–523). These are the §5.3 retirement targets.

- [ ] **Step 5: Read `ParagraphDelegate.qml`**

Read full file. Note the existing `Component.onCompleted` focus-check (~221–235), `Connections { onCursorChanged }` (~159–178), `focusEditAt()` function (~191). These define the pattern to retire across all six text-bearing delegates.

- [ ] **Step 6: Read `LiveView.qml`**

Read full file. Note `Connections { target: cursorState; onCursorChanged }` (~110–127), MouseArea `onPressed` (~302–305) and `onReleased` (~331–333) calls to `focusEditAt`.

- [ ] **Step 7: Confirm build is green at HEAD**

Run:
```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -20
```
Expected: clean build, all tests pass. **If anything fails, stop** — fix the breakage before the refactor.

No commit for Task 1.

---

## Task 2: Add §8.1 core invariant test (QEXPECT_FAIL on bug-A/B scenarios)

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (wire the new test)

- [ ] **Step 1: Write the test skeleton**

Create `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestFocusChokepointInvariant : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void enter_at_paragraph_end();
    void enter_at_paragraph_middle();
    void enter_at_paragraph_start();
    void backspace_at_paragraph_start();
    void heading_demote_via_hash_deletion();
    void paragraph_promote_via_hash_typing();
    void blockquote_demote();
    void blockquote_promote();
    void codeblock_kind_transition();
    void listitem_kind_transition();
    void paste_at_block_start();
    void paste_at_block_middle();
    void paste_at_block_end();
    void undo_after_enter();
    void redo_after_undo();
    void click_to_focus_paragraph();
    void click_to_focus_heading();

private:
    void assertChokepointInvariant(const QString &scenario);
    std::unique_ptr<QmlIntegrationFixture> m_fixture;
};

void TestFocusChokepointInvariant::initTestCase() {
    m_fixture = std::make_unique<QmlIntegrationFixture>();
    m_fixture->loadDocument("# Heading\n\nParagraph one.\n\n> Quote\n\n- list item\n\n```\ncode\n```\n");
}

void TestFocusChokepointInvariant::cleanupTestCase() {
    m_fixture.reset();
}

void TestFocusChokepointInvariant::assertChokepointInvariant(const QString &scenario) {
    // Spec §8.1 — after every structural event:
    //   focusedDelegate.blockAnchor == cursorState.currentBlockAnchor
    //   focusedDelegate.edit.cursorPosition == cursorState.currentQtPos
    auto focused = m_fixture->focusedDelegate();
    QVERIFY2(focused, qPrintable(QString("no focused delegate after %1").arg(scenario)));

    const auto delegateAnchor = focused->property("modelBlockAnchor");
    const auto cursorAnchor   = m_fixture->cursorStateCurrentAnchor();
    QCOMPARE(delegateAnchor.toLongLong(), cursorAnchor);

    QObject *edit = focused->property("edit").value<QObject *>();
    QVERIFY(edit);
    const int delegateCursorPos = edit->property("cursorPosition").toInt();
    const int stateCursorPos    = m_fixture->cursorStateCurrentQtPos();
    QCOMPARE(delegateCursorPos, stateCursorPos);
}

void TestFocusChokepointInvariant::enter_at_paragraph_end() {
    QEXPECT_FAIL("", "Bug A — see docs/specs/2026-05-11-focus-chokepoint-design.md §1.1", Continue);
    m_fixture->placeCursorAtEndOf(2);  // paragraph row
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    assertChokepointInvariant("Enter at paragraph end");
}

void TestFocusChokepointInvariant::heading_demote_via_hash_deletion() {
    QEXPECT_FAIL("", "Bug B — see docs/specs/2026-05-11-focus-chokepoint-design.md §1.1", Continue);
    m_fixture->placeCursorAtPos(0, 1);  // inside heading, after first '#'
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);  // deletes the '#'
    assertChokepointInvariant("Heading→Paragraph kind transition");
}

// Remaining slots: implement following same pattern. Each places the
// cursor, triggers the structural event via LiveRealisticInputHarness,
// asserts the invariant. Scenarios that are not expected to fail today
// do NOT get QEXPECT_FAIL — they should pass against tier-zero.

void TestFocusChokepointInvariant::enter_at_paragraph_middle() {
    m_fixture->placeCursorAtPos(2, 5);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    assertChokepointInvariant("Enter at paragraph middle");
}

void TestFocusChokepointInvariant::enter_at_paragraph_start() {
    m_fixture->placeCursorAtPos(2, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    assertChokepointInvariant("Enter at paragraph start");
}

void TestFocusChokepointInvariant::backspace_at_paragraph_start() {
    m_fixture->placeCursorAtPos(2, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);
    assertChokepointInvariant("Backspace at paragraph start (block merge)");
}

void TestFocusChokepointInvariant::paragraph_promote_via_hash_typing() {
    m_fixture->placeCursorAtPos(2, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('#'));
    h.typeChar(QChar(' '));
    assertChokepointInvariant("Paragraph→Heading kind transition");
}

void TestFocusChokepointInvariant::blockquote_demote() {
    m_fixture->placeCursorAtPos(4, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);  // deletes '>'
    assertChokepointInvariant("Blockquote→Paragraph");
}

void TestFocusChokepointInvariant::blockquote_promote() {
    m_fixture->placeCursorAtPos(2, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('>'));
    h.typeChar(QChar(' '));
    assertChokepointInvariant("Paragraph→Blockquote");
}

void TestFocusChokepointInvariant::codeblock_kind_transition() {
    m_fixture->placeCursorAtEndOf(8);  // inside code fence
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    assertChokepointInvariant("CodeBlock Enter");
}

void TestFocusChokepointInvariant::listitem_kind_transition() {
    m_fixture->placeCursorAtEndOf(6);  // list item
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    assertChokepointInvariant("ListItem Enter");
}

void TestFocusChokepointInvariant::paste_at_block_start() {
    m_fixture->placeCursorAtPos(2, 0);
    m_fixture->pasteText("inserted ");
    assertChokepointInvariant("Paste at block start");
}

void TestFocusChokepointInvariant::paste_at_block_middle() {
    m_fixture->placeCursorAtPos(2, 5);
    m_fixture->pasteText("inserted ");
    assertChokepointInvariant("Paste at block middle");
}

void TestFocusChokepointInvariant::paste_at_block_end() {
    m_fixture->placeCursorAtEndOf(2);
    m_fixture->pasteText("inserted ");
    assertChokepointInvariant("Paste at block end");
}

void TestFocusChokepointInvariant::undo_after_enter() {
    m_fixture->placeCursorAtEndOf(2);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier);
    assertChokepointInvariant("Undo after Enter");
}

void TestFocusChokepointInvariant::redo_after_undo() {
    m_fixture->placeCursorAtEndOf(2);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    assertChokepointInvariant("Redo after undo");
}

void TestFocusChokepointInvariant::click_to_focus_paragraph() {
    m_fixture->clickOnBlock(2);
    assertChokepointInvariant("Click to focus paragraph");
}

void TestFocusChokepointInvariant::click_to_focus_heading() {
    m_fixture->clickOnBlock(0);
    assertChokepointInvariant("Click to focus heading");
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusChokepointInvariant)
#include "tst_live_render_focus_chokepoint_invariant.moc"
```

If `QmlIntegrationFixture` doesn't yet expose `focusedDelegate()`, `cursorStateCurrentAnchor()`, `cursorStateCurrentQtPos()`, `placeCursorAtPos(row, qtPos)`, `placeCursorAtEndOf(row)`, `pasteText`, or `clickOnBlock` — add them in this task. Look at existing helpers in `QmlIntegrationFixture.cpp` and follow the pattern.

- [ ] **Step 2: Wire the test in CMake**

Modify `libs/markoff-live/tests/CMakeLists.txt`. Find an existing QML-integration test (`tst_live_render_qml_integration`) and add a sibling target:

```cmake
add_executable(tst_live_render_focus_chokepoint_invariant
    tst_live_render_focus_chokepoint_invariant.cpp
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_focus_chokepoint_invariant
    PRIVATE
        markoff_live
        markoff-live-app-internal
        Qt6::Test
        Qt6::Quick
)
add_test(NAME tst_live_render_focus_chokepoint_invariant
         COMMAND tst_live_render_focus_chokepoint_invariant -platform offscreen)
```

Match the exact form of the existing `tst_live_render_qml_integration` block in the file; do not invent new linkage patterns.

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_focus_chokepoint_invariant -j 8
ctest --test-dir build-dev -R tst_live_render_focus_chokepoint_invariant --output-on-failure
```

Expected outcomes:
- `enter_at_paragraph_end` — XFAIL (QEXPECT_FAIL with `Continue` so the assert is reported but test passes overall).
- `heading_demote_via_hash_deletion` — XFAIL.
- Other scenarios — depends on what tier-zero handles correctly. **Either pass or XFAIL is acceptable;** **XPASS is a problem** (means QEXPECT_FAIL is on a scenario that already works — remove the marker if so).

If any test fails (not XFAIL, but actual FAIL on a scenario without QEXPECT_FAIL): investigate. Either tier-zero handles that scenario differently than expected, or the fixture helpers have a bug.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp
git add libs/markoff-live/tests/CMakeLists.txt
git add libs/markoff-live/tests/QmlIntegrationFixture.{h,cpp}  # if helpers added
git commit -m "$(cat <<'EOF'
markoff-live: test — focus-chokepoint invariant + bug-A/B QEXPECT_FAIL

Spec §8.1 / §8.3 step 1. Lands the parameterised invariant test on
LiveRealisticInputHarness with QEXPECT_FAIL markers on the two
bug-affected scenarios. Markers come off in the production refactor
commit (spec §8.3 step 2).

This is the test-first commit that earns the rest of the refactor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Add §8.2 named regression tests (full QEXPECT_FAIL)

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_focus_after_enter_at_paragraph_end.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

These are narrow, named-after-the-symptom regressions per spec §8.2. They are entirely QEXPECT_FAIL'd today; markers come off in Task 15.

- [ ] **Step 1: Write Bug A test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tst_live_render_focus_after_enter_at_paragraph_end.cpp
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestFocusAfterEnterAtParagraphEnd : public QObject {
    Q_OBJECT
private slots:
    void user_can_type_immediately_after_pressing_enter();
};

void TestFocusAfterEnterAtParagraphEnd::user_can_type_immediately_after_pressing_enter() {
    QEXPECT_FAIL("", "Bug A — Enter-at-end loses focus; see spec §1.1", Abort);

    QmlIntegrationFixture fx;
    fx.loadDocument("A paragraph.\n");
    fx.placeCursorAtEndOf(0);

    LiveRealisticInputHarness h(fx.window());
    h.keyClick(Qt::Key_Return);
    h.typeChar(QChar('x'));

    // The 'x' should land in the new (second) block. If focus was lost,
    // the keystroke is silently dropped and the document is unchanged.
    QCOMPARE(fx.documentText(), QStringLiteral("A paragraph.\nx\n"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusAfterEnterAtParagraphEnd)
#include "tst_live_render_focus_after_enter_at_paragraph_end.moc"
```

- [ ] **Step 2: Write Bug B test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestFocusAfterHeadingDemote : public QObject {
    Q_OBJECT
private slots:
    void user_can_type_immediately_after_demoting_heading();
};

void TestFocusAfterHeadingDemote::user_can_type_immediately_after_demoting_heading() {
    QEXPECT_FAIL("", "Bug B — heading-demote loses focus intermittently; see spec §1.1", Abort);

    QmlIntegrationFixture fx;
    fx.loadDocument("# Heading\n");
    fx.placeCursorAtPos(0, 1);  // between '#' and ' '

    LiveRealisticInputHarness h(fx.window());
    h.keyClick(Qt::Key_Backspace);  // deletes '#' → kind transitions to paragraph
    h.typeChar(QChar('x'));

    QCOMPARE(fx.documentText(), QStringLiteral("x Heading\n"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusAfterHeadingDemote)
#include "tst_live_render_focus_after_heading_demote_via_hash_deletion.moc"
```

- [ ] **Step 3: Wire both in CMake**

Add two more `add_executable` / `target_link_libraries` / `add_test` blocks to `libs/markoff-live/tests/CMakeLists.txt`, identical in shape to Task 2 step 2. Paths only differ.

- [ ] **Step 4: Build and run**

```bash
cmake --build build-dev --target tst_live_render_focus_after_enter_at_paragraph_end \
                                  tst_live_render_focus_after_heading_demote_via_hash_deletion -j 8
ctest --test-dir build-dev -R 'focus_after_' --output-on-failure
```
Expected: both tests XFAIL.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_focus_after_*.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: test — Bug A + Bug B regressions (QEXPECT_FAIL)

Spec §8.2 / §8.3 step 1. Named-after-symptom regressions. Both
fully QEXPECT_FAIL'd today; markers come off in the production
refactor commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `LiveCursorState` — header additions (skeleton only, no behaviour)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveCursorState.h`
- Modify: `libs/markoff-live/src/LiveCursorState.cpp` (empty stubs)

This task lands the API surface only — no behaviour change. Existing tests must remain green.

- [ ] **Step 1: Add types and members to the header**

In `LiveCursorState.h`, inside the class body, add:

```cpp
// --- §5.1 focus-chokepoint additions (tier 1) ---

/// Spec §5.1. Structural-event sites call this. Always stores as
/// pending; resolution attempted at safe points (after delegate
/// registration, at cascade end). Never dispatches synchronously
/// from this call — see spec §5.1.2.
Q_INVOKABLE void establishFocus(Markoff::BlockAnchor blockAnchor, int qtPos);

/// Spec §5.1. Called by LiveListModelBinding at the top of
/// onD2Changed, before any structural mutation. Suppresses
/// resolution attempts during the cascade.
void beginStructuralCascade();

/// Spec §5.1. Called by LiveListModelBinding at the bottom of
/// onD2Changed, after applyOps. Triggers a pending-resolution
/// attempt with the now-current m_delegates.
void endStructuralCascade();

/// Spec §5.1. Each text-bearing delegate calls this from
/// Component.onCompleted. `kind` is validated against the model
/// on every resolution attempt (spec §5.1.1).
Q_INVOKABLE void delegateAvailable(Markoff::BlockAnchor blockAnchor,
                                   const QString &kind,
                                   QQuickItem *delegateRoot);

/// Spec §5.1. Called from Component.onDestruction.
Q_INVOKABLE void delegateGoingAway(Markoff::BlockAnchor blockAnchor);

private:
struct DelegateRecord {
    QString kind;
    QPointer<QQuickItem> root;
};
struct PendingFocus {
    Markoff::BlockAnchor target;
    int qtPos;
    qint64 enqueuedMs;
};

void tryResolvePending();
void expireIfTimedOut(PendingFocus &p);  // declared, body in Task 8

std::optional<PendingFocus>                 m_pendingFocus;
QHash<Markoff::BlockAnchor, DelegateRecord> m_delegates;
bool                                        m_inStructuralCascade = false;

static constexpr qint64 kPendingFocusTimeoutMs = 500;
```

Adjust includes at top of header:
```cpp
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <optional>
```

- [ ] **Step 2: Add empty stub implementations**

In `LiveCursorState.cpp`:

```cpp
void LiveCursorState::establishFocus(Markoff::BlockAnchor blockAnchor, int qtPos) {
    // TIER-1 STUB. Real implementation lands in Task 7.
    Q_UNUSED(blockAnchor);
    Q_UNUSED(qtPos);
}

void LiveCursorState::beginStructuralCascade() {
    // TIER-1 STUB. Real implementation lands in Task 7.
}

void LiveCursorState::endStructuralCascade() {
    // TIER-1 STUB. Real implementation lands in Task 7.
}

void LiveCursorState::delegateAvailable(Markoff::BlockAnchor blockAnchor,
                                       const QString &kind,
                                       QQuickItem *delegateRoot) {
    // TIER-1 STUB. Real implementation lands in Task 7.
    Q_UNUSED(blockAnchor);
    Q_UNUSED(kind);
    Q_UNUSED(delegateRoot);
}

void LiveCursorState::delegateGoingAway(Markoff::BlockAnchor blockAnchor) {
    // TIER-1 STUB. Real implementation lands in Task 7.
    Q_UNUSED(blockAnchor);
}

void LiveCursorState::tryResolvePending() {
    // TIER-1 STUB. Real implementation lands in Task 7.
}

void LiveCursorState::expireIfTimedOut(PendingFocus &p) {
    // TIER-1 STUB. Real implementation lands in Task 8.
    Q_UNUSED(p);
}
```

The `Q_UNUSED` and "TIER-1 STUB" comments are intentional — they make grep-able the work to be done and prevent unused-parameter warnings.

- [ ] **Step 3: Build and confirm existing tests pass**

```bash
cmake --build build-dev --target markoff_live -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```
Expected: all existing tests still pass; the new bug-A/B tests still XFAIL.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveCursorState chokepoint API skeleton

Spec §5.1. Adds establishFocus / beginStructuralCascade /
endStructuralCascade / delegateAvailable / delegateGoingAway as
empty stubs plus internal types (DelegateRecord, PendingFocus).
Behaviour-free: no caller migrated yet, all stubs Q_UNUSED.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Add `LiveBlockModel::kindFor(BlockAnchor)` if missing

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveBlockModel.h`
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`

- [ ] **Step 1: Verify (again) the accessor is missing**

```bash
grep -n "kindFor" libs/markoff-live/include/markoff/live/LiveBlockModel.h libs/markoff-live/src/LiveBlockModel.cpp
```
If anything matches, **skip this task** entirely.

- [ ] **Step 2: Add the declaration**

In `LiveBlockModel.h`, in the public section near other accessors (after `recordAt`):

```cpp
/// Spec 2026-05-11-focus-chokepoint-design.md §5.1.1. Returns
/// the kind of the block with the given anchor, or empty string
/// if no such block exists. Used by LiveCursorState's stale-
/// registration check.
QString kindFor(Markoff::BlockAnchor anchor) const;
```

- [ ] **Step 3: Add the implementation**

In `LiveBlockModel.cpp`:

```cpp
QString LiveBlockModel::kindFor(Markoff::BlockAnchor anchor) const {
    for (const auto &r : m_rows) {
        if (r.blockAnchor == anchor) {
            return r.kind;
        }
    }
    return {};
}
```

Linear scan is fine — `m_rows` is small (~hundreds at most for any realistic document). If profiling later shows hot, a `QHash<BlockAnchor, int>` cache is the obvious upgrade.

- [ ] **Step 4: Build and confirm green**

```bash
cmake --build build-dev --target markoff_live -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveBlockModel::kindFor(BlockAnchor)

Spec §5.1.1. Lookup accessor used by LiveCursorState's stale-
registration check. Linear scan; m_rows is small.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Edge-case unit tests (§8.4 + §5.1.1) — six TDD tests, no impl yet

**Files:**
- Create: `libs/markoff-live/tests/tst_live_cursor_state_chokepoint.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

These tests exercise the new API directly without QML. They will all fail today (impl is stubbed). They pass in Task 7.

- [ ] **Step 1: Write all six tests**

Create `libs/markoff-live/tests/tst_live_cursor_state_chokepoint.cpp`. Each slot tests one §7 policy or the §5.1.1 stale check. Use a minimal `QQuickItem`-based mock for delegate roots — implement a small `MockDelegate : public QQuickItem` that exposes `takeFocus(int)` as an `Q_INVOKABLE` slot and records its invocations.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/core/BlockAnchor.h>

#include <QQuickItem>
#include <QSignalSpy>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class MockDelegate : public QQuickItem {
    Q_OBJECT
public:
    Q_INVOKABLE void takeFocus(int qtPos) {
        m_calls.append(qtPos);
    }
    QList<int> takeFocusCalls() const { return m_calls; }
    void clearCalls() { m_calls.clear(); }
private:
    QList<int> m_calls;
};

class TestLiveCursorStateChokepoint : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void pending_supersession();                     // §7.1
    void pending_survives_delegate_destruction();    // §7.2
    void bad_blockid_drops_silently();               // §7.3
    void pending_times_out_after_500ms();            // §7.4
    void delegate_arrives_without_pending();         // §7.5
    void stale_registration_holds_pending();         // §5.1.1

private:
    std::unique_ptr<LiveBlockModel>   m_model;
    std::unique_ptr<LiveCursorState>  m_state;
};

void TestLiveCursorStateChokepoint::init() {
    m_model = std::make_unique<LiveBlockModel>();
    m_state = std::make_unique<LiveCursorState>();
    m_state->attachModel(m_model.get());
    // Note: if LiveCursorState doesn't currently take a model pointer,
    // Task 7 adds the wire. Use whichever attach method exists.
}

void TestLiveCursorStateChokepoint::cleanup() {
    m_state.reset();
    m_model.reset();
}

void TestLiveCursorStateChokepoint::pending_supersession() {
    // §7.1 — latest establishFocus wins.
    Markoff::BlockAnchor a{1}, b{2};
    m_state->establishFocus(a, 5);
    m_state->establishFocus(b, 9);

    MockDelegate dB;
    m_state->delegateAvailable(b, "paragraph", &dB);
    QCOMPARE(dB.takeFocusCalls(), QList<int>{9});

    MockDelegate dA;
    m_state->delegateAvailable(a, "paragraph", &dA);
    QCOMPARE(dA.takeFocusCalls(), QList<int>{});  // A was superseded
}

void TestLiveCursorStateChokepoint::pending_survives_delegate_destruction() {
    // §7.2 — pending request survives a delegate being destroyed.
    Markoff::BlockAnchor a{1};

    auto *dOld = new MockDelegate;
    m_state->delegateAvailable(a, "heading", dOld);
    m_state->establishFocus(a, 3);
    // (with stale-kind-check, even if model.kindFor returns "heading"
    // this would dispatch; for this test we want to simulate a clean
    // destroy-and-readd. The MockDelegate is constructed-not-in-model
    // so kindFor returns ""; dispatch is suppressed by stale check.)

    m_state->delegateGoingAway(a);
    delete dOld;

    auto *dNew = new MockDelegate;
    // Add the row to the model so kindFor returns "paragraph".
    // (Helper on QmlIntegrationFixture; here, use model API directly
    // or a friend-class shim if the model doesn't expose row-insertion
    // outside applyOps.)

    m_state->delegateAvailable(a, "paragraph", dNew);
    QCOMPARE(dNew->takeFocusCalls(), QList<int>{3});
    delete dNew;
}

void TestLiveCursorStateChokepoint::bad_blockid_drops_silently() {
    // §7.3 — establishFocus for a BlockAnchor not in the model
    // is dropped silently. No assert, no signal, no exception.
    Markoff::BlockAnchor unknown{9999};
    m_state->establishFocus(unknown, 0);
    QVERIFY(!m_state->hasPendingFocus());
    // hasPendingFocus() is a test-only accessor added in Task 7.
}

void TestLiveCursorStateChokepoint::pending_times_out_after_500ms() {
    // §7.4 — pending request expires after kPendingFocusTimeoutMs.
    Markoff::BlockAnchor a{1};
    m_state->establishFocus(a, 5);
    QVERIFY(m_state->hasPendingFocus());

    QTest::qWait(600);
    // Touch some path that calls tryResolvePending() — e.g.
    // delegateAvailable for an unrelated block, or an explicit
    // tick-pending API. Task 7 decides the exact trigger; here we
    // call a no-op delegateAvailable to trigger resolution attempt.
    m_state->delegateAvailable(Markoff::BlockAnchor{42}, "paragraph", nullptr);
    QVERIFY(!m_state->hasPendingFocus());
}

void TestLiveCursorStateChokepoint::delegate_arrives_without_pending() {
    // §7.5 — delegateAvailable with no pending request is a no-op
    // for focus, but registers the delegate in m_delegates.
    Markoff::BlockAnchor a{1};
    MockDelegate d;
    m_state->delegateAvailable(a, "paragraph", &d);
    QCOMPARE(d.takeFocusCalls(), QList<int>{});
    QVERIFY(m_state->isDelegateRegistered(a));  // test-only accessor
}

void TestLiveCursorStateChokepoint::stale_registration_holds_pending() {
    // §5.1.1 — delegate registered with kind "heading"; model now
    // reports "paragraph"; establishFocus should NOT dispatch.
    Markoff::BlockAnchor a{1};
    MockDelegate dStale;
    m_state->delegateAvailable(a, "heading", &dStale);

    // Simulate model now reporting "paragraph" for the same anchor.
    // (Helper that inserts a paragraph row with anchor 1. Task 7
    // determines the cleanest test-harness mechanism — likely a
    // direct m_rows manipulation through a friend declaration.)
    m_model->insertTestRow(a, "paragraph", "p text");

    m_state->establishFocus(a, 5);
    QCOMPARE(dStale.takeFocusCalls(), QList<int>{});

    MockDelegate dFresh;
    m_state->delegateGoingAway(a);
    m_state->delegateAvailable(a, "paragraph", &dFresh);
    QCOMPARE(dFresh.takeFocusCalls(), QList<int>{5});
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestLiveCursorStateChokepoint)
#include "tst_live_cursor_state_chokepoint.moc"
```

If `hasPendingFocus()` / `isDelegateRegistered(BlockAnchor)` test-only accessors aren't on `LiveCursorState`, add them in this task (or in Task 7 — doesn't matter which task lands them, as long as the tests compile).

If `LiveBlockModel::insertTestRow(...)` isn't a thing today: in Task 7, either use `applyOps` to build a single-row state or add a `friend` declaration so the test can write `m_rows` directly. The test code shown above assumes a small test-only insertion helper.

- [ ] **Step 2: Wire in CMake**

Add to `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
add_executable(tst_live_cursor_state_chokepoint
    tst_live_cursor_state_chokepoint.cpp
)
target_link_libraries(tst_live_cursor_state_chokepoint
    PRIVATE markoff_live Qt6::Test Qt6::Quick
)
add_test(NAME tst_live_cursor_state_chokepoint
         COMMAND tst_live_cursor_state_chokepoint)
```

- [ ] **Step 3: Build and run; expect failures**

```bash
cmake --build build-dev --target tst_live_cursor_state_chokepoint -j 8
ctest --test-dir build-dev -R tst_live_cursor_state_chokepoint --output-on-failure
```
Expected: every test fails (impl is stubbed). This is correct — Task 7 makes them pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_cursor_state_chokepoint.cpp \
        libs/markoff-live/tests/CMakeLists.txt \
        libs/markoff-live/include/markoff/live/LiveCursorState.h  # if test-only accessors added
git commit -m "$(cat <<'EOF'
markoff-live: test — chokepoint edge cases (TDD red)

Spec §8.4 + §5.1.1. Six unit tests covering pending supersession,
pending-survives-delegate-destruction, bad-blockid drop,
500 ms timeout, no-pending registration, stale-registration check.
All red against the stub impl; turn green in next slot.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: `LiveCursorState` — implement the chokepoint (Task 6 tests turn green)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveCursorState.h` (test-only accessors + model attachment)
- Modify: `libs/markoff-live/src/LiveCursorState.cpp` (real bodies)

- [ ] **Step 1: Wire the model pointer**

In `LiveCursorState.h`, add (if not already present):

```cpp
void attachModel(LiveBlockModel *model);

private:
QPointer<LiveBlockModel> m_model;
```

In `LiveCursorState.cpp`:
```cpp
void LiveCursorState::attachModel(LiveBlockModel *model) {
    m_model = model;
}
```

Wire the actual production call: `LiveListModelBinding` constructs the model and the cursor state — find that construction and add `m_cursorState->attachModel(m_blockModel)`. Likely in `LiveListModelBinding`'s constructor or its setter.

- [ ] **Step 2: Implement `delegateAvailable`**

```cpp
void LiveCursorState::delegateAvailable(Markoff::BlockAnchor blockAnchor,
                                       const QString &kind,
                                       QQuickItem *delegateRoot) {
    m_delegates.insert(blockAnchor, { kind, QPointer<QQuickItem>(delegateRoot) });
    if (!m_inStructuralCascade) {
        tryResolvePending();
    }
}
```

- [ ] **Step 3: Implement `delegateGoingAway`**

```cpp
void LiveCursorState::delegateGoingAway(Markoff::BlockAnchor blockAnchor) {
    m_delegates.remove(blockAnchor);
    // Pending request NOT cleared — §7.2.
}
```

- [ ] **Step 4: Implement `beginStructuralCascade` / `endStructuralCascade`**

```cpp
void LiveCursorState::beginStructuralCascade() {
    m_inStructuralCascade = true;
}

void LiveCursorState::endStructuralCascade() {
    m_inStructuralCascade = false;
    tryResolvePending();
}
```

- [ ] **Step 5: Implement `establishFocus`**

```cpp
void LiveCursorState::establishFocus(Markoff::BlockAnchor blockAnchor, int qtPos) {
    // §7.3 — drop silently if the anchor isn't in the model. The
    // "log to Discipline Log" half of §7.3's policy is a human
    // convention (developer files an entry in docs/queue.md if they
    // observe this happening), not an automated emission.
    if (m_model && m_model->kindFor(blockAnchor).isEmpty()) {
        return;
    }
    m_pendingFocus = PendingFocus{
        blockAnchor,
        qtPos,
        QDateTime::currentMSecsSinceEpoch()
    };
    if (!m_inStructuralCascade) {
        tryResolvePending();
    }
}
```

- [ ] **Step 6: Implement `tryResolvePending` with the stale-registration check (spec §5.1.1)**

```cpp
void LiveCursorState::tryResolvePending() {
    if (!m_pendingFocus) return;
    expireIfTimedOut(*m_pendingFocus);
    if (!m_pendingFocus) return;

    const auto anchor = m_pendingFocus->target;
    const auto it = m_delegates.find(anchor);
    if (it == m_delegates.end() || !it->root) return;

    // Stale-registration check — spec §5.1.1.
    const QString currentKind = m_model ? m_model->kindFor(anchor) : QString();
    if (it->kind != currentKind) return;

    QMetaObject::invokeMethod(it->root.data(), "takeFocus",
                              Q_ARG(int, m_pendingFocus->qtPos));
    m_pendingFocus.reset();
}
```

- [ ] **Step 7: Implement `expireIfTimedOut`**

```cpp
void LiveCursorState::expireIfTimedOut(PendingFocus &p) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if ((now - p.enqueuedMs) > kPendingFocusTimeoutMs) {
        m_pendingFocus.reset();
    }
}
```

- [ ] **Step 8: Add test-only accessors if Task 6 used them**

```cpp
public:
    bool hasPendingFocus() const { return m_pendingFocus.has_value(); }
    bool isDelegateRegistered(Markoff::BlockAnchor anchor) const {
        return m_delegates.contains(anchor);
    }
```

Mark them `[[deprecated("test-only — do not use in production")]]` if you want compile-time signal; otherwise just document.

- [ ] **Step 9: Build and verify Task 6 tests pass**

```bash
cmake --build build-dev --target tst_live_cursor_state_chokepoint -j 8
ctest --test-dir build-dev -R tst_live_cursor_state_chokepoint --output-on-failure
```
Expected: all six tests pass.

- [ ] **Step 10: Confirm the rest of the suite stays green**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```
Expected: existing tests unchanged; Bug A/B regressions still XFAIL (no caller migrated yet).

- [ ] **Step 11: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp  # if attachModel wired here
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveCursorState chokepoint implementation

Spec §5.1 + §5.1.1 + §7. Implements establishFocus / cascade
brackets / delegate registry / tryResolvePending with stale-
registration check. Six edge-case unit tests now green; QML-level
bugs A/B remain XFAIL until callers and delegates migrate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Migrate delegates — Paragraph, Heading, Blockquote (3 of 6)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`

Each delegate gets the spec §5.2 block added and the §5.3 retirements removed. **Same pattern, three files.**

- [ ] **Step 1: ParagraphDelegate.qml — add the §5.2 block**

Add (placement: near the bottom of the delegate's root component, before any existing `Component.onCompleted`):

```qml
function takeFocus(qtPos) {
    edit.cursorPosition = Math.min(qtPos, edit.length);
    edit.forceActiveFocus();
}

Component.onCompleted: {
    cursorState.delegateAvailable(model.blockAnchor, model.kind, root);
}

Component.onDestruction: {
    cursorState.delegateGoingAway(model.blockAnchor);
}
```

- [ ] **Step 2: ParagraphDelegate.qml — retire §5.3 elements**

Delete:
- The existing `Component.onCompleted` block that gates focus on `cs.focusedAnchorRow === root.modelIndex` and wraps in `Qt.callLater` (~lines 221–235 today).
- The existing `Connections { target: cursorState; onCursorChanged: { ... } }` block (~lines 159–178).
- The existing `function focusEditAt(qtPos) { ... }` function (~line 191) **only if no longer referenced**. Check first:

```bash
grep -n "focusEditAt" libs/markoff-live/qml/
```
If still referenced from `LiveView.qml` or other delegates (Task 11 retires those callers), keep the function as-is for now and delete in Task 11. Otherwise delete here.

- [ ] **Step 3: Build and confirm**

```bash
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

Expected: build green. Most tests still pass; the Bug A test may now intermittently pass (since `ParagraphDelegate` now registers and the new mechanism partially works), but Bug B still fails (Heading delegate not yet migrated). Don't remove QEXPECT_FAIL yet.

- [ ] **Step 4: Apply identical changes to HeadingDelegate.qml**

Same pattern. Add the §5.2 block, retire §5.3 elements. Heading delegate has `headingLevel` switching but the focus block is the same.

- [ ] **Step 5: Apply identical changes to BlockquoteDelegate.qml**

Same pattern.

- [ ] **Step 6: Build + test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/qml/delegates/{Paragraph,Heading,Blockquote}Delegate.qml
git commit -m "$(cat <<'EOF'
markoff-live: slot — chokepoint migration (Paragraph, Heading, Blockquote)

Spec §5.2 + §5.3. Each delegate gains takeFocus + delegateAvailable
+ delegateGoingAway. Per-delegate Component.onCompleted focus check
(with Qt.callLater) and Connections{onCursorChanged} retired.
focusEditAt() stays for now if cross-referenced from LiveView; that
goes in Task 11.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Migrate delegates — CodeBlock, ListItem, Math (3 of 6)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml`

Identical pattern to Task 8.

- [ ] **Step 1: CodeBlockDelegate.qml** — apply Task 8's §5.2 block + §5.3 retirements.
- [ ] **Step 2: ListItemDelegate.qml** — same.
- [ ] **Step 3: MathDelegate.qml** — same, **but** preserve the `latexEdit.forceActiveFocus` inside `Qt.callLater` at ~line 118. That's `BlockInternalEdit` sub-cursor (spec §2.2, out of scope). Migrate only the *block-level* TextEdit's focus handling; leave the LaTeX-edit popup alone.

- [ ] **Step 4: Build + test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/qml/delegates/{CodeBlock,ListItem,Math}Delegate.qml
git commit -m "$(cat <<'EOF'
markoff-live: slot — chokepoint migration (CodeBlock, ListItem, Math)

Spec §5.2 + §5.3. Math delegate's latexEdit.forceActiveFocus in
Qt.callLater is preserved — BlockInternalEdit sub-cursor, distinct
seam, out of scope per spec §2.2.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Migrate `LiveListModelBinding` — cascade brackets + retire twin re-anchor

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

This is the most delicate change in the plan. The two re-anchor blocks (~441–452 and ~556–569) get replaced; the duplicated clamp folds into the chokepoint.

- [ ] **Step 1: Add cascade brackets around `onD2Changed`'s body**

Find `LiveListModelBinding::onD2Changed`. At the very top of the method body, after parameter validation:

```cpp
m_cursorState->beginStructuralCascade();
auto cascadeEnd = qScopeGuard([this] {
    m_cursorState->endStructuralCascade();
});
```

`qScopeGuard` ensures `endStructuralCascade` runs on every return path, including early returns and exceptions. Include `<QScopeGuard>`.

- [ ] **Step 2: Retire the demote re-anchor block (~441–452)**

Find the block:

```cpp
if (auto *tc = std::get_if<TextCaret>(...);
    tc && tc->block == rec.blockAnchor) {
    const int qtPosClamped = std::min(tc->qtPos, /* clamped to size */);
    m_cursorState->requestTextCaretAtAnchor(rec.blockAnchor, qtPosClamped);
}
```

(Exact shape will vary — read the current code.) Replace with:

```cpp
if (auto *tc = std::get_if<TextCaret>(...);
    tc && tc->block == rec.blockAnchor) {
    m_cursorState->establishFocus(rec.blockAnchor, tc->qtPos);
    // Clamp lives inside takeFocus(qtPos): Math.min(qtPos, edit.length).
}
```

The clamp moves into `takeFocus` (already in spec §5.2's QML body). The duplicated clamp at ~405–406 also goes away.

- [ ] **Step 3: Retire the promote re-anchor block (~556–569)**

Same pattern, second site.

- [ ] **Step 4: Retire the duplicated qtPos clamp at ~405–406 and ~522–523**

These are now redundant. Delete them.

- [ ] **Step 5: Build + test**

```bash
cmake --build build-dev --target markoff_live -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

Expected: full suite green except the still-QEXPECT_FAIL'd Bug A/B regressions, which may now actually pass intermittently — that's fine, they're `Continue`-mode QEXPECT_FAIL.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveListModelBinding cascade brackets + retire twin re-anchor

Spec §4.3 #2 + §5.3. onD2Changed brackets with begin/endStructuralCascade
via qScopeGuard. Twin re-anchor blocks (demote + promote) replaced with
establishFocus. Duplicated qtPos clamp (lines 405–406, 522–523) folded
into takeFocus (queue.md §#2 concern #11 — RESOLVED).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Migrate `LiveStructuralKeyHandler` + retire `focusEditAt` from delegates

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/qml/delegates/*Delegate.qml` (delete `focusEditAt` if Task 8/9 deferred it)

- [ ] **Step 1: Replace `requestTextCaretAtNewRow` etc. with `establishFocus`**

Find call sites in `LiveStructuralKeyHandler.cpp`. For each:

```cpp
c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
```

Replace with the equivalent BlockAnchor-keyed call. You'll need to look up the anchor — either from the model (`m_model->dataAt(c.blockIndex + 1).blockAnchor`) or from the operation result (Cmd::enterAtEnd probably returns the new anchor; check).

```cpp
const auto newAnchor = /* the new block's BlockAnchor */;
c.cursorState->establishFocus(newAnchor, 0);
```

If `Cmd::enterAtEnd` doesn't return the anchor, add a return value (small Cmd-side change) or fetch from the post-mutation model state. Prefer returning from Cmd — cleaner.

- [ ] **Step 2: Retire `focusEditAt` from each delegate**

Now that no caller uses it, delete the function from each text-bearing delegate.

```bash
grep -n "focusEditAt" libs/markoff-live/
```
Should be empty after the deletion. If still referenced, **stop and trace** — there's a caller we missed.

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/qml/delegates/*.qml
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveStructuralKeyHandler → establishFocus; retire focusEditAt

Spec §4.3 #1 + §5.3. Key handler's requestTextCaretAtNewRow calls
migrate to establishFocus. focusEditAt() retired from every text-
bearing delegate (no callers remain).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Migrate `LiveView.qml` — retire `Connections{onCursorChanged}` + MouseArea focus

**Files:**
- Modify: `libs/markoff-live/qml/LiveView.qml`

- [ ] **Step 1: Delete the `Connections{onCursorChanged}` block (~lines 110–127)**

This was the LiveView-level handler that called `item.focusEditAt(...)`. The chokepoint now owns this; the block is dead weight.

- [ ] **Step 2: Migrate MouseArea press/release (~lines 302–305 + 331–333)**

Replace each call to `focusEditAt(...)` with:

```qml
cursorState.establishFocus(model.blockAnchor, /* qtPos from hit-test */);
```

The qtPos derives from the hit-test on the block at the press/release location — same calculation as today, just routed differently. The "re-confirm focus on `onReleased` because press may have been pre-empted" comment at ~line 328 and its associated retry: **delete both**. If the press's `establishFocus` were getting lost, that would be a chokepoint bug surfaced by §8.1 — not a UI-level concern.

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev --target markoff-live-app -j 8
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -10
```

Expected: green. **Bug A/B regressions should now actually pass under their QEXPECT_FAIL `Continue` mode — XPASS status.** That's the signal that Task 13 should remove the markers.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/qml/LiveView.qml
git commit -m "$(cat <<'EOF'
markoff-live: slot — LiveView retires onCursorChanged + MouseArea retry

Spec §4.3 #3 + §5.3. The LiveView-level Connections{onCursorChanged}
handler and the MouseArea's onReleased re-confirm retry are both
dead weight under the chokepoint. Replaced with two establishFocus
calls on press/release.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Remove QEXPECT_FAIL markers — Bug A/B regressions now pass

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_focus_after_enter_at_paragraph_end.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp`

- [ ] **Step 1: Remove QEXPECT_FAIL from the two bug scenarios in the invariant test**

Find:
```cpp
QEXPECT_FAIL("", "Bug A — see ...", Continue);
```
in `enter_at_paragraph_end` and `heading_demote_via_hash_deletion`. Delete those lines.

- [ ] **Step 2: Remove QEXPECT_FAIL from the named regression tests**

Same in the two named-after-symptom tests.

- [ ] **Step 3: Build + test**

```bash
ctest --test-dir build-dev -R 'focus_chokepoint_invariant|focus_after_' --output-on-failure
```
Expected: every test passes outright.

- [ ] **Step 4: Confirm the full suite is green**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp \
        libs/markoff-live/tests/tst_live_render_focus_after_enter_at_paragraph_end.cpp \
        libs/markoff-live/tests/tst_live_render_focus_after_heading_demote_via_hash_deletion.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — drop QEXPECT_FAIL on bug-A/B scenarios (chokepoint complete)

Spec §8.3 step 2 final piece. Bugs A and B now PASS via the
chokepoint. CI fully green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Retirement-evidence tests (§8.5)

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_focus_path_shape.cpp`
- Create: `libs/markoff-live/tests/scripts/check_no_qt_calllater_in_focus_path.sh`
- Create: `libs/markoff-live/tests/scripts/check_focus_path_through_chokepoint.sh`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

Spec §8.5 prescribes two shape-style tests. Easiest implementation: shell scripts wrapped as `add_test` entries. They are CI guards, not behavioural tests.

- [ ] **Step 1: Write the `Qt.callLater` audit script**

Create `libs/markoff-live/tests/scripts/check_no_qt_calllater_in_focus_path.sh`:

```bash
#!/bin/bash
# Spec §8.5 — assert that no Qt.callLater appears in focus-related
# blocks of text-bearing delegates. The Math delegate's latexEdit
# Qt.callLater (BlockInternalEdit sub-cursor, spec §2.2) is the
# one exception and is explicitly allowlisted by line context.
set -euo pipefail

ROOT="${1:?usage: $0 <repo-root>}"
DELEGATES="$ROOT/libs/markoff-live/qml/delegates"

# Find Qt.callLater hits in text-bearing delegates, EXCLUDING the
# MathDelegate latexEdit-popup line (BlockInternalEdit sub-cursor).
HITS=$(grep -rn "Qt\.callLater" "$DELEGATES"/{Paragraph,Heading,Blockquote,CodeBlock,ListItem,Math}Delegate.qml \
       | grep -v "latexEdit\.forceActiveFocus" || true)

if [[ -n "$HITS" ]]; then
    echo "FAIL: Qt.callLater found in focus path:"
    echo "$HITS"
    exit 1
fi

echo "PASS: no Qt.callLater in focus path"
```

Make executable: `chmod +x libs/markoff-live/tests/scripts/check_no_qt_calllater_in_focus_path.sh`.

- [ ] **Step 2: Write the `forceActiveFocus` shape script**

Create `libs/markoff-live/tests/scripts/check_focus_path_through_chokepoint.sh`:

```bash
#!/bin/bash
# Spec §8.5 — assert that forceActiveFocus() appears in QML only
# inside takeFocus() bodies. The Math delegate's latexEdit
# forceActiveFocus is allowlisted.
set -euo pipefail

ROOT="${1:?usage: $0 <repo-root>}"
QML="$ROOT/libs/markoff-live/qml"

# Strategy: for each occurrence of forceActiveFocus, check the
# preceding ~5 lines for a takeFocus function header or for the
# allowlisted latexEdit context.
HITS=$(grep -rn "forceActiveFocus" "$QML" || true)

VIOLATIONS=""
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    file="${line%%:*}"
    lineno="${line#*:}"; lineno="${lineno%%:*}"
    # context window: lines (lineno-6) to lineno
    start=$(( lineno - 6 ))
    [[ $start -lt 1 ]] && start=1
    ctx=$(sed -n "${start},${lineno}p" "$file")
    if echo "$ctx" | grep -q "function takeFocus" || echo "$ctx" | grep -q "latexEdit"; then
        continue
    fi
    VIOLATIONS+="$line"$'\n'
done <<< "$HITS"

if [[ -n "$VIOLATIONS" ]]; then
    echo "FAIL: forceActiveFocus() outside takeFocus():"
    echo "$VIOLATIONS"
    exit 1
fi

echo "PASS: focus path exits through chokepoint"
```

- [ ] **Step 3: Wire as `add_test` entries**

In `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
add_test(NAME tst_live_render_no_qt_calllater_in_focus_path
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_no_qt_calllater_in_focus_path.sh
                 ${CMAKE_SOURCE_DIR})
add_test(NAME tst_live_render_focus_path_exits_through_chokepoint
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_focus_path_through_chokepoint.sh
                 ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 4: Run them**

```bash
ctest --test-dir build-dev -R 'no_qt_calllater_in_focus_path|focus_path_exits' --output-on-failure
```
Expected: both pass (the migration is complete; no stragglers).

If either fails: the failure message names the offending line. Fix it (it's a real retirement gap) and re-run.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/scripts/ libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: test — retirement-evidence shape tests

Spec §8.5. Two CI guards: no Qt.callLater in focus path (excluding
the BlockInternalEdit sub-cursor); forceActiveFocus only inside
takeFocus. Future agents who reintroduce a smell will see CI fail,
not a Discipline-Log entry.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: Falsifiability-stub commit (CI red, expected)

**Files:**
- Modify: `libs/markoff-live/qml/delegates/{Paragraph,Heading,Blockquote,CodeBlock,ListItem,Math}Delegate.qml` (stub `takeFocus`)

Spec §8.3 step 3. This commit is **kept in history**, not rebased away — it's the audit trail proving invariant 4 was honoured.

- [ ] **Step 1: Stub every `takeFocus`**

In each of the six text-bearing delegates, replace the body:

```qml
function takeFocus(qtPos) {
    // INTENTIONALLY EMPTY — falsifiability proof per spec §8.3.
    // Confirms the invariant tests fail when the chokepoint stops
    // delivering focus.
}
```

- [ ] **Step 2: Run the full test suite and record failure counts**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8 2>&1 | tee /tmp/falsifiability-stub-output.txt
ctest --test-dir build-dev -R 'tst_live_cursor_state_chokepoint' --output-on-failure 2>&1 | tee -a /tmp/falsifiability-stub-output.txt

grep -E "Failed|PASS|FAIL" /tmp/falsifiability-stub-output.txt | tail -30
```

Expected: every §8.1 sub-test fails. Both §8.2 named regressions fail. **Pure-policy unit tests in `tst_live_cursor_state_chokepoint` continue to pass** — they don't dispatch through `takeFocus`; they probe the state machine directly. Click-to-focus scenarios in §8.1 also fail.

**If any §8.1 scenario passes with the stub: the test is too lenient.** Stop, fix the test, re-stub, re-run. This is the invariant-4 enforcement; this is the rule.

Record exact counts. Example template:

```
Falsifiability stub results (spec §8.3 step 3):
- tst_live_render_focus_chokepoint_invariant:
    17 sub-tests, 17 failed, 0 passed (expected: all 17 fail)
- tst_live_render_focus_after_enter_at_paragraph_end: FAILED (expected)
- tst_live_render_focus_after_heading_demote_via_hash_deletion: FAILED (expected)
- tst_live_cursor_state_chokepoint:
    6 sub-tests, 6 passed (expected: pure-policy, no dispatch path)
- tst_live_render_no_qt_calllater_in_focus_path: PASSED (shape unchanged)
- tst_live_render_focus_path_exits_through_chokepoint: PASSED (shape unchanged)
```

- [ ] **Step 3: Commit the stub with the failure record in the message**

```bash
git add libs/markoff-live/qml/delegates/*.qml
git commit -m "$(cat <<'EOF'
markoff-live: stub — takeFocus empty (FALSIFIABILITY PROOF, REVERTS NEXT)

Spec §8.3 step 3. Stubs every delegate's takeFocus(qtPos) to be
empty. Confirms the invariant tests fail when the chokepoint stops
delivering focus, per invariant 4 / R5-holes post-mortem §6.2.

Failure counts:
- tst_live_render_focus_chokepoint_invariant: <N>/<TOTAL> sub-tests failed
- tst_live_render_focus_after_enter_at_paragraph_end: FAILED
- tst_live_render_focus_after_heading_demote_via_hash_deletion: FAILED
- tst_live_cursor_state_chokepoint: 6/6 PASSED (pure-policy; expected)
- Retirement-evidence tests: PASSED (shape unchanged)

This commit is REVERTED in the immediately-following commit. It is
kept in history as the audit trail for invariant 4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Replace `<N>/<TOTAL>` with the actual counts from step 2.

---

## Task 16: Revert the falsifiability stub

**Files:**
- Revert: the previous commit.

- [ ] **Step 1: Revert via `git revert`**

```bash
git revert HEAD --no-edit
```

Or, if a fresh commit is preferred over a revert commit, restore the file contents manually and commit. **`git revert` is the recommended form** — it makes the audit trail explicit.

- [ ] **Step 2: Build + full test sweep**

```bash
cmake --build build-dev -j 8
ctest --test-dir build-dev -R '^tst_live_render_|^tst_live_cursor_state_' --output-on-failure -j 8 2>&1 | tail -20
```
Expected: every test passes. **If anything fails, stop** — the revert was incomplete or the stub-revert sequence damaged something else.

- [ ] **Step 3: Confirm the audit trail in `git log`**

```bash
git log --oneline -5
```
Expected to show:
```
<hash> Revert "markoff-live: stub — takeFocus empty (FALSIFIABILITY PROOF, REVERTS NEXT)"
<hash> markoff-live: stub — takeFocus empty (FALSIFIABILITY PROOF, REVERTS NEXT)
<hash> markoff-live: test — retirement-evidence shape tests
...
```

- [ ] **Step 4: No additional commit needed** — `git revert` already committed.

---

## Task 17: Dogfood request + queue update

**Files:**
- Create: `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`
- Modify: `docs/queue.md` (mark concerns #1 partial / #7 / #8 / #11 resolved-by-tier-1)

- [ ] **Step 1: Write the dogfood request**

Create `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md`:

```markdown
# Focus-chokepoint refactor — interactive dogfood request

**Date:** 2026-05-11
**Tag candidate:** `v0.8.0-focus-chokepoint`
**Status:** held pending interactive dogfood (user's local desktop)
**Spec:** `docs/specs/2026-05-11-focus-chokepoint-design.md`
**Plan:** `docs/plans/2026-05-11-focus-chokepoint.md`

## What landed

Tier 1 of the focus-chokepoint refactor programme. `LiveCursorState`
now owns focus delivery on structural events through a single
`establishFocus(BlockAnchor, qtPos)` API and a per-delegate
registration mechanism. The two reported bugs (focus loss after
Enter at paragraph end; focus loss after heading demote via `#`
deletion) now pass dedicated regression tests on the QML
integration harness.

187/187 (or whatever the count is) fast tests pass. The
falsifiability-stub commit (`<hash>` from Task 15) is preserved in
history as the audit trail for invariant 4.

## Dogfood checklist

In `markoff-live-app` against a real markdown document:

1. Place cursor at end of any paragraph; press Enter; immediately
   type — typing should continue in the new block without
   re-clicking. (Bug A.)
2. Place cursor at end of a heading; press Enter; immediately type
   — same.
3. Place cursor inside a heading; delete all leading `#`s; the
   block should transition Heading→Paragraph and immediately accept
   typing without re-clicking. (Bug B.)
4. Place cursor at start of a paragraph; type `#` then space; the
   block should transition Paragraph→Heading and immediately accept
   typing.
5. Click on any block; immediately type — the typed text should
   appear at the click position.
6. Cross-block selection via Shift+Click; trigger an editing
   command (Ctrl+B or whatever exists); focus should be visible
   afterward.
7. In a 200-block document, repeat steps 1–6 with scrolling — focus
   should not get lost when delegate incubation is under stress.
8. After ~5 minutes of typical editing: no observable focus loss,
   no need to click-to-recover.

## Sign-off

Reply on this document or in chat once dogfood completes. Tag
`v0.8.0-focus-chokepoint` lands only after sign-off.

## What's deferred (tier 2/3/4)

Per spec §10 — typing-cursor authority (queue #2 #1 full / #2 / #6
full / #9), API consolidation (#3 / #4 / #5 / #12), selection/cursor
unification (#10). Each tier dogfoods before the next is planned.
```

- [ ] **Step 2: Update `docs/queue.md`**

In the existing §#2 ("Cursor architecture cleanup") banner area, add at the top:

```markdown
> **2026-05-11 — Tier 1 (focus-chokepoint) implemented.** Concerns
> **#1 (partial — structural side)**, **#7**, **#8**, **#11** are
> resolved. See `docs/specs/2026-05-11-focus-chokepoint-design.md`
> and `docs/plans/2026-05-11-focus-chokepoint.md`. Remaining
> concerns (#1 full, #2, #3, #4, #5, #6 full, #9, #10, #12) tier into
> tier 2/3/4 per spec §10; no spec yet, gated on tier-1 dogfood.
```

- [ ] **Step 3: Commit**

```bash
git add docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md \
        docs/queue.md
git commit -m "$(cat <<'EOF'
docs: focus-chokepoint dogfood request + queue #2 status update

Tier 1 of the focus-chokepoint programme is implemented. Dogfood
checklist at docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md.
Tag v0.8.0-focus-chokepoint held pending interactive sign-off.
Queue #2 concerns #1 (partial), #7, #8, #11 marked resolved.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Definition of done

- [ ] All 17 tasks complete in order.
- [ ] `ctest --test-dir build-dev -R '^tst_live_render_|^tst_live_cursor_state_' -j 8` is green.
- [ ] Falsifiability-stub commit (Task 15) is preserved in history with its revert (Task 16). `git log` shows both.
- [ ] `docs/queue.md` §#2 banner updated.
- [ ] `docs/handoff/2026-05-11-focus-chokepoint-dogfood-request.md` exists with the checklist.
- [ ] Tag `v0.8.0-focus-chokepoint` is **not yet** created — held pending dogfood per spec §8.6 / §10.

---

## Self-review checklist (executor: run before declaring done)

1. **Spec coverage** — every §5.1, §5.2, §5.3, §6, §7, §8 item maps to a task. Walk the spec section-by-section; if anything has no task, it's a plan gap.
2. **Bug A and Bug B pass.** Both named regression tests must transition QEXPECT_FAIL → pass between Tasks 2–3 and Task 13.
3. **Invariant 4 honoured.** Stub commit + revert pair is in history. The stub commit message records actual failure counts, not placeholders.
4. **Invariant 3 honoured.** Spec §5.3 retirements all landed: 6 delegate focus-block deletions, 1 LiveView `Connections` block, 6 delegate `Connections{onCursorChanged}` blocks, 6 delegate `focusEditAt` functions, 1 LiveView MouseArea retry, 2 LiveListModelBinding re-anchor blocks, 1 duplicated clamp pair.
5. **Build cap honoured.** Every `cmake --build` and `ctest` call uses `-j 8` or no `-j` — no bare `-j` or `-j N` where N>8 (per project memory).
6. **`Qt.callLater` and `forceActiveFocus` accounting.** The two retirement-evidence tests (Task 14) pass at the end of Task 16.
