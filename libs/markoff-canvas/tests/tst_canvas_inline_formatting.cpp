// SPDX-License-Identifier: GPL-3.0-or-later
//
// T7 — inline spans + delimiter visibility (exit E7).
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click/arrow-key to place the
// caret, drive real QTest::keyClicks. No test-only render or edit entry
// point — View::isDelimiterHiddenAt() only inspects the layout state the
// production paint path already reads (spec §7, invariant 5).

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasInlineFormatting : public QObject {
    Q_OBJECT

private slots:
    void delimiter_visibility_follows_caret();
};

void TstCanvasInlineFormatting::delimiter_visibility_follows_caret()
{
    Markoff::MarkoffDocument doc;
    // Byte offsets: a=0, ' '=1, '*'=2, '*'=3, b=4, '*'=5, '*'=6, ' '=7, c=8.
    doc.loadFromMarkdown("a **b** c\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockText(block), QByteArray("a **b** c"));

    // Click at byte 0, then walk the caret to byte 0 exactly via Home —
    // the caret starts outside the "**b**" span either way.
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 0);
    QCOMPARE(view.realizedBlockCount(), 1);  // isDelimiterHiddenAt below is only
                                              // meaningful once the block has a
                                              // real layout to query.

    // Caret outside the span: both "**" delimiter runs are hidden.
    QVERIFY(view.isDelimiterHiddenAt(block, 2));
    QVERIFY(view.isDelimiterHiddenAt(block, 5));

    // Walk the caret into the span, one ASCII char per Right (1 QChar ==
    // 1 byte for this whole fixture, so byte offset == keypress count).
    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(view.caretByteOffset(), 4);  // right before 'b'

    // Caret inside the span: delimiters reveal.
    QVERIFY(!view.isDelimiterHiddenAt(block, 2));
    QVERIFY(!view.isDelimiterHiddenAt(block, 5));

    // Type while revealed; the buffer round-trips at the caret's byte
    // position like any other insert (T2's contract, unaffected by T7).
    QTest::keyClicks(&view, QStringLiteral("x"));
    QCOMPARE(doc.blockText(block), QByteArray("a **xb** c"));
    QCOMPARE(view.caretByteOffset(), 5);

    // Move the caret back out to byte 0: delimiters hide again.
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);
    QVERIFY(view.isDelimiterHiddenAt(block, 2));
}

QTEST_MAIN(TstCanvasInlineFormatting)
#include "tst_canvas_inline_formatting.moc"
