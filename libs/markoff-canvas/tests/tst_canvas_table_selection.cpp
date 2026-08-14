// SPDX-License-Identifier: GPL-3.0-or-later
//
// P2.3 — table cells + cross-table selection (#18.2).
//
// A selection touching a Table block walks its row-major cell-ordered
// sequence, not raw bytes: Ctrl+C over a within-table drag serializes the
// covered cells pipe-separated, one row per line, trimmed of source
// padding — not a literal substring of the pipe-table markdown (which
// would include stray pipes/padding, and for a range crossing rows would
// even include the alignment-row leftovers). A selection that spans the
// whole table (as one of several selected blocks) serializes the whole
// grid the same way.

#include <QClipboard>
#include <QGuiApplication>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;

namespace {
const QByteArray kSrc =
    "before paragraph.\n"
    "\n"
    "| h0 | h1 |\n"
    "|----|----|\n"
    "| a0 | a1 |\n"
    "| b0 | b1 |\n"
    "\n"
    "after paragraph.\n";
}

class TstCanvasTableSelection : public QObject {
    Q_OBJECT

private slots:
    void within_table_drag_copies_covered_cells_row_major();
    void whole_table_selection_copies_full_grid();
};

void TstCanvasTableSelection::within_table_drag_copies_covered_cells_row_major()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kSrc);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    BlockId tableId;
    for (const BlockId id : doc.iterateBlocks()) {
        if (doc.blockKind(id) == BlockKind::Table)
            tableId = id;
    }
    QVERIFY(!tableId.isNull());

    // row 1 = "a0"/"a1" (first body row), row 2 = "b0"/"b1" (second body
    // row) — press in the "a0" cell, drag to "b1": covers all four body
    // cells, header row excluded.
    const QRectF r1c0 = view.tableCellRect(tableId, 1, 0);
    const QRectF r2c1 = view.tableCellRect(tableId, 2, 1);
    QVERIFY(!r1c0.isNull());
    QVERIFY(!r2c1.isNull());

    const QPoint pressPos(int(r1c0.center().x()), int(r1c0.center().y()));
    const QPoint releasePos(int(r2c1.center().x()), int(r2c1.center().y()));

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, pressPos);
    QCOMPARE(view.caretBlock(), tableId);
    QTest::mouseMove(view.viewport(), releasePos);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, releasePos);

    QVERIFY(view.hasSelection());
    QCOMPARE(view.caretBlock(), tableId);
    QCOMPARE(view.selectionAnchorBlock(), tableId);

    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text().toUtf8(),
             QByteArray("a0 | a1\nb0 | b1"));
}

void TstCanvasTableSelection::whole_table_selection_copies_full_grid()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kSrc);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(3));  // before / table / after
    const BlockId beforeId = blocks[0];
    const BlockId tableId  = blocks[1];
    const BlockId afterId  = blocks[2];
    QCOMPARE(doc.blockKind(tableId), BlockKind::Table);

    const QRectF rBefore = view.blockRect(beforeId);
    const QRectF rAfter  = view.blockRect(afterId);
    const QPoint pressPos(int(rBefore.x()) + 2, int(rBefore.y()) + 8);
    const QPoint releasePos(int(rAfter.x()) + int(rAfter.width()) - 2, int(rAfter.y()) + 8);

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, pressPos);
    const int bBefore = view.caretByteOffset();
    QTest::mouseMove(view.viewport(), releasePos);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, releasePos);

    QVERIFY(view.hasSelection());
    QCOMPARE(view.caretBlock(), afterId);
    const int bAfter = view.caretByteOffset();

    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
    const QByteArray expected = doc.blockText(beforeId).mid(bBefore) + "\n\n"
                               + "h0 | h1\na0 | a1\nb0 | b1" + "\n\n"
                               + doc.blockText(afterId).left(bAfter);
    QCOMPARE(QGuiApplication::clipboard()->text().toUtf8(), expected);
}

QTEST_MAIN(TstCanvasTableSelection)
#include "tst_canvas_table_selection.moc"
