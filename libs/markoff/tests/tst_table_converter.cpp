// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include <QTextFrame>
#include "TableConverter.h"

using Markoff::TableConverter;

class TestTableConverter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void convertBasicTable();
    void convertPreservesTextAround();
    void convertRespectsAlignment();
    void convertMultipleTables();
    void reconcileNoChange();
    void reconcileNewTable();
    void reconcileTableDeleted();
};

// Helper: build pipe-table text and a matching TableRegion.
static TableConverter::TableRegion makeRegion(
    int startPos, int endPos,
    const QStringList &headers,
    const QList<QStringList> &dataRows,
    const QList<Qt::Alignment> &alignments = {})
{
    TableConverter::TableRegion r;
    r.startPos = startPos;
    r.endPos = endPos;
    r.headers = headers;
    r.dataRows = dataRows;
    r.rows = 1 + dataRows.size(); // header + data rows
    r.cols = headers.size();
    r.alignments = alignments;
    return r;
}

// Helper: count QTextTable frames in the document.
static int countTables(QTextDocument *doc)
{
    int count = 0;
    const auto frames = doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        if (qobject_cast<QTextTable *>(frame))
            ++count;
    }
    return count;
}

// Helper: get the first QTextTable in the document.
static QTextTable *firstTable(QTextDocument *doc)
{
    const auto frames = doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        if (auto *table = qobject_cast<QTextTable *>(frame))
            return table;
    }
    return nullptr;
}

// Helper: read cell text from a QTextTable.
static QString cellText(QTextTable *table, int row, int col)
{
    QTextTableCell cell = table->cellAt(row, col);
    QTextCursor c = cell.firstCursorPosition();
    c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    return c.selectedText();
}

void TestTableConverter::convertBasicTable()
{
    // Set up document with pipe text.
    QTextDocument doc;
    const QString pipeText = QStringLiteral(
        "| Name  | Age |\n"
        "| ----- | --- |\n"
        "| Alice | 30  |\n"
        "| Bob   | 25  |");
    doc.setPlainText(pipeText);

    // Build region covering the entire document text.
    auto region = makeRegion(
        0, doc.characterCount() - 1,
        {QStringLiteral("Name"), QStringLiteral("Age")},
        {{QStringLiteral("Alice"), QStringLiteral("30")},
         {QStringLiteral("Bob"), QStringLiteral("25")}});

    TableConverter converter;
    converter.convert(&doc, {region});

    // Should have created one table.
    QCOMPARE(countTables(&doc), 1);

    auto *table = firstTable(&doc);
    QVERIFY(table);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 2);

    // Header cells.
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("Name"));
    QCOMPARE(cellText(table, 0, 1), QStringLiteral("Age"));

    // Data cells.
    QCOMPARE(cellText(table, 1, 0), QStringLiteral("Alice"));
    QCOMPARE(cellText(table, 1, 1), QStringLiteral("30"));
    QCOMPARE(cellText(table, 2, 0), QStringLiteral("Bob"));
    QCOMPARE(cellText(table, 2, 1), QStringLiteral("25"));

    // Record should be tracked.
    QCOMPARE(converter.records().size(), 1);
    QCOMPARE(converter.records()[0].table, table);
    QCOMPARE(converter.records()[0].rows, 3);
    QCOMPARE(converter.records()[0].cols, 2);
}

void TestTableConverter::convertPreservesTextAround()
{
    QTextDocument doc;
    const QString text = QStringLiteral(
        "Some text before the table.\n"
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |\n"
        "Some text after the table.");
    doc.setPlainText(text);

    // Find where the pipe text starts and ends.
    const QString plainText = doc.toPlainText();
    int start = plainText.indexOf(QLatin1Char('|'));
    int pipeEnd = plainText.lastIndexOf(QLatin1Char('|')) + 1;

    auto region = makeRegion(
        start, pipeEnd,
        {QStringLiteral("A"), QStringLiteral("B")},
        {{QStringLiteral("1"), QStringLiteral("2")}});

    TableConverter converter;
    converter.convert(&doc, {region});

    // Table should exist.
    QCOMPARE(countTables(&doc), 1);

    // Text before and after should survive.
    QString result = doc.toPlainText();
    QVERIFY(result.contains(QStringLiteral("Some text before")));
    QVERIFY(result.contains(QStringLiteral("Some text after")));

    // The raw pipe text should be gone.
    QVERIFY(!result.contains(QStringLiteral("| A | B |")));
}

