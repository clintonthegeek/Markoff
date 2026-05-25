# Tier 2 — Cursor typing-authority + invariants — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close queue #2 concerns #1 (docstring honesty), #2 (`TextCaret` field rename), and #6 (per-keystroke sync invariant test). Defer #9 to tier 3 with a discipline-log entry. No production-behavior changes — pure cleanup + additive test coverage.

**Architecture:** Documentation correction (one docblock); mechanical field rename across 16 sites; one new test target on `LiveRealisticInputHarness` with five invariant slots and a falsifiability stub. No new authority introduced; the existing dual-store (TextEdit canonical for in-block typing, `m_cursor` canonical for structural) is documented as-is.

**Tech Stack:** C++20, Qt 6.8+, QML, CMake 3.19+. Tests via QTest + `LiveRealisticInputHarness` + `QmlIntegrationFixture`. Build cap: `-j 8` always (per project memory). Tests run via `xvfb-run`.

**Spec:** `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` is authoritative; cite section numbers when in doubt.

**Reading order before starting:**
1. `docs/INVARIANTS.md` (invariants 1, 2, 3, 4, 5, 8)
2. `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` (full — short)
3. `docs/specs/2026-05-11-focus-chokepoint-design.md` §3, §10 (L4 precedent + tier framing)
4. `libs/markoff-live/include/markoff/live/Cursor.h` (current TextCaret shape)
5. `libs/markoff-live/include/markoff/live/LiveCursorState.h` (class header to rewrite)
6. `libs/markoff-live/tests/tst_live_render_focus_chokepoint_invariant.cpp` (test template)
7. `libs/markoff-live/tests/QmlIntegrationFixture.h` (fixture API — `cursorStateCurrentQtPos`, `delegateCursorPos`)
8. `libs/markoff-live/tests/LiveRealisticInputHarness.h` (typing harness — note ASCII-only `typeChar`)

**Build / test commands:**
```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
cmake --build build-dev -j 8
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8

# Full live-render fast suite (excludes the slow benchmark + realistic tests):
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 --output-on-failure

# This plan's test target only:
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -R 'tst_live_render_cursor_typing_invariant' --output-on-failure
```

**Commit-message prefix convention:** `markoff-live: <slot summary>` for code; `docs:` for spec/plan/queue updates (per recent commit history).

---

## Files touched

| File | Change |
|---|---|
| `libs/markoff-live/include/markoff/live/LiveCursorState.h` | Rewrite class header docblock (lines 21–37). |
| `libs/markoff-live/include/markoff/live/Cursor.h` | Rename `TextCaret::cachedByteOffset` → `cachedQtPos`; rewrite field comment. |
| `libs/markoff-live/src/LiveCursorState.cpp` | Rename 10 references to `cachedByteOffset`. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Rename 2 references. |
| `libs/markoff-live/tests/tst_live_render_cursor.cpp` | Rename 6 references. |
| `libs/markoff-live/tests/LiveRealisticInputHarness.h` | Add `typeUnicode(QChar)` for non-ASCII. |
| `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp` | **New** — 5 invariant slots. |
| `libs/markoff-live/tests/CMakeLists.txt` | Register new test target. |
| `docs/queue.md` | Discipline-log entry for concern #9 deferral; update queue #2 banner. |

---

## Task 1: Pre-flight checks

**Files:** none (verification only).

- [ ] **Step 1:** Confirm worktree is clean except for known-noise files. From `/home/clinton/dev/Markoff/.worktrees/foundation-exploration`:

```bash
git status --short
```

Expected: untracked-only output (`.superpowers/`, `selection.txt`, `selection2.txt`, possibly `Testing/Temporary/LastTest.log`). If anything tracked is modified, stop and report.

- [ ] **Step 2:** Confirm HEAD is `34115a2` (tier-2 spec commit) or later on `exploration/new-foundation`:

```bash
git log --oneline -3
git branch --show-current
```

Expected: branch `exploration/new-foundation`, top commits include `34115a2 docs: spec — Tier 2 cursor typing-authority`.

- [ ] **Step 3:** Confirm build is green:

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
```

Expected: no errors, all targets built.

- [ ] **Step 4:** Record baseline test failures (the "8 preexisting + 1 known" set named in the block-only kinds plan Task 14):

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 | grep -E 'tests failed' | tail -1
```

