// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include "TableSerializer.h"

class TestTableSerializer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void serializeBasic2x2();
    void serializeAlignmentMarkers();
    void serializeAutoFormatsPadding();
    void serializeMinimumWidth();
    void serializeEmptyCells();
    void serializeSingleColumn();
    void extractAlignments();
};

// Helper: create a QTextTable and fill cells from a 2D list.
static QTextTable *makeTable(QTextDocument *doc,
                             const QList<QStringList> &rows)
{
    if (rows.isEmpty()) return nullptr;
    int rowCount = rows.size();
    int colCount = rows[0].size();

    QTextCursor cursor(doc);
    QTextTable *table = cursor.insertTable(rowCount, colCount);
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < colCount; ++c) {
            if (c < rows[r].size()) {
                QTextCursor cell = table->cellAt(r, c).firstCursorPosition();
                cell.insertText(rows[r][c]);
            }
        }
    }
    return table;
}

void TestTableSerializer::serializeBasic2x2()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("Name"), QStringLiteral("Age")},
        {QStringLiteral("Alice"), QStringLiteral("30")},
    });
    QVERIFY(table);

    QString result = Markoff::TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);

    // Header row
    QVERIFY(lines[0].startsWith(QLatin1Char('|')));
    QVERIFY(lines[0].endsWith(QLatin1Char('|')));
    QVERIFY(lines[0].contains(QStringLiteral("Name")));
    QVERIFY(lines[0].contains(QStringLiteral("Age")));

    // Separator row
    QVERIFY(lines[1].contains(QStringLiteral("---")));

    // Data row
    QVERIFY(lines[2].contains(QStringLiteral("Alice")));
    QVERIFY(lines[2].contains(QStringLiteral("30")));
}

void TestTableSerializer::serializeAlignmentMarkers()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("L"), QStringLiteral("C"), QStringLiteral("R")},
        {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
    });
    QVERIFY(table);

    QList<Qt::Alignment> aligns = {Qt::AlignLeft, Qt::AlignCenter, Qt::AlignRight};
    QString result = Markoff::TableSerializer::serialize(table, aligns);
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);

    // Separator row should contain alignment markers
    QString sep = lines[1];
    QStringList sepCells;
    // Split on | and trim
    const QStringList parts = sep.split(QLatin1Char('|'));
    for (const QString &p : parts) {
        QString trimmed = p.trimmed();
        if (!trimmed.isEmpty())
            sepCells.append(trimmed);
    }
    QCOMPARE(sepCells.size(), 3);

    // Left: starts with :, does not end with :
    QVERIFY(sepCells[0].startsWith(QLatin1Char(':')));
    QVERIFY(!sepCells[0].endsWith(QLatin1Char(':')));

    // Center: starts and ends with :
    QVERIFY(sepCells[1].startsWith(QLatin1Char(':')));
    QVERIFY(sepCells[1].endsWith(QLatin1Char(':')));

    // Right: does not start with :, ends with :
    QVERIFY(!sepCells[2].startsWith(QLatin1Char(':')));
    QVERIFY(sepCells[2].endsWith(QLatin1Char(':')));
}

void TestTableSerializer::serializeAutoFormatsPadding()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("Name"), QStringLiteral("Age")},
        {QStringLiteral("Bobbert"), QStringLiteral("42")},
    });
    QVERIFY(table);

    QString result = Markoff::TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));

    // All lines should have the same length (auto-padded)
    QVERIFY(lines.size() >= 3);
    int len0 = lines[0].size();
    for (const QString &line : lines) {
        QCOMPARE(line.size(), len0);
    }

    // "Name" should be padded to match "Bobbert" width
    // Header cell should have trailing spaces
    // Split first line to get cell content
    QString headerName;
    const QStringList parts = lines[0].split(QLatin1Char('|'));
    for (const QString &p : parts) {
        if (p.trimmed() == QStringLiteral("Name")) {
            headerName = p;
            break;
        }
    }
    QVERIFY(!headerName.isEmpty());
    // "Name" is 4 chars, "Bobbert" is 7 chars — cell width should accommodate "Bobbert"
    // Cell content: " Name   " (space + content + padding + space)
    // At minimum the cell should be wider than " Name "
    QVERIFY(headerName.size() > 6); // " Name " is 6
}

