// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QTextCursor>
#include <QTextTable>
#include <QSignalSpy>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "TextControl.h"

using namespace Markoff;

class TestTableOperations : public QObject {
    Q_OBJECT

private slots:
    void tableEnteredSignalExists();
    void insertRowBelow();
    void insertRowAbove();
    void insertColumnRight();
    void insertColumnLeft();
    void deleteRow();
    void deleteColumn();
    void cannotDeleteLastRow();
    void cannotDeleteLastColumn();
    void undoGroupsStructuralOp();

private:
    /// Create an Editor displaying `text`, with the cursor positioned inside
    /// the table.  The caller owns the returned pointer.
    Editor *makeEditorWithTableFocus(const QString &text);

    /// Find the first MarkdownTextItem that contains a QTextTable and
    /// position its cursor inside the table.
    static bool focusInsideTable(Editor *editor);
};

Editor *TestTableOperations::makeEditorWithTableFocus(const QString &text)
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(text);
    editor->show();
    (void)QTest::qWaitForWindowExposed(editor);

    focusInsideTable(editor);
    return editor;
}

bool TestTableOperations::focusInsideTable(Editor *editor)
{
    // Walk all scene items looking for a MarkdownTextItem whose document
    // contains a QTextTable, then position the cursor in the first data
    // cell (row 0, col 0).
    const auto items = editor->scene()->items();
    for (auto *gi : items) {
        auto *ti = dynamic_cast<MarkdownTextItem *>(gi);
        if (!ti) continue;

        QTextDocument *doc = ti->document();
        // Walk frames to find a QTextTable
        QTextFrame *root = doc->rootFrame();
        const auto children = root->childFrames();
        for (QTextFrame *frame : children) {
            auto *table = qobject_cast<QTextTable *>(frame);
            if (!table) continue;

            // Found a table — give this item focus and position cursor
            // inside cell (0, 0).
            ti->setFocus();
            QTextCursor cursor = table->cellAt(0, 0).firstCursorPosition();
            ti->textControl()->setTextCursor(cursor);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestTableOperations::tableEnteredSignalExists()
{
    // Verify the signal exists and is connectable.
    Editor editor;
    QSignalSpy spy(&editor, &Editor::tableEntered);
    QVERIFY(spy.isValid());
}

void TestTableOperations::insertRowBelow()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);

    // Count pipe rows before
    int rowsBefore = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsBefore;

    editor->tableInsertRowBelow();

    int rowsAfter = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsAfter;

    QVERIFY2(rowsAfter > rowsBefore,
             qPrintable(QStringLiteral("Expected more rows after insert. Before: %1, After: %2\nOutput: %3")
                            .arg(rowsBefore).arg(rowsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::insertRowAbove()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);

    int rowsBefore = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsBefore;

    editor->tableInsertRowAbove();

    int rowsAfter = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsAfter;

    QVERIFY2(rowsAfter > rowsBefore,
             qPrintable(QStringLiteral("Expected more rows after insert above. Before: %1, After: %2\nOutput: %3")
                            .arg(rowsBefore).arg(rowsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::insertColumnRight()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);

    // Count columns: count pipes in first pipe-row minus 1
    auto countCols = [](const QString &text) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('|'))) {
                // Count | chars — columns = |count - 1
                return static_cast<int>(trimmed.count(QLatin1Char('|'))) - 1;
            }
        }
        return 0;
    };

    int colsBefore = countCols(editor->toPlainText());
    editor->tableInsertColumnRight();
    int colsAfter = countCols(editor->toPlainText());

    QVERIFY2(colsAfter > colsBefore,
             qPrintable(QStringLiteral("Expected more columns after insert. Before: %1, After: %2\nOutput: %3")
                            .arg(colsBefore).arg(colsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::insertColumnLeft()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);

    auto countCols = [](const QString &text) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('|'))) {
                return static_cast<int>(trimmed.count(QLatin1Char('|'))) - 1;
            }
        }
        return 0;
    };

    int colsBefore = countCols(editor->toPlainText());
    editor->tableInsertColumnLeft();
    int colsAfter = countCols(editor->toPlainText());

    QVERIFY2(colsAfter > colsBefore,
             qPrintable(QStringLiteral("Expected more columns after insert left. Before: %1, After: %2\nOutput: %3")
                            .arg(colsBefore).arg(colsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::deleteRow()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |\n"
        "| 3 | 4 |");

    auto *editor = makeEditorWithTableFocus(input);

    int rowsBefore = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsBefore;

    editor->tableDeleteRow();

    int rowsAfter = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsAfter;

    QVERIFY2(rowsAfter < rowsBefore,
             qPrintable(QStringLiteral("Expected fewer rows after delete. Before: %1, After: %2\nOutput: %3")
                            .arg(rowsBefore).arg(rowsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::deleteColumn()
{
    const QString input = QStringLiteral(
        "| A | B | C |\n"
        "| - | - | - |\n"
        "| 1 | 2 | 3 |");

    auto *editor = makeEditorWithTableFocus(input);

    auto countCols = [](const QString &text) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('|'))) {
                return static_cast<int>(trimmed.count(QLatin1Char('|'))) - 1;
            }
        }
        return 0;
    };

    int colsBefore = countCols(editor->toPlainText());
    editor->tableDeleteColumn();
    int colsAfter = countCols(editor->toPlainText());

    QVERIFY2(colsAfter < colsBefore,
             qPrintable(QStringLiteral("Expected fewer columns after delete. Before: %1, After: %2\nOutput: %3")
                            .arg(colsBefore).arg(colsAfter).arg(editor->toPlainText())));
    delete editor;
}

void TestTableOperations::cannotDeleteLastRow()
{
    // A table with exactly 1 data row (header + 1 row = 2 QTextTable rows).
    // The guard should prevent deleting below 1 row.
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);
    const QString before = editor->toPlainText();

    // The QTextTable for a 2-row markdown table (header + 1 data row)
    // has rows() == 2.  Deleting one row brings it to 1; the second
    // delete attempt must be a no-op because rows() == 1.
    editor->tableDeleteRow();
    editor->tableDeleteRow();

    const QString after = editor->toPlainText();
    // Must still have at least 1 pipe-row (the guard prevented total removal)
    int pipeRows = 0;
    for (const QString &line : after.split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++pipeRows;
    QVERIFY2(pipeRows >= 1,
             qPrintable(QStringLiteral("Guard failed: all rows deleted. Output: ") + after));
    delete editor;
}

void TestTableOperations::cannotDeleteLastColumn()
{
    // A table with exactly 1 column.
    const QString input = QStringLiteral(
        "| A |\n"
        "| - |\n"
        "| 1 |");

    auto *editor = makeEditorWithTableFocus(input);

    editor->tableDeleteColumn();

    const QString after = editor->toPlainText();
    // Must still contain pipe characters — the single column was protected.
    QVERIFY2(after.contains(QLatin1Char('|')),
             qPrintable(QStringLiteral("Guard failed: last column deleted. Output: ") + after));
    delete editor;
}

void TestTableOperations::undoGroupsStructuralOp()
{
    const QString input = QStringLiteral(
        "| A | B |\n"
        "| - | - |\n"
        "| 1 | 2 |");

    auto *editor = makeEditorWithTableFocus(input);

    int rowsBefore = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsBefore;

    editor->tableInsertRowBelow();

    int rowsAfterInsert = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsAfterInsert;
    QVERIFY(rowsAfterInsert > rowsBefore);

    // Undo should remove the inserted row in one step.
    editor->undo();

    int rowsAfterUndo = 0;
    for (const QString &line : editor->toPlainText().split(QLatin1Char('\n')))
        if (line.trimmed().startsWith(QLatin1Char('|'))) ++rowsAfterUndo;

    QCOMPARE(rowsAfterUndo, rowsBefore);
    delete editor;
}

QTEST_MAIN(TestTableOperations)
#include "tst_table_operations.moc"