Expected: `9 tests failed out of 198` (or similar — record the exact set for later comparison). Save the failing-test list:

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier2-baseline-failures.txt
wc -l /tmp/tier2-baseline-failures.txt
```

Expected: 9 lines. This file is the regression-check baseline for Task 14.

---

## Task 2: Concern #1 — `LiveCursorState` docstring honesty

**Files:** Modify `libs/markoff-live/include/markoff/live/LiveCursorState.h:21-37`.

Per spec §5.1.

- [ ] **Step 1:** Open `libs/markoff-live/include/markoff/live/LiveCursorState.h`. The current class-header docblock reads:

```cpp
/// Owns the single canonical cursor value for the live view. Validates
/// `request()` calls against the target block's `BlockKindDescriptor`
/// (so BlockSelected is refused on a paragraph, etc.). Emits
/// `cursorChanged()` only when the cursor actually changes. Spec §5.3.
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

Replace lines 21–37 with:

```cpp
/// Owns the canonical cursor value for **structural events** (kind
/// transitions, cross-block navigation, `BlockSelected`,
/// `BlockInternalEdit`). For **in-block caret position during typing**,
/// `QQuickTextEdit::cursorPosition` is canonical; `m_cursor` mirrors it
/// via `syncFromTextEdit`, called from each text-bearing delegate's
/// `onCursorPositionChanged` and from `LiveEditBinding::onContentsChange`
/// after each buffer edit. The authority split is documented in
/// `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md` §3.
///
/// Validates `request()` calls against the target block's
/// `BlockKindDescriptor` (so `BlockSelected` is refused on a paragraph,
/// etc.). Emits `cursorChanged()` only when the cursor actually changes.
/// Spec §5.3.
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

- [ ] **Step 2:** Build (catches any header-syntax issue):

```bash
cmake --build build-dev --target markoff_live -j 8 2>&1 | tail -3
```

Expected: builds clean.

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h
git commit -m "$(cat <<'EOF'
markoff-live: docstring — LiveCursorState dual-authority honesty (#1)

Replaces the 'single canonical cursor value' framing with the honest
dual-store statement: TextEdit is canonical for in-block typing,
m_cursor is canonical for structural events. syncFromTextEdit is the
reconciliation hook. Cites the tier-2 spec for the authority split.

Closes queue #2 concern #1. No code change.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

Expected: one-file commit landed on `exploration/new-foundation`.

---

## Task 3: Concern #2 — `TextCaret::cachedByteOffset` → `cachedQtPos`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/Cursor.h`
- Modify: `libs/markoff-live/src/LiveCursorState.cpp` (10 sites)
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp` (2 sites)
- Modify: `libs/markoff-live/tests/tst_live_render_cursor.cpp` (6 sites)

Per spec §5.2.

- [ ] **Step 1:** Verify the current count of references is 16 (catches any code that has changed since the spec was written):

```bash
git grep -n 'cachedByteOffset' libs/markoff-live/ | wc -l
```

Expected: `16`. If different, stop and re-check the call-site list before continuing.

- [ ] **Step 2:** Open `libs/markoff-live/include/markoff/live/Cursor.h`. Find the `TextCaret` struct's `cachedByteOffset` field. Rename the field to `cachedQtPos` and replace its surrounding comment with:

```cpp
    /// UTF-16 code-unit offset within the block, as reported by
    /// `QQuickTextEdit::cursorPosition`.
    /// Producer: TextEdit::cursorPositionChanged → LiveCursorState::syncFromTextEdit.
    /// Consumer: focus dispatch (focusEditAt(qtPos)), focusedQtPos Q_PROPERTY.
    /// NOT a byte offset — for conversion to CRDT byte space, see
    /// `Coordinates::qtPosToByte` (LiveEditBinding.cpp:151).
    quint32 cachedQtPos = 0;
```

(Preserve the existing initializer value — `= 0` per Cursor.h. If the existing default differs, keep that default.)

- [ ] **Step 3:** Rename every other reference using `git grep` + `sed`. From the worktree root:

```bash
git grep -l 'cachedByteOffset' libs/markoff-live/ \
  | xargs sed -i 's/cachedByteOffset/cachedQtPos/g'
