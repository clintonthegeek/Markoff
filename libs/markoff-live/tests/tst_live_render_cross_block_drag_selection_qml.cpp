// SPDX-License-Identifier: GPL-3.0-or-later
//
// Falsifiable invariant test for the cross-block selection regression
// logged in docs/queue.md Discipline Log 2026-05-21.
//
// The production callsite is `LiveView.qml` `MouseArea.onPositionChanged`,
// which calls `binding.cursorState.extend(blockIndex, qtPos)` during a
// mouse drag. The same `extend` is reached via Shift+arrow through
// `LiveNavigationController::applyMotion`. After extend() runs and the
// `selectionChanged` emission has been fully dispatched (all delegates'
// `applySelection` slots have fired), `m_cursor` MUST report the drag
// destination as the active end. Today it does not — the delegate-side
// `moveCursorSelection` round-trips through `onCursorPositionChanged`
// and clobbers `m_cursor` back to the FIRST delegate to render the
// selection range. See queue.md entry for the asymmetry diagnosis.
//
// Invariant: `activeBlock(),activeQtPos()` after `extend(targetRow,
// targetQtPos)` must equal `(targetRow, targetQtPos)`, regardless of
// whether the drag is downward or upward.

#include "QmlIntegrationFixture.h"

#include <markoff/live/LiveCursorState.h>

#include <QApplication>
#include <QClipboard>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestCrossBlockDragSelectionQml : public QObject {
    Q_OBJECT

    LiveCursorState *cursorStateOf(QmlIntegrationFixture &fx) {
        return qobject_cast<LiveCursorState *>(
            fx.binding()->property("cursorState").value<QObject *>());
    }

    // Reads the visible selection range painted on the TextEdit of the
    // delegate at `row`. Returns {selectionStart, selectionEnd}; when the
    // delegate has no visible selection both are equal (collapsed caret).
    QPair<int, int> visibleSelectionFor(QmlIntegrationFixture &fx, int row) {
        QQuickItem *te = fx.delegateTextEdit(row);
        if (!te) return {-1, -1};
        return { te->property("selectionStart").toInt(),
                 te->property("selectionEnd").toInt() };
    }

    // Returns window-coordinate centre of the delegate at `row`.
    QPoint windowCentreOfDelegate(QmlIntegrationFixture &fx, int row) {
        QQuickItem *d = fx.delegateAt(row);
        if (!d) return {};
        QQuickItem *lv = fx.listView();
        QVariant contentItemVar = lv ? lv->property("contentItem") : QVariant{};
        QQuickItem *contentItem = contentItemVar.value<QQuickItem *>();
        const qreal offsetX = contentItem ? contentItem->x() : 0.0;
        const qreal offsetY = contentItem ? contentItem->y() : 0.0;
        const int cx = static_cast<int>(d->x() + d->width() / 2 + offsetX);
        const int cy = static_cast<int>(d->y() + d->height() / 2 + offsetY);
        return QPoint(cx, cy);
    }

    // Returns a window-coordinate point that is inside the actual text
    // glyphs of the delegate at `row` (uses the TextEdit's positionAt to
    // map qtPos→x). Avoids clicks that land in the trailing whitespace
    // beyond the rendered text, where positionAt collapses to end-of-text.
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
        // positionToRectangle returns coords relative to the TextEdit's
        // text origin; the TextEdit itself sits at (0,0) inside the
        // delegate after padding. The delegate's positionAt() walks back
        // by leftPadding/topPadding, so we add them here.
        const int cx = static_cast<int>(d->x() + leftPad + rect.center().x() + offsetX);
        const int cy = static_cast<int>(d->y() + topPad  + rect.center().y() + offsetY);
        return QPoint(cx, cy);
    }

private slots:
    void drag_down_active_end_lands_in_lower_block();
    void drag_up_active_end_lands_in_upper_block();
    void drag_down_visible_selection_spans_both_blocks();
    void drag_up_visible_selection_spans_both_blocks();
    void real_mouse_drag_down_spans_both_blocks();
    void real_mouse_drag_up_spans_both_blocks();
    void copy_after_three_block_drag_carries_all_three_blocks();
    void paste_of_three_block_clipboard_inserts_all_three_blocks();
    void ctrl_c_after_three_block_drag_copies_all_three_blocks();
};

