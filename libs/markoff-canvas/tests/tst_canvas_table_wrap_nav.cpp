// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.1 — table cell wrap + navigation.
//
// Three behaviors, one test each:
//  - cell_wrap_grows_row_height: a cell whose text exceeds the column's
//    width budget wraps to multiple lines, and the row's height tracks the
//    tallest wrapped cell in it (not a fixed single-line height).
//  - tab_navigates_cells_and_last_cell_appends_row: Tab/Shift+Tab hop
//    between cells in row-major order; Tab in the table's last cell appends
//    a new row (Obsidian behavior) rather than leaving the table.
//  - up_down_moves_within_wrapped_cell_then_rows_then_exits: Up/Down first
//    walks lines inside a wrapped cell, then rows in the same column once
//    at the cell's own top/bottom line, then exits the table into the
//    adjacent block once off its top/bottom row.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;

namespace {

BlockId findTable(const Markoff::MarkoffDocument &doc)
{
    for (const BlockId id : doc.iterateBlocks()) {
        if (doc.blockKind(id) == BlockKind::Table)
            return id;
    }
    return {};
}

}  // namespace

class TstCanvasTableWrapNav : public QObject {
    Q_OBJECT

private slots:
    void cell_wrap_grows_row_height();
    void tab_navigates_cells_and_last_cell_appends_row();
    void up_down_moves_within_wrapped_cell_then_rows_then_exits();
};

void TstCanvasTableWrapNav::cell_wrap_grows_row_height()
{
    // Row 1's second cell is short; row 2's second cell is long enough to
    // blow well past the 240px column-width cap and must wrap onto several
    // lines. Row 2 should end up noticeably taller than row 1.
    const QByteArray longCell =
        "this is a deliberately long run of table cell text intended to "
        "exceed the two hundred forty pixel column width cap by a wide "
        "margin so that it must wrap across several visual lines";
    const QByteArray src =
        "| h0 | h1 |\n"
        "|----|----|\n"
        "| a0 | short |\n"
        "| b0 | " + longCell + " |\n";

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());

    const QRectF shortRowCell = view.tableCellRect(tableId, 1, 1);
    const QRectF longRowCell  = view.tableCellRect(tableId, 2, 1);
    QVERIFY(!shortRowCell.isNull());
    QVERIFY(!longRowCell.isNull());

    // The wrapped cell's row must be taller than the single-line row —
    // "row height = max wrapped cell height" (P5.1 done-when) — and by
    // enough margin that this is clearly multiple wrapped lines, not
    // font-metrics noise.
    QVERIFY(longRowCell.height() > shortRowCell.height() * 1.8);
}

void TstCanvasTableWrapNav::tab_navigates_cells_and_last_cell_appends_row()
{
    const QByteArray src =
        "| h0 | h1 |\n"
        "|----|----|\n"
        "| a0 | a1 |\n"
        "| b0 | b1 |\n";

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());

    // Land in the header's first cell (row 0, col 0).
    const QRectF r0c0 = view.tableCellRect(tableId, 0, 0);
    QVERIFY(!r0c0.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(int(r0c0.center().x()), int(r0c0.center().y())));
    QCOMPARE(view.caretBlock(), tableId);
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 0));

    // Tab across the header row into the first body row.
    QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 1));
    QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(*view.caretTableCell(), std::make_pair(1, 0));
    QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(*view.caretTableCell(), std::make_pair(1, 1));

    // Shift+Tab walks back the same path.
    QTest::keyClick(&view, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(*view.caretTableCell(), std::make_pair(1, 0));

    // Shift+Tab at the very first cell is a no-op (still swallowed, no
    // crash, position unchanged).
    QTest::keyClick(&view, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 1));
    QTest::keyClick(&view, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 0));
    QTest::keyClick(&view, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 0));  // no-op

    // Drive to the table's last cell (row 2, col 1: "b1").
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(*view.caretTableCell(), std::make_pair(2, 1));

    const int newlinesBefore = doc.blockText(tableId).count('\n');

    // Obsidian behavior (P5.1 done-when): Tab in the last cell appends a
    // new row and lands in its first cell, rather than leaving the table.
    QTest::keyClick(&view, Qt::Key_Tab);
    QCOMPARE(view.caretBlock(), tableId);
    // The caret's byte offset was set straight off the freshly-edited
    // buffer (tryTableTab reads it via TableGeometry, not the cache — see
    // its comment), but the cache's own per-cell grid is still the
    // pre-edit one until the next realize pass; force that now (a real
    // paint, same as tst_canvas_table.cpp's post-edit grab()) before
    // resolving (row, col) through it.
    view.viewport()->grab();
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(*view.caretTableCell(), std::make_pair(3, 0));

    // appendTableRow() adds exactly one new line ("\n" + the row's pipes).
    QCOMPARE(doc.blockText(tableId).count('\n'), newlinesBefore + 1);
}

