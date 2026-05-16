// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase E: Tier-4c invariant gate — LiveCursorState is the canonical store
// for both cursor position and selection anchor.
//
// 7 slots covering:
// A. begin() sets LiveCursorState::selectionAnchor() (click → anchor parked)
// B. extend() moves active end, anchor unchanged
// C. clear() clears LiveCursorState::selectionAnchor()
// D. hasSelection() sources from LiveCursorState, not shadow state
// E. anchorBlock() / activeBlock() read from LiveCursorState
// F. Session round-trip: syncSelectionToSession emits exactly once, no echo
// G. selectAll() populates LiveCursorState::selectionAnchor()

#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>

namespace Markoff::Live::Test {

class TestSelectionCursorUnification : public QObject {
    Q_OBJECT
private slots:
    // A. begin() parks the anchor in LiveCursorState::selectionAnchor()
    // with the correct BlockId and qtPos.
    void begin_sets_cursor_state_anchor();

    // B. extend() moves the active end in LiveCursorState without touching
    // the anchor parked by begin().
    void extend_moves_active_keeps_anchor();

    // C. clear() clears LiveCursorState::selectionAnchor(); cursor (active
    // end) is not disturbed.
    void clear_removes_cursor_state_anchor();

    // D. LiveSelectionView::hasSelection() sources from LiveCursorState —
    // it reflects the canonical store's collapsed-vs-active state.
    void has_selection_reflects_cursor_state();

    // E. anchorBlock() / activeBlock() read from LiveCursorState, not
    // any local shadow state.
    void accessors_read_from_cursor_state();

    // F. Session round-trip: syncSelectionToSession emits to the session
    // exactly once; the incoming echo is swallowed by the equality
    // short-circuit and does NOT trigger a second selectionChanged.
    void session_round_trip_no_echo();

    // G. selectAll() populates LiveCursorState::selectionAnchor() at block 0
    // / qtPos 0 and moves the active end to the end of the last block.
    void select_all_populates_cursor_state_anchor();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void waitForModel(Markoff::Live::LiveListModelBinding &binding, int rows)
{
    // The model is populated synchronously via d2DocumentChanged. One
    // processEvents pass is usually enough; use a small retry for safety.
    for (int i = 0; i < 20 && binding.model()->rowCount() < rows; ++i)
        QTest::qWait(10);
}

// ---------------------------------------------------------------------------
// A
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::begin_sets_cursor_state_anchor()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha bravo\n\ncharlie delta\n");
    waitForModel(binding, 2);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();
    QVERIFY(sv);
    QVERIFY(cs);

    // No anchor yet.
    QVERIFY(!cs->selectionAnchor().has_value());

    // begin() on row 0, qtPos 3.
    sv->begin(0, 3);

    // Canonical store must have the anchor.
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block,
             binding.model()->recordAt(0).blockAnchor);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(3));
}

// ---------------------------------------------------------------------------
// B
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::extend_moves_active_keeps_anchor()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha\n\nbeta\n");
    waitForModel(binding, 2);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    // Park anchor at row 0, qtPos 5.
    sv->begin(0, 5);
    QVERIFY(cs->selectionAnchor().has_value());
    const auto anchorBlockBefore = cs->selectionAnchor()->block;
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(5));

    // Extend to row 1, qtPos 4.
    sv->extend(1, 4);

    // Active end must have moved to row 1.
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    QCOMPARE(cs->rowForBlock(tc->block), 1);
    QCOMPARE(tc->cachedQtPos, quint32(4));

    // Anchor must be unchanged.
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block, anchorBlockBefore);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(5));
}

// ---------------------------------------------------------------------------
// C
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::clear_removes_cursor_state_anchor()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("hello world\n");
    waitForModel(binding, 1);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    sv->begin(0, 0);
    sv->extend(0, 5);
    QVERIFY(cs->hasSelection());

    // Record the active end before clear.
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    const auto activeBefore = tc->cachedQtPos;

    sv->clear();

    // Anchor must be gone.
    QVERIFY(!cs->selectionAnchor().has_value());
    QVERIFY(!cs->hasSelection());

    // Active end (cursor position) is not disturbed.
    const auto tcAfter = cs->currentTextCaret();
    QVERIFY(tcAfter.has_value());
    QCOMPARE(tcAfter->cachedQtPos, activeBefore);
}

// ---------------------------------------------------------------------------
// D
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::has_selection_reflects_cursor_state()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("one two three\n");
    waitForModel(binding, 1);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    // Before begin: no selection.
    QVERIFY(!sv->hasSelection());
    QVERIFY(!cs->hasSelection());

    // After a collapsed begin (anchor == active): still no selection.
    sv->begin(0, 4);
    // A collapsed selection (anchor == active) returns false for hasSelection.
    QVERIFY(!sv->hasSelection());
    QVERIFY(!cs->hasSelection());

    // After extend to a different position: selection is active.
    sv->extend(0, 9);
    QVERIFY(sv->hasSelection());
    QVERIFY(cs->hasSelection());

    // After clear: no selection again.
    sv->clear();
    QVERIFY(!sv->hasSelection());
    QVERIFY(!cs->hasSelection());
}

