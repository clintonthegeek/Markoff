// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit L3 regression net — drag-drop of external text must route
// through LiveClipboardController::pasteText (not Qt's native TextEdit
// drop handler). The DropArea overlay in LiveView.qml hit-tests the
// drop point, moves the cursor there, and inserts via applyFlatEdit.
//
// Spec: docs/specs/2026-05-21-audit-L3-drag-and-drop-text.md.
//
// Drag-drop synthesis uses QDropEvent constructed against a QMimeData
// carrying text/plain. The event is sent to the QQuickWindow; QtQuick's
// scene-graph routing delivers it to the DropArea overlay.

#include "QmlIntegrationFixture.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestDropTextQml : public QObject {
    Q_OBJECT

    // Window-coordinate position inside the text glyphs of (row, qtPos).
    QPoint windowPointInTextAt(QmlIntegrationFixture &fx, int row, int qtPos) {
        QQuickItem *d  = fx.delegateAt(row);
        QQuickItem *te = fx.delegateTextEdit(row);
        if (!d || !te) return {};
        QQuickItem *lv = fx.listView();
        QVariant contentItemVar = lv ? lv->property("contentItem") : QVariant{};
        QQuickItem *contentItem = contentItemVar.value<QQuickItem *>();
        const qreal offsetX = contentItem ? contentItem->x() : 0.0;
        const qreal offsetY = contentItem ? contentItem->y() : 0.0;

        QRectF rect;
        QMetaObject::invokeMethod(te, "positionToRectangle",
                                  Qt::DirectConnection,
                                  Q_RETURN_ARG(QRectF, rect),
                                  Q_ARG(int, qtPos));
        const qreal leftPad = te->property("leftPadding").toReal();
        const qreal topPad  = te->property("topPadding").toReal();
        const int cx = static_cast<int>(d->x() + leftPad + rect.center().x() + offsetX);
        const int cy = static_cast<int>(d->y() + topPad  + rect.center().y() + offsetY);
        return QPoint(cx, cy);
    }

    // Synthesize a drag-enter + drop sequence on the window at `pos`
    // carrying `text` as text/plain. The QMimeData lifetime must outlast
    // both events (Qt copies the pointer, not the contents); we keep it
    // on the stack and let it die after sendEvent returns.
    void simulateTextDrop(QWindow *window, const QPoint &pos, const QString &text) {
        QMimeData mime;
        mime.setText(text);

        QDragEnterEvent enter(pos, Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QCoreApplication::processEvents();

        QDragMoveEvent move(pos, Qt::CopyAction, &mime,
                            Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &move);
        QCoreApplication::processEvents();

        QDropEvent drop(pos, Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QCoreApplication::processEvents();
    }

private slots:
    void drop_text_inserts_at_drop_point_in_target_block();
    void drop_text_into_different_block_than_focused_lands_at_drop_point();
    void drop_collapses_active_cross_block_selection_to_drop_point();
};

void TestDropTextQml::drop_text_inserts_at_drop_point_in_target_block() {
    QmlIntegrationFixture fx("alpha\n\nbravo\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    const QPoint dropPt = windowPointInTextAt(fx, /*row=*/1, /*qtPos=*/3);
    QVERIFY(!dropPt.isNull());

    simulateTextDrop(fx.window(), dropPt, QStringLiteral("DROP"));
    QTest::qWait(50);

    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("braDROPvo"));
    QCOMPARE(fx.model()->rowCount(), 2);
}

void TestDropTextQml::drop_text_into_different_block_than_focused_lands_at_drop_point() {
    // Focus row 0 explicitly; drop happens in row 1; insert must land in
    // row 1, not row 0. This is the user-visible payoff — pre-fix,
    // TextEdit's native drop handler would have inserted into the
    // focused TextEdit (row 0) regardless of the drop position.
    QmlIntegrationFixture fx("alpha\n\nbravo\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/0);
    QTest::qWait(20);

    const QPoint dropPt = windowPointInTextAt(fx, /*row=*/1, /*qtPos=*/5);
    QVERIFY(!dropPt.isNull());

    simulateTextDrop(fx.window(), dropPt, QStringLiteral("X"));
    QTest::qWait(50);

    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("bravoX"));
}

void TestDropTextQml::drop_collapses_active_cross_block_selection_to_drop_point() {
    // An active cross-block selection must be collapsed by the drop
    // (begin() moves the cursor + clears anchor), and the paste lands
    // at the drop point, not at the selection range. This documents
    // that the drop is "drop-at-point" — selection is treated as the
    // pre-drop state of the user's cursor, not a target for replacement.
    QmlIntegrationFixture fx("alpha\n\nbravo\n\ncharlie\n",
                             /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    // Drag-select from row 0 to row 1 by invoking the cursor state.
    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/0);
    QTest::qWait(20);
    auto *cs = fx.binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 5));
    QTest::qWait(20);

    // Drop happens in row 2, unrelated to the selection range.
    const QPoint dropPt = windowPointInTextAt(fx, /*row=*/2, /*qtPos=*/4);
    QVERIFY(!dropPt.isNull());

    simulateTextDrop(fx.window(), dropPt, QStringLiteral("Z"));
    QTest::qWait(50);

    // Rows 0 and 1 must be unchanged (selection was NOT replaced by drop).
    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("bravo"));
    // Row 2 has the dropped text inserted at qtPos 4.
    QCOMPARE(fx.modelText(2), QStringLiteral("charZlie"));
    QCOMPARE(fx.model()->rowCount(), 3);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestDropTextQml)
#include "tst_live_render_drop_text_qml.moc"