void TstCanvasTableWrapNav::up_down_moves_within_wrapped_cell_then_rows_then_exits()
{
    // A paragraph on each side of the table so Up/Down has somewhere to
    // land once it exits the table's top/bottom row.
    const QByteArray longCell =
        "this is a deliberately long run of table cell text intended to "
        "exceed the two hundred forty pixel column width cap by a wide "
        "margin so that it must wrap across several visual lines";
    const QByteArray src =
        "before paragraph.\n"
        "\n"
        "| h0 | h1 |\n"
        "|----|----|\n"
        "| a0 | " + longCell + " |\n"
        "| b0 | b1 |\n"
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
    QCOMPARE(blocks.size(), size_t(3));  // before / table / after
    const BlockId beforeId = blocks[0];
    const BlockId tableId  = blocks[1];
    const BlockId afterId  = blocks[2];
    QCOMPARE(doc.blockKind(tableId), BlockKind::Table);

    // Click near the BOTTOM of the wrapped cell (row 1, col 1) so the caret
    // starts on the cell's last visual line, not its first.
    const QRectF wrappedCell = view.tableCellRect(tableId, 1, 1);
    QVERIFY(!wrappedCell.isNull());
    // Sanity: this cell really did wrap (same shape as the first test).
    const QRectF singleLineCell = view.tableCellRect(tableId, 2, 1);
    QVERIFY(wrappedCell.height() > singleLineCell.height() * 1.8);

    const QPoint bottomOfCell(int(wrappedCell.center().x()), int(wrappedCell.bottom()) - 3);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, bottomOfCell);
    QCOMPARE(view.caretBlock(), tableId);
    QCOMPARE(*view.caretTableCell(), std::make_pair(1, 1));
    const int byteOnLastLine = view.caretByteOffset();

    // Step 1: Up from the cell's last wrapped line moves to an earlier line
    // in the SAME cell (same row/col), not a different row yet.
    QTest::keyClick(&view, Qt::Key_Up);
    QCOMPARE(view.caretBlock(), tableId);
    QCOMPARE(*view.caretTableCell(), std::make_pair(1, 1));
    QVERIFY(view.caretByteOffset() < byteOnLastLine);

    // Step 2: keep pressing Up until it leaves this cell's own wrap and
    // lands one row up, same column (the header row).
    int guard = 0;
    while (view.caretTableCell() && view.caretTableCell()->first == 1 && guard < 20) {
        QTest::keyClick(&view, Qt::Key_Up);
        ++guard;
    }
    QCOMPARE(view.caretBlock(), tableId);
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(view.caretTableCell()->first, 0);
    QCOMPARE(view.caretTableCell()->second, 1);

    // Step 3: one more Up exits the table entirely, landing in the block
    // above it.
    QTest::keyClick(&view, Qt::Key_Up);
    QCOMPARE(view.caretBlock(), beforeId);

    // Now drive Down from the header back through the table and off the
    // bottom, into the block after it.
    const QRectF r0c1 = view.tableCellRect(tableId, 0, 1);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(int(r0c1.center().x()), int(r0c1.center().y())));
    QCOMPARE(view.caretBlock(), tableId);
    QCOMPARE(*view.caretTableCell(), std::make_pair(0, 1));

    guard = 0;
    while (view.caretBlock() == tableId && guard < 40) {
        QTest::keyClick(&view, Qt::Key_Down);
        ++guard;
    }
    QCOMPARE(view.caretBlock(), afterId);
}

QTEST_MAIN(TstCanvasTableWrapNav)
#include "tst_canvas_table_wrap_nav.moc"
