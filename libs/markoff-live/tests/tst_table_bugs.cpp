// SPDX-License-Identifier: GPL-3.0-or-later
// Reproduction tests for showcase table rendering bugs
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QTextTable>
#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "TextControl.h"

using namespace Markoff;

class TestTableBugs : public QObject {
    Q_OBJECT

private slots:
    void noRemnantTextAroundTables();
    void textAfterTablesPreserved();
    void twoTablesRoundTrip();
    void repeatedInsertRowAbovePreservesData();
};

void TestTableBugs::noRemnantTextAroundTables()
{
    Editor editor;
    editor.resize(800, 600);

    QString md = QStringLiteral(
        "# Heading\n"
        "\n"
        "Text before.\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "| X | Y |\n"
        "|---|---|\n"
        "| 3 | 4 |\n"
        "\n"
        "## After\n"
        "\n"
        "Text after tables.");

    editor.setPlainText(md);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    qDebug() << "OUTPUT:" << output;

    // Should NOT contain stray single-char lines (the 'e' and 's' bugs)
    QStringList lines = output.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.trimmed().length() == 1) {
            QChar c = line.trimmed().at(0);
            // Allow '#' (heading marker) and '|' (table pipe) and '-' (separator)
            if (c != QLatin1Char('#') && c != QLatin1Char('|') && c != QLatin1Char('-')) {
                QFAIL(qPrintable(QStringLiteral("Remnant char '%1' at line %2")
                    .arg(c).arg(i)));
            }
        }
    }

    // Should NOT contain partial pipe text like "| A | B" outside a table row
    // (the table should be fully serialized or fully converted)
    QVERIFY2(output.contains(QLatin1Char('A')), "Table A content missing");
    QVERIFY2(output.contains(QLatin1Char('X')), "Table X content missing");
}

void TestTableBugs::textAfterTablesPreserved()
{
    Editor editor;
    editor.resize(800, 600);

    QString md = QStringLiteral(
        "Before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| V |\n"
        "\n"
        "## After\n"
        "\n"
        "Paragraph after table.");

    editor.setPlainText(md);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    qDebug() << "OUTPUT:" << output;

    QVERIFY2(output.contains(QStringLiteral("Before")), "Text before table missing");
    QVERIFY2(output.contains(QStringLiteral("After")), "Heading after table missing");
    QVERIFY2(output.contains(QStringLiteral("Paragraph")), "Paragraph after table missing");
}

void TestTableBugs::twoTablesRoundTrip()
{
    Editor editor;
    editor.resize(800, 600);

    QString md = QStringLiteral(
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "Middle text\n"
        "\n"
        "| X | Y |\n"
        "|---|---|\n"
        "| 3 | 4 |");

    editor.setPlainText(md);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    qDebug() << "OUTPUT:" << output;

    QVERIFY2(output.contains(QStringLiteral("Middle")), "Middle text lost");
    // Both tables should produce pipe output
    int pipeCount = output.count(QLatin1Char('|'));
    QVERIFY2(pipeCount >= 12, qPrintable(
        QStringLiteral("Expected >= 12 pipes, got %1").arg(pipeCount)));
}