void TestTableSerializer::serializeMinimumWidth()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A"), QStringLiteral("B")},
        {QStringLiteral("x"), QStringLiteral("y")},
    });
    QVERIFY(table);

    QString result = Markoff::TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));
    QVERIFY(lines.size() >= 2);

    // Separator row should have at least 3 dashes per column
    QString sep = lines[1];
    const QStringList parts = sep.split(QLatin1Char('|'));
    for (const QString &p : parts) {
        QString trimmed = p.trimmed();
        if (trimmed.isEmpty()) continue;
        // Count actual dash characters
        int dashes = 0;
        for (QChar ch : trimmed) {
            if (ch == QLatin1Char('-')) dashes++;
        }
        QVERIFY2(dashes >= 3,
                 qPrintable(QStringLiteral("Separator cell '%1' has only %2 dashes")
                            .arg(trimmed).arg(dashes)));
    }
}

void TestTableSerializer::serializeEmptyCells()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("Key"), QStringLiteral("Value")},
        {QStringLiteral("foo"), QString()},
        {QString(), QStringLiteral("bar")},
    });
    QVERIFY(table);

    QString result = Markoff::TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 4); // header + sep + 2 data rows

    // Each line should be valid pipe markdown (start/end with |)
    for (const QString &line : lines) {
        QVERIFY2(line.startsWith(QLatin1Char('|')),
                 qPrintable(QStringLiteral("Line does not start with |: %1").arg(line)));
        QVERIFY2(line.endsWith(QLatin1Char('|')),
                 qPrintable(QStringLiteral("Line does not end with |: %1").arg(line)));
    }

    // All lines same length
    int len = lines[0].size();
    for (const QString &line : lines) {
        QCOMPARE(line.size(), len);
    }
}

void TestTableSerializer::serializeSingleColumn()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("Item")},
        {QStringLiteral("Apple")},
        {QStringLiteral("Banana")},
    });
    QVERIFY(table);

    QString result = Markoff::TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 4); // header + sep + 2 data

    // Each line should have exactly 2 pipe characters (| content |)
    for (const QString &line : lines) {
        int pipes = 0;
        for (QChar ch : line) {
            if (ch == QLatin1Char('|')) pipes++;
        }
        QCOMPARE(pipes, 2);
    }
}

void TestTableSerializer::extractAlignments()
{
    // Test parseAlignments with various formats
    {
        auto aligns = Markoff::TableSerializer::parseAlignments(
            QStringLiteral("| :--- | :---: | ---: | --- |"));
        QCOMPARE(aligns.size(), 4);
        QCOMPARE(aligns[0], Qt::AlignLeft);
        QCOMPARE(aligns[1], Qt::AlignCenter);
        QCOMPARE(aligns[2], Qt::AlignRight);
        QCOMPARE(aligns[3], Qt::Alignment{});
    }

    // Varying dash counts
    {
        auto aligns = Markoff::TableSerializer::parseAlignments(
            QStringLiteral("| :------ | :---: | ------: |"));
        QCOMPARE(aligns.size(), 3);
        QCOMPARE(aligns[0], Qt::AlignLeft);
        QCOMPARE(aligns[1], Qt::AlignCenter);
        QCOMPARE(aligns[2], Qt::AlignRight);
    }

    // No outer pipes (also valid markdown)
    {
        auto aligns = Markoff::TableSerializer::parseAlignments(
            QStringLiteral(":--- | :---: | ---:"));
        QCOMPARE(aligns.size(), 3);
        QCOMPARE(aligns[0], Qt::AlignLeft);
        QCOMPARE(aligns[1], Qt::AlignCenter);
        QCOMPARE(aligns[2], Qt::AlignRight);
    }
}

QTEST_MAIN(TestTableSerializer)
#include "tst_table_serializer.moc"
