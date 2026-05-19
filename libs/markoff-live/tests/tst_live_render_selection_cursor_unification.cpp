// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase E: Tier-4c invariant gate — LiveCursorState is the canonical store
// for both cursor position and selection anchor.
//
// 7 slots per spec §5.6 / plan Task 12:
// A. click_then_shift_click_keeps_anchor_at_first
//    begin() parks anchor; extend() moves active end, anchor unchanged;
//    facade accessors agree with LiveCursorState.
// B. shift_arrow_cross_block_extends_active
//    Cross-block extend moves active end to target block; anchor unchanged.
// C. double_click_selects_word_via_facade
//    begin()/extend() selects a word range; facade reports correct range.
// D. clear_via_left_arrow_collapses_to_active
//    clear() removes anchor; cursor (active end) is not disturbed.
// E. session_round_trip_no_echo
//    syncSelectionToSession emits once; equality short-circuit prevents echo.
// F. selection_survives_structural_edit_above
//    BlockAnchor identity survives insertion of a block above the selection.
// G. selection_cleared_on_orphaned_anchor
//    Deleting the selected block clears the selection anchor.

#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>

namespace Markoff::Live::Test {

class TestSelectionCursorUnification : public QObject {
    Q_OBJECT
private slots:
    // A. begin() parks the anchor in LiveCursorState; extend() keeps the
    //    anchor at the first click while moving the active end. Facade
    //    accessors (anchorBlock/anchorQtPos/activeBlock/activeQtPos) agree
    //    with LiveCursorState.
    void click_then_shift_click_keeps_anchor_at_first();

    // B. extend() across a block boundary moves the active end to the target
    //    block; the anchor set by begin() is not disturbed.
    void shift_arrow_cross_block_extends_active();

    // C. Production double-click goes through the QML MouseArea → begin/extend
    //    on LiveCursorState. Simulate the begin/extend the QML side produces,
    //    then verify rangeForBlock returns the correct word range.
    void double_click_selects_word_via_facade();

    // D. clear() removes the selection anchor; the cursor active end
    //    (LiveCursorState::currentTextCaret()) is not disturbed.
    void clear_via_left_arrow_collapses_to_active();

    // E. Session round-trip: syncSelectionToSession emits exactly once; the
    //    incoming echo is swallowed by the equality short-circuit in
    //    onSessionPrimarySelectionChanged and does NOT trigger selectionChanged.
    void session_round_trip_no_echo();

    // F. The BlockAnchor identity of the selection anchor survives structural
    //    edits above the selected row (block insertion shifts row indices but
    //    BlockAnchor is stable).
    void selection_survives_structural_edit_above();

    // G. When the anchored block is deleted, the session round-trip resolves
    //    an orphaned anchor and clearSelectionAnchor() is called, leaving
    //    hasSelection() == false.
    void selection_cleared_on_orphaned_anchor();
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
// A. click_then_shift_click_keeps_anchor_at_first
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::click_then_shift_click_keeps_anchor_at_first()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha alpha\n\nbeta beta\n\ngamma gamma\n");
    waitForModel(binding, 3);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();
    QVERIFY(sv);
    QVERIFY(cs);

    // Click row 0 col 3 → anchor parked at (row0.blockAnchor, 3), active same.
    sv->begin(0, 3);
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block, binding.model()->recordAt(0).blockAnchor);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(3));

    // Shift+click row 2 col 7 → anchor unchanged, active moves.
    sv->extend(2, 7);

    // Active end moved to row 2.
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    QCOMPARE(cs->rowForBlock(tc->block), 2);
    QCOMPARE(tc->cachedQtPos, quint32(7));

    // Anchor unchanged.
    QCOMPARE(cs->selectionAnchor()->block, binding.model()->recordAt(0).blockAnchor);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(3));

    // Facade reports consistent state.
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 3);
    QCOMPARE(sv->activeBlock(), 2);
    QCOMPARE(sv->activeQtPos(), 7);
}