void TestTableBugs::repeatedInsertRowAbovePreservesData()
{
    // Reproduction: showcase.md second table — insert row above header 3 times
    // with reparse between each. After the third insertion, the table should
    // still contain all original data and content after it should be intact.
    Editor editor;
    editor.resize(800, 600);

    // Mimic the showcase structure: two tables with math content after
    QString md = QStringLiteral(
        "| Feature | Status | Notes |\n"
        "|---------|--------|-------|\n"
        "| Headings | Done | H1-H6 |\n"
        "| Bold | Done | Plus strikethrough |\n"
        "| Code | Done | KSyntaxHighlighting |\n"
        "| Callouts | Done | 13 types |\n"
        "| Math | Done | JKQTMathText |\n"
        "| Tables | Done | With alignment |\n"
        "| Preview | Done | Cursor-aware |\n"
        "\n"
        "| Left | Center | Right |\n"
        "|:-----|:------:|------:|\n"
        "| L1   | C1     | R1    |\n"
        "| L2   | C2     | R2    |\n"
        "\n"
        "## Mathematics\n"
        "\n"
        "Inline math: The quadratic formula is $x = \\frac{-b}{2a}$.\n"
        "\n"
        "Euler's identity: $e^{i\\pi} + 1 = 0$\n"
        "\n"
        "## Horizontal Rules\n"
        "\n"
        "Content above.\n"
        "\n"
        "---\n"
        "\n"
        "Content below.");

    editor.setPlainText(md);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));

    // Find the SECOND table and position cursor in its header (row 0)
    auto positionInSecondTable = [&]() -> bool {
        const auto items = editor.scene()->items();
        for (auto *gi : items) {
            auto *ti = dynamic_cast<MarkdownTextItem *>(gi);
            if (!ti) continue;
            QTextDocument *doc = ti->document();
            QList<QTextTable *> tables;
            for (auto *frame : doc->rootFrame()->childFrames()) {
                if (auto *t = qobject_cast<QTextTable *>(frame))
                    tables.append(t);
            }
            if (tables.size() >= 2) {
                QTextTable *table = tables[1]; // second table
                ti->setFocus();
                QTextCursor cursor = table->cellAt(0, 0).firstCursorPosition();
                ti->textControl()->setTextCursor(cursor);
                return true;
            }
        }
        return false;
    };

    QVERIFY2(positionInSecondTable(), "Could not find second table");

    auto getSecondTable = [&]() -> QTextTable * {
        const auto items = editor.scene()->items();
        for (auto *gi : items) {
            auto *ti = dynamic_cast<MarkdownTextItem *>(gi);
            if (!ti) continue;
            QList<QTextTable *> tables;
            for (auto *frame : ti->document()->rootFrame()->childFrames()) {
                if (auto *t = qobject_cast<QTextTable *>(frame))
                    tables.append(t);
            }
            if (tables.size() >= 2)
                return tables[1];
        }
        return nullptr;
    };

    // Verify initial state
    QString output0 = editor.toPlainText();
    QVERIFY2(output0.contains(QStringLiteral("L1")), "Initial: L1 missing");
    QVERIFY2(output0.contains(QStringLiteral("Mathematics")), "Initial: Math heading missing");

    // --- Insertion 1: insert row above header ---
    editor.tableInsertRowAbove();
    QTest::qWait(300); // wait for reparse timer (150ms) to fire + settle

    QTextTable *t1 = getSecondTable();
    QVERIFY2(t1, "After insert 1: second table gone");
    QVERIFY2(t1->rows() >= 4,
             qPrintable(QStringLiteral("After insert 1: expected >= 4 rows, got %1").arg(t1->rows())));

    QString output1 = editor.toPlainText();
    QVERIFY2(output1.contains(QStringLiteral("L1")), "After insert 1: L1 missing");
    QVERIFY2(output1.contains(QStringLiteral("Mathematics")), "After insert 1: Math heading missing");

    // Position cursor in the new row 0 of the second table
    QVERIFY2(positionInSecondTable(), "Could not reposition in second table after insert 1");

    // --- Insertion 2: insert row above again ---
    editor.tableInsertRowAbove();
    QTest::qWait(300);

    QTextTable *t2 = getSecondTable();
    QVERIFY2(t2, "After insert 2: second table gone");
    QVERIFY2(t2->rows() >= 5,
             qPrintable(QStringLiteral("After insert 2: expected >= 5 rows, got %1").arg(t2->rows())));

    QString output2 = editor.toPlainText();
    QVERIFY2(output2.contains(QStringLiteral("L1")), "After insert 2: L1 missing");
    QVERIFY2(output2.contains(QStringLiteral("Mathematics")), "After insert 2: Math heading missing");

    // Position cursor in the new row 0 again
    QVERIFY2(positionInSecondTable(), "Could not reposition in second table after insert 2");

    // --- Insertion 3: this is where the bug used to manifest ---
    editor.tableInsertRowAbove();
    QTest::qWait(300);

    QTextTable *t3 = getSecondTable();
    QVERIFY2(t3, "After insert 3: second table gone");

    QString output3 = editor.toPlainText();
    QVERIFY2(output3.contains(QStringLiteral("Left")), "After insert 3: 'Left' cell data lost");
    QVERIFY2(output3.contains(QStringLiteral("L1")), "After insert 3: L1 data lost");
    QVERIFY2(output3.contains(QStringLiteral("L2")), "After insert 3: L2 data lost");
    QVERIFY2(output3.contains(QStringLiteral("Mathematics")), "After insert 3: Math heading disappeared");
    QVERIFY2(output3.contains(QStringLiteral("Content above")), "After insert 3: Content after math lost");
    QVERIFY2(output3.contains(QStringLiteral("Content below")), "After insert 3: HR section lost");
}

QTEST_MAIN(TestTableBugs)
#include "tst_table_bugs.moc"
