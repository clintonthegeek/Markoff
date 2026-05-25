// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_e2_nav_home_end.cpp.
//
// The two Ctrl+Home / Ctrl+End "cursor lands at target" assertions used to
// live as direct unit tests against `LiveNavigationController::tryHandle`,
// but the focus-chokepoint refactor (LiveCursorState::establishFocus,
// landed in commits 1088edb..249e7ef) gates cursor resolution on a
// registered delegate (m_delegates lookup in tryResolvePending). Direct
// unit-test setups have no delegate registry, so the cursor never lands
// and `focusedQtPos()` stays -1. Per INVARIANTS.md #5 ("Tests exercise
// the production callsite, not a synonym"), the right fix is to exercise
// these through the real QML window. The return-value tests for the
// navigation controller stay in the unit-test file.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestE2NavHomeEndQml : public QObject {
    Q_OBJECT
private slots:
    void ctrl_home_lands_at_first_block_qtpos_0();
    void ctrl_end_lands_at_last_block_end();
};

void TestE2NavHomeEndQml::ctrl_home_lands_at_first_block_qtpos_0() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n\nGamma\n", /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    // Park the caret somewhere in the last block so Ctrl+Home has work to do.
    fx.placeCursorAtPos(/*row=*/2, /*qtPos=*/3);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 2, 2000);

    fx.harness().keyClick(Qt::Key_Home, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
}

void TestE2NavHomeEndQml::ctrl_end_lands_at_last_block_end() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n\nGamma\n", /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    // Park the caret in the first block; Ctrl+End must walk to the last.
    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    fx.harness().keyClick(Qt::Key_End, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 2, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 5);  // "Gamma" length
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestE2NavHomeEndQml)
#include "tst_live_render_e2_nav_home_end_qml.moc"