void TestTableConverter::convertRespectsAlignment()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "| L | C | R |\n"
        "| :-- | :--: | --: |\n"
        "| a | b | c |"));

    QList<Qt::Alignment> aligns = {Qt::AlignLeft, Qt::AlignCenter, Qt::AlignRight};

    auto region = makeRegion(
        0, doc.characterCount() - 1,
        {QStringLiteral("L"), QStringLiteral("C"), QStringLiteral("R")},
        {{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}},
        aligns);

    TableConverter converter;
    converter.convert(&doc, {region});

    QCOMPARE(converter.records().size(), 1);
    const auto &rec = converter.records()[0];
    QCOMPARE(rec.alignments.size(), 3);
    QCOMPARE(rec.alignments[0], Qt::AlignLeft);
    QCOMPARE(rec.alignments[1], Qt::AlignCenter);
    QCOMPARE(rec.alignments[2], Qt::AlignRight);
}

void TestTableConverter::convertMultipleTables()
{
    QTextDocument doc;
    const QString text = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |\n"
        "\n"
        "Middle paragraph.\n"
        "\n"
        "| X | Y | Z |\n"
        "| - | - | - |\n"
        "| a | b | c |");
    doc.setPlainText(text);

    const QString plainText = doc.toPlainText();

    // First table: from start to end of "| 1 | 2 |"
    int firstStart = 0;
    // Find end of first table (end of "| 1 | 2 |")
    int firstTableLastPipe = plainText.indexOf(QStringLiteral("| 1 | 2 |"));
    int firstEnd = firstTableLastPipe + QString(QStringLiteral("| 1 | 2 |")).size();

    // Second table: starts at "| X | Y | Z |"
    int secondStart = plainText.indexOf(QStringLiteral("| X |"));
    int secondEnd = doc.characterCount() - 1;

    auto region1 = makeRegion(
        firstStart, firstEnd,
        {QStringLiteral("A"), QStringLiteral("B")},
        {{QStringLiteral("1"), QStringLiteral("2")}});

    auto region2 = makeRegion(
        secondStart, secondEnd,
        {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")},
        {{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}});

    TableConverter converter;
    converter.convert(&doc, {region1, region2});

    QCOMPARE(countTables(&doc), 2);
    QCOMPARE(converter.records().size(), 2);

    // First record should be the 2-col table, second the 3-col table.
    QCOMPARE(converter.records()[0].cols, 2);
    QCOMPARE(converter.records()[1].cols, 3);
}

void TestTableConverter::reconcileNoChange()
{
    // Create a document with a table already converted.
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |"));

    auto region = makeRegion(
        0, doc.characterCount() - 1,
        {QStringLiteral("A"), QStringLiteral("B")},
        {{QStringLiteral("1"), QStringLiteral("2")}});

    TableConverter converter;
    converter.convert(&doc, {region});

    QCOMPARE(converter.records().size(), 1);

    // Reconcile with no new regions — same tables still exist.
    bool changed = converter.reconcile(&doc, {});
    QVERIFY(!changed);
    QCOMPARE(converter.records().size(), 1);
}

void TestTableConverter::reconcileNewTable()
{
    // reconcile() only syncs records with existing document tables —
    // it never creates new tables (that is convert()'s job in the
    // loadMarkdown / structural-change paths). Passing new regions
    // to reconcile should not modify the document.
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Hello world"));

    TableConverter converter;
    QCOMPARE(converter.records().size(), 0);

    auto region = makeRegion(
        0, doc.characterCount() - 1,
        {QStringLiteral("Key"), QStringLiteral("Value")},
        {{QStringLiteral("foo"), QStringLiteral("bar")}});

    bool changed = converter.reconcile(&doc, {region});
    // No existing tables in doc, so records stay empty.
    QVERIFY(!changed);
    QCOMPARE(countTables(&doc), 0);
    QCOMPARE(converter.records().size(), 0);
}

void TestTableConverter::reconcileTableDeleted()
{
    // Convert a table, then remove it from the document.
    QTextDocument doc;
    doc.setPlainText(QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |"));

    auto region = makeRegion(
        0, doc.characterCount() - 1,
        {QStringLiteral("A"), QStringLiteral("B")},
        {{QStringLiteral("1"), QStringLiteral("2")}});

    TableConverter converter;
    converter.convert(&doc, {region});
    QCOMPARE(converter.records().size(), 1);
    QCOMPARE(countTables(&doc), 1);

    // Replace the entire document content, removing the table.
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    cursor.insertText(QStringLiteral("No more tables here."));

    // Sanity: table should be gone.
    QCOMPARE(countTables(&doc), 0);

    // Reconcile should detect the deletion.
    bool changed = converter.reconcile(&doc, {});
    QVERIFY(changed);
    QCOMPARE(converter.records().size(), 0);
}

QTEST_MAIN(TestTableConverter)
#include "tst_table_converter.moc"
