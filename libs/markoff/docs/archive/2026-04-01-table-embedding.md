# Table Embedding via QTextTable Harvest — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed tables as QTextTable objects in the QTextDocument, with layout code harvested from Qt's QTextDocumentLayout, enabling full Obsidian-style interactive table editing.

**Architecture:** Pipe-delimited markdown is replaced by QTextTable objects during live preview. Table layout code (~500 lines) is grafted from Qt's GPL QTextDocumentLayout into the existing forked PlainTextDocumentLayout. Cell editing, cursor navigation, and undo work natively via QTextDocument. Obsidian chrome (grid lines, handles, buttons) is painted in the existing paintEvent.

**Tech Stack:** Qt6 (QTextTable, QTextDocumentLayout source harvest), C++20, existing Markoff editor fork

**Spec:** `docs/specs/2026-04-01-table-embedding-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/Editor.cpp` | Modify | Add table-aware paint path, delete embedded widget code |
| `src/Editor_p.h` | Modify | Remove EmbeddedWidget, add table tracking state |
| `src/TextControl.cpp` | Modify | Add table cell navigation (harvest from Qt) |
| `src/TextControl_p.h` | Modify | Add table navigation helpers |
| `src/TableHandler.cpp` | Modify | Replace QTableWidget with QTextTable conversion |
| `src/TableHandler.h` | Modify | Update API: remove TableWidget, add QTextTable conversion |
| `src/AtomicBlock.h` | Delete | Deprecated |
| `src/AtomicBlock.cpp` | Delete | Deprecated |
| `src/CodeAtomicBlock.h` | Delete | Deprecated |
| `src/CodeAtomicBlock.cpp` | Delete | Deprecated |
| `src/CalloutAtomicBlock.h` | Delete | Deprecated |
| `src/CalloutAtomicBlock.cpp` | Delete | Deprecated |
| `tests/tst_table.cpp` | Create | Table parsing, conversion, serialization, round-trip tests |
| `tests/CMakeLists.txt` | Modify | Add tst_table test executable |

---

### Task 1: Delete deprecated code and clean up

Remove AtomicBlock classes and the embedded widget overlay system. These are dead code that will conflict with the new approach.

**Files:**
- Delete: `src/AtomicBlock.h`, `src/AtomicBlock.cpp`
- Delete: `src/CodeAtomicBlock.h`, `src/CodeAtomicBlock.cpp`
- Delete: `src/CalloutAtomicBlock.h`, `src/CalloutAtomicBlock.cpp`
- Modify: `src/Editor_p.h`
- Modify: `src/Editor.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Delete AtomicBlock files**

```bash
rm src/AtomicBlock.h src/AtomicBlock.cpp
rm src/CodeAtomicBlock.h src/CodeAtomicBlock.cpp
rm src/CalloutAtomicBlock.h src/CalloutAtomicBlock.cpp
```

- [ ] **Step 2: Remove EmbeddedWidget from Editor_p.h**

In `src/Editor_p.h`, delete the `EmbeddedWidget` struct and its associated method declarations. Replace with table tracking state:

```cpp
// DELETE these lines (the entire EmbeddedWidget block):
//     // Embedded widgets (tables, images, diagrams — positioned over hidden text)
//     struct EmbeddedWidget {
//         QWidget *widget = nullptr;
//         int firstBlock = -1;
//         int lastBlock = -1;
//     };
//     QList<EmbeddedWidget> embeddedWidgets;
//     void createEmbeddedWidgets();
//     void clearEmbeddedWidgets();
//     void repositionEmbeddedWidgets();

// ADD in its place:
    // Live tables (QTextTable objects that replaced pipe text)
    QList<QTextTable *> liveTables;
    QList<QList<Qt::Alignment>> tableAlignments;
    void convertTables();
    void revertTables();
```

- [ ] **Step 3: Remove embedded widget methods from Editor.cpp**

Delete the three methods `createEmbeddedWidgets()`, `clearEmbeddedWidgets()`, and `repositionEmbeddedWidgets()` from `src/Editor.cpp`. Also delete any calls to them (search for `embeddedWidgets`, `createEmbeddedWidgets`, `clearEmbeddedWidgets`, `repositionEmbeddedWidgets`).

- [ ] **Step 4: Remove AtomicBlock references from CMakeLists.txt**

The AtomicBlock files are not currently listed in CMakeLists.txt (they were already removed from the build), so verify no references remain:

```bash
grep -n "AtomicBlock\|CodeAtomicBlock\|CalloutAtomicBlock" CMakeLists.txt
```

Expected: no matches.

- [ ] **Step 5: Remove any remaining AtomicBlock includes**

```bash
grep -rn "AtomicBlock\|CodeAtomicBlock\|CalloutAtomicBlock" src/ include/
```

Remove any `#include` lines found. Remove any references to `AtomicBlock` in `MarkoffBlockData.h` if present (it was already cleaned in commit f205617, verify).

- [ ] **Step 6: Build to verify clean compilation**

```bash
cmake --build build 2>&1 | head -40
```

Expected: clean build with no errors related to AtomicBlock or EmbeddedWidget.

- [ ] **Step 7: Commit**

```bash
git add -A src/AtomicBlock.* src/CodeAtomicBlock.* src/CalloutAtomicBlock.* src/Editor.cpp src/Editor_p.h
git commit -m "refactor(markoff): delete AtomicBlock classes and embedded widget overlay system

These are replaced by the QTextTable harvest approach.
AtomicBlock, CodeAtomicBlock, CalloutAtomicBlock — deleted.
EmbeddedWidget struct and create/clear/reposition methods — deleted."
```

---

### Task 2: Table parsing and serialization tests

Write tests for the table data path BEFORE modifying TableHandler. These tests define the expected behavior of parsing, QTextTable conversion, and round-trip serialization.

**Files:**
- Create: `tests/tst_table.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create test file with test cases**

Create `tests/tst_table.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include "TableHandler.h"

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

