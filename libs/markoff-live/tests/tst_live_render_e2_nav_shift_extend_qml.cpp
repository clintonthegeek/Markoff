// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_e2_nav_shift_extend.cpp.
//
// Six cross-block selection-extension assertions (Shift+Left/Right at
// boundaries, Shift+Up/Down across visual lines, Ctrl+Shift+Left/Right
// at boundaries). All depend on the focus chokepoint resolving the
// cross-block cursor jump via a registered delegate, plus
// LiveSelectionView observing the extended selection range.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveSelectionView.h>

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestE2NavShiftExtendQml : public QObject {
    Q_OBJECT

    LiveCursorState *cursorStateOf(QmlIntegrationFixture &fx) {
        return qobject_cast<LiveCursorState *>(
            fx.binding()->property("cursorState").value<QObject *>());
    }
    LiveSelectionView *selectionViewOf(QmlIntegrationFixture &fx) {
        return qobject_cast<LiveSelectionView *>(
            fx.binding()->property("selectionView").value<QObject *>());
    }

private slots:
    void shift_left_at_qtpos_0_extends_selection_to_prev_block_end();
    void shift_right_at_end_extends_selection_to_next_block_start();
    void shift_up_at_visual_top_extends_selection();
    void shift_down_at_visual_bottom_extends_selection();
    void ctrl_shift_left_at_block_start_extends_into_prev_block();
    void ctrl_shift_right_at_block_end_extends_into_next_block();
};

void TestE2NavShiftExtendQml::shift_left_at_qtpos_0_extends_selection_to_prev_block_end() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    fx.harness().keyClick(Qt::Key_Left, Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 5);  // end of "Alpha"
    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    QVERIFY(sv->hasSelection());
}

void TestE2NavShiftExtendQml::shift_right_at_end_extends_selection_to_next_block_start() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 5, 2000);

    fx.harness().keyClick(Qt::Key_Right, Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    QVERIFY(sv->hasSelection());
}

void TestE2NavShiftExtendQml::shift_up_at_visual_top_extends_selection() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/2);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    fx.harness().keyClick(Qt::Key_Up, Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    QVERIFY(sv->hasSelection());
}

void TestE2NavShiftExtendQml::shift_down_at_visual_bottom_extends_selection() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/3);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    fx.harness().keyClick(Qt::Key_Down, Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    QVERIFY(sv->hasSelection());
}

void TestE2NavShiftExtendQml::ctrl_shift_left_at_block_start_extends_into_prev_block() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    // Seed the selection anchor at the current position. QML's Shift+arrow
    // path auto-seeds on press, but Ctrl+Shift+arrow at a row boundary
    // depends on the anchor already existing — matches the production flow
    // where the user has an active selection before issuing the Ctrl+Shift
    // word-jump.
    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    sv->begin(1, 0);

    fx.harness().keyClick(Qt::Key_Left,
                          Qt::ControlModifier | Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 5);
    QVERIFY(sv->hasSelection());
}

void TestE2NavShiftExtendQml::ctrl_shift_right_at_block_end_extends_into_next_block() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 5, 2000);

    auto *sv = selectionViewOf(fx);
    QVERIFY(sv);
    sv->begin(0, 5);  // seed anchor at end-of-Alpha — see ctrl_shift_left above

    fx.harness().keyClick(Qt::Key_Right,
                          Qt::ControlModifier | Qt::ShiftModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
    QVERIFY(sv->hasSelection());
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestE2NavShiftExtendQml)
#include "tst_live_render_e2_nav_shift_extend_qml.moc"