void TestCrossBlockDragSelectionQml::drag_down_active_end_lands_in_lower_block() {
    // Two paragraph blocks. Drag from middle of "Alpha" down to middle of
    // "Beta". Active end of the selection must end up in row 1.
    QmlIntegrationFixture fx("Alpha paragraph\n\nBeta paragraph\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    // Simulate MouseArea.onPressed: begin selection at (row 0, qtPos 3).
    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/3);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 3));
    QCoreApplication::processEvents();

    // Simulate MouseArea.onPositionChanged drag-down: extend to (row 1, qtPos 4).
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 4));
    QCoreApplication::processEvents();

    // Active end must be at (row 1, qtPos 4); anchor at (row 0, qtPos 3).
    int activeBlock = -1, activeQtPos = -1, anchorBlock = -1, anchorQtPos = -1;
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "activeQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeQtPos));
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPos));

    QCOMPARE(anchorBlock, 0);
    QCOMPARE(anchorQtPos, 3);
    QCOMPARE(activeBlock, 1);
    QCOMPARE(activeQtPos, 4);
    QVERIFY(cs->hasSelection());
}

void TestCrossBlockDragSelectionQml::drag_up_active_end_lands_in_upper_block() {
    // Two paragraph blocks. Drag from middle of "Beta" up to middle of
    // "Alpha". Active end of the selection must end up in row 0.
    QmlIntegrationFixture fx("Alpha paragraph\n\nBeta paragraph\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/4);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 4));
    QCoreApplication::processEvents();

    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 3));
    QCoreApplication::processEvents();

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
    QCOMPARE(anchorQtPos, 4);
    QCOMPARE(activeBlock, 0);
    QCOMPARE(activeQtPos, 3);
    QVERIFY(cs->hasSelection());
}

void TestCrossBlockDragSelectionQml::drag_down_visible_selection_spans_both_blocks() {
    // Two blocks ("Alpha paragraph" / "Beta paragraph"). After begin(0,3) +
    // extend(1,4) the upper block must have a visible selection from qtPos 3
    // to its end, AND the lower block must have a visible selection from
    // qtPos 0 to 4. If either delegate paints only a collapsed caret, the
    // user sees a broken selection (the regression).
    QmlIntegrationFixture fx("Alpha paragraph\n\nBeta paragraph\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    fx.placeCursorAtPos(/*row=*/0, /*qtPos=*/3);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 3));
    QCoreApplication::processEvents();
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 4));
    QCoreApplication::processEvents();

    const QString textA = fx.modelText(0);   // "Alpha paragraph"
    const auto rangeA = visibleSelectionFor(fx, 0);
    const auto rangeB = visibleSelectionFor(fx, 1);

    // Upper block: selected from qtPos 3 to its end.
    QCOMPARE(rangeA.first,  3);
    QCOMPARE(rangeA.second, textA.length());
    // Lower block: selected from qtPos 0 to qtPos 4 ("Beta").
    QCOMPARE(rangeB.first,  0);
    QCOMPARE(rangeB.second, 4);
}

void TestCrossBlockDragSelectionQml::drag_up_visible_selection_spans_both_blocks() {
    QmlIntegrationFixture fx("Alpha paragraph\n\nBeta paragraph\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    fx.placeCursorAtPos(/*row=*/1, /*qtPos=*/4);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 1, 2000);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, 4));
    QCoreApplication::processEvents();
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 3));
    QCoreApplication::processEvents();

    const QString textA = fx.modelText(0);
    const auto rangeA = visibleSelectionFor(fx, 0);
    const auto rangeB = visibleSelectionFor(fx, 1);

    // Upper block: selected from qtPos 3 to end (anchor is in lower block,
    // active is at qtPos 3 of upper).
    QCOMPARE(rangeA.first,  3);
    QCOMPARE(rangeA.second, textA.length());
    // Lower block: selected from qtPos 0 to qtPos 4.
    QCOMPARE(rangeB.first,  0);
    QCOMPARE(rangeB.second, 4);
}

