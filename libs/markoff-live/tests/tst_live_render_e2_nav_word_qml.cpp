// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_e2_nav_word.cpp.
//
// Ctrl+Left at block-start and Ctrl+Right at block-end cross block
// boundaries via the navigation controller. The cursor-landing half
// depends on the focus chokepoint resolving via a registered delegate,
// so these run through the QML window per INVARIANTS.md #5.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestE2NavWordQml : public QObject {
    Q_OBJECT
private slots:
    void ctrl_left_at_block_start_crosses_to_prev_block_end();
    void ctrl_right_at_block_end_crosses_to_next_block_start();
};

void TestE2NavWordQml::ctrl_left_at_block_start_crosses_to_prev_block_end() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);

    QObject *csObj = fx.binding()->property("cursorState").value<QObject *>();
    auto *cs = qobject_cast<LiveCursorState *>(csObj);
    QVERIFY(cs);
    cs->setDesiredVisualX(77.0);

    fx.harness().keyClick(Qt::Key_Left, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 5);  // end of "Alpha"
    QCOMPARE(cs->desiredVisualX(), -1.0);
}

void TestE2NavWordQml::ctrl_right_at_block_end_crosses_to_next_block_start() {
    QmlIntegrationFixture fx("Alpha\n\nBeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 5, 2000);

    QObject *csObj = fx.binding()->property("cursorState").value<QObject *>();
    auto *cs = qobject_cast<LiveCursorState *>(csObj);
    QVERIFY(cs);
    cs->setDesiredVisualX(77.0);

    fx.harness().keyClick(Qt::Key_Right, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
    QCOMPARE(cs->desiredVisualX(), -1.0);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestE2NavWordQml)
#include "tst_live_render_e2_nav_word_qml.moc"
