// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include <QKeyEvent>
#include "TextControl.h"

using namespace Markoff;

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

// Helper: send a key event to a TextControl via processEvent.
static void sendKey(TextControl &ctrl, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, mods);
    ctrl.processEvent(&press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    ctrl.processEvent(&release);
}

class TestTableNavigation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void enterMovesDownInMiddleRow();
    void enterInsertsRowAtLastRow();
    void tabMovesToNextCell();
    void shiftTabMovesToPreviousCell();
    void tabAtLastCellInsertsRow();
    void escapeExitsTable();
};

void TestTableNavigation::enterMovesDownInMiddleRow()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
        {QStringLiteral("A3"), QStringLiteral("B3")},
    });
    QVERIFY(table);
    int origRows = table->rows();

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor in row 1, column 1
    QTextCursor cur = table->cellAt(1, 1).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Return);

    // Should move to row 2, column 1 — no new row created
    QCOMPARE(table->rows(), origRows);
    QTextCursor after = ctrl.textCursor();
    QTextTableCell cell = table->cellAt(after);
    QCOMPARE(cell.row(), 2);
    QCOMPARE(cell.column(), 1);

    // Content should be selected
    QVERIFY(after.hasSelection());
    QCOMPARE(after.selectedText(), QStringLiteral("B3"));
}

void TestTableNavigation::enterInsertsRowAtLastRow()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
    });
    QVERIFY(table);
    int origRows = table->rows();

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor in last row, column 0
    QTextCursor cur = table->cellAt(1, 0).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Return);

    // Should have inserted a new row
    QCOMPARE(table->rows(), origRows + 1);
    QTextCursor after = ctrl.textCursor();
    QTextTableCell cell = table->cellAt(after);
    QCOMPARE(cell.row(), 2);
    QCOMPARE(cell.column(), 0);
}

void TestTableNavigation::tabMovesToNextCell()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
    });
    QVERIFY(table);

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor in row 1, column 0
    QTextCursor cur = table->cellAt(1, 0).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Tab);

    QTextCursor after = ctrl.textCursor();
    QTextTableCell cell = table->cellAt(after);
    QCOMPARE(cell.row(), 1);
    QCOMPARE(cell.column(), 1);
}

void TestTableNavigation::shiftTabMovesToPreviousCell()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
    });
    QVERIFY(table);

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor in row 1, column 1
    QTextCursor cur = table->cellAt(1, 1).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Tab, Qt::ShiftModifier);

    QTextCursor after = ctrl.textCursor();
    QTextTableCell cell = table->cellAt(after);
    QCOMPARE(cell.row(), 1);
    QCOMPARE(cell.column(), 0);
}

void TestTableNavigation::tabAtLastCellInsertsRow()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
    });
    QVERIFY(table);
    int origRows = table->rows();

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor in last cell (row 1, col 1)
    QTextCursor cur = table->cellAt(1, 1).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Tab);

    // New row should be added
    QCOMPARE(table->rows(), origRows + 1);
    QTextCursor after = ctrl.textCursor();
    QTextTableCell cell = table->cellAt(after);
    QCOMPARE(cell.row(), 2);
    QCOMPARE(cell.column(), 0);
}

void TestTableNavigation::escapeExitsTable()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, {
        {QStringLiteral("A1"), QStringLiteral("B1")},
        {QStringLiteral("A2"), QStringLiteral("B2")},
    });
    QVERIFY(table);

    TextControl ctrl;
    ctrl.setDocument(&doc);
    ctrl.setTextInteractionFlags(Qt::TextEditorInteraction);

    // Position cursor inside the table
    QTextCursor cur = table->cellAt(0, 0).firstCursorPosition();
    ctrl.setTextCursor(cur);

    sendKey(ctrl, Qt::Key_Escape);

    QTextCursor after = ctrl.textCursor();
    // Cursor should be outside the table
    QVERIFY(!after.currentTable());
}

QTEST_MAIN(TestTableNavigation)
#include "tst_table_navigation.moc"
