// SPDX-License-Identifier: GPL-3.0-or-later
//
// T9 — minimal table (exit E8).
//
// Clicks into a body cell via View::tableCellRect (row 1 = first body row,
// col 1 = second column), types a character, and asserts the edit landed
// exactly at the click point in the table block's buffer — nothing else in
// that buffer moved — and that the block after the table is untouched.
// Also exercises a repaint after the edit (the QML-Repeater UAF class has
// no analogue here; the assertion is simply that this doesn't crash).

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;

class TstCanvasTable : public QObject {
    Q_OBJECT

private slots:
    void table_cell_edit_isolated();
};

void TstCanvasTable::table_cell_edit_isolated()
{
    const QByteArray src =
        "| h0 | h1 | h2 |\n"
        "|----|----|----|\n"
        "| a0 | a1 | a2 |\n"
        "| b0 | b1 | b2 |\n"
        "\n"
        "after paragraph.\n";

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    BlockId tableId, afterId;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (doc.blockKind(blocks[i]) == BlockKind::Table) {
            tableId = blocks[i];
            QVERIFY(i + 1 < blocks.size());
            afterId = blocks[i + 1];
        }
    }
    QVERIFY(!tableId.isNull());
    QVERIFY(!afterId.isNull());
    QCOMPARE(doc.blockText(afterId), QByteArray("after paragraph."));

    // row 1 = first body row ("a0"/"a1"/"a2"), col 1 = "a1".
    const QRectF cellRect = view.tableCellRect(tableId, 1, 1);
    QVERIFY(!cellRect.isNull());
    const QPoint clickPos(int(cellRect.center().x()), int(cellRect.center().y()));

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    QCOMPARE(view.caretBlock(), tableId);
    const int off = view.caretByteOffset();

    // The click must have landed inside the "a1" cell, not spilled into a
    // neighbor — the table source's only occurrence of "a1" is that cell.
    const QByteArray before = doc.blockText(tableId);
    const int a1Pos = before.indexOf("a1");
    QVERIFY(a1Pos >= 0);
    QVERIFY(off >= a1Pos && off <= a1Pos + 2);

    QTest::keyClick(&view, Qt::Key_X);

    const QByteArray expected = before.left(off) + "x" + before.mid(off);
    QCOMPARE(doc.blockText(tableId), expected);
    QCOMPARE(doc.blockText(afterId), QByteArray("after paragraph."));

    // Repaint after the edit: the assertion is that this does not crash.
    view.viewport()->grab();
}

QTEST_MAIN(TstCanvasTable)
#include "tst_canvas_table.moc"
