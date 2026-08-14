// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.2 — table row/col ops + alignment.
//
// One test each for: insert row above/below, delete row (+ header guard),
// insert column left/right, delete column (+ last-column guard), the
// alignment tri-state write, and the "one transaction per op" contract
// (a single undoD2() fully reverses whichever op ran, regardless of how
// many lines it touched).

#include <QTest>

#include <functional>
#include <vector>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::TableAlign;
using Markoff::Canvas::View;

namespace {

const QByteArray kBaseTable =
    "| h0 | h1 |\n"
    "| --- | --- |\n"
    "| a0 | a1 |\n"
    "| b0 | b1 |\n";

BlockId findTable(const Markoff::MarkoffDocument &doc)
{
    for (const BlockId id : doc.iterateBlocks()) {
        if (doc.blockKind(id) == BlockKind::Table)
            return id;
    }
    return {};
}

/// Clicks the (row, col) cell's center and asserts the caret landed there —
/// same click-to-place technique tst_canvas_table_wrap_nav.cpp uses.
void clickCell(View &view, BlockId tableId, int row, int col)
{
    const QRectF r = view.tableCellRect(tableId, row, col);
    QVERIFY(!r.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(int(r.center().x()), int(r.center().y())));
    QCOMPARE(view.caretBlock(), tableId);
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(*view.caretTableCell(), std::make_pair(row, col));
}

}  // namespace

class TstCanvasTableOps : public QObject {
    Q_OBJECT

private slots:
    void insert_row_above_and_below();
    void delete_row_and_header_guard();
    void insert_column_left_and_right();
    void delete_column_and_last_column_guard();
    void alignment_tri_state_writes_delimiter_row();
    void each_op_is_one_undoable_transaction();
};

void TstCanvasTableOps::insert_row_above_and_below()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kBaseTable);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());
    const int newlinesBefore = doc.blockText(tableId).count('\n');

    // Insert below row 1 ("a0"/"a1") — lands a new empty row between the
    // "a" row and the "b" row.
    clickCell(view, tableId, 1, 0);
    view.insertTableRowBelow();
    view.viewport()->grab();  // force a realize pass, same as P5.1's tests

    const QByteArray afterBelow = doc.blockText(tableId);
    QVERIFY(afterBelow.contains("a0"));
    QVERIFY(afterBelow.contains("b0"));
    // The new row must sit between "a0" and "b0", not after "b0".
    QVERIFY(afterBelow.indexOf("a0") < afterBelow.indexOf("|  |  |"));
    QVERIFY(afterBelow.indexOf("|  |  |") < afterBelow.indexOf("b0"));
    QCOMPARE(afterBelow.count('\n'), newlinesBefore + 1);
    // Caret stays logically in the "a" row (row 1), which insertBelow does
    // not shift.
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(view.caretTableCell()->first, 1);

    // Insert above row 1 (now still the "a" row) — lands directly after the
    // delimiter row, before "a0".
    clickCell(view, tableId, 1, 0);
    view.insertTableRowAbove();
    view.viewport()->grab();

    const QByteArray afterAbove = doc.blockText(tableId);
    QVERIFY(afterAbove.indexOf("|  |  |") < afterAbove.indexOf("a0"));
    QCOMPARE(afterAbove.count('\n'), afterBelow.count('\n') + 1);
    // insertAbove shifts the caret's own row down by one (the new row is
    // now row 1, "a0" is row 2).
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(view.caretTableCell()->first, 2);

    // Inserting above the header (row 0) is a no-op — nothing sensible
    // sits "above" it.
    clickCell(view, tableId, 0, 0);
    const QByteArray beforeHeaderAbove = doc.blockText(tableId);
    view.insertTableRowAbove();
    QCOMPARE(doc.blockText(tableId), beforeHeaderAbove);
}

