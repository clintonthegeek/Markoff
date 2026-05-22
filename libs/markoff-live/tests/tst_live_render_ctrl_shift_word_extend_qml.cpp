// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit L4 regression net — Ctrl+Shift+Left/Right within-block word-
// extend must set the document-layer selection anchor, not just the
// TextEdit's visible within-block selection. Spec:
// docs/specs/2026-05-21-audit-L4-ctrl-shift-word-extend.md.
//
// Pre-fix: TextEdit's native handler builds a visible selection while
// m_selectionAnchor stays empty; Ctrl+C copies the caret position.
// Post-fix: LiveNavigationController claims the chord, computes the
// word boundary via QTextBoundaryFinder, and routes through
// cursorState->begin/extend like every other Shift-modified motion.

#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>

#include <QApplication>
#include <QClipboard>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestCtrlShiftWordExtendQml : public QObject {
    Q_OBJECT

    LiveCursorState *cursorStateOf(QmlIntegrationFixture &fx) {
        return qobject_cast<LiveCursorState *>(
            fx.binding()->property("cursorState").value<QObject *>());
    }

    QPair<int, int> docSelectionFor(QmlIntegrationFixture &fx, int row) {
        auto *cs = cursorStateOf(fx);
        if (!cs) return {-1, -1};
        QPoint r;
        QMetaObject::invokeMethod(cs, "rangeForBlock", Qt::DirectConnection,
                                  Q_RETURN_ARG(QPoint, r), Q_ARG(int, row));
        return {r.x(), r.y()};
    }

    QPair<int, int> visibleSelectionFor(QmlIntegrationFixture &fx, int row) {
        QQuickItem *te = fx.delegateTextEdit(row);
        if (!te) return {-1, -1};
        return { te->property("selectionStart").toInt(),
                 te->property("selectionEnd").toInt() };
    }

private slots:
    void ctrl_shift_left_within_block_sets_cross_block_anchor();
    void ctrl_shift_right_within_block_sets_cross_block_anchor();
    void ctrl_shift_left_at_qtpos_zero_jumps_to_prev_block_end();
    void ctrl_shift_left_within_block_then_ctrl_c_copies_visible_selection();
    void ctrl_shift_left_extends_existing_cross_block_selection();
};

void TestCtrlShiftWordExtendQml::ctrl_shift_left_within_block_sets_cross_block_anchor() {
    // "hello world" — cursor at end (qtPos 11). Ctrl+Shift+Left should
    // extend selection back to the start of "world" (qtPos 6) and the
    // document-layer selection range must reflect that — not just the
    // visible TextEdit range.
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Left,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const auto docRange = docSelectionFor(fx, 0);
    QCOMPARE(docRange.first,  6);   // start of "world"
    QCOMPARE(docRange.second, 11);  // end of text

    // Visible selection must agree with the document layer.
    const auto visRange = visibleSelectionFor(fx, 0);
    QCOMPARE(visRange.first,  6);
    QCOMPARE(visRange.second, 11);
}

void TestCtrlShiftWordExtendQml::ctrl_shift_right_within_block_sets_cross_block_anchor() {
    // "hello world" — cursor at start. Ctrl+Shift+Right should extend
    // selection to the end of "hello" (qtPos 5) or end of "hello "
    // (qtPos 6), depending on Qt-version's WordRight semantics.
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtPos(0, 0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Right,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const auto docRange = docSelectionFor(fx, 0);
    QCOMPARE(docRange.first, 0);
    QVERIFY2(docRange.second == 5 || docRange.second == 6,
             qPrintable(QString("expected WordRight to land at 5 or 6; got %1")
                            .arg(docRange.second)));

    const auto visRange = visibleSelectionFor(fx, 0);
    QCOMPARE(visRange.first,  docRange.first);
    QCOMPARE(visRange.second, docRange.second);
}

void TestCtrlShiftWordExtendQml::ctrl_shift_left_at_qtpos_zero_jumps_to_prev_block_end() {
    // Regression for the cross-block fallthrough that already worked.
    // Cursor at start of block 1; Ctrl+Shift+Left jumps to end of block 0,
    // selection spans block 0 from end to qtPos 0 of block 1 (which is
    // empty in block 1's local range).
    QmlIntegrationFixture fx("para one\n\npara two\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(1, 0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Left,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    int activeBlock = -1, activeQtPos = -1, anchorBlock = -1, anchorQtPos = -1;
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "activeQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeQtPos));
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPos));

    QCOMPARE(anchorBlock, 1);
    QCOMPARE(anchorQtPos, 0);
    QCOMPARE(activeBlock, 0);
    QCOMPARE(activeQtPos, 8);  // length of "para one"
}

void TestCtrlShiftWordExtendQml::ctrl_shift_left_within_block_then_ctrl_c_copies_visible_selection() {
    // The user-facing payoff. Without the L4 fix, Ctrl+C after
    // Ctrl+Shift+Left within a block puts NOTHING on the clipboard
    // because the document-layer anchor is empty even though TextEdit
    // shows a highlighted word.
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Left,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    QApplication::clipboard()->clear();
    QTest::qWait(10);

    QTest::keyClick(fx.window(), Qt::Key_C, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    const QString cb = QApplication::clipboard()->text();
    QCOMPARE(cb, QStringLiteral("world"));
}

void TestCtrlShiftWordExtendQml::ctrl_shift_left_extends_existing_cross_block_selection() {
    // Cross-block selection already active: anchor in block 0, active in
    // block 1 mid-word. Ctrl+Shift+Left from within block 1 must extend
    // the active end by one word *within block 1*, leaving anchor at
    // block 0 untouched.
    QmlIntegrationFixture fx("alpha beta\n\ngamma delta\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    // Begin selection at (block 0, qtPos 3) — inside "alpha".
    fx.placeCursorAtPos(0, 3);
    QTest::qWait(20);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 3));
    // Extend to (block 1, qtPos 11) — end of "gamma delta".
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 11));
    fx.placeCursorAtPos(1, 11);
    QTest::qWait(20);

    // Ctrl+Shift+Left — should walk active end back one word within block 1.
    QTest::keyClick(fx.window(), Qt::Key_Left,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    int anchorBlock = -1, anchorQtPos = -1, activeBlock = -1, activeQtPos = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPos));
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "activeQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeQtPos));

    // Anchor must stay where we put it.
    QCOMPARE(anchorBlock, 0);
    QCOMPARE(anchorQtPos, 3);
    // Active end moved back one word within block 1 — to start of "delta".
    QCOMPARE(activeBlock, 1);
    QCOMPARE(activeQtPos, 6);  // "gamma " = 6 chars, "delta" starts at 6
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCtrlShiftWordExtendQml)
#include "tst_live_render_ctrl_shift_word_extend_qml.moc"