```

- [ ] **Step 4:** Verify zero occurrences remain:

```bash
git grep -n 'cachedByteOffset' libs/markoff-live/
```

Expected: empty output.

- [ ] **Step 5:** Verify the renamed field is present at all 16 prior sites:

```bash
git grep -n 'cachedQtPos' libs/markoff-live/ | wc -l
```

Expected: `16` (15 references + 1 field declaration — actually 16 total if you count the declaration; verify it matches the grep count from Step 1 +1 for the new declaration site if it was prior counted, OR equal if the declaration was in the original 16; expected exact count `16`).

- [ ] **Step 6:** Build:

```bash
cmake --build build-dev -j 8 2>&1 | tail -5
```

Expected: builds clean. Any compiler error here means a reference was missed or the rename hit a string literal — investigate.

- [ ] **Step 7:** Run the cursor test to verify no regression:

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -R '^tst_live_render_cursor$' --output-on-failure
```

Expected: test passes (it was failing in baseline per `/tmp/tier2-baseline-failures.txt` — but if it's failing now for *different* reasons than baseline, investigate. The failure mode must be identical to baseline; compare with `diff`).

If `tst_live_render_cursor` was *not* in the baseline failure list, expect it to pass.

- [ ] **Step 8:** Commit:

```bash
git add libs/markoff-live/include/markoff/live/Cursor.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "$(cat <<'EOF'
markoff-live: rename — TextCaret::cachedByteOffset → cachedQtPos (#2)

The field stores qtPos (UTF-16 code units from QQuickTextEdit::
cursorPosition) but was named for bytes. All 16 references treat it
as qtPos; no consumer ever used it as a byte offset, so the rename is
safe and mechanical. Field comment updated to name UTF-16 explicitly
and point at Coordinates::qtPosToByte for the byte-conversion entry
point.

Closes queue #2 concern #2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Extend `LiveRealisticInputHarness` with `typeUnicode`

**Files:** Modify `libs/markoff-live/tests/LiveRealisticInputHarness.h`.

The existing `typeChar(QChar)` asserts `c.unicode() < 0x80 && c.isLetterOrNumber()` and routes through `QTest::keyClick(window, char)`. For CJK and emoji slots, we need a path that delivers a `QKeyEvent` whose `text()` field carries the Unicode character. `QQuickTextEdit::keyPressEvent` reads `text()` for insertion, so this works regardless of `Qt::Key` code.

- [ ] **Step 1:** Open `libs/markoff-live/tests/LiveRealisticInputHarness.h`. After the `typeString` method (around line 60), add:

```cpp
    /// Any Unicode QChar, including non-ASCII and surrogate-pair halves.
    /// Delivers a QKeyEvent with `text()` set to the character. Use this
    /// for CJK / emoji / accented input. ASCII letters and digits should
    /// still go through `typeChar` (which uses QTest::keyClick's char
    /// overload — battle-tested path).
    void typeUnicode(QChar c) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                        QString(c));
        QCoreApplication::sendEvent(m_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier,
                          QString(c));
        QCoreApplication::sendEvent(m_window, &release);
        QTest::qWait(m_defaultGapMs);
        QCoreApplication::processEvents();
    }

    /// Like `typeString` but for arbitrary Unicode (including surrogate
    /// pairs). Iterates by `QChar` so a code point outside the BMP is
    /// delivered as two events — which is exactly how `cursorPosition`
    /// (UTF-16 code units) will advance.
    void typeUnicodeString(const QString &text) {
        for (QChar c : text) typeUnicode(c);
    }
```

Also ensure `<QKeyEvent>` is included (Qt usually pulls it via `<QtTest>`, but be explicit). Near the top, add if missing:

```cpp
#include <QKeyEvent>
```

- [ ] **Step 2:** Build:

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
```

Expected: builds clean. (No test target uses `typeUnicode` yet; this just verifies the header compiles.)

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/tests/LiveRealisticInputHarness.h
git commit -m "$(cat <<'EOF'
markoff-live: harness — typeUnicode/typeUnicodeString for non-ASCII

