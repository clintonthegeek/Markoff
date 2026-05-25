// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_structural.cpp.
//
// The three paragraph-Enter assertions verify that pressing Return splits
// the block AND lands the cursor at the new position. The cursor-landing
// half goes through LiveCursorState::establishFocus (chokepoint),
// which only fires cursorChanged when a delegate is registered for the
// newborn block's anchor. Direct unit-test setups can verify the model
// half (rowCount, blockText) but not the cursor half — those landings live
// here per INVARIANTS.md #5.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/core/MarkoffDocument.h>

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestLiveRenderStructuralQml : public QObject {
    Q_OBJECT
private slots:
    void enter_at_end_of_paragraph_creates_new_block_and_lands_cursor();
    void enter_in_middle_of_paragraph_splits_and_lands_cursor();
    void enter_at_start_of_paragraph_lands_cursor_in_new_empty_block();
};

void TestLiveRenderStructuralQml::enter_at_end_of_paragraph_creates_new_block_and_lands_cursor() {
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtEndOf(0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    fx.harness().keyClick(Qt::Key_Return);

    QVERIFY(fx.waitForRowCount(2, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
}

void TestLiveRenderStructuralQml::enter_in_middle_of_paragraph_splits_and_lands_cursor() {
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/5);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 5, 2000);

    fx.harness().keyClick(Qt::Key_Return);

    QVERIFY(fx.waitForRowCount(2, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);

    // Original split is preserved: row 0 has "hello", row 1 starts with " world".
    QCOMPARE(fx.modelText(0), QStringLiteral("hello"));
    QCOMPARE(fx.modelText(1), QStringLiteral(" world"));
}

void TestLiveRenderStructuralQml::enter_at_start_of_paragraph_lands_cursor_in_new_empty_block() {
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentQtPos(), 0, 2000);

    fx.harness().keyClick(Qt::Key_Return);

    QVERIFY(fx.waitForRowCount(2, 2000));
    // After Enter at start: row 0 is the new empty block, row 1 has the
    // original "hello world". Cursor stays at row 0 (the new empty block).
    QCOMPARE(fx.modelText(0), QString());
    QCOMPARE(fx.modelText(1), QStringLiteral("hello world"));
    QTRY_VERIFY_WITH_TIMEOUT(fx.cursorStateCurrentRow() >= 0, 2000);
    QCOMPARE(fx.cursorStateCurrentRow(), 0);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestLiveRenderStructuralQml)
#include "tst_live_render_structural_qml.moc"
