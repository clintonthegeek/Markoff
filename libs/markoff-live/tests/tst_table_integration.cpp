// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QApplication>

#include <markoff/Editor.h>

using namespace Markoff;

class TestTableIntegration : public QObject {
    Q_OBJECT

private slots:
    void roundTripBasicTable();
    void roundTripTableWithSurroundingText();
    void roundTripMultipleTables();
    void insertTableViaApi();

private:
    Editor *makeEditor(const QString &text);
};

Editor *TestTableIntegration::makeEditor(const QString &text)
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(text);
    editor->show();
    (void)QTest::qWaitForWindowExposed(editor);
    return editor;
}

void TestTableIntegration::roundTripBasicTable()
{
    // A simple 2-row, 2-column table should survive a setPlainText -> toPlainText
    // round-trip. The table text is converted to a QTextTable by
    // SceneCoordinator/TableConverter on load, then serialized back by
    // allMarkdown() via TableSerializer.
    const QString input = QStringLiteral(
        "| Name  | Age |\n"
        "| ----- | --- |\n"
        "| Alice | 30  |");

    auto *editor = makeEditor(input);
    const QString output = editor->toPlainText();

    // The output must contain a pipe table with the same data.
    // TableSerializer may reformat padding, so check content not exact string.
    QVERIFY2(output.contains(QStringLiteral("Name")),
             qPrintable(QStringLiteral("Missing 'Name' in output: ") + output));
    QVERIFY2(output.contains(QStringLiteral("Age")),
             qPrintable(QStringLiteral("Missing 'Age' in output: ") + output));
    QVERIFY2(output.contains(QStringLiteral("Alice")),
             qPrintable(QStringLiteral("Missing 'Alice' in output: ") + output));
    QVERIFY2(output.contains(QStringLiteral("30")),
             qPrintable(QStringLiteral("Missing '30' in output: ") + output));
    QVERIFY2(output.contains(QLatin1Char('|')),
             qPrintable(QStringLiteral("Missing pipe characters in output: ") + output));

    // Verify the separator line is present (contains dashes between pipes)
    const QStringList lines = output.split(QLatin1Char('\n'));
    bool hasSeparator = false;
    for (const QString &line : lines) {
        if (line.contains(QLatin1Char('-')) && line.contains(QLatin1Char('|'))) {
            hasSeparator = true;
            break;
        }
    }
    QVERIFY2(hasSeparator,
             qPrintable(QStringLiteral("Missing separator row in output: ") + output));

    delete editor;
}

void TestTableIntegration::roundTripTableWithSurroundingText()
{
    // Text before and after a table must survive the round-trip alongside
    // the table itself.
    const QString input = QStringLiteral(
        "Some text before\n"
        "\n"
        "| Col1 | Col2 |\n"
        "| ---- | ---- |\n"
        "| a    | b    |\n"
        "\n"
        "Some text after");

    auto *editor = makeEditor(input);
    const QString output = editor->toPlainText();

    QVERIFY2(output.contains(QStringLiteral("Some text before")),
             qPrintable(QStringLiteral("Missing 'Some text before': ") + output));
    QVERIFY2(output.contains(QStringLiteral("Some text after")),
             qPrintable(QStringLiteral("Missing 'Some text after': ") + output));
    QVERIFY2(output.contains(QStringLiteral("Col1")),
             qPrintable(QStringLiteral("Missing 'Col1': ") + output));
    QVERIFY2(output.contains(QStringLiteral("Col2")),
             qPrintable(QStringLiteral("Missing 'Col2': ") + output));
    QVERIFY2(output.contains(QLatin1Char('|')),
             qPrintable(QStringLiteral("Missing pipe characters: ") + output));

    delete editor;
}

void TestTableIntegration::roundTripMultipleTables()
{
    // Two tables separated by text should both survive the round-trip.
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |\n"
        "\n"
        "Middle text\n"
        "\n"
        "| X | Y |\n"
        "| - | - |\n"
        "| 3 | 4 |");

    auto *editor = makeEditor(input);
    const QString output = editor->toPlainText();

    // Both tables' cell data must be present.
    QVERIFY2(output.contains(QStringLiteral("A")) && output.contains(QStringLiteral("B")),
             qPrintable(QStringLiteral("First table headers missing: ") + output));
    QVERIFY2(output.contains(QStringLiteral("X")) && output.contains(QStringLiteral("Y")),
             qPrintable(QStringLiteral("Second table headers missing: ") + output));
    QVERIFY2(output.contains(QStringLiteral("Middle text")),
             qPrintable(QStringLiteral("Middle text missing: ") + output));

    // Count pipe-delimited rows: at least 6 (3 per table: header + sep + data)
    int pipeRows = 0;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().startsWith(QLatin1Char('|')))
            ++pipeRows;
    }
    QVERIFY2(pipeRows >= 6,
             qPrintable(QStringLiteral("Expected >= 6 pipe rows, got %1: ").arg(pipeRows) + output));

    delete editor;
}

void TestTableIntegration::insertTableViaApi()
{
    // Editor::insertTable() inserts pipe markdown which gets converted to
    // a QTextTable on reparse. After that, toPlainText() must serialize it.
    auto *editor = makeEditor(QStringLiteral("some text"));
    editor->insertTable(2, 3);

    const QString output = editor->toPlainText();

    // The inserted table has empty header cells and pipe-delimited rows.
    // 2 data rows + 1 header row + 1 separator row = at least 4 pipe lines.
    QVERIFY2(output.contains(QLatin1Char('|')),
             qPrintable(QStringLiteral("Missing pipe characters: ") + output));
    // Header row should be present (empty cells still produce pipes)
    QVERIFY2(output.contains(QStringLiteral("---")),
             qPrintable(QStringLiteral("Missing separator row: ") + output));

    delete editor;
}

QTEST_MAIN(TestTableIntegration)
#include "tst_table_integration.moc"