Adds a QKeyEvent-based typing path that carries arbitrary Unicode in
the event's text() field. QQuickTextEdit reads text() to insert, so
this works for CJK, emoji (surrogate pairs), and accented input —
none of which QTest::keyClick(window, char) supports. ASCII typing
still uses typeChar.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Scaffold `tst_live_render_cursor_typing_invariant`

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1:** Create `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp` with the following skeleton (slot bodies added in Tasks 6–10):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

// Per-keystroke invariant: after every TextEdit cursorPositionChanged
// emission, LiveCursorState.focusedQtPos == focused TextEdit's
// cursorPosition. Spec §5.3.
//
// Test document layout (1 row):
//   Row 0: paragraph "" (empty — ready for typing)
class TestCursorTypingInvariant : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void cursor_mirrors_textedit_through_ascii_typing();
    void cursor_mirrors_textedit_through_cjk_typing();
    void cursor_mirrors_textedit_through_emoji_typing();
    void cursor_mirrors_textedit_through_arrow_within_block();
    void cursor_mirrors_textedit_through_kind_transition();

private:
    void assertMirrorMatches(const QString &scenario);
    std::unique_ptr<QmlIntegrationFixture> m_fixture;
};

void TestCursorTypingInvariant::init() {
    // One empty paragraph; cursor lands in it at qtPos=0.
    m_fixture = std::make_unique<QmlIntegrationFixture>(QByteArray("\n"), 1);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    m_fixture->placeCursorAtPos(0, 0);
}

void TestCursorTypingInvariant::cleanup() {
    m_fixture.reset();
}

// Asserts the per-keystroke invariant. Reads both the canonical mirror
// (m_cursor.focusedQtPos) and the production-truth (delegate's
// TextEdit cursorPosition) and compares.
void TestCursorTypingInvariant::assertMirrorMatches(const QString &scenario) {
    const int focusedRow      = m_fixture->cursorStateCurrentRow();
    QVERIFY2(focusedRow >= 0,
             qPrintable(QString("no focused row after %1").arg(scenario)));
    const int delegateQtPos = m_fixture->delegateCursorPos(focusedRow);
    const int mirrorQtPos   = m_fixture->cursorStateCurrentQtPos();
    QCOMPARE(mirrorQtPos, delegateQtPos);
}

