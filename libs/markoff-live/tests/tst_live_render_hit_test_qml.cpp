// SPDX-License-Identifier: GPL-3.0-or-later
//
// LiveView.qml hit() click-target invariant — clicks in the trailing
// whitespace of a delegate (vertically below the last visual line of
// text, or horizontally past the end of a wrapped visual line) must
// snap to the END OF THE VISUAL LINE under the click, not the END OF
// THE BLOCK.
//
// Existing workaround comment in
// `tst_live_render_cross_block_drag_selection_qml.cpp:67` already
// documents the symptom: "Avoids clicks that land in the trailing
// whitespace beyond the rendered text, where positionAt collapses to
// end-of-text." This test pins it.

#include "QmlIntegrationFixture.h"

#include <QApplication>
#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestHitTestQml : public QObject {
    Q_OBJECT

    // Calls the delegate's QML-side `positionAt(x, y)` function directly.
    int delegatePositionAt(QQuickItem *delegate, qreal x, qreal y) {
        QVariant ret;
        QMetaObject::invokeMethod(delegate, "positionAt",
                                  Qt::DirectConnection,
                                  Q_RETURN_ARG(QVariant, ret),
                                  Q_ARG(QVariant, x),
                                  Q_ARG(QVariant, y));
        return ret.toInt();
    }

private slots:
    void click_below_last_visual_line_returns_end_of_block_for_single_line();
    void click_right_of_short_first_line_in_wrapped_paragraph_returns_end_of_line();
    void click_below_text_in_trailing_delegate_whitespace_returns_end_of_block();
};

void TestHitTestQml::click_below_last_visual_line_returns_end_of_block_for_single_line() {
    // Baseline: single-line block, click at delegate's bottom-right corner.
    // For a single-line block, end-of-line == end-of-block, so this just
    // anchors the baseline and confirms hit() returns a sensible value.
    QmlIntegrationFixture fx("Short.\n", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    QQuickItem *d = fx.delegateAt(0);
    QVERIFY(d);

    // Bottom-right corner of the delegate (definitely past any text).
    const int pos = delegatePositionAt(d, d->width() - 1, d->height() - 1);
    QCOMPARE(pos, 6);  // length of "Short."
}

void TestHitTestQml::click_right_of_short_first_line_in_wrapped_paragraph_returns_end_of_line() {
    // The actual failure mode: a paragraph that wraps onto multiple
    // visual lines. A click at (right edge, middle-of-first-visual-line)
    // must land at the end of that visual line, not the end of the block.
    //
    // Window is 900px wide. The long sentence below is comfortably wider
    // than 900px at the default font size, so it will wrap to at least
    // two visual lines.
    const QByteArray longPara =
        "This is a paragraph long enough that with the default font and "
        "the 900-pixel test window it will wrap onto at least two visual "
        "lines for the hit-test invariant to be exercised meaningfully.\n";
    QmlIntegrationFixture fx(longPara, /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    QQuickItem *d  = fx.delegateAt(0);
    QQuickItem *te = fx.delegateTextEdit(0);
    QVERIFY(d && te);

    const int textLen = te->property("length").toInt();
    QVERIFY2(textLen > 0, "text edit reports empty length");

    // Confirm the paragraph actually wrapped by checking the cursor
    // rectangle of the last character vs the first character — different
    // y means we got at least two visual lines.
    QRectF rectFirst, rectLast;
    QMetaObject::invokeMethod(te, "positionToRectangle",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QRectF, rectFirst),
                              Q_ARG(int, 0));
    QMetaObject::invokeMethod(te, "positionToRectangle",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(QRectF, rectLast),
                              Q_ARG(int, textLen));
    QVERIFY2(rectLast.y() > rectFirst.y() + rectFirst.height() / 2,
             qPrintable(QString("paragraph did not wrap: first=%1,%2 last=%3,%4")
                            .arg(rectFirst.x()).arg(rectFirst.y())
                            .arg(rectLast.x()).arg(rectLast.y())));

    // Click at (right edge of delegate, middle of the FIRST visual line).
    // The first line ends with some word boundary — its end-of-line cursor
    // position is strictly less than textLen.
    const qreal topPadding = te->property("topPadding").toReal();
    const qreal yFirstLine = topPadding + rectFirst.center().y();

    const int pos = delegatePositionAt(d, d->width() - 1, yFirstLine);

    // The invariant: pos lands at the end of the first visual line, which
    // is strictly less than the full text length. If pos == textLen, we
    // hit the bug (collapse to end-of-text instead of end-of-line).
    QVERIFY2(pos > 0 && pos < textLen,
             qPrintable(QString("hit() collapsed to end-of-text: pos=%1, "
                                "textLen=%2 (expected end of first visual "
                                "line, somewhere in between)")
                            .arg(pos).arg(textLen)));
}

void TestHitTestQml::click_below_text_in_trailing_delegate_whitespace_returns_end_of_block() {
    // Click vertically below the last visual line of a wrapped paragraph.
    // The contract: lands at end-of-block (the last visual line's end is
    // the block end, so this is consistent with clicking on the last line
    // far-right). This documents the intended behavior for the L-shaped
    // bottom-right corner of any delegate.
    const QByteArray longPara =
        "This is a paragraph long enough that with the default font and "
        "the 900-pixel test window it will wrap onto at least two visual "
        "lines for the hit-test invariant to be exercised meaningfully.\n";
    QmlIntegrationFixture fx(longPara, /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    QQuickItem *d  = fx.delegateAt(0);
    QQuickItem *te = fx.delegateTextEdit(0);
    QVERIFY(d && te);

    const int textLen = te->property("length").toInt();

    // Bottom edge of delegate — well past the last visual line.
    const int pos = delegatePositionAt(d, d->width() / 2, d->height() - 1);
    QCOMPARE(pos, textLen);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestHitTestQml)
#include "tst_live_render_hit_test_qml.moc"
