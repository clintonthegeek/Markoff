// SPDX-License-Identifier: GPL-3.0-or-later
//
// T3 — structural keys: split and merge (exit E2).
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click to place the caret, drive
// real QTest::keyClicks. StructuralKeyHandler is the authority; this test
// checks the leaf routes to it correctly and lands the caret it returns.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasStructural : public QObject {
    Q_OBJECT

private slots:
    void enter_splits_backspace_merges_caret_at_join();
};

void TstCanvasStructural::enter_splits_backspace_merges_caret_at_join()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId firstBlock = doc.iterateBlocks().front();

    // Click right after "Hello" (byte 5) to place the caret mid-paragraph.
    const QRectF rect = view.blockRect(firstBlock);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 30, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), firstBlock);
    const int splitByte = view.caretByteOffset();
    QVERIFY(splitByte > 0);
    QVERIFY(splitByte < doc.blockText(firstBlock).size());

    QTest::keyClick(&view, Qt::Key_Return);

    const auto blocksAfterSplit = doc.iterateBlocks();
    QCOMPARE(blocksAfterSplit.size(), size_t(2));
    QCOMPARE(doc.blockText(blocksAfterSplit[0]), QByteArray("Hello"));
    QCOMPARE(doc.blockText(blocksAfterSplit[1]), QByteArray(" world."));

    // Caret lands at byte 0 of the new (second) block.
    QCOMPARE(view.caretBlock(), blocksAfterSplit[1]);
    QCOMPARE(view.caretByteOffset(), 0);

    // Backspace at byte 0 merges back into the previous block; caret lands
    // at the join byte (end of what used to be the first block).
    QTest::keyClick(&view, Qt::Key_Backspace);

    const auto blocksAfterMerge = doc.iterateBlocks();
    QCOMPARE(blocksAfterMerge.size(), size_t(1));
    QCOMPARE(doc.blockText(blocksAfterMerge[0]), QByteArray("Hello world."));
    QCOMPARE(view.caretBlock(), blocksAfterMerge[0]);
    QCOMPARE(view.caretByteOffset(), splitByte);
}

QTEST_MAIN(TstCanvasStructural)
#include "tst_canvas_structural.moc"
