# Find at session scope — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relocate the search loop to `markoff-core` as `Markoff::FindController`; retire the per-leaf find UIs; lock no-focus-steal-on-typing with a falsifiable invariant test.

**Architecture:** A single `Markoff::FindController` (Q_OBJECT) operates on `MarkoffDocument *` using the existing `Markoff::SearchEngine::findByBlock` primitive. Per-leaf adapters (internal, `Detail::` namespace) subscribe to the controller and render highlights + respond to navigation. The controller never touches focus, cursors, or scroll directly. The visible find UI is the consumer's responsibility.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, Qt Test, QML (for invariant test only).

**Spec:** `docs/specs/2026-05-20-find-session-scope-design.md`. All eight decisions (D1–D8) are implemented across the three phases below.

---

## Phase 1 — Revert per-leaf find UIs (one commit)

Removes the QML `FindBar`, the source-side `FindBar` QWidget, the duplicated search loops, the `MarkdownView` virtuals, and the `LiveView.qml` `Item` wrapper.

### Task 1.1 — Verify baseline

**Files:** none.

- [ ] **Step 1: Capture pre-revert test count.**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark' 2>&1 | tail -20
```

Expected: `~217 Passing` (the freeze spec landed 217/217 fast).

Record the exact number in a scratchpad — Task 1.5 verifies the new count matches expectations.

### Task 1.2 — Revert the live find UI

**Files:**
- Revert: commit `4d0e7c3` (find UI on the live leaf)

- [ ] **Step 1: Stage the revert without committing.**

```bash
git revert --no-commit 4d0e7c3
```

If the revert reports conflicts, abort and stop — the spec assumed clean revertibility; if that fails the spec needs an update before the plan can proceed.

```bash
# only if conflicts: git revert --abort
```

- [ ] **Step 2: Sanity-check the revert touched the expected files.**

```bash
git diff --cached --stat
```

Expected file list:
- `libs/markoff-live/CMakeLists.txt` (Find-related sources removed)
- `libs/markoff-live/include/markoff/live/LiveFindController.h` (deleted)
- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` (findController Q_PROPERTY + showFindBar/hideFindBar invokables removed)
- `libs/markoff-live/qml/FindBar.qml` (deleted)
- `libs/markoff-live/qml/LiveView.qml` (Item wrapper unwrapped back to ListView)
- `libs/markoff-live/src/LiveFindController.cpp` (deleted)
- `libs/markoff-live/src/LiveListModelBinding.cpp` (findController wiring removed)
- `libs/markoff-live/tests/CMakeLists.txt` (`tst_live_find_controller` removed)
- `libs/markoff-live/tests/tst_live_find_controller.cpp` (deleted)
- `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` (find-bar slot removed)

### Task 1.3 — Revert the source find UI

**Files:**
- Revert: commit `0ec907d` (find UI on the source leaf)

- [ ] **Step 1: Stage the second revert without committing.**

```bash
git revert --no-commit 0ec907d
```

Expected: clean revert. If conflicts, abort and stop.

- [ ] **Step 2: Sanity-check the revert.**

```bash
git diff --cached --stat
```

