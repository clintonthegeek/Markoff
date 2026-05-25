// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_e2_nav_arrows.cpp.
//
// Four assertions on cross-block arrow navigation (Up/Down visual-line
// crossing, Left at qtPos 0, Right at qtPos end) used to live as unit
// tests against `LiveNavigationController::tryHandle` with a `MockTextEdit`.
// The focus-chokepoint refactor gates `cursorState()->focusedAnchorRow()`
// on a registered delegate — direct unit-test setups have none. Per
// INVARIANTS.md #5 these are now exercised through the QML window.
//
// The mock-driven `desiredVisualX == <pinned-number>` assertions are
// dropped: integration tests see real layout, and the user-visible
// behaviour is "cursor lands at the right row", not the specific x.
// The clear-on-cross-block-boundary assertion for Left/Right is kept.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestE2NavArrowsQml : public QObject {
    Q_OBJECT

    LiveCursorState *cursorStateOf(QmlIntegrationFixture &fx) {
        QObject *o = fx.binding()->property("cursorState").value<QObject *>();
        return qobject_cast<LiveCursorState *>(o);
    }

private slots:
    void up_at_visual_top_line_crosses_to_prev_block();
    void down_at_visual_bottom_line_crosses_to_next_block();
    void left_at_qtpos_0_crosses_to_prev_block_end_clears_visual_x();
    void right_at_end_crosses_to_next_block_start_clears_visual_x();
};

void TestE2NavArrowsQml::up_at_visual_top_line_crosses_to_prev_block() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    // Place cursor at start of block 1, then press Up — should cross.
    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    fx.harness().keyClick(Qt::Key_Up);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
}

void TestE2NavArrowsQml::down_at_visual_bottom_line_crosses_to_next_block() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    fx.harness().keyClick(Qt::Key_Down);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
}

void TestE2NavArrowsQml::left_at_qtpos_0_crosses_to_prev_block_end_clears_visual_x() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    // Inject a non-default desiredVisualX so we can observe Left clearing it.
    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);
    cs->setDesiredVisualX(55.0);

    fx.harness().keyClick(Qt::Key_Left);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 5);  // end of "Alpha"
    QCOMPARE(cs->desiredVisualX(), -1.0);
}

void TestE2NavArrowsQml::right_at_end_crosses_to_next_block_start_clears_visual_x() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 5, 2000);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);
    cs->setDesiredVisualX(77.0);

    fx.harness().keyClick(Qt::Key_Right);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
    QCOMPARE(cs->desiredVisualX(), -1.0);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestE2NavArrowsQml)
#include "tst_live_render_e2_nav_arrows_qml.moc"
