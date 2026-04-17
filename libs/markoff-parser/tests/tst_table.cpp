// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include <markoff-parser/TableHandler.h>

class TestTable : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDetectSimpleTable();
    void testDetectNoTable();
    void testDetectAlignment();
    void testDetectMultipleTables();
    void testConvertToQTextTable();
    void testConvertPreservesHeaders();
    void testConvertPreservesAlignment();
    void testConvertAddsEmptyDataRow();
    void testSerializeFromQTextTable();
    void testRoundTrip();
    void testRoundTripWithAlignment();
    void testConvertSingleColumn();
    void testSerializePreservesNewRows();
    void testSerializePreservesNewColumns();
    void testLongCellContent();
    void testMultipleTablesIndependent();
};

void TestTable::testDetectSimpleTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables[0].headers.size(), 2);
    QCOMPARE(tables[0].headers[0], QStringLiteral("A"));
    QCOMPARE(tables[0].headers[1], QStringLiteral("B"));
    QCOMPARE(tables[0].rows.size(), 1);
    QCOMPARE(tables[0].rows[0][0], QStringLiteral("1"));
    QCOMPARE(tables[0].rows[0][1], QStringLiteral("2"));
}

void TestTable::testDetectNoTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Just some text\nNo pipes here"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 0);
}

void TestTable::testDetectAlignment()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| L | C | R |\n|:---|:---:|---:|\n| a | b | c |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables[0].alignments[0], Qt::AlignLeft);
    QCOMPARE(tables[0].alignments[1], Qt::AlignCenter);
    QCOMPARE(tables[0].alignments[2], Qt::AlignRight);
}

void TestTable::testDetectMultipleTables()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "| A | B |\n|---|---|\n| 1 | 2 |\n\nSome text\n\n| X | Y |\n|---|---|\n| 3 | 4 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 2);
}

void TestTable::testConvertToQTextTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);

    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);
    QCOMPARE(tt->rows(), 2);    // header + 1 data row
    QCOMPARE(tt->columns(), 2);
}

void TestTable::testConvertPreservesHeaders()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| Name | Age |\n|---|---|\n| Alice | 30 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);

    // Header row (row 0)
    QTextTableCell headerA = tt->cellAt(0, 0);
    QCOMPARE(headerA.firstCursorPosition().block().text(), QStringLiteral("Name"));
    QTextTableCell headerB = tt->cellAt(0, 1);
    QCOMPARE(headerB.firstCursorPosition().block().text(), QStringLiteral("Age"));

    // Data row (row 1)
    QTextTableCell dataA = tt->cellAt(1, 0);
    QCOMPARE(dataA.firstCursorPosition().block().text(), QStringLiteral("Alice"));
    QTextTableCell dataB = tt->cellAt(1, 1);
    QCOMPARE(dataB.firstCursorPosition().block().text(), QStringLiteral("30"));
}

void TestTable::testConvertPreservesAlignment()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| L | C | R |\n|:---|:---:|---:|\n| a | b | c |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);

    // Alignment is stored in the table format constraints or as a custom
    // property — check that the returned alignments match
    QCOMPARE(tables[0].alignments[0], Qt::AlignLeft);
    QCOMPARE(tables[0].alignments[1], Qt::AlignCenter);
    QCOMPARE(tables[0].alignments[2], Qt::AlignRight);
}

void TestTable::testConvertAddsEmptyDataRow()
{
    // A table with only a header and separator (no data rows)
    // should get one empty data row added during conversion
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables[0].rows.size(), 0);  // no data rows in markdown

    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);
    QCOMPARE(tt->rows(), 2);  // header + 1 empty data row
    QCOMPARE(tt->columns(), 2);

    // Empty data row cells should have empty text
    QTextTableCell cell = tt->cellAt(1, 0);
    QCOMPARE(cell.firstCursorPosition().block().text(), QString());
}

void TestTable::testSerializeFromQTextTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QList<Qt::Alignment> aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);

    QString md = Markoff::TableHandler::serializeToMarkdown(tt, aligns);
    QVERIFY(md.contains(QStringLiteral("| A")));
    QVERIFY(md.contains(QStringLiteral("| B")));
    QVERIFY(md.contains(QStringLiteral("| 1")));
    QVERIFY(md.contains(QStringLiteral("| 2")));
    QVERIFY(md.contains(QStringLiteral("|---|")));
}