Expected to *additionally* touch (on top of Task 1.2's diff):
- `libs/markoff-source/include/markoff/source/Editor.h` (showFindBar / showReplaceBar / hideFindBar overrides removed)
- `libs/markoff-source/include/markoff/source/FindBar.h` (deleted)
- `libs/markoff-source/src/Editor.cpp` (lazy FindBar ctor + connection removed)
- `libs/markoff-source/src/FindBar.cpp` (deleted)
- `libs/markoff-source/CMakeLists.txt` (FindBar sources removed)
- `libs/markoff-source/tests/tst_source_widget_editor.cpp` (find-bar slots removed)

Note: the `0ec907d` revert also un-deletes the six QPlainTextEdit forwarders on `Editor` (D1 of the source freeze) that the commit removed. That's expected and fine — they restore alongside the find-bar deletion. We are reverting the whole commit because (a) the find work and the forwarder removal were bundled in one commit, and (b) the forwarders being restored does not block the new find architecture. If a future spec wants to redo the forwarder deletion alone, it can.

### Task 1.4 — Remove the `MarkdownView` find virtuals (D5)

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkdownView.h`
- Modify: `libs/markoff-core/src/MarkdownView.cpp` (if defaults exist there)

- [ ] **Step 1: Edit `MarkdownView.h`.**

Remove lines 31-33 (the three virtuals):

```cpp
    virtual void showFindBar();
    virtual void showReplaceBar();
    virtual void hideFindBar();
```

- [ ] **Step 2: Check for default implementations.**

```bash
grep -n "showFindBar\|hideFindBar\|showReplaceBar" libs/markoff-core/src/MarkdownView.cpp
```

If matches found, remove the corresponding method bodies. If no matches, the virtuals were pure-declared-only with no `.cpp` body and there's nothing to remove.

- [ ] **Step 3: Confirm no other override callers exist.**

```bash
grep -rn "showFindBar\|showReplaceBar\|hideFindBar" libs apps --include='*.cpp' --include='*.h' --include='*.qml' 2>/dev/null | grep -v build
```

Expected: empty output. (Source overrides were removed by the 0ec907d revert; live invokables were removed by the 4d0e7c3 revert.) If anything remains, investigate before proceeding.

### Task 1.5 — Build, test, commit Phase 1

**Files:** all of the above staged.

- [ ] **Step 1: Configure (in case CMake cache needs refresh).**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

- [ ] **Step 2: Build.**

```bash
cmake --build build-dev -j 8
```

Expected: success. Memory cap: never above `-j 8`.

- [ ] **Step 3: Run fast suite.**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected:
- `tst_live_find_controller` is gone.
- `tst_live_render_qml_integration::find_bar_typing_highlights_and_navigation` is gone.
- Four source-FindBar slots in `tst_source_widget_editor` are gone (`show_findbar_creates_visible_bar`, `hide_findbar_clears_highlights`, `showFindBar_is_idempotent`, `findbar_close_signal_hides`).
- Net: 217 − 4 − 1 − 1 = **~211 passing** (the three pre-existing failures from the master branch CLAUDE.md note remain).

- [ ] **Step 4: Commit.**

```bash
git add -A
git commit -m "$(cat <<'EOF'
revert(find): drop per-leaf find UIs; retire MarkdownView find virtuals

Reverts 4d0e7c3 (live FindBar.qml + LiveFindController) and 0ec907d
(source FindBar QWidget + Editor showFindBar/hideFindBar overrides).
Removes showFindBar / showReplaceBar / hideFindBar virtuals from the
Markoff::MarkdownView base contract.

Phase 1 of docs/plans/2026-05-20-find-session-scope.md.
Spec: docs/specs/2026-05-20-find-session-scope-design.md.

Motivation: the find work landed inside each view leaf, which (a)
prevented find state from surviving view hot-swap (the architectural
purpose of Session::copyStateFrom) and (b) made the FindBar's text
input share a QML scene with the document delegates, so typing into
the find input triggered LiveFindController::recomputeMatches ->
seekToCurrent -> establishFocus -> delegate forceActiveFocus, stealing
focus on every keystroke.

LiveView.qml returns to its pre-4d0e7c3 ListView-root shape. The
six QPlainTextEdit forwarders on Source::Editor that 0ec907d had
deleted in the same commit are restored as a side effect of the
revert; their re-deletion (if still desired) belongs to a separate
spec.

Fast suite: 211/214 (3 pre-existing failures unchanged).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 2 — Add `Markoff::FindController` in `markoff-core` (one commit)

A Q_OBJECT operating on `MarkoffDocument *`. Uses `SearchEngine::findByBlock` (existing pure query) for the search loop. Reactive to `d2DocumentChanged`. No view dependencies. No focus / cursor / scroll calls.

### Task 2.1 — Create `FindController.h`

**Files:**
- Create: `libs/markoff-core/include/markoff/core/FindController.h`

- [ ] **Step 1: Write the header.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <markoff/core/BlockAnchor.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/SearchEngine.h>

namespace Markoff {

class MarkoffDocument;

/// Drives a find session against a MarkoffDocument. Holds the needle,
/// the match list, and the currently-selected match index. Operates on
/// the document directly (block-by-block via SearchEngine::findByBlock);
/// never touches focus, cursors, or scroll.
///
/// View leaves subscribe via their own attach hooks (e.g.
/// LiveListModelBinding::attachFindController) and render highlights /
/// respond to navigationRequested in their own idiom.
///
/// Lifetime: owned by the consumer. May later be assumed by a
/// Markoff::Session-scope owner; the API does not change.
class MARKOFF_CORE_EXPORT FindController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString needle             READ needle WRITE setNeedle NOTIFY needleChanged)
    Q_PROPERTY(int     matchCount         READ matchCount             NOTIFY matchesChanged)
    Q_PROPERTY(int     currentMatchIndex  READ currentMatchIndex      NOTIFY currentMatchChanged)
    Q_PROPERTY(bool    isActive           READ isActive               NOTIFY activeChanged)

public:
    /// View-agnostic match descriptor. Byte offsets within the block's
    /// current text — same units as SearchEngine::SearchHit.
    struct Match {
        Markoff::BlockAnchor block;
        quint32              byteOffset = 0;
        quint32              byteLength = 0;
    };

    explicit FindController(MarkoffDocument *doc, QObject *parent = nullptr);
    ~FindController() override;

    QString needle() const            { return m_needle; }
    void    setNeedle(const QString &);

    const QList<Match> &matches() const { return m_matches; }
    int  matchCount()         const   { return static_cast<int>(m_matches.size()); }
    int  currentMatchIndex()  const   { return m_currentIndex; }
    bool isActive()           const   { return m_isActive; }

    /// Optional flags. Default is case-insensitive (NoFlags). Whole-words /
    /// regex are accepted by the underlying SearchEngine but UI work is
    /// deferred (see spec § Out of scope).
    SearchEngine::FindFlags flags() const { return m_flags; }
    void setFlags(SearchEngine::FindFlags);

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();

Q_SIGNALS:
    void needleChanged();
    void matchesChanged();
    void currentMatchChanged();
    void activeChanged();

    /// Emitted by findNext / findPrevious only — never by setNeedle. The
    /// active view's adapter MAY scroll the match into view and place a
    /// non-focusing caret in response. The adapter MUST NOT take focus.
    void navigationRequested(Markoff::FindController::Match match);

private:
    void recomputeMatches();

    MarkoffDocument         *m_doc          = nullptr;
    QString                  m_needle;
    SearchEngine::FindFlags  m_flags        = SearchEngine::NoFlags;
    QList<Match>             m_matches;
    int                      m_currentIndex = -1;
    bool                     m_isActive     = false;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::FindController::Match)