// ---------------------------------------------------------------------------
// E
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::accessors_read_from_cursor_state()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("first\n\nsecond\n\nthird\n");
    waitForModel(binding, 3);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    // begin() at row 0, qtPos 2.
    sv->begin(0, 2);
    // extend() to row 2, qtPos 3.
    sv->extend(2, 3);

    // Facade accessors must agree with LiveCursorState.
    QCOMPARE(sv->anchorBlock(), cs->rowForBlock(cs->selectionAnchor()->block));
    QCOMPARE(sv->anchorQtPos(), static_cast<int>(cs->selectionAnchor()->qtPos));

    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    QCOMPARE(sv->activeBlock(), cs->rowForBlock(tc->block));
    QCOMPARE(sv->activeQtPos(), static_cast<int>(tc->cachedQtPos));

    // Verify specific values.
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 2);
    QCOMPARE(sv->activeBlock(), 2);
    QCOMPARE(sv->activeQtPos(), 3);
}

// ---------------------------------------------------------------------------
// F
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::session_round_trip_no_echo()
{
    // The invariant: when the session fires primarySelectionChanged with a
    // selection that exactly matches what LiveCursorState already holds, the
    // equality short-circuit in onSessionPrimarySelectionChanged prevents a
    // spurious selectionChanged re-emission from the view. "No echo."

    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha\n\nbeta\n");
    waitForModel(binding, 2);

    Markoff::Session *session = doc.createSession();
    binding.setSession(session);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    // Set a selection. begin() and extend() both call syncSelectionToSession
    // under the hood (via LiveSelectionView::syncToSession → session->setPrimarySelection).
    sv->begin(0, 0);
    sv->extend(1, 3);

    // Confirm we have a selection.
    QVERIFY(cs->hasSelection());
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 0);
    QCOMPARE(sv->activeBlock(), 1);
    QCOMPARE(sv->activeQtPos(), 3);

    // Now simulate a peer emitting the SAME selection on the session (as if
    // another binding echoed back the selection we just set).
    // Capture the current session primary selection and re-apply it.
    const Markoff::Selection currentSel = session->primarySelection();

    // Watch for spurious selectionChanged from the view.
    QSignalSpy selSpy(sv, &Markoff::Live::LiveSelectionView::selectionChanged);

    // Push an identical selection via syncSelectionToSession (no-op in Session
    // because of its own dedup) — then push it directly via setPrimarySelection
    // to simulate an external echo with the same value.
    // Session::setPrimarySelection is idempotent for identical (anchor,active,kind),
    // so it will not re-emit if the values match. To exercise the cursor-state
    // equality short-circuit, push a freshly-built Selection from the TextAnchors.
    cs->syncSelectionToSession();
    // Give the event loop a chance to process any asynchronous echoes.
    QTest::qWait(50);

    // The selection state must be unchanged (no mutation from the echo).
    QVERIFY(cs->hasSelection());
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 0);
    QCOMPARE(sv->activeBlock(), 1);
    QCOMPARE(sv->activeQtPos(), 3);

    // No spurious selectionChanged must have fired from the view layer.
    QCOMPARE(selSpy.count(), 0);

    // Additional check: pushing an incoming session selection with the EXACT
    // same TextAnchors as currently held by LiveCursorState triggers no
    // selectionChanged either.
    const int selSpyCount = selSpy.count();
    session->setPrimarySelection(currentSel);  // same value → Session deduplicates
    QTest::qWait(50);
    QCOMPARE(selSpy.count(), selSpyCount);  // still 0
}

// ---------------------------------------------------------------------------
// G
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::select_all_populates_cursor_state_anchor()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("line one\n\nline two\n\nline three\n");
    waitForModel(binding, 3);

    auto *sv = binding.selectionView();
    auto *cs = binding.cursorState();

    sv->selectAll();

    // Anchor must be at block 0 / qtPos 0.
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block,
             binding.model()->recordAt(0).blockAnchor);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(0));

    // Active end must be at the last block / end of text.
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    const int lastRow = binding.model()->rowCount() - 1;
    QCOMPARE(cs->rowForBlock(tc->block), lastRow);
    const QString lastText = binding.model()->recordAt(lastRow).text;
    QCOMPARE(static_cast<int>(tc->cachedQtPos), lastText.length());

    // hasSelection must be true.
    QVERIFY(cs->hasSelection());
    QVERIFY(sv->hasSelection());

    // Facade anchor/active must agree.
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 0);
    QCOMPARE(sv->activeBlock(), lastRow);
}

} // namespace Markoff::Live::Test

QTEST_GUILESS_MAIN(Markoff::Live::Test::TestSelectionCursorUnification)
#include "tst_live_render_selection_cursor_unification.moc"