// Slot bodies — added in Tasks 6–10.
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_ascii_typing() {
    QSKIP("Implemented in Task 6");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_cjk_typing() {
    QSKIP("Implemented in Task 7");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_emoji_typing() {
    QSKIP("Implemented in Task 8");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_arrow_within_block() {
    QSKIP("Implemented in Task 9");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_kind_transition() {
    QSKIP("Implemented in Task 10");
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCursorTypingInvariant)
#include "tst_live_render_cursor_typing_invariant.moc"
```

- [ ] **Step 2:** Open `libs/markoff-live/tests/CMakeLists.txt`. Immediately after the existing `tst_live_render_focus_chokepoint_invariant` block (end at line ~532), insert the new target:

```cmake
qt_add_executable(tst_live_render_cursor_typing_invariant
    tst_live_render_cursor_typing_invariant.cpp
    QmlIntegrationFixture.h
    QmlIntegrationFixture.cpp
)
target_link_libraries(tst_live_render_cursor_typing_invariant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Qml
    Qt6::Widgets Qt6::Test
    markoff_live markoff_core
    markoff-live-app-internal
    markoff-live-app-internalplugin
    markoff-live-app-internalplugin_init)
add_test(NAME tst_live_render_cursor_typing_invariant
         COMMAND tst_live_render_cursor_typing_invariant)
set_tests_properties(tst_live_render_cursor_typing_invariant
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3:** Configure + build the new target:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -3
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
```

Expected: target builds clean.

- [ ] **Step 4:** Run the skeleton (5 skips expected):

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -R 'tst_live_render_cursor_typing_invariant' --output-on-failure
```

Expected: test runs, 5 slots all SKIP. (`ctest` reports test as passed when all slots skip.)

- [ ] **Step 5:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: test scaffold — tst_live_render_cursor_typing_invariant

Skeleton with 5 QSKIP'd slots. Fixture loads a single empty paragraph;
cursor placed at (row=0, qtPos=0). assertMirrorMatches helper compares
LiveCursorState.focusedQtPos against the focused delegate's TextEdit
cursorPosition. Slot bodies land in Tasks 6–10.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Slot — ASCII typing

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`.

- [ ] **Step 1:** Replace the `cursor_mirrors_textedit_through_ascii_typing` body (currently `QSKIP`) with:

```cpp
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_ascii_typing() {
    LiveRealisticInputHarness h(m_fixture->window());

    const QString text = QStringLiteral("Hello12");  // letters + digits (typeChar contract)
    for (int i = 0; i < text.size(); ++i) {
        h.typeChar(text.at(i));
        assertMirrorMatches(
            QString("ASCII typing after char #%1 (%2)").arg(i + 1).arg(text.at(i)));
    }
    // Expected qtPos == text.size() after the loop.
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
```

(`typeChar` requires letters/digits only — hence `Hello12` rather than `Hello, World!`. Space and punctuation are exercised in subsequent slots via different paths; the invariant test does not need to enumerate every character class.)

- [ ] **Step 2:** Build + run:

```bash
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant \
  cursor_mirrors_textedit_through_ascii_typing 2>&1 | tail -10
```

Expected: `PASS` (production already maintains the invariant via `syncFromTextEdit`; the test pins it).

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cursor mirror invariant on ASCII typing

Types 'Hello12' one char at a time; asserts m_cursor.focusedQtPos
matches delegate TextEdit cursorPosition after every keystroke.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Slot — CJK typing

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`.

- [ ] **Step 1:** Replace the `cursor_mirrors_textedit_through_cjk_typing` body with:

```cpp
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_cjk_typing() {
    LiveRealisticInputHarness h(m_fixture->window());

    // 6 BMP code points; each advances qtPos by 1 (no surrogate pairs).
    const QString text = QString::fromUtf8("これはテスト");
    for (int i = 0; i < text.size(); ++i) {
        h.typeUnicode(text.at(i));
        assertMirrorMatches(
            QString("CJK typing after char #%1").arg(i + 1));
    }
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
```

- [ ] **Step 2:** Build + run:

```bash
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant \
  cursor_mirrors_textedit_through_cjk_typing 2>&1 | tail -10
```

Expected: `PASS`. If it fails, the most likely cause is `typeUnicode` not delivering text to TextEdit — investigate by adding a `qDebug() << m_fixture->modelText(0)` after the loop and check whether the model received any characters.

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cursor mirror invariant on CJK typing

Types 'これはテスト' via typeUnicode; each BMP code point advances
qtPos by 1. Asserts mirror matches delegate after every keystroke.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Slot — emoji typing (surrogate pairs)

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`.

- [ ] **Step 1:** Replace the `cursor_mirrors_textedit_through_emoji_typing` body with:

```cpp
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_emoji_typing() {
    LiveRealisticInputHarness h(m_fixture->window());

    // 3 emoji, each a UTF-16 surrogate pair: qtPos advances by 2 per emoji
    // because typeUnicodeString iterates by QChar (one event per surrogate
    // half).
    const QString text = QString::fromUtf8("🎉🚀✨");
    h.typeUnicodeString(text);
    assertMirrorMatches("emoji typing");

    // text.size() is QChar count (== UTF-16 code units) — exactly what
    // cursorPosition / focusedQtPos report.
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
```

(Note: `typeUnicodeString` delivers one event per `QChar` (UTF-16 code unit), so each emoji contributes two events. We assert the invariant once at the end rather than per-keystroke because mid-surrogate-pair the mirror and delegate may briefly differ during the inter-event qWait — QQuickTextEdit may or may not accept a lone high-surrogate. End-of-string assertion is the meaningful contract.)

- [ ] **Step 2:** Build + run:

```bash
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant \
  cursor_mirrors_textedit_through_emoji_typing 2>&1 | tail -10
```

Expected: `PASS`. If `cursorStateCurrentQtPos()` returns a value smaller than `text.size()`, Qt may be combining surrogate halves into a single event — that's still correct behavior; relax the per-emoji expectation and assert only that mirror == delegate, dropping the `QCOMPARE(..., text.size())` line.

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cursor mirror invariant on emoji (surrogate pairs)

Types 3 emoji via typeUnicodeString; each contributes 2 UTF-16 events.
Asserts mirror matches delegate at end-of-string and qtPos == QChar
count (UTF-16 code units, as cursorPosition reports).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Slot — in-block arrow navigation

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`.

- [ ] **Step 1:** Replace the `cursor_mirrors_textedit_through_arrow_within_block` body with:

```cpp
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_arrow_within_block() {
    LiveRealisticInputHarness h(m_fixture->window());

    h.typeString(QStringLiteral("abcdef"));  // qtPos == 6
    assertMirrorMatches("after typing abcdef");

    // Left arrow within the block — no structural event, no kind transition.
    // TextEdit handles natively; mirror must follow via cursorPositionChanged.
    for (int i = 0; i < 3; ++i) {
        h.keyClick(Qt::Key_Left);
        assertMirrorMatches(QString("Left #%1 within block").arg(i + 1));
    }
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), 3);
}
```

- [ ] **Step 2:** Build + run:

```bash
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant \
  cursor_mirrors_textedit_through_arrow_within_block 2>&1 | tail -10
```

Expected: `PASS`.

- [ ] **Step 3:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cursor mirror invariant on in-block arrow nav

Type 'abcdef', then Left arrow three times. Asserts mirror matches
delegate after every arrow keystroke — the in-block path that TextEdit
handles natively without going through the structural-key handler.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Slot — kind transition (Paragraph → Heading)

**Files:** Modify `libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp`.

- [ ] **Step 1:** Replace the `cursor_mirrors_textedit_through_kind_transition` body with:

```cpp
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_kind_transition() {
    LiveRealisticInputHarness h(m_fixture->window());

    // Type '#', then space — space triggers Paragraph → Heading kind change.
    // Mirror must match the NEW HeadingDelegate's TextEdit cursorPosition
    // after the transition completes (tier-1's S1/S2/S3 re-anchor fix).
    h.typeChar(QLatin1Char('#'));
    assertMirrorMatches("after typing '#'");

    h.keyClick(Qt::Key_Space);
    // Kind transition is debounced through onD2Changed → applyOps; allow
    // the new delegate to register with the chokepoint.
    QVERIFY(m_fixture->waitForKindAt(0, QStringLiteral("Heading"), 2000));

    // After the transition, the focused delegate is the new HeadingDelegate;
    // cursorStateCurrentRow should still be 0; qtPos should equal the
    // current heading content's length (typically 2 if '# ' is kept in
    // buffer, or 0 if stripped — production decides; the invariant only
    // requires mirror == delegate).
    assertMirrorMatches("after kind transition Paragraph → Heading");
}
```

- [ ] **Step 2:** Build + run:

```bash
cmake --build build-dev --target tst_live_render_cursor_typing_invariant -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant \
  cursor_mirrors_textedit_through_kind_transition 2>&1 | tail -10
```

Expected: `PASS`. If it fails with mirror != delegate after the transition, the tier-1 S1/S2/S3 fix has a gap — file a discipline-log entry and re-scope; do not weaken the assertion.

- [ ] **Step 3:** Confirm all 5 slots pass together:

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant 2>&1 | tail -10
```

Expected: `Totals: 5 passed, 0 failed, 0 skipped`.

- [ ] **Step 4:** Commit:

```bash
git add libs/markoff-live/tests/tst_live_render_cursor_typing_invariant.cpp
git commit -m "$(cat <<'EOF'
markoff-live: test — cursor mirror invariant on Paragraph→Heading

Types '# ' to promote an empty paragraph to a Heading. Asserts mirror
matches the NEW delegate's TextEdit cursorPosition after the
DelegateChooser swap completes. Pins tier-1's S1/S2/S3 re-anchor fix.

Closes queue #2 concern #6: 5 slots cover ASCII, CJK, emoji,
in-block arrow, and kind transition. All passing on head.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Falsifiability proof — stub `syncFromTextEdit`

**Files:** Modify `libs/markoff-live/src/LiveCursorState.cpp` (revert in Task 12).

Per spec §5.4 / invariant 4.

- [ ] **Step 1:** Open `libs/markoff-live/src/LiveCursorState.cpp`. Locate `LiveCursorState::syncFromTextEdit` (around line 139). Replace its body with `return;` immediately after parameter validation, keeping the signature intact:

```cpp
void LiveCursorState::syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos)
{
    // FALSIFIABILITY PROOF (tier 2 spec §5.4). REVERTS in next commit.
    Q_UNUSED(anchor);
    Q_UNUSED(qtPos);
    return;
}
```

- [ ] **Step 2:** Build:

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
```

Expected: builds clean.

- [ ] **Step 3:** Run the typing-invariant suite — **all 5 slots must fail**:

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant 2>&1 | tail -12
```

Expected: `Totals: 0 passed, 5 failed`. If any slot passes under the stub, the invariant is too lenient — **stop here, revert the stub, strengthen the failing slot's assertions**, then retry.

- [ ] **Step 4:** Commit the stub as proof:

```bash
git add libs/markoff-live/src/LiveCursorState.cpp
git commit -m "$(cat <<'EOF'
markoff-live: stub — syncFromTextEdit no-op (FALSIFIABILITY PROOF, REVERTS NEXT)

Per tier-2 spec §5.4 / invariant 4. Stubbing the production
reconciliation hook should make every slot in
tst_live_render_cursor_typing_invariant fail. Verified: 5/5 fail.

Reverts in next commit.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Revert the falsifiability stub

**Files:** Revert the previous commit.

- [ ] **Step 1:** Revert:

```bash
git revert --no-edit HEAD
```

Expected: commit `Revert "markoff-live: stub — syncFromTextEdit no-op..."` lands.

- [ ] **Step 2:** Build + verify all 5 slots pass again:

```bash
cmake --build build-dev -j 8 2>&1 | tail -3
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  build-dev/bin/tst_live_render_cursor_typing_invariant 2>&1 | tail -3
```

Expected: `Totals: 5 passed, 0 failed`.

---

## Task 13: Discipline-log entry + queue update

**Files:** Modify `docs/queue.md`.

Per spec §0 / §2.2 — file the deferred-#9 thread as a discipline-log entry, and update queue #2's banner with the tier-2 landing.

- [ ] **Step 1:** Open `docs/queue.md`. Find the `## Discipline Log` section. Append a new entry at the bottom (entries are dated, append-only):

```
- 2026-05-15 `libs/markoff-live/src/LiveCursorState.cpp:459-487` — inv #2 — `tryResolvePending` bypasses `request()`'s `validateVariant` per the in-comment rationale "reject some valid transient states during a structural cascade". Those transient states are not specified anywhere in the spec/post-mortem/handoff record — tier 2 deferred concern #9 on the strength of this gap. Tier 3 should investigate and either document the transients or eliminate them.
```

- [ ] **Step 2:** Find the `## #2 — Cursor architecture cleanup` section banner (top-of-section). Append a new banner entry at the top of the existing banner stack:

```
> **2026-05-15 — Tier 2 implemented.** Spec
> `docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`;
> plan `docs/plans/2026-05-15-tier-2-cursor-typing-authority.md`.
> Concerns **#1** (docstring honesty), **#2** (cachedQtPos rename),
> **#6** (per-keystroke invariant test — 5 slots on
> `LiveRealisticInputHarness`) all closed. Falsifiability proof
> committed + reverted per invariant 4. Concern **#9** deferred to
> tier 3 with discipline-log entry naming the unspecified
> transients in `tryResolvePending`. Remaining concerns: #3, #4, #5,
> #10, #12.
```

- [ ] **Step 3:** Commit:

```bash
git add docs/queue.md
git commit -m "$(cat <<'EOF'
docs: queue — tier-2 landed; discipline-log entry for #9 deferral

Records tier-2 completion (concerns #1, #2, #6) and the deferred-#9
thread (unspecified 'valid transient states' in tryResolvePending's
validation bypass). Tier 3 picks up the investigation.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: Final regression check

**Files:** none (verification only).

- [ ] **Step 1:** Run the live-render fast suite 3× for stability:

```bash
for i in 1 2 3; do
  echo "=== Run $i ==="
  xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
    ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 \
    | grep -E 'tests failed|tests passed' | tail -1
done
```

Expected: the **same** failure count (≈9–10) all three runs, where the new `tst_live_render_cursor_typing_invariant` is **not** in the failing set.

- [ ] **Step 2:** Diff the failing-test list against the baseline:

```bash
xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" \
  ctest --test-dir build-dev -E 'realistic|benchmark' -j 4 2>&1 \
  | grep -E '\(Failed\)$' | sort > /tmp/tier2-final-failures.txt
diff /tmp/tier2-baseline-failures.txt /tmp/tier2-final-failures.txt
```

Expected: identical (no diff output), OR the new test target is *not* present in the right-hand side. Any newly-failing target → regression; stop and investigate.

- [ ] **Step 3:** Rebuild `markoff-live-app` for dogfood:

```bash
cmake --build build-dev --target markoff-live-app -j 8 2>&1 | tail -3
```

Expected: builds clean. The binary at `build-dev/bin/markoff-live-app` is ready.

- [ ] **Step 4:** Confirm no new `Qt.callLater` or re-entrance guards introduced by this tier:

```bash
git log --oneline 34115a2..HEAD -- libs/markoff-live/ \
  | awk '{print $1}' \
  | xargs -I{} git show {} -- libs/markoff-live/ \
  | grep -E 'Qt\.callLater|m_applying[A-Z]|callLater\(' | head
```

Expected: empty output.

- [ ] **Step 5:** Verify the file-touched-list matches the spec §2.1 in-scope statement (no scope creep):

```bash
git diff --stat 34115a2..HEAD | head
```

Expected: only files listed in the `Files touched` table above (plus the spec/plan/queue docs).

---

## Definition of done

- [ ] All 14 tasks complete.
- [ ] Tier-2 invariant suite green: 5/5 slots pass.
- [ ] Falsifiability stub preserved in history with its revert.
- [ ] `cachedByteOffset` grep returns zero; `cachedQtPos` grep matches the prior count.
- [ ] `LiveCursorState.h` docstring reflects dual-authority truth.
- [ ] Discipline-log entry filed for concern #9 deferral.
- [ ] No new `Qt.callLater` or re-entrance guards.
- [ ] Wider live-render suite shows no new failures vs. `/tmp/tier2-baseline-failures.txt`.
- [ ] `markoff-live-app` binary rebuilt; ready for light dogfood pass.

## Dogfood gate (light, per spec §9)

Tier 2 has no production-behavior changes, so the dogfood gate is correspondingly light:

- Open `markoff-live-app` against a familiar document.
- Type a paragraph of mixed content (ASCII, an emoji, an accented char).
- Use arrow keys, Enter, Backspace, kind transitions (`#`, `##`, `>`, `-`, `---`).
- **The widget should feel identical to start-of-tier-2.** Any perceptible change → regression; investigate.

Once dogfood signs off, tier 2's work is closed. Tier 3 (API consolidation + the deferred-#9 investigation) becomes the next executable item in `docs/queue.md`.

## Self-review checklist

- [ ] Spec §5.1 (docstring) — covered in Task 2.
- [ ] Spec §5.2 (rename) — covered in Task 3.
- [ ] Spec §5.3 (5 invariant slots) — covered in Tasks 5–10.
- [ ] Spec §5.4 (falsifiability proof) — covered in Tasks 11–12.
- [ ] Spec §2.2 deferral of #9 — covered in Task 13 (discipline-log entry).
- [ ] Spec §8 (no new `Qt.callLater` / re-entrance guards) — covered in Task 14 Step 4.
- [ ] Invariant 1 (cite developmental record before refactoring seam) — not applicable; tier 2 is documentation, not refactor.
- [ ] Invariant 2 (L4 decided in writing first) — already decided in spec §3.
- [ ] Invariant 3 (new authority retires old) — not applicable; tier 2 retires nothing (named explicitly in spec §3).
- [ ] Invariant 4 (falsifiable test) — Tasks 11–12.
- [ ] Invariant 5 (tests exercise production callsite) — `LiveRealisticInputHarness` + `QmlIntegrationFixture` per the established pattern.
- [ ] Invariant 8 (discipline log) — Task 13.
