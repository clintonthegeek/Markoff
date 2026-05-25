// SPDX-License-Identifier: GPL-3.0-or-later
//
// L1 from `docs/specs/2026-05-21-textedit-interface-audit.md`: when a
// cross-block selection is active and the user presses a mutating key
// (Backspace, Delete, Return, or a printable char), the selection must
// collapse cleanly via the document layer — not leave the unfocused
// blocks alone while TextEdit mutates only the focused block's
// within-block selection.

#include <QtTest/QtTest>
#include <QQuickItem>
#include <QQuickWindow>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveCursorState.h>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live;
using namespace Markoff::Live::Test;

namespace {

// Drag-select from (row0, qtPos0) to (row1, qtPos1) via the cursorState.
// Mirrors the proven setup pattern from tst_live_render_cross_block_drag_selection_qml.cpp.
void selectAcross(QmlIntegrationFixture &fx, int row0, int qtPos0,
                  int row1, int qtPos1)
{
    QVERIFY(fx.waitForDelegateAt(row0, 2000));
    QVERIFY(fx.waitForDelegateAt(row1, 2000));

    auto *cs = qobject_cast<LiveCursorState *>(
        fx.binding()->property("cursorState").value<QObject*>());
    QVERIFY(cs);

    fx.placeCursorAtPos(row0, qtPos0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), row0, 2000);

    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
        Q_ARG(int, row0), Q_ARG(int, qtPos0));
    QCoreApplication::processEvents();

    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
        Q_ARG(int, row1), Q_ARG(int, qtPos1));
    QCoreApplication::processEvents();

    QVERIFY2(cs->hasSelection(), "cross-block selection setup failed");
}

}  // namespace

class TestCrossBlockMutatingKey : public QObject {
    Q_OBJECT
private slots:
    void backspace_on_cross_block_selection_collapses() {
        QmlIntegrationFixture fx(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                 /*expectedRowCount=*/3);
        selectAcross(fx, /*row0=*/0, /*qtPos0=*/2,
                         /*row1=*/2, /*qtPos1=*/3);

        QTest::keyClick(fx.window(), Qt::Key_Backspace);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // The three blocks have collapsed: prefix "al" of row 0 + suffix
        // "ma" of row 2 joined into one block "alma".
        const auto ids = fx.document()->iterateBlocks();
        QCOMPARE(ids.size(), 1u);
        QCOMPARE(fx.document()->blockText(ids[0]), QByteArray("alma"));
    }

    void delete_on_cross_block_selection_collapses() {
        QmlIntegrationFixture fx(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                 /*expectedRowCount=*/3);
        selectAcross(fx, 0, 2, 2, 3);

        QTest::keyClick(fx.window(), Qt::Key_Delete);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        const auto ids = fx.document()->iterateBlocks();
        QCOMPARE(ids.size(), 1u);
        QCOMPARE(fx.document()->blockText(ids[0]), QByteArray("alma"));
    }

    void printable_char_on_cross_block_selection_replaces() {
        QmlIntegrationFixture fx(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                 /*expectedRowCount=*/3);
        selectAcross(fx, 0, 2, 2, 3);

        QTest::keyClick(fx.window(), 'X');
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // Selection collapses to "alma", then 'X' inserts at the collapse
        // point (qtPos 2 of the surviving block) → "alXma".
        const auto ids = fx.document()->iterateBlocks();
        QCOMPARE(ids.size(), 1u);
        QCOMPARE(fx.document()->blockText(ids[0]), QByteArray("alXma"));
    }

    void return_on_cross_block_selection_splits_at_collapse_point() {
        QmlIntegrationFixture fx(/*markdown=*/"alpha\n\nbeta\n\ngamma",
                                 /*expectedRowCount=*/3);
        selectAcross(fx, 0, 2, 2, 3);

        QTest::keyClick(fx.window(), Qt::Key_Return);
        QTest::qWait(30);
        QCoreApplication::processEvents();

        // Selection collapses to "alma", then Return splits at qtPos 2 →
        // two blocks "al" and "ma".
        const auto ids = fx.document()->iterateBlocks();
        QCOMPARE(ids.size(), 2u);
        QCOMPARE(fx.document()->blockText(ids[0]), QByteArray("al"));
        QCOMPARE(fx.document()->blockText(ids[1]), QByteArray("ma"));
    }
};

QTEST_MAIN(TestCrossBlockMutatingKey)
#include "tst_live_render_cross_block_mutating_key.moc"