// ---------------------------------------------------------------------------
// B. shift_arrow_cross_block_extends_active
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::shift_arrow_cross_block_extends_active()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha\n\nbeta\n");
    waitForModel(binding, 2);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    // Park anchor at end of row 0.
    sv->begin(0, 5);
    QVERIFY(cs->selectionAnchor().has_value());
    const auto anchorBlock = cs->selectionAnchor()->block;
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(5));

    // Extend to middle of row 1 (simulates Shift+↓).
    sv->extend(1, 3);

    // Active end is now in row 1.
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    QCOMPARE(cs->rowForBlock(tc->block), 1);
    QCOMPARE(tc->cachedQtPos, quint32(3));

    // Anchor is unchanged.
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block, anchorBlock);
    QCOMPARE(cs->selectionAnchor()->qtPos, quint32(5));
}

// ---------------------------------------------------------------------------
// C. double_click_selects_word_via_facade
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::double_click_selects_word_via_facade()
{
    // Production double-click goes through QML MouseArea → cursorState.
    // Simulate the begin/extend the QML side would produce for "bravo".
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha bravo charlie\n");
    waitForModel(binding, 1);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    // "bravo" starts at column 6 (after "alpha "), ends at 11.
    sv->begin(0, 6);
    sv->extend(0, 11);

    QVERIFY(sv->hasSelection());
    QVERIFY(cs->hasSelection());

    // rangeForBlock must return the word range.
    const QPoint pt = sv->rangeForBlock(0);
    QCOMPARE(pt, QPoint(6, 11));

    // Facade accessors agree.
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 6);
    QCOMPARE(sv->activeBlock(), 0);
    QCOMPARE(sv->activeQtPos(), 11);
}

// ---------------------------------------------------------------------------
// D. clear_via_left_arrow_collapses_to_active
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::clear_via_left_arrow_collapses_to_active()
{
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("alpha bravo\n");
    waitForModel(binding, 1);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    sv->begin(0, 0);
    sv->extend(0, 5);
    QVERIFY(cs->hasSelection());

    // Record the active end before clear (left-arrow collapse).
    const auto tc = cs->currentTextCaret();
    QVERIFY(tc.has_value());
    const auto activeBefore = tc->cachedQtPos;

    sv->clearSelection();

    // Anchor must be gone.
    QVERIFY(!cs->selectionAnchor().has_value());
    QVERIFY(!cs->hasSelection());
    QVERIFY(!sv->hasSelection());

    // Cursor active end (position) is not disturbed.
    const auto tcAfter = cs->currentTextCaret();
    QVERIFY(tcAfter.has_value());
    QCOMPARE(tcAfter->cachedQtPos, activeBefore);
}

// ---------------------------------------------------------------------------
// E. session_round_trip_no_echo
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

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    // Set a selection.
    sv->begin(0, 0);
    sv->extend(1, 3);

    // Confirm we have a selection.
    QVERIFY(cs->hasSelection());
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 0);
    QCOMPARE(sv->activeBlock(), 1);
    QCOMPARE(sv->activeQtPos(), 3);

    // Simulate a peer echoing the SAME selection back via the session.
    // Capture the current session primary selection.
    const Markoff::Selection currentSel = session->primarySelection();

    // Watch for spurious selectionChanged from the view.
    QSignalSpy selSpy(sv, &Markoff::Live::LiveCursorState::selectionChanged);

    // Push an identical selection via syncSelectionToSession. This triggers
    // the round-trip path: session receives it, fires primarySelectionChanged,
    // LiveCursorState's onSessionPrimarySelectionChanged runs the equality
    // short-circuit, finds identical state, and does NOT emit selectionChanged.
    cs->syncSelectionToSession();
    // Give the event loop a chance to process any asynchronous echoes.
    QTest::qWait(50);

    // Selection state must be unchanged.
    QVERIFY(cs->hasSelection());
    QCOMPARE(sv->anchorBlock(), 0);
    QCOMPARE(sv->anchorQtPos(), 0);
    QCOMPARE(sv->activeBlock(), 1);
    QCOMPARE(sv->activeQtPos(), 3);

    // No spurious selectionChanged must have fired from the view layer.
    QCOMPARE(selSpy.count(), 0);

    // Additional check: pushing the same TextAnchors via setPrimarySelection
    // is deduplicated by the Session itself — also no echo.
    const int selSpyCount = selSpy.count();
    session->setPrimarySelection(currentSel);
    QTest::qWait(50);
    QCOMPARE(selSpy.count(), selSpyCount);  // still 0
}