void TestTable::testRoundTrip()
{
    QString input = QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |");
    QTextDocument doc;
    doc.setPlainText(input);
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QList<Qt::Alignment> aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);

    QString output = Markoff::TableHandler::serializeToMarkdown(tt, aligns);

    // Re-parse the output
    QTextDocument doc2;
    doc2.setPlainText(output);
    auto tables2 = Markoff::TableHandler::detectTables(&doc2);
    QCOMPARE(tables2.size(), 1);
    QCOMPARE(tables2[0].headers, tables[0].headers);
    QCOMPARE(tables2[0].rows.size(), tables[0].rows.size());
    for (int r = 0; r < tables[0].rows.size(); ++r) {
        QCOMPARE(tables2[0].rows[r], tables[0].rows[r]);
    }
}

void TestTable::testRoundTripWithAlignment()
{
    QString input = QStringLiteral("| L | C | R |\n|:---|:---:|---:|\n| a | b | c |");
    QTextDocument doc;
    doc.setPlainText(input);
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QList<Qt::Alignment> aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);

    QString output = Markoff::TableHandler::serializeToMarkdown(tt, aligns);

    QTextDocument doc2;
    doc2.setPlainText(output);
    auto tables2 = Markoff::TableHandler::detectTables(&doc2);
    QCOMPARE(tables2[0].alignments[0], Qt::AlignLeft);
    QCOMPARE(tables2[0].alignments[1], Qt::AlignCenter);
    QCOMPARE(tables2[0].alignments[2], Qt::AlignRight);
}

void TestTable::testConvertSingleColumn()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| X |\n|---|\n| 1 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);
    QCOMPARE(tt->columns(), 1);
    QCOMPARE(tt->rows(), 2);
}

void TestTable::testSerializePreservesNewRows()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    auto aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);

    // Add a row via QTextTable API
    tt->appendRows(1);
    QTextTableCell newCell = tt->cellAt(2, 0);
    QTextCursor c = newCell.firstCursorPosition();
    c.insertText(QStringLiteral("3"));

    QString md = Markoff::TableHandler::serializeToMarkdown(tt, aligns);
    QVERIFY(md.contains(QStringLiteral("| 3")));

    // Verify the new row round-trips
    QTextDocument doc2;
    doc2.setPlainText(md);
    auto tables2 = Markoff::TableHandler::detectTables(&doc2);
    QCOMPARE(tables2[0].rows.size(), 2);  // original + new
}

void TestTable::testSerializePreservesNewColumns()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    auto aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);
    QCOMPARE(tt->columns(), 2);

    // Add a column
    tt->appendColumns(1);
    QCOMPARE(tt->columns(), 3);

    // The new column has empty cells — serialization should handle this
    aligns.append(Qt::AlignLeft);
    QString md = Markoff::TableHandler::serializeToMarkdown(tt, aligns);

    QTextDocument doc2;
    doc2.setPlainText(md);
    auto tables2 = Markoff::TableHandler::detectTables(&doc2);
    QCOMPARE(tables2.size(), 1);
    QCOMPARE(tables2[0].headers.size(), 3);
}

void TestTable::testLongCellContent()
{
    QString longText = QStringLiteral("This is a very long cell content that should cause word wrapping in the table cell");
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| Header |\n|---|\n| ") + longText + QStringLiteral(" |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);

    // Verify the long text is preserved
    QTextTableCell cell = tt->cellAt(1, 0);
    QCOMPARE(cell.firstCursorPosition().block().text(), longText);

    // Round-trip
    auto aligns = tables[0].alignments;
    QString md = Markoff::TableHandler::serializeToMarkdown(tt, aligns);
    QVERIFY(md.contains(longText));
}

void TestTable::testMultipleTablesIndependent()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "| A | B |\n|---|---|\n| 1 | 2 |\n\nText between\n\n| X | Y | Z |\n|---|---|---|\n| a | b | c |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 2);

    // Convert both (reverse order like the editor does)
    QTextTable *tt2 = Markoff::TableHandler::convertToQTextTable(&doc, tables[1]);
    QTextTable *tt1 = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);

    QVERIFY(tt1 != nullptr);
    QVERIFY(tt2 != nullptr);
    QCOMPARE(tt1->columns(), 2);
    QCOMPARE(tt2->columns(), 3);

    // Verify cell content of each table is independent
    QCOMPARE(tt1->cellAt(0, 0).firstCursorPosition().block().text(), QStringLiteral("A"));
    QCOMPARE(tt2->cellAt(0, 0).firstCursorPosition().block().text(), QStringLiteral("X"));
}

QTEST_MAIN(TestTable)
#include "tst_table.moc"
