// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QRectF>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

// MockTextEdit: simulates a QML TextEdit's layout state (read via Qt property system)
class MockTextEdit : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle CONSTANT)
    Q_PROPERTY(qreal contentHeight READ contentHeight CONSTANT)
    Q_PROPERTY(qreal width READ width CONSTANT)
public:
    QRectF m_cursorRect;
    qreal  m_contentHeight = 20.0;
    qreal  m_width = 200.0;
    int    m_positionAtReturn = 7;
    QRectF cursorRectangle() const { return m_cursorRect; }
    qreal  contentHeight()   const { return m_contentHeight; }
    qreal  width()           const { return m_width; }
    Q_INVOKABLE int positionAt(double /*x*/, double /*y*/) const {
        return m_positionAtReturn;
    }
};

class TestE2NavShiftExtend : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- G1: Shift+Left ----

    // shift_left_at_qtpos_0_extends_selection_to_prev_block_end moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).

    void shift_left_at_nonzero_qtpos_extends_within_block() {
        // Option B contract: within-block Shift+Left is handled by the
        // controller and extends LiveCursorState's active by one position.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        sv->begin(1, 3);
        const int result = nav->tryHandle(Qt::Key_Left, Qt::ShiftModifier,
                                          1, 3, nullptr, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->anchorBlock(), 1);
        QCOMPARE(sv->anchorQtPos(), 3);
        QCOMPARE(sv->activeBlock(), 1);
        QCOMPARE(sv->activeQtPos(), 2);
    }

    void shift_left_at_boundary_row0_returns_handled_no_extend() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        sv->begin(0, 0);
        QSignalSpy spy(sv, &LiveCursorState::selectionChanged);

        // At row 0, pos 0: no previous block → Handled, no extend
        const int result = nav->tryHandle(Qt::Key_Left, Qt::ShiftModifier,
                                          0, 0, nullptr, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        // No extend call: selectionChanged should not have fired after begin
        QCOMPARE(spy.count(), 0);
        // Selection is not active (anchor==active)
        QVERIFY(!sv->hasSelection());
    }

    // ---- G1: Shift+Right ----

    // shift_right_at_end_extends_selection_to_next_block_start moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).

    void shift_right_at_non_end_extends_within_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        sv->begin(0, 2);
        const int result = nav->tryHandle(Qt::Key_Right, Qt::ShiftModifier,
                                          0, 2, nullptr, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->anchorBlock(), 0);
        QCOMPARE(sv->anchorQtPos(), 2);
        QCOMPARE(sv->activeBlock(), 0);
        QCOMPARE(sv->activeQtPos(), 3);
    }

    void shift_right_at_last_row_end_returns_handled_no_extend() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        sv->begin(0, 9);  // end of "OnlyBlock"
        QSignalSpy spy(sv, &LiveCursorState::selectionChanged);

        // At last block's end: Handled, no extend (no next block)
        const int result = nav->tryHandle(Qt::Key_Right, Qt::ShiftModifier,
                                          0, 9, nullptr, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(spy.count(), 0);
        QVERIFY(!sv->hasSelection());
    }

    // ---- G1: Shift+Up ----

    // shift_up_at_visual_top_extends_selection_and_sets_hint moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).

    void shift_up_at_non_top_line_extends_within_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Beta multiline content");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(0, 25, 2, 20);  // not at top
        mockEdit.m_contentHeight = 40.0;
        mockEdit.m_positionAtReturn = 5;  // within-block visual-line up lands here

        sv->begin(0, 8);  // anchor before Shift+Up
        const int result = nav->tryHandle(Qt::Key_Up, Qt::ShiftModifier,
                                          0, 8, &mockEdit, QStringLiteral("Beta multiline"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->anchorBlock(), 0);
        QCOMPARE(sv->anchorQtPos(), 8);
        QCOMPARE(sv->activeBlock(), 0);
        QCOMPARE(sv->activeQtPos(), 5);
    }

    // ---- G1: Shift+Down ----

    // shift_down_at_visual_bottom_extends_selection_and_sets_hint moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).

    void shift_down_at_non_bottom_line_extends_within_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha multiline content");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.cursorState();
        QVERIFY(nav && sv);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(0, 0, 2, 10);  // not at bottom
        mockEdit.m_contentHeight = 30.0;
        mockEdit.m_positionAtReturn = 9;

        sv->begin(0, 2);
        const int result = nav->tryHandle(Qt::Key_Down, Qt::ShiftModifier,
                                          0, 2, &mockEdit, QStringLiteral("Alpha multiline"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(sv->anchorBlock(), 0);
        QCOMPARE(sv->anchorQtPos(), 2);
        QCOMPARE(sv->activeBlock(), 0);
        QCOMPARE(sv->activeQtPos(), 9);
    }

    // ---- G2: Ctrl+Shift+Left ----

    // Renamed 2026-06-16 (queue #10 item 1). Pre-audit-L4 (commit `0cbdf48`)
    // Ctrl+Shift+Left/Right within a block fell through to native TextEdit
    // word-select, so tryHandle correctly returned NotHandled. Audit L4
    // (`docs/specs/2026-05-21-audit-L4-ctrl-shift-word-extend.md`) deliberately
    // changed this: `LiveNavigationController::tryHandle` now claims the
    // chord within-block too, computing the word boundary in C++ and routing
    // through `cursorState->begin/extend` so the document-layer selection
    // anchor stays authoritative — the native-selection path left
    // `m_selectionAnchor` empty, which silently broke Ctrl+C after a
    // Ctrl+Shift+Left/Right word-extend. That fix is closed, ratified, and
    // pinned by its own falsifiable QML test
    // (`tst_live_render_ctrl_shift_word_extend_qml`, 5 slots, all green) —
    // the production chord-claiming behavior is correct. These two slots
    // were never updated when L4 landed; they were asserting the retired
    // pre-L4 contract, not exercising a real regression. Checked: no
    // production callsite (all delegates: UnifiedInlineTextDelegate,
    // CodeBlockDelegate, BlockOnlyDelegateBase, TableDelegate, MathDelegate)
    // treats a NotHandled return from this chord specially — every callsite
    // just does `event.accepted = (tryHandle(...) != NotHandled)`. Reshaping
    // to match the current, intentional contract.
    void ctrl_shift_left_inside_block_claims_the_chord() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        const int result = nav->tryHandle(Qt::Key_Left,
                                          Qt::ControlModifier | Qt::ShiftModifier,
                                          1, 5, nullptr, QStringLiteral("hello world"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }

    // ctrl_shift_left_at_block_start_extends_into_prev_block moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).

    void ctrl_shift_right_inside_block_claims_the_chord() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        const int result = nav->tryHandle(Qt::Key_Right,
                                          Qt::ControlModifier | Qt::ShiftModifier,
                                          0, 3, nullptr, QStringLiteral("hello world"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
    }

    // ctrl_shift_right_at_block_end_extends_into_next_block moved to
    // tst_live_render_e2_nav_shift_extend_qml.cpp (chokepoint).
};

QTEST_MAIN(TestE2NavShiftExtend)
#include "tst_live_render_e2_nav_shift_extend.moc"