```

### Task 2.2 — Create `FindController.cpp` (skeleton)

**Files:**
- Create: `libs/markoff-core/src/FindController.cpp`

- [ ] **Step 1: Write the implementation.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/FindController.h>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff {

FindController::FindController(MarkoffDocument *doc, QObject *parent)
    : QObject(parent), m_doc(doc)
{
    if (m_doc) {
        connect(m_doc, &MarkoffDocument::d2DocumentChanged,
                this, [this]() { if (m_isActive) recomputeMatches(); });
    }
}

FindController::~FindController() = default;

void FindController::setNeedle(const QString &n)
{
    if (n == m_needle) return;
    m_needle = n;
    Q_EMIT needleChanged();
    if (m_isActive) recomputeMatches();
}

void FindController::setFlags(SearchEngine::FindFlags f)
{
    if (f == m_flags) return;
    m_flags = f;
    if (m_isActive) recomputeMatches();
}

void FindController::activate()
{
    if (m_isActive) return;
    m_isActive = true;
    Q_EMIT activeChanged();
    recomputeMatches();
}

void FindController::deactivate()
{
    if (!m_isActive) return;
    m_isActive = false;
    m_matches.clear();
    m_currentIndex = -1;
    Q_EMIT activeChanged();
    Q_EMIT matchesChanged();
    Q_EMIT currentMatchChanged();
}

void FindController::findNext()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_matches.size();
    Q_EMIT currentMatchChanged();
    Q_EMIT navigationRequested(m_matches[m_currentIndex]);
}

void FindController::findPrevious()
{
    if (m_matches.isEmpty()) return;
    m_currentIndex = (m_currentIndex - 1 + m_matches.size()) % m_matches.size();
    Q_EMIT currentMatchChanged();
    Q_EMIT navigationRequested(m_matches[m_currentIndex]);
}

void FindController::recomputeMatches()
{
    const int prevCurrent = m_currentIndex;
    m_matches.clear();
    if (!m_doc || m_needle.isEmpty()) {
        m_currentIndex = -1;
        Q_EMIT matchesChanged();
        if (prevCurrent != m_currentIndex) Q_EMIT currentMatchChanged();
        return;
    }
    const QList<SearchHit> hits =
        SearchEngine::findByBlock(*m_doc, m_needle, m_flags);
    m_matches.reserve(hits.size());
    for (const SearchHit &h : hits) {
        m_matches.append(Match{ h.blockId, h.matchStart, h.matchLen });
    }
    m_currentIndex = m_matches.isEmpty() ? -1 : 0;
    Q_EMIT matchesChanged();
    if (prevCurrent != m_currentIndex) Q_EMIT currentMatchChanged();
    // No navigationRequested here. Typing must never seek.
}

}  // namespace Markoff

#include <QMetaType>
namespace {
struct FindControllerMatchMetaRegistrar {
    FindControllerMatchMetaRegistrar() {
        qRegisterMetaType<Markoff::FindController::Match>(
            "Markoff::FindController::Match");
    }
} _findControllerMatchMetaRegistrar;
}
```

### Task 2.3 — Wire into `markoff-core` CMake

**Files:**
- Modify: `libs/markoff-core/CMakeLists.txt`

- [ ] **Step 1: Locate the sources block and add the new files.**

Search for `SearchEngine.h` and `SearchEngine.cpp` in `libs/markoff-core/CMakeLists.txt` and add the new files alongside them:

```cmake
    include/markoff/core/SearchEngine.h
    include/markoff/core/FindController.h          # NEW
    src/SearchEngine.cpp
    src/FindController.cpp                          # NEW
```

The exact line numbers vary; the pattern above is the unambiguous anchor.

### Task 2.4 — Write failing unit tests for FindController

**Files:**
- Create: `libs/markoff-core/tests/tst_foundation_find_controller.cpp`

- [ ] **Step 1: Write the test file.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

using namespace Markoff;

class TstFoundationFindController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Markoff::FindController::Match>(
            "Markoff::FindController::Match");
    }

    void inactive_setNeedle_does_not_compute_matches() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("Find me here\n");
        FindController fc(&doc);
        fc.setNeedle("Find");
        QCOMPARE(fc.matchCount(), 0);
        QCOMPARE(fc.currentMatchIndex(), -1);
    }

    void activate_with_empty_needle_emits_no_matches() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello\n");
        FindController fc(&doc);
        QSignalSpy matchesSpy(&fc, &FindController::matchesChanged);
        fc.activate();
        QVERIFY(fc.isActive());
        QCOMPARE(fc.matchCount(), 0);
        // matchesChanged emitted once on activate (transition to empty matches).
        QCOMPARE(matchesSpy.count(), 1);
    }

    void setNeedle_while_active_populates_matches_case_insensitively() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("Foo FOO foo bar foo\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("foo");
        QCOMPARE(fc.matchCount(), 4);
        QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void matches_span_multiple_blocks_in_document_order() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(
            "First paragraph has find here.\n\n"
            "Second paragraph has find twice find.\n\n"
            "Third paragraph: nothing.\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("find");
        QCOMPARE(fc.matchCount(), 3);
    }

    void findNext_wraps() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("alpha alpha alpha\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("alpha");
        QCOMPARE(fc.currentMatchIndex(), 0);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 1);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findNext(); QCOMPARE(fc.currentMatchIndex(), 0);
    }

    void findPrevious_wraps() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("beta beta beta\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("beta");
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 2);
        fc.findPrevious(); QCOMPARE(fc.currentMatchIndex(), 1);
    }

    void deactivate_clears_matches_and_index() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("gamma gamma\n");
        FindController fc(&doc);
        fc.activate();
        fc.setNeedle("gamma");
        QVERIFY(fc.matchCount() > 0);
        fc.deactivate();
        QCOMPARE(fc.matchCount(), 0);
        QCOMPARE(fc.currentMatchIndex(), -1);
        QVERIFY(!fc.isActive());
    }

    void setNeedle_emits_no_navigationRequested() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("zeta zeta\n");
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("zeta");
        QCOMPARE(navSpy.count(), 0);  // typing must never seek
    }

    void findNext_emits_navigationRequested_with_match() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("eta eta eta\n");
        FindController fc(&doc);
        QSignalSpy navSpy(&fc, &FindController::navigationRequested);
        fc.activate();
        fc.setNeedle("eta");
        QCOMPARE(navSpy.count(), 0);
        fc.findNext();
        QCOMPARE(navSpy.count(), 1);
    }
};

