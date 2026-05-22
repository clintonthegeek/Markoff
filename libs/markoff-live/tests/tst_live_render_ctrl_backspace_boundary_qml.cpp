// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit L9 regression net — Ctrl+Backspace / Ctrl+Delete at the block
// boundary. The spec (docs/specs/2026-05-21-audit-L9-ctrl-backspace-
// boundary.md) records the decision: the Ctrl modifier is IGNORED at
// the boundary (qtPos = 0 for Backspace, qtPos = length for Delete);
// the behaviour collapses to plain block-merge. In-block use of the
// chord continues to delegate to TextEdit's native word-distance
// deletion, which feeds back into the per-block CRDT buffer via
// LiveEditBinding.
//
// Per INVARIANTS.md invariant 5, the test exercises the real
// production callsite (delegate's Keys.onPressed → KeyDispatch →
// LiveStructuralKeyHandler), not a synonym.

#include "QmlIntegrationFixture.h"

#include <QApplication>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestCtrlBackspaceBoundaryQml : public QObject {
    Q_OBJECT

private slots:
    void ctrl_backspace_in_block_word_deletes_via_text_edit_native_path();
    void ctrl_backspace_at_qtpos_zero_merges_blocks_like_plain_backspace();
    void ctrl_delete_at_end_of_block_merges_with_next_like_plain_delete();
};

void TestCtrlBackspaceBoundaryQml::ctrl_backspace_in_block_word_deletes_via_text_edit_native_path() {
    // Baseline: in-block Ctrl+Backspace must reach TextEdit's native
    // word-delete and the resulting delta must flow into the CRDT
    // buffer. Place the cursor immediately after "world" in
    // "hello world", press Ctrl+Backspace, expect "hello " (the word
    // "world" gone; whether the trailing space goes too is Qt-version-
    // dependent, so the assertion only requires "world" to be gone).
    QmlIntegrationFixture fx("hello world\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtEndOf(0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Backspace, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const QString row0 = fx.modelText(0);
    QVERIFY2(!row0.contains("world"),
             qPrintable(QString("expected 'world' to be word-deleted; "
                                "row 0 still reads: '%1'").arg(row0)));
    QVERIFY2(row0.startsWith("hello"),
             qPrintable(QString("expected 'hello' prefix to survive "
                                "word-delete; row 0: '%1'").arg(row0)));
    // Row count must not have changed — this is an in-block edit.
    QCOMPARE(fx.model()->rowCount(), 1);
}

void TestCtrlBackspaceBoundaryQml::ctrl_backspace_at_qtpos_zero_merges_blocks_like_plain_backspace() {
    // The L9 boundary case. Cursor at start of block 2, Ctrl held,
    // Backspace pressed. Expected: blocks 1 and 2 merge into a single
    // block (Ctrl modifier silently dropped). Same outcome as plain
    // Backspace at qtPos=0.
    QmlIntegrationFixture fx("para one\n\npara two\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Backspace, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    // The block-merge collapses two rows into one.
    QCOMPARE(fx.model()->rowCount(), 1);
    // Merged content is the two paragraphs concatenated.
    QCOMPARE(fx.modelText(0), QStringLiteral("para onepara two"));
}

void TestCtrlBackspaceBoundaryQml::ctrl_delete_at_end_of_block_merges_with_next_like_plain_delete() {
    // Symmetric forward case. Cursor at end of block 1, Ctrl held,
    // Delete pressed. Expected: blocks 1 and 2 merge.
    QmlIntegrationFixture fx("para one\n\npara two\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtEndOf(/*row=*/0);
    QTest::qWait(20);

    QTest::keyClick(fx.window(), Qt::Key_Delete, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    QCOMPARE(fx.model()->rowCount(), 1);
    QCOMPARE(fx.modelText(0), QStringLiteral("para onepara two"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCtrlBackspaceBoundaryQml)
#include "tst_live_render_ctrl_backspace_boundary_qml.moc"