void TstCanvasTableOps::delete_row_and_header_guard()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kBaseTable);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());
    const int newlinesBefore = doc.blockText(tableId).count('\n');

    // Deleting the header (row 0) is a no-op.
    clickCell(view, tableId, 0, 0);
    const QByteArray beforeHeaderDelete = doc.blockText(tableId);
    view.deleteTableRow();
    QCOMPARE(doc.blockText(tableId), beforeHeaderDelete);

    // Delete the "a" row (row 1) — "b0"/"b1" survive, "a0"/"a1" don't.
    clickCell(view, tableId, 1, 0);
    view.deleteTableRow();
    view.viewport()->grab();

    const QByteArray afterDelete = doc.blockText(tableId);
    QVERIFY(!afterDelete.contains("a0"));
    QVERIFY(!afterDelete.contains("a1"));
    QVERIFY(afterDelete.contains("b0"));
    QVERIFY(afterDelete.contains("h0"));
    QCOMPARE(afterDelete.count('\n'), newlinesBefore - 1);
}

void TstCanvasTableOps::insert_column_left_and_right()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kBaseTable);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());

    // Insert left of column 0 — every existing column shifts right by one;
    // the delimiter row grows a matching new "---" cell too.
    clickCell(view, tableId, 1, 0);
    view.insertTableColumnLeft();
    view.viewport()->grab();

    QByteArray afterLeft = doc.blockText(tableId);
    // Every original row still has its own two cells, plus one new empty
    // one — 3 pipes' worth of cells per line now (4 pipes).
    for (const QByteArray &line : afterLeft.split('\n')) {
        if (line.isEmpty())
            continue;
        QCOMPARE(line.count('|'), 4);
    }
    QVERIFY(afterLeft.contains("h0"));
    QVERIFY(afterLeft.contains("---"));
    // Caret moved with its column: was col 0, now col 1 (new empty column
    // took its old slot).
    QVERIFY(view.caretTableCell().has_value());
    QCOMPARE(view.caretTableCell()->second, 1);

    // Insert right of the (now rightmost) original column — appends a
    // fourth column at the end.
    clickCell(view, tableId, 1, 2);  // "a1", now at col 2
    view.insertTableColumnRight();
    view.viewport()->grab();

    const QByteArray afterRight = doc.blockText(tableId);
    for (const QByteArray &line : afterRight.split('\n')) {
        if (line.isEmpty())
            continue;
        QCOMPARE(line.count('|'), 5);
    }
    QCOMPARE(view.caretTableCell()->second, 2);  // unchanged by insert-right
}

void TstCanvasTableOps::delete_column_and_last_column_guard()
{
    Markoff::MarkoffDocument doc;
    // A 3-column table so deleting one still leaves a valid 2-column table.
    doc.loadFromMarkdown(
        "| h0 | h1 | h2 |\n"
        "| --- | --- | --- |\n"
        "| a0 | a1 | a2 |\n");
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());

    clickCell(view, tableId, 1, 1);  // "a1"
    view.deleteTableColumn();
    view.viewport()->grab();

    const QByteArray afterDelete = doc.blockText(tableId);
    QVERIFY(afterDelete.contains("a0"));
    QVERIFY(!afterDelete.contains("a1"));
    QVERIFY(afterDelete.contains("a2"));
    for (const QByteArray &line : afterDelete.split('\n')) {
        if (line.isEmpty())
            continue;
        QCOMPARE(line.count('|'), 3);  // 2 columns == 3 pipes
    }

    // Guard: the table's last surviving column can't be deleted. Drive the
    // now-2-column table down to a single column first.
    clickCell(view, tableId, 1, 0);
    view.deleteTableColumn();
    view.viewport()->grab();

    for (const QByteArray &line : doc.blockText(tableId).split('\n')) {
        if (line.isEmpty())
            continue;
        QCOMPARE(line.count('|'), 2);  // 1 column == 2 pipes
    }

    clickCell(view, tableId, 1, 0);
    const QByteArray beforeGuard = doc.blockText(tableId);
    view.deleteTableColumn();
    QCOMPARE(doc.blockText(tableId), beforeGuard);
}