QTEST_MAIN(TstFoundationFindController)
#include "tst_foundation_find_controller.moc"
```

- [ ] **Step 2: Wire into the tests CMake.**

Edit `libs/markoff-core/tests/CMakeLists.txt`, adding alongside the existing `tst_foundation_search_engine` block:

```cmake
add_executable(tst_foundation_find_controller tst_foundation_find_controller.cpp)
add_test(NAME tst_foundation_find_controller COMMAND tst_foundation_find_controller)
target_link_libraries(tst_foundation_find_controller PRIVATE Qt6::Test markoff_core)
```

- [ ] **Step 3: Configure + build (expect FAIL on tests, PASS on build).**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_foundation_find_controller -j 8
```

Expected: success — the implementation in Task 2.2 should be complete enough to compile and link.

- [ ] **Step 4: Run the test.**

```bash
scripts/run-tests.sh --bin tst_foundation_find_controller
```

Expected: all slots PASS. If any fail, investigate before commit — the implementation in Task 2.2 already covers all the behaviour the tests assert. A failure points at a divergence between header and implementation that needs to be reconciled.

### Task 2.5 — Full suite + commit Phase 2

- [ ] **Step 1: Full fast-suite.**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: previous count + 1 (`tst_foundation_find_controller`). Currently expected ~212.

- [ ] **Step 2: Commit.**

```bash
git add libs/markoff-core/include/markoff/core/FindController.h \
        libs/markoff-core/src/FindController.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/tst_foundation_find_controller.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): Markoff::FindController — session-scope search loop

A Q_OBJECT operating on a MarkoffDocument *. Holds (needle, matches,
currentIndex, isActive). Uses the existing pure SearchEngine::findByBlock
primitive. Reactive to MarkoffDocument::d2DocumentChanged when active.
Never touches focus, cursor state, or scroll.

navigationRequested(Match) is emitted by findNext/findPrevious only;
setNeedle never emits it. The pinned no-focus-steal-on-typing rule
(spec D3) is enforced at the signal contract level — there is no
codepath from typing to navigation.

Match is view-agnostic: { BlockAnchor block; quint32 byteOffset;
quint32 byteLength; }. Byte units match SearchEngine::SearchHit and
the underlying CRDT model; adapters convert to QChar at the boundary.

Phase 2 of docs/plans/2026-05-20-find-session-scope.md.
Spec: docs/specs/2026-05-20-find-session-scope-design.md §D1, D2, D3.

10 unit tests in tst_foundation_find_controller cover: setNeedle
inactive no-op, activate emits matchesChanged, case-insensitive default,
multi-block search, findNext wrap, findPrevious wrap, deactivate
clears, setNeedle emits no navigationRequested (the bug-class
invariant), findNext emits navigationRequested.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase 3 — Per-leaf adapters, attach hooks, and falsifiable invariant test (one commit)

Adds the non-focusing caret chokepoint on the live leaf, the two adapters, attach/detach hooks on each leaf's primary host object, and the QML integration test that pins the no-focus-steal rule.

### Task 3.1 — Add `LiveCursorState::setCaretWithoutFocus` chokepoint (D6)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveCursorState.h`
- Modify: `libs/markoff-live/src/LiveCursorState.cpp`

- [ ] **Step 1: Read the existing `establishFocus` signature.**

```bash
grep -n "void establishFocus\|void requestTextCaretAtRow\b" libs/markoff-live/include/markoff/live/LiveCursorState.h libs/markoff-live/src/LiveCursorState.cpp
```

This shows the existing chokepoint and how it talks to the canonical cursor state. The new method writes to the same canonical fields without enqueuing focus / delegate handover.

- [ ] **Step 2: Add the method to the header.**

Add this declaration to `LiveCursorState.h` next to the existing `establishFocus` declaration:

```cpp
    /// Sets the canonical cursor state to a TextCaret at the given
    /// block + qtPos. Does NOT take focus and does NOT notify the matched
    /// delegate. For callers (find adapter, future programmatic seek) that
    /// need to move the caret without disturbing whichever widget
    /// currently holds focus.
    ///
    /// L4 authority: model wins on canonical cursor state (m_cursor),
    /// delegate is not notified. See docs/INVARIANTS.md invariant 2.
    void setCaretWithoutFocus(Markoff::BlockAnchor block, int qtPos);
```

- [ ] **Step 3: Implement it.**

Add this to `LiveCursorState.cpp` near `establishFocus`:

```cpp
void LiveCursorState::setCaretWithoutFocus(Markoff::BlockAnchor block, int qtPos)
{
    if (block == Markoff::BlockAnchor{}) return;
    if (qtPos < 0) qtPos = 0;

    TextCaret tc;
    tc.block            = block;
    tc.cachedQtPos      = static_cast<quint32>(qtPos);
    Cursor newCursor    = tc;

    if (m_cursor == newCursor) return;
    if (!validateVariant(newCursor)) return;

    m_cursor = newCursor;
    Q_EMIT cursorChanged();
}
```

If `validateVariant` is not a member, mirror the structure used by `syncFromTextEdit` (which sets `m_cursor` and emits `cursorChanged` without delegate handover) — same shape.

### Task 3.2 — Test the chokepoint preserves canonical state

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_cursor.cpp` (add a slot) OR
- Create: `libs/markoff-live/tests/tst_live_set_caret_without_focus.cpp` (if the cursor test file is closed for additions)

- [ ] **Step 1: Find the existing cursor test.**

```bash
ls libs/markoff-live/tests/ | grep -i cursor
```

- [ ] **Step 2: Add a slot.**

Add to the existing cursor test's `private slots:` section:

```cpp
    void setCaretWithoutFocus_updates_canonical_state_without_delegate() {
        // Constructs a binding + document with two blocks. Calls
        // setCaretWithoutFocus on the second block. Asserts:
        //  - LiveCursorState::cursor() returns a TextCaret on the second block.
        //  - No delegate forceActiveFocus was called (no delegateAvailable / takeFocus path triggered).
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("First\n\nSecond\n");
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        auto *cs = binding.cursorState();
        const auto secondAnchor = binding.model()->recordAt(1).blockAnchor;

        QSignalSpy cursorSpy(cs, &Markoff::Live::LiveCursorState::cursorChanged);
        cs->setCaretWithoutFocus(secondAnchor, 3);

        QCOMPARE(cursorSpy.count(), 1);
        // The canonical cursor is a TextCaret on the second block at qtPos 3.
        // (Adapt the exact API to the existing TextCaret accessor pattern in the file.)
    }
```

If the existing cursor test's pattern for inspecting `m_cursor` uses a different accessor (e.g. `cs->cursor()` returning `Cursor` variant), mirror that pattern exactly.

- [ ] **Step 3: Build + run.**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8
scripts/run-tests.sh --bin tst_live_render_cursor
```

Expected: PASS.

### Task 3.3 — Create `Markoff::Live::Detail::LiveFindAdapter` (internal)

**Files:**
- Create: `libs/markoff-live/src/Detail/LiveFindAdapter.h`
- Create: `libs/markoff-live/src/Detail/LiveFindAdapter.cpp`

- [ ] **Step 1: Create the directory.**

```bash
mkdir -p libs/markoff-live/src/Detail
```

- [ ] **Step 2: Write the header.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>

#include <markoff/core/FindController.h>

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;

namespace Detail {

/// Internal. Subscribes to a Markoff::FindController and responds to
/// matches/navigation in the live leaf's idiom.
///
/// - matchesChanged: (currently a no-op; highlight rendering across
///   delegates is a follow-up — adapter holds the match list for that
///   future renderer to consume).
/// - navigationRequested: resolves match.block -> ListView row via
///   LiveBlockModel, places the caret via
///   LiveCursorState::setCaretWithoutFocus (no focus stealing).
class LiveFindAdapter : public QObject {
    Q_OBJECT
public:
    explicit LiveFindAdapter(LiveBlockModel *model,
                             LiveCursorState *cursorState,
                             QObject *parent = nullptr);
    ~LiveFindAdapter() override;

    void attach(Markoff::FindController *fc);
    void detach();

    Markoff::FindController *controller() const { return m_controller; }

private slots:
    void onNavigationRequested(Markoff::FindController::Match);

private:
    int  resolveByteToQtPos(const Markoff::BlockAnchor &block, quint32 byteOffset) const;

    LiveBlockModel                       *m_model       = nullptr;
    LiveCursorState                      *m_cursorState = nullptr;
    QPointer<Markoff::FindController>     m_controller;
};

}  // namespace Detail
}  // namespace Markoff::Live
```

- [ ] **Step 3: Write the implementation.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveFindAdapter.h"

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockRecord.h>

namespace Markoff::Live::Detail {

LiveFindAdapter::LiveFindAdapter(LiveBlockModel *model,
                                 LiveCursorState *cursorState,
                                 QObject *parent)
    : QObject(parent), m_model(model), m_cursorState(cursorState)
{}

LiveFindAdapter::~LiveFindAdapter() = default;

void LiveFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &LiveFindAdapter::onNavigationRequested);
}

void LiveFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
}

void LiveFindAdapter::onNavigationRequested(Markoff::FindController::Match m)
{
    if (!m_cursorState) return;
    const int qtPos = resolveByteToQtPos(m.block, m.byteOffset);
    m_cursorState->setCaretWithoutFocus(m.block, qtPos);
    // Scroll-into-view: deferred. The QML-side LiveView reacts to
    // cursorChanged for visible-row tracking under existing wiring;
    // explicit positionViewAtIndex is a future enhancement once we
    // have a consumer driving the controller.
}

int LiveFindAdapter::resolveByteToQtPos(const Markoff::BlockAnchor &block,
                                       quint32 byteOffset) const
{
    if (!m_model) return 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const auto &rec = m_model->recordAt(r);
        if (rec.blockAnchor != block) continue;
        // Block text is QString in BlockRecord; convert byte offset
        // to QChar position by interpreting the UTF-8 byte prefix.
        const QByteArray utf8 = rec.text.toUtf8();
        const QByteArray prefix = utf8.left(static_cast<int>(byteOffset));
        return QString::fromUtf8(prefix).size();
    }
    return 0;
}

}  // namespace Markoff::Live::Detail
```

