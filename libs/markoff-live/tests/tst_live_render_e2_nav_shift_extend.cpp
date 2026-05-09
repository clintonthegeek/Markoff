// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QRectF>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

// MockTextEdit: simulates a QML TextEdit's layout state (read via Qt property system)
class MockTextEdit : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle CONSTANT)
    Q_PROPERTY(qreal contentHeight READ contentHeight CONSTANT)
public:
    QRectF m_cursorRect;
    qreal  m_contentHeight = 20.0;
    QRectF cursorRectangle() const { return m_cursorRect; }
    qreal  contentHeight()   const { return m_contentHeight; }
};

class TestE2NavShiftExtend : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- G1: Shift+Left ----

    void shift_left_at_qtpos_0_extends_selection_to_prev_block_end() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && cs && sv);

        // Establish anchor at block 1, pos 0 (simulates QML begin() before Shift press)
        sv->begin(1, 0);
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);

        const int result = nav->tryHandle(Qt::Key_Left, Qt::ShiftModifier,
                                          /*blockIndex=*/1, /*qtPos=*/0,
                                          nullptr, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));

        // selectionChanged must have been emitted (extend was called)
        QVERIFY(spy.count() >= 1);
        // Selection is now active (anchor=block1,pos0 → active=block0,pos5)
        QVERIFY(sv->hasSelection());
        // Cursor moved to end of "Alpha" (len=5)
        QCOMPARE(cs->focusedQtPos(), 5);
        QCOMPARE(cs->focusedAnchorRow(), 0);
        // desiredVisualX cleared
        QCOMPARE(cs->desiredVisualX(), -1.0);
    }

    void shift_left_at_nonzero_qtpos_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        // qtPos > 0: let TextEdit handle it natively
        const int result = nav->tryHandle(Qt::Key_Left, Qt::ShiftModifier,
                                          1, 3, nullptr, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::NotHandled));
    }

    void shift_left_at_boundary_row0_returns_handled_no_extend() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && sv);

        sv->begin(0, 0);
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);

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

    void shift_right_at_end_extends_selection_to_next_block_start() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && cs && sv);

        // Establish anchor at block 0, pos 5 (end of "Alpha")
        sv->begin(0, 5);
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);

        const int result = nav->tryHandle(Qt::Key_Right, Qt::ShiftModifier,
                                          /*blockIndex=*/0, /*qtPos=*/5,
                                          nullptr, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));

        // selectionChanged must have been emitted (extend was called)
        QVERIFY(spy.count() >= 1);
        // Selection active: anchor=block0,pos5 → active=block1,pos0
        QVERIFY(sv->hasSelection());
        // Cursor at start of next block
        QCOMPARE(cs->focusedQtPos(), 0);
        QCOMPARE(cs->focusedAnchorRow(), 1);
        // desiredVisualX cleared
        QCOMPARE(cs->desiredVisualX(), -1.0);
    }

    void shift_right_at_non_end_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        // qtPos < length: let TextEdit handle it natively
        const int result = nav->tryHandle(Qt::Key_Right, Qt::ShiftModifier,
                                          0, 2, nullptr, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::NotHandled));
    }

    void shift_right_at_last_row_end_returns_handled_no_extend() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("OnlyBlock");
        QTest::qWait(200);

        auto *nav = binding.navigationController();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && sv);

        sv->begin(0, 9);  // end of "OnlyBlock"
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);

        // At last block's end: Handled, no extend (no next block)
        const int result = nav->tryHandle(Qt::Key_Right, Qt::ShiftModifier,
                                          0, 9, nullptr, QStringLiteral("OnlyBlock"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));
        QCOMPARE(spy.count(), 0);
        QVERIFY(!sv->hasSelection());
    }

    // ---- G1: Shift+Up ----

    void shift_up_at_visual_top_extends_selection_and_sets_hint() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && cs && sv);

        // Establish anchor at block 1, some position
        sv->begin(1, 2);
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);
        QSignalSpy hintSpy(cs, &LiveCursorState::visualLineHintChanged);

        // Mock edit at visual top line
        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(42.0, 0, 2, 20);  // y=0 < height*0.5=10 → top
        mockEdit.m_contentHeight = 20.0;

        const int result = nav->tryHandle(Qt::Key_Up, Qt::ShiftModifier,
                                          /*blockIndex=*/1, /*qtPos=*/2,
                                          &mockEdit, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));

        // extend was called → selectionChanged fired
        QVERIFY(spy.count() >= 1);
        // hint was set (LastLine) then resolved synchronously
        QVERIFY(hintSpy.count() >= 1);
        // Cursor at row 0 (resolved immediately since row exists)
        QCOMPARE(cs->focusedAnchorRow(), 0);
        // desiredVisualX set from cursorRect.x()
        QCOMPARE(cs->desiredVisualX(), 42.0);
    }

    void shift_up_at_non_top_line_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(0, 25, 2, 20);  // y=25 > height*0.5=10 → not top
        mockEdit.m_contentHeight = 40.0;

        const int result = nav->tryHandle(Qt::Key_Up, Qt::ShiftModifier,
                                          1, 2, &mockEdit, QStringLiteral("Beta"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ---- G1: Shift+Down ----

    void shift_down_at_visual_bottom_extends_selection_and_sets_hint() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Alpha\n\nBeta");
        QTest::qWait(200);
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *nav = binding.navigationController();
        auto *cs  = binding.cursorState();
        auto *sv  = binding.selectionView();
        QVERIFY(nav && cs && sv);

        // Establish anchor at block 0
        sv->begin(0, 3);
        QSignalSpy spy(sv, &LiveSelectionView::selectionChanged);
        QSignalSpy hintSpy(cs, &LiveCursorState::visualLineHintChanged);

        // Mock edit at visual bottom line
        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(30.0, 5, 2, 15);  // bottom = 5+15 = 20
        mockEdit.m_contentHeight = 20.0;  // 20 > 20 - 15*0.5 = 12.5 → at bottom

        const int result = nav->tryHandle(Qt::Key_Down, Qt::ShiftModifier,
                                          /*blockIndex=*/0, /*qtPos=*/3,
                                          &mockEdit, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::Handled));

        // extend was called → selectionChanged fired
        QVERIFY(spy.count() >= 1);
        // hint was set (FirstLine)
        QVERIFY(hintSpy.count() >= 1);
        // Cursor at row 1
        QCOMPARE(cs->focusedAnchorRow(), 1);
        // desiredVisualX set from cursorRect.x()
        QCOMPARE(cs->desiredVisualX(), 30.0);
    }

    void shift_down_at_non_bottom_line_returns_not_handled() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav);

        MockTextEdit mockEdit;
        mockEdit.m_cursorRect  = QRectF(0, 0, 2, 10);  // bottom = 10, contentH = 30 → not bottom
        mockEdit.m_contentHeight = 30.0;

        const int result = nav->tryHandle(Qt::Key_Down, Qt::ShiftModifier,
                                          0, 0, &mockEdit, QStringLiteral("Alpha"));
        QCOMPARE(result, static_cast<int>(LiveNavigationController::NotHandled));
    }

};

QTEST_MAIN(TestE2NavShiftExtend)
#include "tst_live_render_e2_nav_shift_extend.moc"