void TstCanvasTableOps::alignment_tri_state_writes_delimiter_row()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(kBaseTable);
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId tableId = findTable(doc);
    QVERIFY(!tableId.isNull());

    auto delimiterLine = [&]() -> QByteArray {
        const auto lines = doc.blockText(tableId).split('\n');
        return lines.size() > 1 ? lines.at(1) : QByteArray();
    };

    clickCell(view, tableId, 1, 0);  // column 0

    view.setTableColumnAlignment(TableAlign::Left);
    view.viewport()->grab();
    QVERIFY(delimiterLine().contains(":---"));
    QVERIFY(!delimiterLine().contains(":---:"));
    QVERIFY(view.caretTableContext().has_value());
    QCOMPARE(int(view.caretTableContext()->columnAlign), int(TableAlign::Left));

    view.setTableColumnAlignment(TableAlign::Center);
    view.viewport()->grab();
    QVERIFY(delimiterLine().contains(":---:"));
    QCOMPARE(int(view.caretTableContext()->columnAlign), int(TableAlign::Center));

    view.setTableColumnAlignment(TableAlign::Right);
    view.viewport()->grab();
    QVERIFY(delimiterLine().contains("---:"));
    QVERIFY(!delimiterLine().contains(":---"));
    QCOMPARE(int(view.caretTableContext()->columnAlign), int(TableAlign::Right));

    view.setTableColumnAlignment(TableAlign::None);
    view.viewport()->grab();
    QVERIFY(!delimiterLine().split('|').at(1).contains(':'));
    QCOMPARE(int(view.caretTableContext()->columnAlign), int(TableAlign::None));

    // Column 1 is untouched by any of the above.
    QVERIFY(view.caretTableContext().has_value());
}

void TstCanvasTableOps::each_op_is_one_undoable_transaction()
{
    // Every op below must be exactly one undoD2() away from the table's
    // pre-op bytes — however many lines it touched internally.
    struct Case {
        const char *name;
        std::function<void(View &, BlockId)> run;
    };

    const std::vector<Case> cases = {
        {"insertRowBelow", [](View &v, BlockId) { v.insertTableRowBelow(); }},
        {"insertRowAbove", [](View &v, BlockId) { v.insertTableRowAbove(); }},
        {"deleteRow",      [](View &v, BlockId) { v.deleteTableRow(); }},
        {"insertColumnLeft",  [](View &v, BlockId) { v.insertTableColumnLeft(); }},
        {"insertColumnRight", [](View &v, BlockId) { v.insertTableColumnRight(); }},
        {"setAlignment", [](View &v, BlockId) {
             v.setTableColumnAlignment(TableAlign::Center);
         }},
    };

    for (const Case &c : cases) {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(kBaseTable);
        View view;
        view.resize(500, 400);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const BlockId tableId = findTable(doc);
        QVERIFY(!tableId.isNull());
        clickCell(view, tableId, 1, 0);

        const QByteArray before = doc.blockText(tableId);
        c.run(view, tableId);
        view.viewport()->grab();
        QVERIFY2(doc.blockText(tableId) != before, c.name);

        doc.undoD2();
        QCOMPARE(doc.blockText(tableId), before);
    }

    // deleteColumn separately (needs a 3-column table so it isn't a no-op).
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown(
            "| h0 | h1 | h2 |\n"
            "| --- | --- | --- |\n"
            "| a0 | a1 | a2 |\n");
        View view;
        view.resize(500, 400);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const BlockId tableId = findTable(doc);
        clickCell(view, tableId, 1, 1);

        const QByteArray before = doc.blockText(tableId);
        view.deleteTableColumn();
        view.viewport()->grab();
        QVERIFY(doc.blockText(tableId) != before);

        doc.undoD2();
        QCOMPARE(doc.blockText(tableId), before);
    }

    // insertTable (ActionId::InsertTable) inserts a whole new block — its
    // "one transaction" is d2InsertBlock + d2ApplyBufferEdit undone
    // together, i.e. the new block must be gone after a single undo.
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown("just a paragraph.\n");
        View view;
        view.resize(500, 400);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        const auto blocksBefore = doc.iterateBlocks();
        QCOMPARE(blocksBefore.size(), size_t(1));
        view.setCaretPosition(blocksBefore[0], 0);

        view.insertTable();
        QVERIFY(!findTable(doc).isNull());

        doc.undoD2();
        QVERIFY(findTable(doc).isNull());
        QCOMPARE(doc.iterateBlocks().size(), size_t(1));
    }
}

QTEST_MAIN(TstCanvasTableOps)
#include "tst_canvas_table_ops.moc"