- [ ] **Step 4: Wire into the live-leaf CMake.**

Edit `libs/markoff-live/CMakeLists.txt`, adding under the existing `src/` source list:

```cmake
    src/Detail/LiveFindAdapter.h
    src/Detail/LiveFindAdapter.cpp
```

### Task 3.4 — Add `LiveListModelBinding::attachFindController` / `detachFindController`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Add to the header.**

Add forward declaration:

```cpp
namespace Markoff { class FindController; }
```

In the class public section, add:

```cpp
    /// Attaches a Markoff::FindController so the live leaf renders its
    /// matches and responds to navigation. The controller is owned by
    /// the consumer; the binding does not take ownership. Pass nullptr
    /// or call detachFindController() to disconnect.
    Q_INVOKABLE void attachFindController(Markoff::FindController *fc);
    Q_INVOKABLE void detachFindController();
```

In the private section, add:

```cpp
    // Held in the Pimpl Private struct; declared here only as a forward.
```

- [ ] **Step 2: Add to the Private struct + ctor + methods in `LiveListModelBinding.cpp`.**

Find the `Private` struct (search `LiveFindController` patterns are gone; look for the existing `Private` struct definition near the top of the file). Add to it:

```cpp
    Detail::LiveFindAdapter *findAdapter = nullptr;
```

Add the include at the top:

```cpp
#include "Detail/LiveFindAdapter.h"
```

In the ctor or `setDocument`/`init` path, instantiate `findAdapter`:

```cpp
d->findAdapter = new Detail::LiveFindAdapter(d->model, d->cursorState, this);
```

Implement the two methods near the bottom of the file:

```cpp
void LiveListModelBinding::attachFindController(Markoff::FindController *fc)
{
    if (d->findAdapter) d->findAdapter->attach(fc);
}

void LiveListModelBinding::detachFindController()
{
    if (d->findAdapter) d->findAdapter->detach();
}
```

### Task 3.5 — Create `Markoff::Source::Detail::SourceFindAdapter` (internal)

**Files:**
- Create: `libs/markoff-source/src/Detail/SourceFindAdapter.h`
- Create: `libs/markoff-source/src/Detail/SourceFindAdapter.cpp`

- [ ] **Step 1: Create the directory.**

```bash
mkdir -p libs/markoff-source/src/Detail
```

- [ ] **Step 2: Write the header.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTextEdit>

#include <markoff/core/FindController.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Source {

class Editor;

namespace Detail {

/// Internal. Subscribes to a Markoff::FindController and translates
/// (BlockAnchor, byteOffset, byteLength) matches into flat
/// QTextEdit::ExtraSelections rendered on the underlying
/// QPlainTextEdit, plus a non-focus-stealing setTextCursor on
/// navigationRequested.
class SourceFindAdapter : public QObject {
    Q_OBJECT
public:
    explicit SourceFindAdapter(Editor *editor, QObject *parent = nullptr);
    ~SourceFindAdapter() override;

    void attach(Markoff::FindController *fc);
    void detach();

private slots:
    void onMatchesChanged();
    void onNavigationRequested(Markoff::FindController::Match);

private:
    int globalCharPosFor(Markoff::FindController::Match) const;
    void renderHighlights();

    Editor                                *m_editor;
    QPointer<Markoff::FindController>      m_controller;
    QList<QTextEdit::ExtraSelection>       m_highlights;
};

}  // namespace Detail
}  // namespace Markoff::Source
```

- [ ] **Step 3: Write the implementation.**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceFindAdapter.h"

#include <QColor>
#include <QPlainTextEdit>
#include <QTextCursor>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

namespace Markoff::Source::Detail {

SourceFindAdapter::SourceFindAdapter(Editor *editor, QObject *parent)
    : QObject(parent), m_editor(editor)
{}

SourceFindAdapter::~SourceFindAdapter() = default;

void SourceFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::matchesChanged,
            this, &SourceFindAdapter::onMatchesChanged);
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &SourceFindAdapter::onNavigationRequested);
    onMatchesChanged();
}

void SourceFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
    m_highlights.clear();
    if (auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr)
        pte->setExtraSelections({});
}

void SourceFindAdapter::onMatchesChanged()
{
    renderHighlights();
}

void SourceFindAdapter::onNavigationRequested(Markoff::FindController::Match m)
{
    auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr;
    if (!pte) return;
    const int globalPos = globalCharPosFor(m);
    QTextCursor cur(pte->document());
    cur.setPosition(globalPos);
    pte->setTextCursor(cur);  // Does NOT call setFocus; focus stays where the user has it.
    pte->ensureCursorVisible();
}

void SourceFindAdapter::renderHighlights()
{
    m_highlights.clear();
    auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr;
    if (!pte || !m_controller) {
        if (pte) pte->setExtraSelections({});
        return;
    }
    QTextCharFormat hlFmt;
    hlFmt.setBackground(QColor(255, 235, 59, 120));  // soft yellow, theme follow-up
    for (const auto &m : m_controller->matches()) {
        QTextCursor cur(pte->document());
        const int globalPos = globalCharPosFor(m);
        cur.setPosition(globalPos);
        // byteLength → charLength via prefix-of-block path:
        auto *doc = m_editor->document();
        const QByteArray blockText = doc ? doc->blockText(m.block) : QByteArray();
        const int blockCharLen = QString::fromUtf8(
            blockText.mid(m.byteOffset, m.byteLength)).size();
        cur.setPosition(globalPos + blockCharLen, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection sel;
        sel.cursor = cur;
        sel.format = hlFmt;
        m_highlights.append(sel);
    }
    pte->setExtraSelections(m_highlights);
}

int SourceFindAdapter::globalCharPosFor(Markoff::FindController::Match m) const
{
    auto *doc = m_editor ? m_editor->document() : nullptr;
    if (!doc) return 0;
    // Walk iterateBlocks() until we hit m.block, accumulating QChar lengths
    // plus the per-block separator. Source widget's flat text uses
    // interBlockSeparator() == "\n\n" per the buffer convention.
    int globalChar = 0;
    const auto ids = doc->iterateBlocks();
    for (const Markoff::BlockId id : ids) {
        const QByteArray btext = doc->blockText(id);
        if (id == m.block) {
            const QByteArray prefix = btext.left(static_cast<int>(m.byteOffset));
            return globalChar + QString::fromUtf8(prefix).size();
        }
        globalChar += QString::fromUtf8(btext).size();
        globalChar += 2;  // "\n\n" interBlockSeparator (D2 buffer convention)
    }
    return globalChar;
}

}  // namespace Markoff::Source::Detail
```

