// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit L2 regression net — middle-click PRIMARY-selection paste must
// (a) be routed through LiveClipboardController (not handled natively
// by the focused TextEdit), (b) target the block under the cursor (not
// the focused block), and (c) read from QClipboard::Selection (the X11
// PRIMARY buffer) rather than the CLIPBOARD buffer.
//
// Spec: docs/specs/2026-05-21-audit-L2-middle-click-primary-paste.md.

#include "QmlIntegrationFixture.h"

#include <QApplication>
#include <QClipboard>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestMiddleClickPasteQml : public QObject {
    Q_OBJECT

    // Returns the window-coordinate centre of a position inside the
    // text glyphs of the delegate at `row` and `qtPos`. Reused pattern
    // from tst_live_render_cross_block_drag_selection_qml.
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

private slots:
    void middle_click_pastes_primary_selection_at_click_point();
    void middle_click_when_primary_unavailable_is_safe_noop();
    void middle_click_does_not_paste_into_focused_block_when_clicked_in_other_block();
};

void TestMiddleClickPasteQml::middle_click_pastes_primary_selection_at_click_point() {
    if (!QApplication::clipboard()->supportsSelection())
        QSKIP("PRIMARY selection not supported on this platform "
              "(offscreen QPA on the test runner). The companion "
              "no-op test exercises the safe path.");

    QmlIntegrationFixture fx("alpha\n\nbravo\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QApplication::clipboard()->setText(QStringLiteral("INJ"),
                                       QClipboard::Selection);
    QTest::qWait(10);

    // Click at qtPos 3 of row 1 (between 'a' and 'v' of "bravo").
    const QPoint clickPt = windowPointInTextAt(fx, /*row=*/1, /*qtPos=*/3);
    QVERIFY(!clickPt.isNull());

    QTest::mouseClick(fx.window(), Qt::MiddleButton, Qt::NoModifier, clickPt);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("braINJvo"));
}

void TestMiddleClickPasteQml::middle_click_when_primary_unavailable_is_safe_noop() {
    // On platforms where PRIMARY isn't supported (or where the buffer
    // is empty), middle-click must not crash and must not produce any
    // document change. The clipboardController.pastePrimary() guard
    // short-circuits.
    QmlIntegrationFixture fx("alpha\n\nbravo\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    // Best-effort: clear PRIMARY if supported so the paste has nothing
    // to insert even if the platform reports support.
    if (QApplication::clipboard()->supportsSelection())
        QApplication::clipboard()->setText(QString(), QClipboard::Selection);

    const QPoint clickPt = windowPointInTextAt(fx, /*row=*/1, /*qtPos=*/3);
    QVERIFY(!clickPt.isNull());

    QTest::mouseClick(fx.window(), Qt::MiddleButton, Qt::NoModifier, clickPt);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("bravo"));
    QCOMPARE(fx.model()->rowCount(), 2);
}

void TestMiddleClickPasteQml::middle_click_does_not_paste_into_focused_block_when_clicked_in_other_block() {
    // The user-visible payoff: focus block 0, middle-click in block 1,
    // paste must land in block 1 (paste-at-click), not block 0
    // (TextEdit's paste-at-focus default).
    if (!QApplication::clipboard()->supportsSelection())
        QSKIP("PRIMARY selection not supported on this platform");

    QmlIntegrationFixture fx("alpha\n\nbravo\n", /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    // Focus row 0 explicitly.
    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/0);
    QTest::qWait(20);

    QApplication::clipboard()->setText(QStringLiteral("X"),
                                       QClipboard::Selection);
    QTest::qWait(10);

    // Middle-click at end of row 1 ("bravo|").
    const QPoint clickPt = windowPointInTextAt(fx, /*row=*/1, /*qtPos=*/5);
    QVERIFY(!clickPt.isNull());

    QTest::mouseClick(fx.window(), Qt::MiddleButton, Qt::NoModifier, clickPt);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    QCOMPARE(fx.modelText(0), QStringLiteral("alpha"));
    QCOMPARE(fx.modelText(1), QStringLiteral("bravoX"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestMiddleClickPasteQml)
#include "tst_live_render_middle_click_paste_qml.moc"