void TestCrossBlockDragSelectionQml::real_mouse_drag_down_spans_both_blocks() {
    // The PRODUCTION callsite (invariant #5): drive a real mouse
    // press + intermediate moves + release on the QQuickWindow. The
    // MouseArea in LiveView.qml routes these to cursorState.begin /
    // .extend. After release, both delegates must show a visible
    // selection range, and the cursor state must report the lower
    // block as the active end.
    QmlIntegrationFixture fx("Alpha paragraph here\n\nBeta paragraph here\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    // Click at qtPos 5 inside row 0's text and drag to qtPos 4 inside row 1.
    const QPoint startPt = windowPointInTextAt(fx, 0, 5);
    const QPoint endPt   = windowPointInTextAt(fx, 1, 4);
    QVERIFY(!startPt.isNull());
    QVERIFY(!endPt.isNull());

    // Move into the window without a button so we start with a known
    // cursor position; then press-drag-release.
    QTest::mouseMove(fx.window(), startPt);
    QCoreApplication::processEvents();

    QTest::mousePress(fx.window(), Qt::LeftButton, Qt::NoModifier, startPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    // After press, anchor must be at row 0 (begin called).
    int anchorAfterPress = -1, anchorQtPosAfterPress = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorAfterPress));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPosAfterPress));
    qInfo() << "[probe] after press anchorBlock:" << anchorAfterPress
            << "anchorQtPos:" << anchorQtPosAfterPress;

    // Intermediate drag steps so onPositionChanged fires at several
    // y-coordinates between startPt and endPt.
    const int steps = 6;
    for (int i = 1; i <= steps; ++i) {
        const int x = startPt.x() + (endPt.x() - startPt.x()) * i / steps;
        const int y = startPt.y() + (endPt.y() - startPt.y()) * i / steps;
        QTest::mouseMove(fx.window(), QPoint(x, y));
        QCoreApplication::processEvents();
        QTest::qWait(10);
        int activeNow = -1;
        QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, activeNow));
        const auto rA = visibleSelectionFor(fx, 0);
        const auto rB = visibleSelectionFor(fx, 1);
        QQuickItem *teA = fx.delegateTextEdit(0);
        QQuickItem *teB = fx.delegateTextEdit(1);
        const int curA = teA ? teA->property("cursorPosition").toInt() : -2;
        const int curB = teB ? teB->property("cursorPosition").toInt() : -2;
        qInfo() << "[probe] step" << i << "y=" << y
                << "active:" << activeNow
                << "rowA-sel=[" << rA.first << rA.second << "] cur=" << curA
                << "rowB-sel=[" << rB.first << rB.second << "] cur=" << curB;
    }

    QTest::mouseRelease(fx.window(), Qt::LeftButton, Qt::NoModifier, endPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    int activeBlock = -1, anchorBlock = -1;
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    qInfo() << "[probe] FINAL anchor=" << anchorBlock << "active=" << activeBlock;
    QCOMPARE(anchorBlock, 0);
    QCOMPARE(activeBlock, 1);
    QVERIFY(cs->hasSelection());

    // Both blocks must show a non-empty visible selection.
    const auto rangeA = visibleSelectionFor(fx, 0);
    const auto rangeB = visibleSelectionFor(fx, 1);
    QVERIFY2(rangeA.second > rangeA.first,
             qPrintable(QString("upper block has no visible selection: [%1, %2]")
                            .arg(rangeA.first).arg(rangeA.second)));
    QVERIFY2(rangeB.second > rangeB.first,
             qPrintable(QString("lower block has no visible selection: [%1, %2]")
                            .arg(rangeB.first).arg(rangeB.second)));
}

