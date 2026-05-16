// SPDX-License-Identifier: GPL-3.0-or-later
//
// QML-integration companion to tst_live_render_cursor.cpp.
//
// Two LiveCursorState assertions that depend on the focus chokepoint
// (m_delegates lookup in tryResolvePending) cannot run against a directly
// instantiated `LiveCursorState` — no delegate is registered, so the
// resolution short-circuits. Per INVARIANTS.md #5 they exercise the
// production callsite (QML → requestTextCaretAtRow → establishFocus →
// tryResolvePending with a real delegate registered).
//
// `requestTextCaretAtRow_pending_resolves_on_structural_insert` was deleted
// rather than ported: the chokepoint API no longer supports "pending request
// for a row that doesn't yet exist" via requestTextCaretAtRow (that semantic
// belongs to establishFocus, already covered by
// enterAtEnd_landsFocusOnNewRowViaChokepoint in the unit file).

#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestLiveRenderCursorQml : public QObject {
    Q_OBJECT
private slots:
    void requestTextCaretAtRow_already_exists_lands_via_chokepoint();
    void request_text_caret_at_row_visual_x_records_hint();
};

void TestLiveRenderCursorQml::requestTextCaretAtRow_already_exists_lands_via_chokepoint() {
    // Two-paragraph doc, request caret at row 1; the chokepoint resolves
    // through the real delegate and fires cursorChanged.
    QmlIntegrationFixture fx("alpha\n\nbeta\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    // Wait for the initial cursor settling to quiet down before spying.
    QTest::qWait(50);
    QCoreApplication::processEvents();

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);

    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QCOMPARE(fx.cursorStateCurrentQtPos(), 0);
}

void TestLiveRenderCursorQml::request_text_caret_at_row_visual_x_records_hint() {
    // requestTextCaretAtRowVisualX should set pendingVisualLineHint, route
    // through the chokepoint, and clear the hint once the caret resolves.
    QmlIntegrationFixture fx("hello\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    QObject *csObj = fx.binding()->property("cursorState").value<QObject *>();
    QVERIFY(csObj);
    auto *cs = qobject_cast<LiveCursorState *>(csObj);
    QVERIFY(cs);

    cs->setDesiredVisualX(123.0);
    cs->requestTextCaretAtRowVisualX(0, LiveCursorState::VisualLineHint::LastLine);

    // desiredVisualX is independent of the hint and stays put.
    QCOMPARE(cs->desiredVisualX(), 123.0);
    // After the chokepoint resolves, the hint must be back to None.
    QTRY_COMPARE_WITH_TIMEOUT(cs->pendingVisualLineHint(),
                              LiveCursorState::VisualLineHint::None, 2000);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestLiveRenderCursorQml)
#include "tst_live_render_cursor_qml.moc"
