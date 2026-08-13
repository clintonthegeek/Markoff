// SPDX-License-Identifier: GPL-3.0-or-later
//
// T5 — selection + clipboard (exit E4).
//
// Real mouse press/move/release drives drag-selection in both directions
// (the 2026-05-21 asymmetry class: a naive implementation that always
// treats "anchor" as "selection start" gets the upward drag backwards).
// Ctrl+C is checked against the system clipboard; the collapse-on-type
// path is checked against blockText()/iterateBlocks(), same as the other
// exit-criterion tests in this suite.

#include <QClipboard>
#include <QGuiApplication>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasSelection : public QObject {
    Q_OBJECT

private slots:
    void drag_selection_both_directions_copy_and_collapse();
};

void TstCanvasSelection::drag_selection_both_directions_copy_and_collapse()
{
    const QByteArray src = "Alpha one.\n\nBeta two.\n\nGamma three.\n";

    // --- Downward drag: press in block 0, release in block 2. ------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(src);
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t(3));

        const QRectF r0 = view.blockRect(blocks[0]);
        const QRectF r2 = view.blockRect(blocks[2]);
        const QPoint pressPos(int(r0.x()) + 2, int(r0.y()) + 8);
        const QPoint releasePos(int(r2.x()) + int(r2.width()) - 2, int(r2.y()) + 8);

        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, pressPos);
        const int b0 = view.caretByteOffset();
        QCOMPARE(view.caretBlock(), blocks[0]);

        QTest::mouseMove(view.viewport(), releasePos);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, releasePos);

        QVERIFY(view.hasSelection());
        QCOMPARE(view.selectionAnchorBlock(), blocks[0]);
        QCOMPARE(view.selectionAnchorByteOffset(), b0);
        QCOMPARE(view.caretBlock(), blocks[2]);
        const int b2 = view.caretByteOffset();
        QVERIFY(b2 > 0);

        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        const QByteArray expected = doc.blockText(blocks[0]).mid(b0) + "\n\n"
                                   + doc.blockText(blocks[1]) + "\n\n"
                                   + doc.blockText(blocks[2]).left(b2);
        QCOMPARE(QGuiApplication::clipboard()->text().toUtf8(), expected);
    }

    // --- Upward drag: press in block 2, release in block 0. The 2026-05-21
    // asymmetry class — clipboard order must still be document order, not
    // press order. -----------------------------------------------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(src);
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const auto blocks = doc.iterateBlocks();
        const QRectF r0 = view.blockRect(blocks[0]);
        const QRectF r2 = view.blockRect(blocks[2]);
        const QPoint pressPos(int(r2.x()) + 2, int(r2.y()) + 8);
        const QPoint releasePos(int(r0.x()) + int(r0.width()) - 2, int(r0.y()) + 8);

        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, pressPos);
        const int b2 = view.caretByteOffset();
        QCOMPARE(view.caretBlock(), blocks[2]);

        QTest::mouseMove(view.viewport(), releasePos);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, releasePos);

        // Anchor is where the press happened (block 2); the caret is where
        // the drag ended (block 0) — but the selection/clipboard below must
        // still read in document order, start-to-end, not press-to-release.
        QVERIFY(view.hasSelection());
        QCOMPARE(view.selectionAnchorBlock(), blocks[2]);
        QCOMPARE(view.caretBlock(), blocks[0]);
        const int b0 = view.caretByteOffset();

        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        const QByteArray expected = doc.blockText(blocks[0]).mid(b0) + "\n\n"
                                   + doc.blockText(blocks[1]) + "\n\n"
                                   + doc.blockText(blocks[2]).left(b2);
        QCOMPARE(QGuiApplication::clipboard()->text().toUtf8(), expected);
    }

    // --- A printable key on the selection collapses it and inserts at the
    // first (document-order) corner, in one step. ------------------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(src);
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const auto blocks = doc.iterateBlocks();
        const QRectF r0 = view.blockRect(blocks[0]);
        const QRectF r2 = view.blockRect(blocks[2]);
        const QPoint pressPos(int(r0.x()) + 2, int(r0.y()) + 8);
        const QPoint releasePos(int(r2.x()) + int(r2.width()) - 2, int(r2.y()) + 8);

        const QByteArray head0 = doc.blockText(blocks[0]);
        const QByteArray tail2 = doc.blockText(blocks[2]);

        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, pressPos);
        const int b0 = view.caretByteOffset();
        QTest::mouseMove(view.viewport(), releasePos);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, releasePos);
        QVERIFY(view.hasSelection());
        const int b2 = view.caretByteOffset();

        QTest::keyClick(&view, Qt::Key_X);

        QVERIFY(!view.hasSelection());
        const auto blocksAfter = doc.iterateBlocks();
        QCOMPARE(blocksAfter.size(), size_t(1));
        const QByteArray expected = head0.left(b0) + "x" + tail2.mid(b2);
        QCOMPARE(doc.blockText(blocksAfter[0]), expected);
        QCOMPARE(view.caretBlock(), blocksAfter[0]);
        QCOMPARE(view.caretByteOffset(), b0 + 1);
    }
}

QTEST_MAIN(TstCanvasSelection)
#include "tst_canvas_selection.moc"