void TestCrossBlockDragSelectionQml::real_mouse_drag_up_spans_both_blocks() {
    QmlIntegrationFixture fx("Alpha paragraph here\n\nBeta paragraph here\n",
                             /*expectedRowCount=*/2);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    const QPoint startPt = windowPointInTextAt(fx, 1, 4);
    const QPoint endPt   = windowPointInTextAt(fx, 0, 5);
    QVERIFY(!startPt.isNull());
    QVERIFY(!endPt.isNull());

    QTest::mouseMove(fx.window(), startPt);
    QCoreApplication::processEvents();

    QTest::mousePress(fx.window(), Qt::LeftButton, Qt::NoModifier, startPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const int steps = 6;
    for (int i = 1; i <= steps; ++i) {
        const int x = startPt.x() + (endPt.x() - startPt.x()) * i / steps;
        const int y = startPt.y() + (endPt.y() - startPt.y()) * i / steps;
        QTest::mouseMove(fx.window(), QPoint(x, y));
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }

    QTest::mouseRelease(fx.window(), Qt::LeftButton, Qt::NoModifier, endPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    int activeBlock = -1, anchorBlock = -1;
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QCOMPARE(anchorBlock, 1);
    QCOMPARE(activeBlock, 0);
    QVERIFY(cs->hasSelection());

    const auto rangeA = visibleSelectionFor(fx, 0);
    const auto rangeB = visibleSelectionFor(fx, 1);
    QVERIFY2(rangeA.second > rangeA.first,
             qPrintable(QString("upper block has no visible selection: [%1, %2]")
                            .arg(rangeA.first).arg(rangeA.second)));
    QVERIFY2(rangeB.second > rangeB.first,
             qPrintable(QString("lower block has no visible selection: [%1, %2]")
                            .arg(rangeB.first).arg(rangeB.second)));
}

void TestCrossBlockDragSelectionQml::copy_after_three_block_drag_carries_all_three_blocks() {
    // User dogfood 2026-05-21: drag-select across three blocks, copy, paste
    // produces only the last block. Pin the regression: after a three-block
    // drag selection, the clipboard's plain-text payload must contain text
    // from all three blocks (the spans the user visibly selected).
    QmlIntegrationFixture fx(
        "First paragraph here.\n\nSecond paragraph here.\n\nThird paragraph here.\n",
        /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    const QPoint startPt = windowPointInTextAt(fx, 0, 6);   // mid of "First"
    const QPoint endPt   = windowPointInTextAt(fx, 2, 5);   // "Third" → after 5 chars
    QVERIFY(!startPt.isNull());
    QVERIFY(!endPt.isNull());

    QTest::mouseMove(fx.window(), startPt);
    QCoreApplication::processEvents();

    QTest::mousePress(fx.window(), Qt::LeftButton, Qt::NoModifier, startPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const int steps = 8;
    for (int i = 1; i <= steps; ++i) {
        const int x = startPt.x() + (endPt.x() - startPt.x()) * i / steps;
        const int y = startPt.y() + (endPt.y() - startPt.y()) * i / steps;
        QTest::mouseMove(fx.window(), QPoint(x, y));
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }

    QTest::mouseRelease(fx.window(), Qt::LeftButton, Qt::NoModifier, endPt);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);

    int anchorBlock = -1, anchorQtPos = -1, activeBlock = -1, activeQtPos = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPos));
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "activeQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeQtPos));
    qInfo() << "[probe] FINAL anchor=("<<anchorBlock<<","<<anchorQtPos<<")"
            << "active=("<<activeBlock<<","<<activeQtPos<<")";

    // Inspect rangeForBlock for each row directly — this is what the
    // clipboard serializer iterates.
    for (int row = 0; row < 3; ++row) {
        QPoint r;
        QMetaObject::invokeMethod(cs, "rangeForBlock", Qt::DirectConnection,
                                  Q_RETURN_ARG(QPoint, r),
                                  Q_ARG(int, row));
        qInfo() << "[probe] rangeForBlock("<<row<<") =" << r;
    }

    // Trigger the clipboard controller's copy slot via the binding's property.
    QObject *clip = fx.binding()->property("clipboardController").value<QObject *>();
    QVERIFY(clip);
    QMetaObject::invokeMethod(clip, "copy", Qt::DirectConnection);
    QTest::qWait(20);

    const QString cb = QApplication::clipboard()->text();
    qInfo() << "[probe] clipboard text:" << cb;
    // Anchor at qtPos 6 of "First paragraph here." → "First " is NOT in
    // clipboard. The drag selection covers: block 0 from qtPos 6 to end,
    // block 1 in full, block 2 from start to qtPos 5.
    QVERIFY2(cb.contains("paragraph here."),
             qPrintable(QString("clipboard missing block 1 tail. Got: %1").arg(cb)));
    QVERIFY2(cb.contains("Second paragraph here."),
             qPrintable(QString("clipboard missing block 2 full. Got: %1").arg(cb)));
    QVERIFY2(cb.contains("Third"),
             qPrintable(QString("clipboard missing block 3 head. Got: %1").arg(cb)));
}

void TestCrossBlockDragSelectionQml::paste_of_three_block_clipboard_inserts_all_three_blocks() {
    // End-to-end roundtrip: drag-select 3 blocks, copy, click in a 4th block
    // to collapse selection, paste, and assert that the document now contains
    // the original 4 blocks PLUS three new blocks inserted at the click site.
    // User report 2026-05-21: "only the last block is pasted".
    QmlIntegrationFixture fx(
        "First paragraph here.\n\n"
        "Second paragraph here.\n\n"
        "Third paragraph here.\n\n"
        "Sink paragraph here.\n",
        /*expectedRowCount=*/4);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));
    QVERIFY(fx.waitForDelegateAt(3, 2000));

    // --- Drag-select rows 0..2 (mid-of-"First" → mid-of-"Third") ---
    const QPoint dragStart = windowPointInTextAt(fx, 0, 6);
    const QPoint dragEnd   = windowPointInTextAt(fx, 2, 5);
    QVERIFY(!dragStart.isNull());
    QVERIFY(!dragEnd.isNull());

    QTest::mouseMove(fx.window(), dragStart);
    QCoreApplication::processEvents();
    QTest::mousePress(fx.window(), Qt::LeftButton, Qt::NoModifier, dragStart);
    QCoreApplication::processEvents();
    QTest::qWait(30);
    for (int i = 1; i <= 8; ++i) {
        const int x = dragStart.x() + (dragEnd.x() - dragStart.x()) * i / 8;
        const int y = dragStart.y() + (dragEnd.y() - dragStart.y()) * i / 8;
        QTest::mouseMove(fx.window(), QPoint(x, y));
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }
    QTest::mouseRelease(fx.window(), Qt::LeftButton, Qt::NoModifier, dragEnd);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    // --- Copy ---
    QObject *clip = fx.binding()->property("clipboardController").value<QObject *>();
    QVERIFY(clip);
    QMetaObject::invokeMethod(clip, "copy", Qt::DirectConnection);
    QTest::qWait(20);
    const QString clipText = QApplication::clipboard()->text();
    qInfo() << "[probe] copied text:" << clipText;

    // --- Click in the 4th block ("Sink paragraph here.") to collapse selection ---
    const QPoint clickIn4th = windowPointInTextAt(fx, 3, 5);
    QVERIFY(!clickIn4th.isNull());
    // Step the cursor far away first so the click registers as a fresh position.
    QTest::mouseMove(fx.window(), QPoint(1, 1));
    QCoreApplication::processEvents();
    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::NoModifier, clickIn4th);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    auto *cs = cursorStateOf(fx);
    QVERIFY(cs);
    int anchorBlock = -1, anchorQtPos = -1, activeBlock = -1, activeQtPos = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QMetaObject::invokeMethod(cs, "anchorQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorQtPos));
    QMetaObject::invokeMethod(cs, "activeBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeBlock));
    QMetaObject::invokeMethod(cs, "activeQtPos", Qt::DirectConnection,
                              Q_RETURN_ARG(int, activeQtPos));
    qInfo() << "[probe] pre-paste anchor=("<<anchorBlock<<","<<anchorQtPos<<")"
            << "active=("<<activeBlock<<","<<activeQtPos<<")";

    // --- Paste ---
    QMetaObject::invokeMethod(clip, "paste", Qt::DirectConnection);
    QTest::qWait(50);
    QCoreApplication::processEvents();

    // --- Inspect the resulting document ---
    auto *model = fx.model();
    const int rowsAfter = model->rowCount();
    QStringList blockTexts;
    for (int r = 0; r < rowsAfter; ++r) {
        blockTexts << fx.modelText(r);
    }
    qInfo() << "[probe] post-paste rows:" << rowsAfter;
    for (int r = 0; r < rowsAfter; ++r)
        qInfo() << "[probe] row" << r << ":" << blockTexts.at(r);

    // We started with 4 blocks. We pasted a 3-block selection. The result
    // should contain BOTH the original first three blocks AND the three
    // newly-inserted blocks. The "Sink" block becomes split (head before
    // click + tail after, joined with pasted content). At minimum, the
    // result must contain "First paragraph here.", "Second paragraph here.",
    // AND the "paragraph here." tail of "Third" — in distinct rows.
    const QString joined = blockTexts.join("|");
    QVERIFY2(joined.contains("paragraph here."),
             qPrintable(QString("block 1 tail missing from doc. Got: %1").arg(joined)));
    QVERIFY2(joined.contains("Second paragraph here."),
             qPrintable(QString("block 2 full missing from doc. Got: %1").arg(joined)));
    QVERIFY2(joined.contains("Third"),
             qPrintable(QString("block 3 head missing from doc. Got: %1").arg(joined)));
    // Row count must have grown — pasting 3 blocks into a single block
    // splits that block + adds 2 blocks → at least 4+2=6 rows.
    QVERIFY2(rowsAfter >= 6,
             qPrintable(QString("expected >=6 rows after paste, got %1").arg(rowsAfter)));
}

void TestCrossBlockDragSelectionQml::ctrl_c_after_three_block_drag_copies_all_three_blocks() {
    // The production path the user is using: drag-select cross-block, then
    // hit Ctrl+C. The clipboard must contain ALL the spans visible in the
    // selection — not just the focused block's portion. Hypothesis: a
    // focused TextEdit may intercept Ctrl+C and copy only its internal
    // (within-block) selection, bypassing LiveClipboardController.
    QmlIntegrationFixture fx(
        "First paragraph here.\n\nSecond paragraph here.\n\nThird paragraph here.\n",
        /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    QVERIFY(fx.waitForDelegateAt(1, 2000));
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    const QPoint dragStart = windowPointInTextAt(fx, 0, 6);
    const QPoint dragEnd   = windowPointInTextAt(fx, 2, 5);
    QVERIFY(!dragStart.isNull());
    QVERIFY(!dragEnd.isNull());

    QTest::mouseMove(fx.window(), dragStart);
    QCoreApplication::processEvents();
    QTest::mousePress(fx.window(), Qt::LeftButton, Qt::NoModifier, dragStart);
    QCoreApplication::processEvents();
    QTest::qWait(30);
    for (int i = 1; i <= 8; ++i) {
        const int x = dragStart.x() + (dragEnd.x() - dragStart.x()) * i / 8;
        const int y = dragStart.y() + (dragEnd.y() - dragStart.y()) * i / 8;
        QTest::mouseMove(fx.window(), QPoint(x, y));
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }
    QTest::mouseRelease(fx.window(), Qt::LeftButton, Qt::NoModifier, dragEnd);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    // Probe which delegate has focus, and what its internal selection is.
    auto *te0 = fx.delegateTextEdit(0);
    auto *te1 = fx.delegateTextEdit(1);
    auto *te2 = fx.delegateTextEdit(2);
    qInfo() << "[probe] focus before Ctrl+C: row0="
            << (te0 ? te0->property("activeFocus").toBool() : false)
            << "row1=" << (te1 ? te1->property("activeFocus").toBool() : false)
            << "row2=" << (te2 ? te2->property("activeFocus").toBool() : false);
    for (int r = 0; r < 3; ++r) {
        auto te = fx.delegateTextEdit(r);
        if (!te) continue;
        qInfo() << "[probe] row" << r
                << "selectedText=" << te->property("selectedText").toString();
    }

    // Clear clipboard so we can detect what Ctrl+C writes.
    QApplication::clipboard()->clear();
    QTest::qWait(10);

    // Send REAL Ctrl+C key event to the window, simulating the user.
    QTest::keyClick(fx.window(), Qt::Key_C, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    const QString cb = QApplication::clipboard()->text();
    qInfo() << "[probe] clipboard after Ctrl+C:" << cb;

    QVERIFY2(cb.contains("paragraph here."),
             qPrintable(QString("Ctrl+C clipboard missing block 1 tail. Got: %1").arg(cb)));
    QVERIFY2(cb.contains("Second paragraph here."),
             qPrintable(QString("Ctrl+C clipboard missing block 2. Got: %1").arg(cb)));
    QVERIFY2(cb.contains("Third"),
             qPrintable(QString("Ctrl+C clipboard missing block 3 head. Got: %1").arg(cb)));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCrossBlockDragSelectionQml)
#include "tst_live_render_cross_block_drag_selection_qml.moc"