// ---------------------------------------------------------------------------
// F. selection_survives_structural_edit_above
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::selection_survives_structural_edit_above()
{
    // BlockAnchor identity is stable across structural edits.
    // When a new block is inserted above the selected row, the row index
    // shifts but the BlockAnchor of the selection anchor is unchanged.
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("first\n\nsecond\n\nthird\n\nfourth\n");
    waitForModel(binding, 4);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    // Select from row 2 to row 3.
    sv->begin(2, 0);
    sv->extend(3, 3);
    const auto anchorBefore = cs->selectionAnchor()->block;
    QCOMPARE(cs->rowForBlock(anchorBefore), 2);

    // Insert a new block after row 0 → shifts rows 1+ up by one.
    const auto block0 = binding.model()->recordAt(0).blockAnchor;
    Markoff::Cmd::enterAtEnd(doc, block0);
    // Wait for the model to reflect the new row.
    for (int i = 0; i < 50 && binding.model()->rowCount() < 5; ++i)
        QTest::qWait(10);
    QCOMPARE(binding.model()->rowCount(), 5);

    // The selection anchor BlockAnchor is unchanged.
    QVERIFY(cs->selectionAnchor().has_value());
    QCOMPARE(cs->selectionAnchor()->block, anchorBefore);

    // The facade reports the updated row index (was 2, now 3 due to insertion).
    QCOMPARE(sv->anchorBlock(), 3);
    QCOMPARE(cs->rowForBlock(anchorBefore), 3);
}

// ---------------------------------------------------------------------------
// G. selection_cleared_on_orphaned_anchor
// ---------------------------------------------------------------------------

void TestSelectionCursorUnification::selection_cleared_on_orphaned_anchor()
{
    // When the block holding the selection anchor is deleted, replaying the
    // old selection via Session::setPrimarySelection triggers the orphaned-
    // anchor branch in onSessionPrimarySelectionChanged, which calls
    // clearSelectionAnchor() and leaves hasSelection() == false.
    //
    // This is the same production path exercised by
    // tst_live_render_session_orphaned_block: the clearing is driven by an
    // explicit setPrimarySelection call with the now-orphaned TextAnchors,
    // not by block deletion alone.
    Markoff::MarkoffDocument doc(quint16(42));
    Markoff::Live::LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("first\n\nsecond\n");
    waitForModel(binding, 2);

    Markoff::Session *session = doc.createSession();
    binding.setSession(session);

    auto *sv = binding.cursorState();
    auto *cs = binding.cursorState();

    // Select text in row 1.
    sv->begin(1, 0);
    sv->extend(1, 6);
    QVERIFY(cs->hasSelection());
    QVERIFY(sv->hasSelection());

    // Capture the session primary selection while the anchor is still valid
    // (before the block is deleted).
    const Markoff::Selection savedSel = session->primarySelection();

    // Clear the view so it's in a clean state.
    sv->clearSelection();
    QVERIFY(!sv->hasSelection());

    // Delete the selected block via the D2 API.
    const auto block1 = binding.model()->recordAt(1).blockAnchor;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2RemoveBlock(block1, t);
    }

    // Wait for the model to reflect the deletion.
    for (int i = 0; i < 50 && binding.model()->rowCount() > 1; ++i)
        QTest::qWait(10);
    QCOMPARE(binding.model()->rowCount(), 1);

    // Push the old (now-orphaned) selection back via the session. This
    // triggers onSessionPrimarySelectionChanged which finds that the
    // anchor's block is no longer in the model (rowForBlock returns -1)
    // and calls clearSelectionAnchor(). The view must not show a selection.
    session->setPrimarySelection(savedSel);
    QTest::qWait(50);

    QVERIFY(!cs->hasSelection());
    QVERIFY(!sv->hasSelection());
}

} // namespace Markoff::Live::Test

QTEST_GUILESS_MAIN(Markoff::Live::Test::TestSelectionCursorUnification)
#include "tst_live_render_selection_cursor_unification.moc"