QTEST_MAIN(TestTable)
#include "tst_table.moc"
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(tst_markoff_table tst_table.cpp)
add_test(NAME tst_markoff_table COMMAND tst_markoff_table)
target_link_libraries(tst_markoff_table PRIVATE Qt6::Test markoff)
set_tests_properties(tst_markoff_table PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run tests — verify detection tests pass, conversion tests fail**

```bash
cd build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target tst_markoff_table && ctest -R tst_markoff_table --output-on-failure
```

Expected: `testDetect*` tests pass (detection already works). `testConvert*` and `testSerialize*` and `testRoundTrip*` tests fail because `convertToQTextTable()` doesn't exist yet (the old version was deleted with the QTableWidget refactor).

- [ ] **Step 4: Commit**

```bash
git add tests/tst_table.cpp tests/CMakeLists.txt
git commit -m "test(markoff): add table parsing, conversion, and round-trip tests

Tests define expected behavior for QTextTable-based table embedding.
Detection tests pass with existing code. Conversion/serialization
tests will pass after TableHandler is updated."
```

---

### Task 3: Implement QTextTable conversion in TableHandler

Replace the QTableWidget-based code with QTextTable conversion. The `convertToQTextTable()` method replaces pipe text in the QTextDocument with a QTextTable, and `serializeToMarkdown()` is updated to read from QTextTable instead of QTableWidget.

**Files:**
- Modify: `src/TableHandler.h`
- Modify: `src/TableHandler.cpp`

- [ ] **Step 1: Update TableHandler.h**

Replace the entire file with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLEHANDLER_H
#define MARKOFF_TABLEHANDLER_H

#include <QString>
#include <QStringList>
#include <QList>

class QTextDocument;
class QTextTable;

namespace Markoff {

/// Parsed representation of a markdown pipe table
struct ParsedTable {
    QStringList headers;
    QList<Qt::Alignment> alignments;
    QList<QStringList> rows;
    int firstBlock = -1;
    int lastBlock = -1;
};

/// Handles detection, conversion, and serialization of markdown tables.
class TableHandler {
public:
    /// Detect pipe table patterns in the document
    static QList<ParsedTable> detectTables(QTextDocument *doc);

    /// Replace pipe text with a QTextTable in the document.
    /// Returns the created QTextTable, or nullptr on failure.
    /// If the parsed table has no data rows, one empty row is added.
    static QTextTable *convertToQTextTable(QTextDocument *doc,
                                            const ParsedTable &table);

    /// Serialize a QTextTable back to pipe-delimited markdown
    static QString serializeToMarkdown(QTextTable *table,
                                        const QList<Qt::Alignment> &alignments);

private:
    static QStringList parseRow(const QString &line);
    static Qt::Alignment parseAlignment(const QString &cell);
};

} // namespace Markoff

#endif // MARKOFF_TABLEHANDLER_H
```

- [ ] **Step 2: Update TableHandler.cpp**

Replace the entire file. Keep `detectTables()`, `parseRow()`, `parseAlignment()` unchanged. Replace `TableWidget` class and QTableWidget serialization with QTextTable versions:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableHandler.h"

#include <QTextDocument>
#include <QTextBlock>
#include <QTextTable>
#include <QTextCursor>
#include <QTextTableFormat>
#include <QRegularExpression>

namespace Markoff {

// ---------------------------------------------------------------------------
// Parsing (unchanged)
// ---------------------------------------------------------------------------

QStringList TableHandler::parseRow(const QString &line)
{
    QStringList cells;
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|')))
        trimmed = trimmed.mid(1);
    if (trimmed.endsWith(QLatin1Char('|')))
        trimmed.chop(1);
    const QStringList parts = trimmed.split(QLatin1Char('|'));
    for (const QString &part : parts)
        cells.append(part.trimmed());
    return cells;
}

Qt::Alignment TableHandler::parseAlignment(const QString &cell)
{
    QString trimmed = cell.trimmed();
    bool left = trimmed.startsWith(QLatin1Char(':'));
    bool right = trimmed.endsWith(QLatin1Char(':'));
    if (left && right) return Qt::AlignCenter;
    if (right) return Qt::AlignRight;
    return Qt::AlignLeft;
}

QList<ParsedTable> TableHandler::detectTables(QTextDocument *doc)
{
    QList<ParsedTable> tables;

    static const QRegularExpression pipeRowRe(
        QStringLiteral(R"(^\s*\|.*\|\s*$)"));
    static const QRegularExpression separatorRe(
        QStringLiteral(R"(^\s*\|[\s:]*-+[\s:]*(\|[\s:]*-+[\s:]*)*\|\s*$)"));

    QTextBlock block = doc->begin();
    while (block.isValid()) {
        if (!pipeRowRe.match(block.text()).hasMatch()) {
            block = block.next();
            continue;
        }

        QTextBlock sepBlock = block.next();
        if (!sepBlock.isValid() || !separatorRe.match(sepBlock.text()).hasMatch()) {
            block = block.next();
            continue;
        }

        ParsedTable table;
        table.firstBlock = block.blockNumber();
        table.headers = parseRow(block.text());

        QStringList sepCells = parseRow(sepBlock.text());
        for (const QString &cell : sepCells)
            table.alignments.append(parseAlignment(cell));
        while (table.alignments.size() < table.headers.size())
            table.alignments.append(Qt::AlignLeft);

        QTextBlock dataBlock = sepBlock.next();
        table.lastBlock = sepBlock.blockNumber();

        while (dataBlock.isValid() && pipeRowRe.match(dataBlock.text()).hasMatch()) {
            table.rows.append(parseRow(dataBlock.text()));
            table.lastBlock = dataBlock.blockNumber();
            dataBlock = dataBlock.next();
        }

        tables.append(table);
        block = dataBlock;
    }

    return tables;
}

// ---------------------------------------------------------------------------
// QTextTable conversion
// ---------------------------------------------------------------------------

QTextTable *TableHandler::convertToQTextTable(QTextDocument *doc,
                                               const ParsedTable &table)
{
    int numCols = table.headers.size();
    int numDataRows = table.rows.size();
    int numRows = 1 + qMax(numDataRows, 1);  // header + at least 1 data row

    // Find the text range to replace
    QTextBlock firstBlock = doc->findBlockByNumber(table.firstBlock);
    QTextBlock lastBlock = doc->findBlockByNumber(table.lastBlock);
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return nullptr;

    int startPos = firstBlock.position();
    int endPos = lastBlock.position() + lastBlock.length() - 1;

    // Select and delete the pipe text
    QTextCursor cursor(doc);
    cursor.setPosition(startPos);
    cursor.setPosition(endPos, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.removeSelectedText();

    // Insert the QTextTable
    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    fmt.setCellPadding(4);
    fmt.setCellSpacing(0);
    fmt.setBorder(0);  // we paint our own borders

    // Equal column widths
    QList<QTextLength> constraints;
    for (int c = 0; c < numCols; ++c)
        constraints.append(QTextLength(QTextLength::PercentageLength, 100.0 / numCols));
    fmt.setColumnWidthConstraints(constraints);

    QTextTable *tt = cursor.insertTable(numRows, numCols, fmt);

    // Populate header row
    for (int c = 0; c < numCols; ++c) {
        QTextTableCell cell = tt->cellAt(0, c);
        QTextCursor cellCursor = cell.firstCursorPosition();
        cellCursor.insertText(table.headers[c]);

        // Bold header text
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        cellCursor.setPosition(cell.firstCursorPosition().position());
        cellCursor.setPosition(cell.lastCursorPosition().position(),
                               QTextCursor::KeepAnchor);
        cellCursor.mergeCharFormat(bold);
    }

    // Populate data rows
    for (int r = 0; r < numDataRows; ++r) {
        for (int c = 0; c < numCols && c < table.rows[r].size(); ++c) {
            QTextTableCell cell = tt->cellAt(r + 1, c);
            QTextCursor cellCursor = cell.firstCursorPosition();
            cellCursor.insertText(table.rows[r][c]);
        }
    }

    cursor.endEditBlock();
    return tt;
}

// ---------------------------------------------------------------------------
// Serialization from QTextTable
// ---------------------------------------------------------------------------

QString TableHandler::serializeToMarkdown(QTextTable *table,
                                           const QList<Qt::Alignment> &alignments)
{
    if (!table)
        return {};

    int rows = table->rows();
    int cols = table->columns();

    // Collect cell text and compute column widths
    QList<QList<QString>> cellTexts(rows);
    QList<int> colWidths(cols, 3);  // minimum width 3 for separator dashes

    for (int r = 0; r < rows; ++r) {
        cellTexts[r].resize(cols);
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            QString text;
            // Cell may span multiple blocks; join with space
            QTextCursor start = cell.firstCursorPosition();
            QTextCursor end = cell.lastCursorPosition();
            QTextBlock b = start.block();
            while (b.isValid() && b.position() <= end.block().position()) {
                if (!text.isEmpty())
                    text += QLatin1Char(' ');
                text += b.text();
                b = b.next();
            }
            cellTexts[r][c] = text;
            if (text.length() > colWidths[c])
                colWidths[c] = text.length();
        }
    }

    QString md;

    // Header row (row 0)
    md += QLatin1Char('|');
    for (int c = 0; c < cols; ++c) {
        md += QLatin1Char(' ');
        md += cellTexts[0][c].leftJustified(colWidths[c]);
        md += QStringLiteral(" |");
    }
    md += QLatin1Char('\n');

    // Separator row
    md += QLatin1Char('|');
    for (int c = 0; c < cols; ++c) {
        Qt::Alignment align = c < alignments.size() ? alignments[c] : Qt::AlignLeft;
        QString sep(colWidths[c] + 2, QLatin1Char('-'));
        if (align == Qt::AlignCenter) {
            sep[0] = QLatin1Char(':');
            sep[sep.size() - 1] = QLatin1Char(':');
        } else if (align == Qt::AlignRight) {
            sep[sep.size() - 1] = QLatin1Char(':');
        }
        md += sep + QLatin1Char('|');
    }
    md += QLatin1Char('\n');

    // Data rows (row 1+)
    for (int r = 1; r < rows; ++r) {
        md += QLatin1Char('|');
        for (int c = 0; c < cols; ++c) {
            md += QLatin1Char(' ');
            md += cellTexts[r][c].leftJustified(colWidths[c]);
            md += QStringLiteral(" |");
        }
        md += QLatin1Char('\n');
    }

    return md;
}

} // namespace Markoff
```

- [ ] **Step 3: Build and run the table tests**

```bash
cd build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . --target tst_markoff_table && ctest -R tst_markoff_table --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Run all tests to verify no regressions**

```bash
cd build && ctest --output-on-failure
```

Expected: all existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/TableHandler.h src/TableHandler.cpp
git commit -m "feat(markoff): QTextTable conversion replaces QTableWidget overlay

TableHandler::convertToQTextTable() replaces pipe text in the
QTextDocument with a real QTextTable. Serialization reads from
QTextTable cells instead of QTableWidget. TableWidget class deleted."
```

---

### Task 4: Obtain Qt source and study layoutTable()

Before grafting table layout code, we need the Qt source. It is not installed on this system.

**Files:**
- No code changes. Research task.

- [ ] **Step 1: Clone the relevant Qt source file**

```bash
cd /tmp && curl -L -o qtextdocumentlayout.cpp "https://raw.githubusercontent.com/qt/qtbase/v6.8.0/src/gui/text/qtextdocumentlayout.cpp"
curl -L -o qtextdocumentlayout_p.h "https://raw.githubusercontent.com/qt/qtbase/v6.8.0/src/gui/text/qtextdocumentlayout_p.h"
```

If curl fails (no network), check if Qt source is available via pacman:
```bash
pacman -Ss qt6-base-src || asp checkout qt6-base
```

- [ ] **Step 2: Identify the functions to harvest**

In `/tmp/qtextdocumentlayout.cpp`, find:
- `QTextDocumentLayoutPrivate::layoutTable()` — search for `void QTextDocumentLayoutPrivate::layoutTable`
- `QTextDocumentLayoutPrivate::drawTableCell()` — search for `drawTableCell`
- `QTextDocumentLayoutPrivate::drawTableCellBorder()` — search for `drawTableCellBorder`

Read each function. Note:
- What internal types they use (can they be replaced with public API?)
- What helper methods they call (do we need those too?)
- What can be stripped (CSS features, border-collapse, nested frames)

Document the dependency chain in a scratch note. This informs Task 5.

- [ ] **Step 3: Study how QTextTable interacts with block iteration**

In the Qt source, search for how `documentChanged()` handles QTextTable frames. Key questions:
- How does block iteration (`QTextBlock::next()`) traverse table cells?
- What does `QTextFrame::iterator` provide that `QTextBlock::next()` doesn't?
- How does `hitTest()` resolve clicks inside table cells?

This informs the layout graft in Task 5.

---

### Task 5: Graft table layout into PlainTextDocumentLayout

This is the core engineering task. Add table-aware branches to the forked PlainTextDocumentLayout so it can lay out and render QTextTable objects.

**Files:**
- Modify: `src/Editor.cpp` (PlainTextDocumentLayout methods + paintEvent)

- [ ] **Step 1: Add table detection helper**

At the top of the PlainTextDocumentLayout section in `src/Editor.cpp`, add a helper to check if a block belongs to a QTextTable:

```cpp
/// Returns the QTextTable containing this block, or nullptr.
static QTextTable *tableForBlock(const QTextBlock &block)
{
    QTextCursor cursor(block);
    return cursor.currentTable();
}

/// Returns true if this block is the first block of a QTextTable frame.
static bool isFirstTableBlock(const QTextBlock &block, QTextTable *table)
{
    if (!table)
        return false;
    QTextTableCell firstCell = table->cellAt(0, 0);
    return firstCell.firstCursorPosition().block() == block;
}
```

- [ ] **Step 2: Modify blockBoundingRect() for table blocks**

In `PlainTextDocumentLayout::blockBoundingRect()`, add table handling before the existing `renderedHeight` check:

```cpp
QRectF PlainTextDocumentLayout::blockBoundingRect(const QTextBlock &block) const
{
    if (!block.isValid()) { return QRectF(); }

    // Table blocks: the first block of a table reports the full table height,
    // subsequent blocks report zero height (consumed by table layout)
    QTextTable *table = tableForBlock(block);
    if (table) {
        if (isFirstTableBlock(block, table)) {
            // Compute table height from cell contents
            // For now: estimate from row count * row height
            // This will be replaced by proper layoutTable() in a later step
            qreal rowHeight = 30.0;  // rough estimate
            qreal tableHeight = table->rows() * rowHeight;
            return QRectF(0, 0, d.width, tableHeight);
        } else {
            // Non-first table block: zero height, consumed by table
            return QRectF(0, 0, d.width, 0);
        }
    }

    // ... existing code unchanged ...
```

- [ ] **Step 3: Modify documentChanged() to skip table cell blocks**

In `PlainTextDocumentLayout::documentChanged()`, table cell blocks should not trigger individual relayout — the table is laid out as a whole. Add early detection:

```cpp
void PlainTextDocumentLayout::documentChanged(int from, int charsRemoved, int charsAdded)
{
    QTextDocument *doc = document();
    int newBlockCount = doc->blockCount();
    int charsChanged = charsRemoved + charsAdded;

    QTextBlock changeStartBlock = doc->findBlock(from);

    // If the change is inside a table cell, just invalidate the whole viewport
    // (the table will be re-laid-out as a unit during paint)
    QTextTable *table = tableForBlock(changeStartBlock);
    if (table) {
        d.blockCount = newBlockCount;
        emit update(QRectF(0., -doc->documentMargin(), 1000000000., 1000000000.));
        return;
    }

    // ... rest of existing code unchanged ...
```

- [ ] **Step 4: Build and verify basic table rendering doesn't crash**

At this point, table blocks get height from `blockBoundingRect()` and skip individual layout. The paint path doesn't render them yet, but scrolling and viewport calculations should work.

```bash
cd build && cmake --build . 2>&1 | head -20
```

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/Editor.cpp
git commit -m "feat(markoff): add table-aware branches to PlainTextDocumentLayout

blockBoundingRect() reports table height for first table block,
zero for subsequent blocks. documentChanged() invalidates viewport
for in-table edits. Table rendering comes in next commit."
```

---

### Task 6: Wire table conversion into the editor lifecycle

Connect the table conversion to the reparse path so tables are created during live preview.

**Files:**
- Modify: `src/Editor.cpp` (reparseDocument, mode switching)
- Modify: `src/Editor_p.h` (already updated in Task 1)

- [ ] **Step 1: Implement convertTables() and revertTables()**

Add to `src/Editor.cpp`, in the `Editor::Private` methods section:

```cpp
void Editor::Private::convertTables()
{
    if (mode != Editor::Mode::LivePreview)
        return;

    // Don't reconvert if tables already exist
    if (!liveTables.isEmpty())
        return;

    QList<ParsedTable> tables = TableHandler::detectTables(q->document());

    // Convert in reverse order so block numbers stay valid
    for (int i = tables.size() - 1; i >= 0; --i) {
        const ParsedTable &pt = tables[i];
        QTextTable *tt = TableHandler::convertToQTextTable(q->document(), pt);
        if (tt) {
            liveTables.prepend(tt);
            tableAlignments.prepend(pt.alignments);
        }
    }
}

void Editor::Private::revertTables()
{
    // Serialize all QTextTables back to pipe markdown
    for (int i = liveTables.size() - 1; i >= 0; --i) {
        QTextTable *tt = liveTables[i];
        if (!tt)
            continue;

        const auto &aligns = i < tableAlignments.size()
            ? tableAlignments[i] : QList<Qt::Alignment>();
        QString md = TableHandler::serializeToMarkdown(tt, aligns);

        // Replace the entire table frame with pipe text
        QTextCursor cursor(tt->firstCursorPosition());
        cursor.setPosition(tt->lastCursorPosition().position(),
                           QTextCursor::KeepAnchor);
        cursor.beginEditBlock();
        // Select from before the table frame to after it
        cursor.movePosition(QTextCursor::PreviousBlock);
        cursor.movePosition(QTextCursor::StartOfBlock);
        QTextCursor endCursor(tt->lastCursorPosition());
        endCursor.movePosition(QTextCursor::NextBlock);
        endCursor.movePosition(QTextCursor::EndOfBlock);
        cursor.setPosition(endCursor.position(), QTextCursor::KeepAnchor);
        cursor.insertText(md);
        cursor.endEditBlock();
    }
    liveTables.clear();
    tableAlignments.clear();
}
```

Note: The exact cursor positioning for replacing a QTextTable frame with text is tricky. The implementation above is a starting point — test and adjust the cursor range to correctly select the entire table frame including its boundary characters.

- [ ] **Step 2: Call convertTables() in reparseDocument()**

In `Editor::Private::reparseDocument()`, find where `applyBlockFormats()` is called (at the end of the reparse sequence). Add `convertTables()` after it:

```cpp
// At the end of reparseDocument(), after existing calls:
    highlighter->setDecoratedRanges(decoratedRanges);
    applyBlockFormats();

    // Convert tables AFTER span map is applied (table conversion changes
    // document structure which would invalidate tree-sitter offsets)
    convertTables();
```

- [ ] **Step 3: Call revertTables() before reparse and on mode switch**

Before tree-sitter reparsing, tables must be reverted to pipe text so the parser sees raw markdown. At the TOP of `reparseDocument()`:

```cpp
void Editor::Private::reparseDocument()
{
    if (inReparse)
        return;
    inReparse = true;

    // Revert tables to pipe text before reparsing
    revertTables();

    // ... existing reparse code ...
```

Also, when switching to source mode, revert tables. Find the mode-switching code and add `revertTables()` before setting source mode.

- [ ] **Step 4: Build and test with the standalone app**

```bash
cd build && cmake --build . && ./Corbomite
```

Open a markdown file with a table. Switch to live preview mode. The pipe text should disappear and be replaced by QTextTable content. The table won't be painted yet (Task 7), but the document structure should be correct. Check that switching to source mode shows pipe text again.

- [ ] **Step 5: Commit**

```bash
git add src/Editor.cpp src/Editor_p.h
git commit -m "feat(markoff): wire table conversion into editor lifecycle

convertTables() called after reparse in live preview mode.
revertTables() called before reparse and on source mode switch.
Tables are QTextTable objects in the document during live preview,
pipe text on disk and in source mode."
```

---

### Task 7: Paint tables in paintEvent

Add table rendering to the existing `paintEvent` paint path. Paint grid lines, cell content, and basic borders. Chrome (handles, buttons) comes in Task 9.

**Files:**
- Modify: `src/Editor.cpp` (paintEvent)

- [ ] **Step 1: Add table painting in the block iteration loop**

In `Editor::paintEvent()`, inside the `while (block.isValid())` loop, after the `if (r.bottom() >= er.top() && r.top() <= er.bottom())` visibility check, add table block handling:

```cpp
        if (r.bottom() >= er.top() && r.top() <= er.bottom()) {

            // Table blocks: paint the table grid
            QTextTable *table = tableForBlock(block);
            if (table && isFirstTableBlock(block, table)) {
                paintTable(&painter, table, r, offset, viewportRect);
                // Skip to the block after the table
                QTextTableCell lastCell = table->cellAt(table->rows() - 1,
                                                         table->columns() - 1);
                block = lastCell.lastCursorPosition().block();
                offset.ry() += r.height();
                block = block.next();
                continue;
            }

            // Skip non-first table blocks (height is 0, already painted)
            if (table) {
                block = block.next();
                continue;
            }

            // ... existing decorated range painting and text painting ...
```

- [ ] **Step 2: Implement paintTable()**

Add a `paintTable()` method to `Editor` (or as a free function in the anonymous namespace). This paints the table grid using QPainter:

```cpp
void Editor::paintTable(QPainter *painter, QTextTable *table,
                        const QRectF &tableRect, const QPointF &offset,
                        const QRect &viewportRect)
{
    int rows = table->rows();
    int cols = table->columns();
    qreal margin = document()->documentMargin();

    // Compute geometry
    qreal tableWidth = viewportRect.width() - margin * 2;
    qreal colWidth = tableWidth / cols;
    qreal rowHeight = 30.0;  // TODO: compute from cell content
    qreal tableX = tableRect.left() + margin;
    qreal tableY = tableRect.top();

    painter->save();

    // Background
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0xfa, 0xfa, 0xfa));
    painter->drawRect(QRectF(tableX, tableY, tableWidth, rows * rowHeight));

    // Grid lines
    painter->setPen(QPen(QColor(0xe0, 0xe0, 0xe0), 1));
    for (int r = 0; r <= rows; ++r) {
        qreal y = tableY + r * rowHeight;
        painter->drawLine(QPointF(tableX, y), QPointF(tableX + tableWidth, y));
    }
    for (int c = 0; c <= cols; ++c) {
        qreal x = tableX + c * colWidth;
        painter->drawLine(QPointF(x, tableY), QPointF(x, tableY + rows * rowHeight));
    }

    // Header separator (thicker line after row 0)
    painter->setPen(QPen(QColor(0xc0, 0xc0, 0xc0), 2));
    qreal sepY = tableY + rowHeight;
    painter->drawLine(QPointF(tableX, sepY), QPointF(tableX + tableWidth, sepY));

    // Cell text
    painter->setPen(palette().text().color());
    QFont cellFont = font();
    for (int r = 0; r < rows; ++r) {
        if (r == 0) {
            QFont bold = cellFont;
            bold.setWeight(QFont::Bold);
            painter->setFont(bold);
        } else {
            painter->setFont(cellFont);
        }
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            QString text = cell.firstCursorPosition().block().text();
            QRectF cellRect(tableX + c * colWidth + 8, tableY + r * rowHeight,
                            colWidth - 16, rowHeight);
            painter->drawText(cellRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }

    // Cursor inside table: draw cell cursor
    QTextCursor tc = d->control->textCursor();
    QTextTable *cursorTable = tc.currentTable();
    if (cursorTable == table) {
        int cellRow, cellCol, _, _2;
        tc.selectedTableCells(&cellRow, &_, &cellCol, &_2);
        if (cellRow < 0) {
            QTextTableCell curCell = table->cellAt(tc);
            cellRow = curCell.row();
            cellCol = curCell.column();
        }
        // Highlight current cell
        QRectF highlight(tableX + cellCol * colWidth, tableY + cellRow * rowHeight,
                         colWidth, rowHeight);
        painter->setPen(QPen(palette().highlight().color(), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(highlight.adjusted(1, 1, -1, -1));
    }

    painter->restore();
}
```

Declare `paintTable` in the Editor class header or as a private method.

- [ ] **Step 3: Update blockBoundingRect() with content-based height**

Replace the rough `30.0` estimate with actual content-based height:

```cpp
if (isFirstTableBlock(block, table)) {
    QFontMetricsF fm(document()->defaultFont());
    qreal rowHeight = fm.height() + 16;  // text height + padding
    qreal tableHeight = table->rows() * rowHeight;
    return QRectF(0, 0, d.width, tableHeight);
}
```

Use the same `rowHeight` calculation in `paintTable()` to keep them consistent.

- [ ] **Step 4: Build and test visually**

```bash
cd build && cmake --build . && ./Corbomite
```

Open a file with tables. Switch to live preview. Tables should render as grids with cell text, grid lines, and a highlighted current cell.

- [ ] **Step 5: Commit**

```bash
git add src/Editor.cpp include/markoff/Editor.h
git commit -m "feat(markoff): paint QTextTable as grid in live preview

Tables render with grid lines, cell text, bold headers, and
header separator. Current cell highlighted when cursor is inside.
Cell height computed from font metrics."
```

---

### Task 8: Table cell navigation in TextControl

Add Tab/Shift+Tab cell navigation and Enter/Escape behavior. These were stripped from the QWidgetTextControl fork — harvest them back from Qt source.

**Files:**
- Modify: `src/TextControl.cpp`
- Modify: `src/TextControl_p.h`

- [ ] **Step 1: Add table navigation methods to TextControlPrivate**

In `src/TextControl_p.h`, add:

```cpp
    // Table cell navigation (harvested from QWidgetTextControl)
    void gotoNextTableCell();
    void gotoPreviousTableCell();
```

- [ ] **Step 2: Implement gotoNextTableCell() and gotoPreviousTableCell()**

In `src/TextControl.cpp`, add (harvested from Qt's `qwidgettextcontrol.cpp`, adapted):

```cpp
void TextControlPrivate::gotoNextTableCell()
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;

    QTextTableCell cell = table->cellAt(cursor);
    int row = cell.row();
    int col = cell.column() + cell.columnSpan();

    if (col >= table->columns()) {
        col = 0;
        ++row;
    }
    if (row >= table->rows()) {
        // Past last cell: insert new row
        table->insertRows(table->rows(), 1);
        row = table->rows() - 1;
        col = 0;
    }

    cell = table->cellAt(row, col);
    cursor = cell.firstCursorPosition();
    q->ensureCursorVisible();
}

void TextControlPrivate::gotoPreviousTableCell()
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;

    QTextTableCell cell = table->cellAt(cursor);
    int row = cell.row();
    int col = cell.column() - 1;

    if (col < 0) {
        col = table->columns() - 1;
        --row;
    }
    if (row < 0)
        return;  // Already at first cell, do nothing

    cell = table->cellAt(row, col);
    cursor = cell.firstCursorPosition();
    q->ensureCursorVisible();
}
```

- [ ] **Step 3: Route Tab/Shift+Tab/Enter/Escape in key event handling**

In `TextControlPrivate::processKeyEvent()` (or wherever key events are handled in TextControl.cpp), add table-specific key handling:

```cpp
    // Table navigation
    QTextTable *table = cursor.currentTable();
    if (table) {
        if (e->key() == Qt::Key_Tab && !(e->modifiers() & Qt::ShiftModifier)) {
            gotoNextTableCell();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Tab && (e->modifiers() & Qt::ShiftModifier)) {
            gotoPreviousTableCell();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            // Insert new row below current row
            QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row();
            table->insertRows(row + 1, 1);
            cursor = table->cellAt(row + 1, 0).firstCursorPosition();
            q->ensureCursorVisible();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            // Move cursor out of table (below)
            QTextTableCell lastCell = table->cellAt(table->rows() - 1,
                                                     table->columns() - 1);
            cursor = lastCell.lastCursorPosition();
            cursor.movePosition(QTextCursor::NextBlock);
            q->ensureCursorVisible();
            e->accept();
            return;
        }
    }
```

Find the exact location in TextControl.cpp where key events are processed and add this block at the beginning of the key handling, before the default text insertion path.

- [ ] **Step 4: Build and test navigation**

```bash
cd build && cmake --build . && ./Corbomite
```

Open a file with a table in live preview. Click in a cell. Verify:
- Tab moves to next cell
- Shift+Tab moves to previous cell
- Tab from last cell creates new row
- Enter creates new row
- Escape exits the table

- [ ] **Step 5: Commit**

```bash
git add src/TextControl.cpp src/TextControl_p.h
git commit -m "feat(markoff): table cell navigation — Tab, Shift+Tab, Enter, Escape

Harvested from QWidgetTextControl's table navigation.
Tab/Shift+Tab navigate between cells. Tab from last cell inserts row.
Enter inserts row below. Escape exits table."
```

---

### Task 9: Table creation trigger

Detect when the user completes a table separator line (types the final `|`) and auto-convert to QTextTable.

**Files:**
- Modify: `src/TextControl.cpp` or `src/Editor.cpp`

- [ ] **Step 1: Add separator completion detection**

In the key event handler (same location as Task 8), add detection for when a `|` keystroke completes a table separator:

```cpp
    // Table auto-creation: detect when typing | completes a separator line
    if (e->key() == Qt::Key_Bar && !cursor.currentTable()) {
        // Let the character be inserted first
        // (handled by the default path below)
        // Then check if we just completed a table pattern
        // Use a single-shot to check after the character is inserted
    }
```

A cleaner approach: check after every `|` insertion in the `contentsChanged` handler or at the end of key processing. Add a helper method:

```cpp
void Editor::Private::checkTableCreationTrigger()
{
    QTextCursor tc = control->textCursor();
    QTextBlock currentBlock = tc.block();
    QString currentText = currentBlock.text().trimmed();

    // Check: is this line a valid separator?
    static const QRegularExpression separatorRe(
        QStringLiteral(R"(^\s*\|[\s:]*-+[\s:]*(\|[\s:]*-+[\s:]*)*\|\s*$)"));
    if (!separatorRe.match(currentText).hasMatch())
        return;

    // Check: is the previous line a valid header row?
    QTextBlock prevBlock = currentBlock.previous();
    if (!prevBlock.isValid())
        return;
    static const QRegularExpression pipeRowRe(
        QStringLiteral(R"(^\s*\|.*\|\s*$)"));
    if (!pipeRowRe.match(prevBlock.text()).hasMatch())
        return;

    // We have a header + separator! Convert to table.
    ParsedTable pt;
    pt.firstBlock = prevBlock.blockNumber();
    pt.lastBlock = currentBlock.blockNumber();
    pt.headers = TableHandler::parseRow(prevBlock.text());

    QStringList sepCells = TableHandler::parseRow(currentBlock.text());
    for (const QString &cell : sepCells)
        pt.alignments.append(TableHandler::parseAlignment(cell));
    while (pt.alignments.size() < pt.headers.size())
        pt.alignments.append(Qt::AlignLeft);

    // Convert pipe text to QTextTable
    QTextTable *tt = TableHandler::convertToQTextTable(q->document(), pt);
    if (tt) {
        liveTables.append(tt);
        tableAlignments.append(pt.alignments);

        // Place cursor in first cell of data row (row 1, col 0)
        QTextTableCell dataCell = tt->cellAt(1, 0);
        control->setTextCursor(dataCell.firstCursorPosition());
    }
}
```

Note: `TableHandler::parseRow` and `TableHandler::parseAlignment` are currently private. Make them public (or add a `friend` declaration, or add a `parseSeparator()` public method).

- [ ] **Step 2: Make parseRow and parseAlignment accessible**

In `src/TableHandler.h`, move `parseRow` and `parseAlignment` to `public`:

```cpp
public:
    static QList<ParsedTable> detectTables(QTextDocument *doc);
    static QTextTable *convertToQTextTable(QTextDocument *doc,
                                            const ParsedTable &table);
    static QString serializeToMarkdown(QTextTable *table,
                                        const QList<Qt::Alignment> &alignments);
    static QStringList parseRow(const QString &line);
    static Qt::Alignment parseAlignment(const QString &cell);
```

- [ ] **Step 3: Call checkTableCreationTrigger() after `|` insertion**

In the key event handler, after a `|` character is inserted through the normal path, call the trigger check:

```cpp
    // After default key handling inserts the character:
    if (e->text() == QStringLiteral("|")) {
        d->checkTableCreationTrigger();
    }
```

Find the right place in Editor.cpp or TextControl.cpp where typed characters are handled, and add this check after the character is committed to the document.

- [ ] **Step 4: Build and test the creation trigger**

```bash
cd build && cmake --build . && ./Corbomite
```

In live preview mode, type: `| Foo | Bar |` Enter `|---|---|` — at the final `|`, the text should transform into a table with cursor in the first data cell.

- [ ] **Step 5: Commit**

```bash
git add src/Editor.cpp src/TableHandler.h
git commit -m "feat(markoff): table auto-creation on separator completion

Typing the final | of a valid separator line (|---|---|)
triggers conversion of header+separator to QTextTable.
Cursor placed in first cell of auto-inserted data row."
```

---

### Task 10: Table chrome — handles, buttons, context menu

Paint Obsidian-style interactive chrome on tables: hover handles for columns/rows, + buttons at edges, and a right-click context menu.

**Files:**
- Modify: `src/Editor.cpp` (paintEvent, mouse events)
- Modify: `src/Editor_p.h` (hover state)

- [ ] **Step 1: Add hover tracking state to Editor::Private**

In `src/Editor_p.h`:

```cpp
    // Table hover state
    QTextTable *hoverTable = nullptr;
    int hoverTableRow = -1;
    int hoverTableCol = -1;
    bool showColumnHandle = false;
    bool showRowHandle = false;
    bool showAddColumnButton = false;
    bool showAddRowButton = false;
```

- [ ] **Step 2: Track mouse hover in mouseMoveEvent**

In `Editor::mouseMoveEvent()`, detect when the mouse is near table edges and update hover state:

```cpp
    // Table hover tracking
    QTextTable *table = tableForBlock(cursorForPosition(e->pos()).block());
    d->hoverTable = table;
    if (table) {
        // Compute which cell the mouse is over using table geometry
        // Update hoverTableRow, hoverTableCol
        // Set showColumnHandle if near top edge of column
        // Set showRowHandle if near left edge of row
        // Set showAddColumnButton if near right edge
        // Set showAddRowButton if near bottom edge
        viewport()->update();  // trigger repaint for hover state changes
    }
```

The exact geometry calculation depends on the cell rect computation from `paintTable()`. Extract the geometry into a shared helper.

- [ ] **Step 3: Paint chrome in paintTable()**

Extend `paintTable()` to draw chrome based on hover state:

```cpp
    // Column handle (above hovered column)
    if (d->showColumnHandle && d->hoverTable == table) {
        qreal x = tableX + d->hoverTableCol * colWidth;
        QRectF handleRect(x + colWidth / 2 - 8, tableY - 20, 16, 16);
        painter->setPen(QPen(QColor(0x99, 0x99, 0x99), 1));
        painter->setBrush(QColor(0xf0, 0xf0, 0xf0));
        painter->drawRoundedRect(handleRect, 3, 3);
        // Draw grip dots or arrow icon
    }

    // Row handle (left of hovered row)
    if (d->showRowHandle && d->hoverTable == table) {
        qreal y = tableY + d->hoverTableRow * rowHeight;
        QRectF handleRect(tableX - 20, y + rowHeight / 2 - 8, 16, 16);
        painter->setPen(QPen(QColor(0x99, 0x99, 0x99), 1));
        painter->setBrush(QColor(0xf0, 0xf0, 0xf0));
        painter->drawRoundedRect(handleRect, 3, 3);
    }

    // Add column button (+ at right edge)
    if (d->showAddColumnButton && d->hoverTable == table) {
        qreal x = tableX + tableWidth + 4;
        qreal y = tableY + tableHeight / 2 - 10;
        painter->setPen(QPen(QColor(0x99, 0x99, 0x99), 1));
        painter->setBrush(QColor(0xf5, 0xf5, 0xf5));
        painter->drawRoundedRect(QRectF(x, y, 20, 20), 3, 3);
        painter->drawText(QRectF(x, y, 20, 20), Qt::AlignCenter, QStringLiteral("+"));
    }

    // Add row button (+ at bottom edge)
    if (d->showAddRowButton && d->hoverTable == table) {
        qreal x = tableX + tableWidth / 2 - 10;
        qreal y = tableY + tableHeight + 4;
        painter->setPen(QPen(QColor(0x99, 0x99, 0x99), 1));
        painter->setBrush(QColor(0xf5, 0xf5, 0xf5));
        painter->drawRoundedRect(QRectF(x, y, 20, 20), 3, 3);
        painter->drawText(QRectF(x, y, 20, 20), Qt::AlignCenter, QStringLiteral("+"));
    }
```

- [ ] **Step 4: Implement context menu**

In `Editor::contextMenuEvent()`, check if the click is inside a table. If so, show a table-specific context menu:

```cpp
    QTextTable *table = cursorForPosition(e->pos()).currentTable();
    if (table && d->mode == Mode::LivePreview) {
        QMenu menu(this);
        QTextTableCell cell = table->cellAt(cursorForPosition(e->pos()));
        int row = cell.row();
        int col = cell.column();

        menu.addAction(i18n("Insert Row Above"), [=]() { table->insertRows(row, 1); });
        menu.addAction(i18n("Insert Row Below"), [=]() { table->insertRows(row + 1, 1); });
        menu.addAction(i18n("Insert Column Before"), [=]() { table->insertColumns(col, 1); });
        menu.addAction(i18n("Insert Column After"), [=]() { table->insertColumns(col + 1, 1); });
        menu.addSeparator();
        menu.addAction(i18n("Delete Row"), [=]() {
            if (table->rows() > 1) table->removeRows(row, 1);
        });
        menu.addAction(i18n("Delete Column"), [=]() {
            if (table->columns() > 1) table->removeColumns(col, 1);
        });
        menu.addSeparator();
        menu.addAction(i18n("Align Left"), [=]() {
            if (col < d->tableAlignments.last().size())
                d->tableAlignments.last()[col] = Qt::AlignLeft;
            viewport()->update();
        });
        menu.addAction(i18n("Align Center"), [=]() {
            if (col < d->tableAlignments.last().size())
                d->tableAlignments.last()[col] = Qt::AlignCenter;
            viewport()->update();
        });
        menu.addAction(i18n("Align Right"), [=]() {
            if (col < d->tableAlignments.last().size())
                d->tableAlignments.last()[col] = Qt::AlignRight;
            viewport()->update();
        });

        menu.exec(e->globalPos());
        e->accept();
        return;
    }
```

- [ ] **Step 5: Handle clicks on chrome buttons**

In `Editor::mousePressEvent()`, detect clicks on the + buttons and handles:

```cpp
    if (d->showAddColumnButton && d->hoverTable) {
        d->hoverTable->appendColumns(1);
        // Extend alignment list
        // ... update tableAlignments ...
        e->accept();
        return;
    }
    if (d->showAddRowButton && d->hoverTable) {
        d->hoverTable->appendRows(1);
        e->accept();
        return;
    }
```

- [ ] **Step 6: Build and test visually**

```bash
cd build && cmake --build . && ./Corbomite
```

Test: hover over table edges to see handles/buttons. Click + to add rows/columns. Right-click for context menu. Verify all operations work.

- [ ] **Step 7: Commit**

```bash
git add src/Editor.cpp src/Editor_p.h include/markoff/Editor.h
git commit -m "feat(markoff): table chrome — hover handles, edge buttons, context menu

Column/row handles appear on hover. + buttons at right and bottom
edges. Right-click context menu for insert/delete rows/columns
and alignment changes."
```

---

### Task 11: Table destruction (backspace-select-delete)

Implement the Obsidian behavior: backspace from the line after a table selects the entire table, second backspace deletes it.

**Files:**
- Modify: `src/TextControl.cpp` or `src/Editor.cpp`

- [ ] **Step 1: Add table selection on backspace**

In the key event handler, before default backspace processing:

```cpp
    if (e->key() == Qt::Key_Backspace && !cursor.hasSelection()) {
        // Check if cursor is at the start of a block immediately after a table
        if (cursor.atBlockStart()) {
            QTextBlock prevBlock = cursor.block().previous();
            if (prevBlock.isValid()) {
                QTextCursor probe(prevBlock);
                QTextTable *table = probe.currentTable();
                if (table) {
                    // Select the entire table
                    QTextCursor sel(table->firstCursorPosition());
                    sel.setPosition(table->lastCursorPosition().position(),
                                    QTextCursor::KeepAnchor);
                    control->setTextCursor(sel);
                    e->accept();
                    return;
                }
            }
        }
    }

    if (e->key() == Qt::Key_Backspace && cursor.hasSelection()) {
        // Check if selection spans a table — if so, delete the table
        QTextTable *table = cursor.currentTable();
        if (table) {
            // Find this table in liveTables and remove tracking
            int idx = liveTables.indexOf(table);
            if (idx >= 0) {
                liveTables.removeAt(idx);
                if (idx < tableAlignments.size())
                    tableAlignments.removeAt(idx);
            }
            // Delete the table frame
            cursor.removeSelectedText();
            e->accept();
            return;
        }
    }
```

- [ ] **Step 2: Build and test**

```bash
cd build && cmake --build . && ./Corbomite
```

Place cursor on the line after a table. Press Backspace — table should be selected (highlighted). Press Backspace again — table should be deleted.

- [ ] **Step 3: Commit**

```bash
git add src/Editor.cpp src/TextControl.cpp
git commit -m "feat(markoff): backspace-select-delete for tables

Backspace at start of line after table selects the whole table.
Second backspace deletes it. Table tracking cleaned up on deletion."
```

---

### Task 12: Integration testing and edge cases

Verify the full table lifecycle works end-to-end and handle edge cases.

**Files:**
- Modify: `tests/tst_table.cpp`

- [ ] **Step 1: Add lifecycle edge case tests**

Add to `tests/tst_table.cpp`:

```cpp
void TestTable::testConvertEmptyTable()
{
    // Table with only headers, no data rows at all
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    QCOMPARE(tables.size(), 1);
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);
    QVERIFY(tt != nullptr);
    QCOMPARE(tt->rows(), 2);  // header + 1 empty data row
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
    // Create table, add a row via QTextTable API, serialize
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
    auto tables = Markoff::TableHandler::detectTables(&doc);
    auto aligns = tables[0].alignments;
    QTextTable *tt = Markoff::TableHandler::convertToQTextTable(&doc, tables[0]);

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
```

Don't forget to add the new test slot declarations to the class.

- [ ] **Step 2: Run all tests**

```bash
cd build && cmake --build . && ctest --output-on-failure
```

Expected: all pass.

- [ ] **Step 3: Commit**

```bash
git add tests/tst_table.cpp
git commit -m "test(markoff): add edge case tests for table conversion

Empty tables, single-column tables, serialization after row insertion."
```

---

## Post-Implementation Notes

**What this plan does NOT cover (future work):**

- **Harvesting the full `layoutTable()` from QTextDocumentLayout**: Task 5 uses a simplified height calculation (font metrics * rows). The proper harvested `layoutTable()` with content-based column widths and cell word-wrapping is a follow-up. The simplified version is sufficient for MVP.
- **Column resize by dragging**: Requires mouse drag tracking on column borders.
- **Sort by column**: Requires reading all cells, sorting, rewriting.
- **Move/duplicate rows and columns**: Context menu stubs, implementation follow-up.
- **Inline markdown in table cells**: Cells currently contain plain text. Bold, italic, links within cells are a follow-up.
- **Other replaced blocks** (images, math, mermaid, embeds): The layout infrastructure from Task 5 enables these, but their implementation is separate work.

**Key risk to monitor during implementation:** The cursor positioning when replacing a QTextTable frame with pipe text (`revertTables()`) is the trickiest operation. The QTextCursor must correctly select the entire frame boundary. If this proves unreliable, an alternative is to track the character positions of table frames and use those for selection.