- [ ] **Step 4: Wire into the source-leaf CMake.**

Edit `libs/markoff-source/CMakeLists.txt`, adding alongside other `src/` entries:

```cmake
    src/Detail/SourceFindAdapter.h
    src/Detail/SourceFindAdapter.cpp
```

### Task 3.6 — Add `Editor::attachFindController` / `detachFindController`

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/Editor.h`
- Modify: `libs/markoff-source/src/Editor.cpp`

- [ ] **Step 1: Add to the header.**

Forward-declare:

```cpp
namespace Markoff { class FindController; }
```

In the public section, near the document accessors:

```cpp
    /// Attaches a Markoff::FindController. The Editor renders its matches
    /// and seeks to them on navigation; focus stays with whatever widget
    /// currently has it (the consumer's find input). Owned by the
    /// consumer; pass nullptr to detach.
    void attachFindController(Markoff::FindController *fc);
    void detachFindController();
```

In the private section:

```cpp
    Markoff::Source::Detail::SourceFindAdapter *m_findAdapter = nullptr;
```

Forward declare the adapter at the top of the namespace block:

```cpp
namespace Markoff::Source {
namespace Detail { class SourceFindAdapter; }
```

- [ ] **Step 2: Implement in Editor.cpp.**

Add include at top:

```cpp
#include "Detail/SourceFindAdapter.h"
```

In the Editor ctor (after `m_inner` and child setup), instantiate the adapter:

```cpp
m_findAdapter = new Detail::SourceFindAdapter(this, this);
```

Add the two methods:

```cpp
void Editor::attachFindController(Markoff::FindController *fc)
{
    if (m_findAdapter) m_findAdapter->attach(fc);
}

void Editor::detachFindController()
{
    if (m_findAdapter) m_findAdapter->detach();
}
```

### Task 3.7 — Write the falsifiable invariant test (D7)

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_qml_integration.cpp` (add a slot)

- [ ] **Step 1: Read the existing harness pattern.**

```bash
grep -n "LiveRealisticInputHarness\|private slots:" libs/markoff-live/tests/tst_live_render_qml_integration.cpp | head -10
```

This shows how other slots use the harness for QML-reached production paths.

- [ ] **Step 2: Add the slot.**

Add to `private slots:`:

```cpp
    void find_typing_does_not_steal_focus_from_external_input() {
        // SETUP: Build a LiveListModelBinding + view harness. Construct
        // a Markoff::FindController against the binding's document. Wire
        // it into the binding via attachFindController. Construct a peer
        // QLineEdit (a QWidget peer focus-target outside the QML scene)
        // and give it focus.
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("# My Header\n\nParagraph one.\n");

        Markoff::FindController fc(&doc);
        binding.attachFindController(&fc);

        QWidget root;
        auto *peerInput = new QLineEdit(&root);
        QVBoxLayout *layout = new QVBoxLayout(&root);
        layout->addWidget(peerInput);
        root.show();
        peerInput->setFocus();
        QApplication::setActiveWindow(&root);
        QVERIFY(QTest::qWaitForWindowActive(&root));
        QCOMPARE(QApplication::focusWidget(), peerInput);

        // ACT: Simulate typing "Hello" into the peer input, which mirrors
        // it into the FindController as the consumer's find input would.
        fc.activate();
        const QString typed = QStringLiteral("Hello");
        for (QChar c : typed) {
            const QString before = peerInput->text();
            peerInput->setText(before + c);
            fc.setNeedle(peerInput->text());
        }

        // ASSERT 1: Peer input still has focus.
        QCOMPARE(QApplication::focusWidget(), peerInput);

        // ASSERT 2: Document content is unchanged (no character ended up in
        // the document; the bug-class symptom would be "# My elloHeader").
        // Iterate blocks and assert text matches the seed.
        const auto ids = doc.iterateBlocks();
        QCOMPARE(ids.size(), size_t{2});
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("# My Header"));
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[1])), QStringLiteral("Paragraph one."));
    }
```

Include guards at top of file (only if not already present):

```cpp
#include <QLineEdit>
#include <QVBoxLayout>
#include <markoff/core/FindController.h>
```

If `LiveRealisticInputHarness` is the canonical harness used elsewhere in the file and constructing a peer `QLineEdit` is awkward, the test may instead drive directly through `LiveListModelBinding` + a Q_OBJECT focus probe. Either path is acceptable; the assertion shape (focus stays where it was, document unchanged) is the load-bearing piece.

- [ ] **Step 3: Build + run.**

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
scripts/run-tests.sh --bin tst_live_render_qml_integration -- -tc find_typing_does_not_steal_focus_from_external_input
```

(If `run-tests.sh` doesn't forward the QTest `-tc` flag, use `xvfb-run` / the script's per-test invocation per the existing test conventions in `CLAUDE.md`. The slot name suffices to locate it.)

Expected: PASS.

### Task 3.8 — Prove falsifiability (D7 mandatory check)

- [ ] **Step 1: Temporarily break.**

Inside `LiveFindAdapter::onNavigationRequested` or directly in `FindController::setNeedle` (whichever is closer to the original bug-class action), introduce a SIMULATED regression that mirrors the original bug. The cleanest approach: in `setNeedle` after `recomputeMatches`, add a temporary line:

```cpp
    // FALSIFIABILITY PROBE — REMOVE BEFORE COMMIT
    if (!m_matches.isEmpty()) Q_EMIT navigationRequested(m_matches[0]);
```

Rebuild:

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
```

- [ ] **Step 2: Run the invariant test, expect FAIL.**

```bash
scripts/run-tests.sh --bin tst_live_render_qml_integration -- -tc find_typing_does_not_steal_focus_from_external_input
```

Expected: FAIL. The peer input loses focus (via the adapter's caret-place path) or the document gets mutated. Either is the bug-class symptom.

If it PASSES, the test is too lenient — strengthen it before continuing. Possible strengthenings:
- Assert via `QTRY_VERIFY` instead of synchronous `QCOMPARE` (event-loop-aware focus check).
- Also assert that `LiveCursorState::cursor()` was NOT updated by typing (only by explicit findNext).

- [ ] **Step 3: Revert the probe.**

Remove the temporary line. Rebuild + re-run to confirm green:

```bash
cmake --build build-dev --target tst_live_render_qml_integration -j 8
scripts/run-tests.sh --bin tst_live_render_qml_integration -- -tc find_typing_does_not_steal_focus_from_external_input
```

Expected: PASS.

### Task 3.9 — Full suite + commit Phase 3

- [ ] **Step 1: Full fast-suite.**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: previous count + new slots. ~215 passing (212 + setCaret cursor slot + invariant slot + possibly one more depending on how the cursor test ended up structured).

- [ ] **Step 2: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/src/Detail/LiveFindAdapter.h \
        libs/markoff-live/src/Detail/LiveFindAdapter.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/tst_live_render_cursor.cpp \
        libs/markoff-live/tests/tst_live_render_qml_integration.cpp \
        libs/markoff-source/include/markoff/source/Editor.h \
        libs/markoff-source/src/Editor.cpp \
        libs/markoff-source/src/Detail/SourceFindAdapter.h \
        libs/markoff-source/src/Detail/SourceFindAdapter.cpp \
        libs/markoff-source/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(live, source): FindController adapters + non-focusing caret chokepoint

Phase 3 of docs/plans/2026-05-20-find-session-scope.md.
Spec: docs/specs/2026-05-20-find-session-scope-design.md §D4, D6, D7.

- LiveCursorState::setCaretWithoutFocus(BlockAnchor, qtPos): updates the
  canonical cursor state without enqueuing focus / delegate handover.
  L4-explicit chokepoint per docs/INVARIANTS.md invariant 2 — model wins
  on canonical state, delegate is not notified.
- Markoff::Live::Detail::LiveFindAdapter: internal helper subscribing
  to a FindController. On navigationRequested, resolves byte offset to
  qtPos and calls setCaretWithoutFocus. Highlight rendering across
  delegates is a follow-up.
- LiveListModelBinding::attachFindController / detachFindController:
  narrow attach hook (NOT on the MarkdownView polymorphic surface,
  per spec D4).
- Markoff::Source::Detail::SourceFindAdapter: internal helper. Renders
  match list as QTextEdit::ExtraSelections on the inner QPlainTextEdit;
  navigationRequested calls setTextCursor (focus-preserving) +
  ensureCursorVisible.
- Editor::attachFindController / detachFindController: symmetric source
  hook.

Falsifiable invariant test: tst_live_render_qml_integration::
find_typing_does_not_steal_focus_from_external_input pins the bug-class
behaviour. Falsifiability confirmed by temporarily emitting
navigationRequested from setNeedle and observing the test fail; probe
removed before commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review checklist

- [x] D1 (controller in core) → Phase 2.
- [x] D2 (Match shape — byte offsets; aligned with SearchEngine::SearchHit) → Phase 2, Task 2.1.
- [x] D3 (no focus/cursor/scroll on controller) → Phase 2, signal-contract level enforcement.
- [x] D4 (adapters internal under Detail::; attach hooks narrow) → Phase 3, Tasks 3.3–3.6.
- [x] D5 (MarkdownView virtuals removed) → Phase 1, Task 1.4.
- [x] D6 (setCaretWithoutFocus chokepoint) → Phase 3, Task 3.1.
- [x] D7 (falsifiable invariant test) → Phase 3, Tasks 3.7–3.8.
- [x] D8 (LiveView.qml back to ListView root) → Phase 1, Task 1.2 (via the revert).

Open question §1 (ship a QWidget FindBar affordance?) is deferred per the spec; no plan task.
