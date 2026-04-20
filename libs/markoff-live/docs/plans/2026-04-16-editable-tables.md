# Editable Tables Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace read-only `TableBlockItem` with live-editable `QTextTable`s inside `MarkdownTextItem`, giving native cell editing, undo/redo, and cursor navigation.

**Architecture:** Tables stop being separate scene items. The `MarkdownSplitter` stops splitting at table boundaries; pipe text stays inside `MarkdownTextItem`'s `QTextDocument` and is converted to a `QTextTable` post-load. Since `QTextDocument` already uses Qt's rich-text `QTextDocumentLayout` (not a forked plain-text layout), `QTextTable` layout and rendering works out of the box — no layout engine harvesting needed. The work is: (1) `TableSerializer` to round-trip QTextTable ↔ pipe markdown, (2) `TableConverter` to detect and convert pipe text, (3) fix `allMarkdown()` to serialize table frames, (4) TextControl nav tweaks, (5) context menu + public API.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), QTextTable, QTextCursor

**Spec:** `docs/specs/2026-04-16-editable-tables-design.md`

---

## File Map

| File | Responsibility |
|------|---------------|
| `src/TableSerializer.h/.cpp` (new) | `QTextTable` → auto-formatted pipe markdown. Standalone utility, no deps beyond Qt. |
| `src/TableConverter.h/.cpp` (new) | Pipe text → `QTextTable` conversion + reparse reconciliation via `TableRecord` tracking. |
| `src/TableStyle.h` (new) | Visual constants struct (grid colors, padding, header bg). Theme-ready defaults. |
| `src/MarkdownTextItem.cpp` | `allMarkdown()`/`selectedMarkdown()` must serialize table frames via `TableSerializer`. |
| `src/SceneCoordinator.cpp` | Stop creating `TableBlockItem`; run `TableConverter` after `loadMarkdown()` and on reparse. |
| `src/TextControl.cpp` | Revise Enter, add up/down cross-cell nav, smart column memory, Escape blank-line insertion. |
| `src/MarkdownHighlighter.cpp` | Table-frame skip guard already exists (lines 310-320) — verify it still works. |
| `src/Editor.cpp` / `include/markoff/Editor.h` | Table signals, operation slots, context menu. |
| `libs/markoff-parser/src/MarkdownSplitter.cpp` | Stop splitting at table boundaries. |
| `CMakeLists.txt` | Add new source files. |
| `tests/tst_table_serializer.cpp` (new) | Serializer unit tests. |
| `tests/tst_table_converter.cpp` (new) | Converter unit tests. |
| `tests/tst_table_navigation.cpp` (new) | Navigation integration tests. |
| `tests/tst_table_operations.cpp` (new) | Context menu operations tests. |
| `tests/tst_table_integration.cpp` (new) | End-to-end round-trip tests. |

---

### Task 1: TableSerializer — QTextTable → Pipe Markdown

**Files:**
- Create: `libs/markoff/src/TableSerializer.h`
- Create: `libs/markoff/src/TableSerializer.cpp`
- Create: `libs/markoff/tests/tst_table_serializer.cpp`
- Modify: `libs/markoff/CMakeLists.txt`
- Modify: `libs/markoff/tests/CMakeLists.txt`

This is the foundation — a pure utility that takes a `QTextTable*` and produces auto-formatted pipe markdown. No dependencies on the rest of Markoff. We build and test it in isolation before touching anything else.

- [ ] **Step 1: Write failing test — basic 2x2 table serialization**

Create `tests/tst_table_serializer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>

#include "TableSerializer.h"

using namespace Markoff;

class TestTableSerializer : public QObject {
    Q_OBJECT

private:
    QTextTable *makeTable(QTextDocument *doc, int rows, int cols,
                          const QStringList &cells);

private slots:
    void serializeBasic2x2();
    void serializeAlignmentMarkers();
    void serializeAutoFormatsPadding();
    void serializeMinimumWidth();
    void serializeEmptyCells();
    void serializeSingleColumn();
    void extractAlignments();
};

QTextTable *TestTableSerializer::makeTable(QTextDocument *doc, int rows,
                                            int cols,
                                            const QStringList &cells)
{
    QTextCursor cursor(doc);
    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    auto *table = cursor.insertTable(rows, cols, fmt);
    for (int i = 0; i < cells.size() && i < rows * cols; ++i) {
        int r = i / cols;
        int c = i % cols;
        QTextCursor cellCursor = table->cellAt(r, c).firstCursorPosition();
        cellCursor.insertText(cells[i]);
    }
    return table;
}

void TestTableSerializer::serializeBasic2x2()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 2, {
        QStringLiteral("Name"), QStringLiteral("Age"),
        QStringLiteral("Alice"), QStringLiteral("30")
    });
    QString result = TableSerializer::serialize(table);
    QString expected =
        QStringLiteral("| Name  | Age |\n")
      + QStringLiteral("| ----- | --- |\n")
      + QStringLiteral("| Alice | 30  |");
    QCOMPARE(result, expected);
}

void TestTableSerializer::serializeAlignmentMarkers()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 3, {
        QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right"),
        QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")
    });
    QList<Qt::Alignment> alignments = {
        Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight
    };
    QString result = TableSerializer::serialize(table, alignments);
    // Check separator row has correct markers
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines[1].contains(QStringLiteral(":---")));    // left
    QVERIFY(lines[1].contains(QStringLiteral(":---:")));   // center (substring)
    QVERIFY(lines[1].contains(QStringLiteral("---:")));    // right
}

void TestTableSerializer::serializeAutoFormatsPadding()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 2, {
        QStringLiteral("Name"), QStringLiteral("Age"),
        QStringLiteral("Bobbert"), QStringLiteral("42")
    });
    QString result = TableSerializer::serialize(table);
    // "Bobbert" is longest in col 0 → "Name" should be padded to match
    QStringList lines = result.split(QLatin1Char('\n'));
    // Header and data cells in same column should have same total width
    // between pipes
    int headerCol0Start = lines[0].indexOf(QLatin1Char('|')) + 1;
    int headerCol0End = lines[0].indexOf(QLatin1Char('|'), headerCol0Start);
    int dataCol0Start = lines[2].indexOf(QLatin1Char('|')) + 1;
    int dataCol0End = lines[2].indexOf(QLatin1Char('|'), dataCol0Start);
    QCOMPARE(headerCol0End - headerCol0Start, dataCol0End - dataCol0Start);
}

void TestTableSerializer::serializeMinimumWidth()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 1, {
        QStringLiteral("A"),
        QStringLiteral("B")
    });
    QString result = TableSerializer::serialize(table);
    // Separator must be at least 3 dashes
    QStringList lines = result.split(QLatin1Char('\n'));
    QVERIFY(lines[1].contains(QStringLiteral("---")));
}

void TestTableSerializer::serializeEmptyCells()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 2, {
        QStringLiteral("H1"), QStringLiteral("H2"),
        QString(), QString()
    });
    QString result = TableSerializer::serialize(table);
    // Should still produce valid pipe table with spaces
    QVERIFY(result.contains(QStringLiteral("|")));
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
}

void TestTableSerializer::serializeSingleColumn()
{
    QTextDocument doc;
    auto *table = makeTable(&doc, 2, 1, {
        QStringLiteral("Header"),
        QStringLiteral("Data")
    });
    QString result = TableSerializer::serialize(table);
    QStringList lines = result.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    // Each line starts and ends with |
    for (const auto &line : lines) {
        QVERIFY(line.startsWith(QLatin1Char('|')));
        QVERIFY(line.endsWith(QLatin1Char('|')));
    }
}

void TestTableSerializer::extractAlignments()
{
    QList<Qt::Alignment> result = TableSerializer::parseAlignments(
        QStringLiteral("| :--- | :---: | ---: | --- |"));
    QCOMPARE(result.size(), 4);
    QCOMPARE(result[0], Qt::AlignLeft);
    QCOMPARE(result[1], Qt::AlignHCenter);
    QCOMPARE(result[2], Qt::AlignRight);
    QCOMPARE(result[3], Qt::Alignment{});  // none
}

QTEST_MAIN(TestTableSerializer)
#include "tst_table_serializer.moc"
```

- [ ] **Step 2: Write TableSerializer header**

Create `libs/markoff/src/TableSerializer.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLESERIALIZER_H
#define MARKOFF_TABLESERIALIZER_H

#include <QString>
#include <QList>

class QTextTable;

namespace Markoff {

class TableSerializer {
public:
    static QString serialize(const QTextTable *table,
                             const QList<Qt::Alignment> &alignments = {});

    static QList<Qt::Alignment> parseAlignments(const QString &separatorLine);
};

} // namespace Markoff

#endif // MARKOFF_TABLESERIALIZER_H
```

- [ ] **Step 3: Write TableSerializer implementation**

Create `libs/markoff/src/TableSerializer.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableSerializer.h"
#include <QTextTable>
#include <QTextCursor>
#include <QTextDocument>

namespace Markoff {

static QString cellText(const QTextTable *table, int row, int col)
{
    QTextTableCell cell = table->cellAt(row, col);
    QTextCursor cur = cell.firstCursorPosition();
    cur.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    return cur.selectedText().trimmed();
}

QString TableSerializer::serialize(const QTextTable *table,
                                    const QList<Qt::Alignment> &alignments)
{
    const int rows = table->rows();
    const int cols = table->columns();

    // Collect cell text and compute column widths
    QList<QStringList> data;
    data.reserve(rows);
    QList<int> colWidths(cols, 3); // minimum 3 for "---"

    for (int r = 0; r < rows; ++r) {
        QStringList row;
        row.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            QString text = cellText(table, r, c);
            row.append(text);
            colWidths[c] = qMax(colWidths[c], text.length());
        }
        data.append(row);
    }

    auto padCell = [](const QString &text, int width, Qt::Alignment align) -> QString {
        int pad = width - text.length();
        if (pad <= 0) return text;
        if (align == Qt::AlignRight)
            return QString(pad, QLatin1Char(' ')) + text;
        if (align == Qt::AlignHCenter) {
            int left = pad / 2;
            int right = pad - left;
            return QString(left, QLatin1Char(' ')) + text + QString(right, QLatin1Char(' '));
        }
        return text + QString(pad, QLatin1Char(' '));
    };

    auto getAlign = [&](int col) -> Qt::Alignment {
        if (col < alignments.size())
            return alignments[col];
        return {};
    };

    QString out;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            out += QStringLiteral("| ");
            out += padCell(data[r][c], colWidths[c], getAlign(c));
            out += QLatin1Char(' ');
        }
        out += QLatin1Char('|');

        // Separator after header (row 0)
        if (r == 0) {
            out += QLatin1Char('\n');
            for (int c = 0; c < cols; ++c) {
                out += QStringLiteral("| ");
                Qt::Alignment a = getAlign(c);
                int w = colWidths[c];
                if (a == Qt::AlignHCenter) {
                    out += QLatin1Char(':');
                    out += QString(w - 2, QLatin1Char('-'));
                    out += QLatin1Char(':');
                } else if (a == Qt::AlignRight) {
                    out += QString(w - 1, QLatin1Char('-'));
                    out += QLatin1Char(':');
                } else if (a == Qt::AlignLeft) {
                    out += QLatin1Char(':');
                    out += QString(w - 1, QLatin1Char('-'));
                } else {
                    out += QString(w, QLatin1Char('-'));
                }
                out += QLatin1Char(' ');
            }
            out += QLatin1Char('|');
        }

        if (r < rows - 1)
            out += QLatin1Char('\n');
    }
    return out;
}

QList<Qt::Alignment> TableSerializer::parseAlignments(const QString &line)
{
    QList<Qt::Alignment> result;
    const QStringList cells = line.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &cell : cells) {
        QString trimmed = cell.trimmed();
        bool left = trimmed.startsWith(QLatin1Char(':'));
        bool right = trimmed.endsWith(QLatin1Char(':'));
        if (left && right)
            result.append(Qt::AlignHCenter);
        else if (right)
            result.append(Qt::AlignRight);
        else if (left)
            result.append(Qt::AlignLeft);
        else
            result.append({});
    }
    return result;
}

} // namespace Markoff
```

- [ ] **Step 4: Register files in CMakeLists**

Add to `libs/markoff/CMakeLists.txt` source list (after `src/DecoratedRange.cpp`):

```cmake
    src/TableSerializer.h
    src/TableSerializer.cpp
```

Add to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_table_serializer tst_table_serializer.cpp)
add_test(NAME tst_markoff_table_serializer COMMAND tst_markoff_table_serializer)
target_link_libraries(tst_markoff_table_serializer PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_table_serializer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_serializer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Build and run tests**

Run: `cd /home/clinton/dev/Corbomite && cmake --build build --target tst_markoff_table_serializer && QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_table_serializer`

Expected: All 7 tests pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/TableSerializer.h libs/markoff/src/TableSerializer.cpp \
       libs/markoff/tests/tst_table_serializer.cpp \
       libs/markoff/CMakeLists.txt libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): add TableSerializer for QTextTable → pipe markdown"
```

---

### Task 2: TableConverter — Pipe Text → QTextTable

**Files:**
- Create: `libs/markoff/src/TableConverter.h`
- Create: `libs/markoff/src/TableConverter.cpp`
- Create: `libs/markoff/tests/tst_table_converter.cpp`
- Modify: `libs/markoff/CMakeLists.txt`
- Modify: `libs/markoff/tests/CMakeLists.txt`

The converter takes a `QTextDocument` and a list of table regions (from the parser) and replaces pipe text with `QTextTable`s. It also tracks `TableRecord`s for reparse reconciliation.

- [ ] **Step 1: Write failing test — convert pipe text to QTextTable**

Create `tests/tst_table_converter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>

#include "TableConverter.h"

using namespace Markoff;

class TestTableConverter : public QObject {
    Q_OBJECT

private slots:
    void convertBasicTable();
    void convertPreservesTextAround();
    void convertRespectsAlignment();
    void convertMultipleTables();
    void reconcileNoChange();
    void reconcileNewTable();
    void reconcileTableDeleted();
};

void TestTableConverter::convertBasicTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |"));

    TableConverter converter;
    TableConverter::TableRegion region;
    region.startPos = 0;
    region.endPos = doc.characterCount() - 1;
    region.rows = 2;
    region.cols = 2;
    region.headers = {QStringLiteral("A"), QStringLiteral("B")};
    region.dataRows = {{QStringLiteral("1"), QStringLiteral("2")}};
    region.alignments = {};

    converter.convert(&doc, {region});

    // Should have a QTextTable in the document
    auto frames = doc.rootFrame()->childFrames();
    QCOMPARE(frames.size(), 1);
    auto *table = qobject_cast<QTextTable *>(frames.first());
    QVERIFY(table);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 2);
}

void TestTableConverter::convertPreservesTextAround()
{
    QString text = QStringLiteral("Before\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nAfter");
    QTextDocument doc;
    doc.setPlainText(text);

    // Find table region (lines 2-4, char offsets vary)
    int tableStart = text.indexOf(QLatin1Char('|'));
    int tableEnd = text.indexOf(QStringLiteral("|\n\nAfter")) + 1;

    TableConverter converter;
    TableConverter::TableRegion region;
    region.startPos = tableStart;
    region.endPos = tableEnd;
    region.rows = 2;
    region.cols = 2;
    region.headers = {QStringLiteral("A"), QStringLiteral("B")};
    region.dataRows = {{QStringLiteral("1"), QStringLiteral("2")}};

    converter.convert(&doc, {region});

    // Text before and after should survive
    QTextBlock first = doc.begin();
    QCOMPARE(first.text(), QStringLiteral("Before"));

    // There should be a table frame
    auto frames = doc.rootFrame()->childFrames();
    QCOMPARE(frames.size(), 1);
}

void TestTableConverter::convertRespectsAlignment()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| L | C | R |\n| :--- | :---: | ---: |\n| a | b | c |"));

    TableConverter converter;
    TableConverter::TableRegion region;
    region.startPos = 0;
    region.endPos = doc.characterCount() - 1;
    region.rows = 2;
    region.cols = 3;
    region.headers = {QStringLiteral("L"), QStringLiteral("C"), QStringLiteral("R")};
    region.dataRows = {{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}};
    region.alignments = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};

    converter.convert(&doc, {region});

    // Alignments should be stored and retrievable
    QCOMPARE(converter.records().size(), 1);
    QCOMPARE(converter.records().first().alignments.size(), 3);
    QCOMPARE(converter.records().first().alignments[1], Qt::AlignHCenter);
}

void TestTableConverter::convertMultipleTables()
{
    QString text = QStringLiteral(
        "| A |\n| --- |\n| 1 |\n\nMiddle\n\n| B |\n| --- |\n| 2 |");
    QTextDocument doc;
    doc.setPlainText(text);

    int t1Start = 0;
    int t1End = text.indexOf(QStringLiteral("|\n\nMiddle")) + 1;
    int t2Start = text.lastIndexOf(QStringLiteral("| B |"));
    int t2End = text.length();

    TableConverter converter;
    QList<TableConverter::TableRegion> regions;

    TableConverter::TableRegion r1;
    r1.startPos = t1Start;
    r1.endPos = t1End;
    r1.rows = 2;
    r1.cols = 1;
    r1.headers = {QStringLiteral("A")};
    r1.dataRows = {{QStringLiteral("1")}};
    regions.append(r1);

    TableConverter::TableRegion r2;
    r2.startPos = t2Start;
    r2.endPos = t2End;
    r2.rows = 2;
    r2.cols = 1;
    r2.headers = {QStringLiteral("B")};
    r2.dataRows = {{QStringLiteral("2")}};
    regions.append(r2);

    converter.convert(&doc, regions);

    auto frames = doc.rootFrame()->childFrames();
    QCOMPARE(frames.size(), 2);
}

void TestTableConverter::reconcileNoChange()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A |\n| --- |\n| 1 |"));

    TableConverter converter;
    TableConverter::TableRegion region;
    region.startPos = 0;
    region.endPos = doc.characterCount() - 1;
    region.rows = 2;
    region.cols = 1;
    region.headers = {QStringLiteral("A")};
    region.dataRows = {{QStringLiteral("1")}};

    converter.convert(&doc, {region});
    QCOMPARE(converter.records().size(), 1);

    // Reconcile with same table structure — should be no-op
    bool changed = converter.reconcile(&doc, {region});
    QVERIFY(!changed);
    QCOMPARE(converter.records().size(), 1);
}

void TestTableConverter::reconcileNewTable()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("Hello\n\n| A |\n| --- |\n| 1 |"));

    TableConverter converter;
    // No initial tables
    QCOMPARE(converter.records().size(), 0);

    // Now a table appears
    TableConverter::TableRegion region;
    region.startPos = 8;
    region.endPos = doc.characterCount() - 1;
    region.rows = 2;
    region.cols = 1;
    region.headers = {QStringLiteral("A")};
    region.dataRows = {{QStringLiteral("1")}};

    bool changed = converter.reconcile(&doc, {region});
    QVERIFY(changed);
    QCOMPARE(converter.records().size(), 1);
}

void TestTableConverter::reconcileTableDeleted()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("| A |\n| --- |\n| 1 |"));

    TableConverter converter;
    TableConverter::TableRegion region;
    region.startPos = 0;
    region.endPos = doc.characterCount() - 1;
    region.rows = 2;
    region.cols = 1;
    region.headers = {QStringLiteral("A")};
    region.dataRows = {{QStringLiteral("1")}};

    converter.convert(&doc, {region});
    QCOMPARE(converter.records().size(), 1);

    // Reconcile with no tables — record should be cleared
    bool changed = converter.reconcile(&doc, {});
    QVERIFY(changed);
    QCOMPARE(converter.records().size(), 0);
}

QTEST_MAIN(TestTableConverter)
#include "tst_table_converter.moc"
```

- [ ] **Step 2: Write TableConverter header**

Create `libs/markoff/src/TableConverter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLECONVERTER_H
#define MARKOFF_TABLECONVERTER_H

#include <QList>
#include <QString>
#include <QStringList>

class QTextDocument;
class QTextTable;

namespace Markoff {

class TableConverter {
public:
    struct TableRegion {
        int startPos = 0;
        int endPos = 0;
        int rows = 0;
        int cols = 0;
        QStringList headers;
        QList<QStringList> dataRows;
        QList<Qt::Alignment> alignments;
    };

    struct TableRecord {
        QTextTable *table = nullptr;
        int rows = 0;
        int cols = 0;
        QList<Qt::Alignment> alignments;
    };

    void convert(QTextDocument *doc, const QList<TableRegion> &regions);

    bool reconcile(QTextDocument *doc, const QList<TableRegion> &regions);

    const QList<TableRecord> &records() const { return m_records; }

    void clear() { m_records.clear(); }

private:
    QTextTable *insertTable(QTextDocument *doc, const TableRegion &region,
                            int adjustedStart, int adjustedEnd);
    QList<TableRecord> m_records;
};

} // namespace Markoff

#endif // MARKOFF_TABLECONVERTER_H
```

- [ ] **Step 3: Write TableConverter implementation**

Create `libs/markoff/src/TableConverter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableConverter.h"
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>
#include <QTextTableFormat>

namespace Markoff {

QTextTable *TableConverter::insertTable(QTextDocument *doc,
                                         const TableRegion &region,
                                         int start, int end)
{
    QTextCursor cursor(doc);
    cursor.setPosition(start);
    cursor.setPosition(qMin(end, doc->characterCount() - 1),
                       QTextCursor::KeepAnchor);

    cursor.beginEditBlock();
    cursor.removeSelectedText();

    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    fmt.setCellPadding(8);
    fmt.setCellSpacing(0);
    fmt.setBorder(1);

    auto *table = cursor.insertTable(region.rows, region.cols, fmt);

    // Populate header row (row 0)
    for (int c = 0; c < region.cols && c < region.headers.size(); ++c) {
        QTextCursor cell = table->cellAt(0, c).firstCursorPosition();
        cell.insertText(region.headers[c]);
    }

    // Populate data rows
    for (int r = 0; r < region.dataRows.size(); ++r) {
        const auto &rowData = region.dataRows[r];
        for (int c = 0; c < region.cols && c < rowData.size(); ++c) {
            QTextCursor cell = table->cellAt(r + 1, c).firstCursorPosition();
            cell.insertText(rowData[c]);
        }
    }

    cursor.endEditBlock();
    return table;
}

void TableConverter::convert(QTextDocument *doc,
                              const QList<TableRegion> &regions)
{
    m_records.clear();

    // Process regions in reverse order so earlier positions aren't shifted
    for (int i = regions.size() - 1; i >= 0; --i) {
        const auto &region = regions[i];
        auto *table = insertTable(doc, region, region.startPos, region.endPos);

        TableRecord record;
        record.table = table;
        record.rows = region.rows;
        record.cols = region.cols;
        record.alignments = region.alignments;
        m_records.prepend(record);
    }
}

bool TableConverter::reconcile(QTextDocument *doc,
                                const QList<TableRegion> &regions)
{
    // Find existing tables in the document
    QList<QTextTable *> existingTables;
    for (auto *frame : doc->rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(frame))
            existingTables.append(t);
    }

    // If counts match and each existing table is still alive, no change
    if (existingTables.size() == regions.size()
        && existingTables.size() == m_records.size()) {
        bool allMatch = true;
        for (int i = 0; i < m_records.size(); ++i) {
            if (m_records[i].table != existingTables[i]) {
                allMatch = false;
                break;
            }
        }
        if (allMatch)
            return false;
    }

    // Something changed — reconvert new regions that aren't already tables
    // and clean up stale records

    // Rebuild: clear records, convert any regions that aren't already tables
    QList<TableRecord> newRecords;
    for (auto *t : existingTables) {
        // Existing table still present — keep its record
        TableRecord rec;
        rec.table = t;
        rec.rows = t->rows();
        rec.cols = t->columns();
        // Find matching alignment from old records
        for (const auto &old : m_records) {
            if (old.table == t) {
                rec.alignments = old.alignments;
                break;
            }
        }
        newRecords.append(rec);
    }

    // Convert any new pipe-text regions (regions without a matching table)
    int existingIdx = 0;
    for (const auto &region : regions) {
        if (existingIdx < existingTables.size()) {
            existingIdx++;
            continue;
        }
        // New table — convert it
        auto *table = insertTable(doc, region, region.startPos, region.endPos);
        TableRecord rec;
        rec.table = table;
        rec.rows = region.rows;
        rec.cols = region.cols;
        rec.alignments = region.alignments;
        newRecords.append(rec);
    }

    bool changed = (newRecords.size() != m_records.size());
    m_records = newRecords;
    return changed;
}

} // namespace Markoff
```

- [ ] **Step 4: Register files in CMakeLists**

Add to `libs/markoff/CMakeLists.txt` source list:

```cmake
    src/TableConverter.h
    src/TableConverter.cpp
```

Add to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_table_converter tst_table_converter.cpp)
add_test(NAME tst_markoff_table_converter COMMAND tst_markoff_table_converter)
target_link_libraries(tst_markoff_table_converter PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_table_converter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_converter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Build and run tests**

Run: `cd /home/clinton/dev/Corbomite && cmake --build build --target tst_markoff_table_converter && QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_table_converter`

Expected: All 7 tests pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/TableConverter.h libs/markoff/src/TableConverter.cpp \
       libs/markoff/tests/tst_table_converter.cpp \
       libs/markoff/CMakeLists.txt libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): add TableConverter for pipe text → QTextTable conversion"
```

---

### Task 3: TableStyle Defaults

**Files:**
- Create: `libs/markoff/src/TableStyle.h`
- Modify: `libs/markoff/CMakeLists.txt`

A small header-only struct with visual constants. No test needed — it's just a data struct.

- [ ] **Step 1: Create TableStyle.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLESTYLE_H
#define MARKOFF_TABLESTYLE_H

#include <QColor>

namespace Markoff {

struct TableStyle {
    QColor headerBackground{240, 240, 240};
    QColor gridLineColor{208, 208, 208};
    QColor headerSeparatorColor{160, 160, 160};
    qreal cellPadding = 8.0;
    qreal gridLineWidth = 1.0;
    qreal headerSeparatorWidth = 2.0;
};

} // namespace Markoff

#endif // MARKOFF_TABLESTYLE_H
```

- [ ] **Step 2: Register in CMakeLists**

Add `src/TableStyle.h` to the source list in `libs/markoff/CMakeLists.txt`.

- [ ] **Step 3: Build**

Run: `cd /home/clinton/dev/Corbomite && cmake --build build --target markoff`

Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff/src/TableStyle.h libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add TableStyle struct with theme-ready defaults"
```

---

### Task 4: MarkdownSplitter — Stop Splitting at Table Boundaries

**Files:**
- Modify: `libs/markoff-parser/src/MarkdownSplitter.cpp:64-83`

Tables must stay inside `Text` segments so they end up in `MarkdownTextItem`'s `QTextDocument` for conversion to `QTextTable`.

- [ ] **Step 1: Write failing test**

Add to a new file `libs/markoff-parser/tests/tst_splitter_tables.cpp` (or add to existing splitter tests if they exist):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>

#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TreeSitterParser.h>

using namespace Markoff;

class TestSplitterTables : public QObject {
    Q_OBJECT
private slots:
    void tableStaysInTextSegment();
    void tableWithSurroundingText();
    void imageStillSplitsOut();
};

void TestSplitterTables::tableStaysInTextSegment()
{
    TreeSitterParser parser;
    QString md = QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |");
    auto segments = MarkdownSplitter::split(md, parser);

    // Table should NOT be split out — should be a single Text segment
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("| A | B |")));
}

void TestSplitterTables::tableWithSurroundingText()
{
    TreeSitterParser parser;
    QString md = QStringLiteral("Before\n\n| A |\n| --- |\n| 1 |\n\nAfter");
    auto segments = MarkdownSplitter::split(md, parser);

    // Everything should be one Text segment (table not split out)
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Before")));
    QVERIFY(segments[0].text.contains(QStringLiteral("After")));
}

void TestSplitterTables::imageStillSplitsOut()
{
    TreeSitterParser parser;
    QString md = QStringLiteral("Text\n\n![alt](image.png)\n\nMore");
    auto segments = MarkdownSplitter::split(md, parser);

    // Image should still be split out
    bool hasImage = false;
    for (const auto &seg : segments) {
        if (seg.type == MarkdownSegment::Image)
            hasImage = true;
    }
    QVERIFY(hasImage);
}

QTEST_MAIN(TestSplitterTables)
#include "tst_splitter_tables.moc"
```

- [ ] **Step 2: Register test in CMakeLists**

Check if `libs/markoff-parser/tests/CMakeLists.txt` exists. If so, add:

```cmake
add_executable(tst_splitter_tables tst_splitter_tables.cpp)
add_test(NAME tst_splitter_tables COMMAND tst_splitter_tables)
target_link_libraries(tst_splitter_tables PRIVATE Qt6::Test MarkoffParser::MarkoffParser)
set_tests_properties(tst_splitter_tables PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

If the parser doesn't have a tests dir, put the test in `libs/markoff/tests/` instead (linking `MarkoffParser::MarkoffParser`).

- [ ] **Step 3: Run test to verify it fails**

Run: build and execute. Expected: `tableStaysInTextSegment` FAILS because the splitter currently emits a `Table` segment.

- [ ] **Step 4: Modify MarkdownSplitter to skip table boundaries**

In `libs/markoff-parser/src/MarkdownSplitter.cpp:64-75`, change the switch to skip `Table` boundaries:

```cpp
        // The block itself
        MarkdownSegment blockSeg;
        switch (boundary.type) {
        case TreeSitterParser::BlockBoundary::Table:
            // Tables stay in text segments — they'll be converted to
            // QTextTable by TableConverter in the editor.
            continue;
        case TreeSitterParser::BlockBoundary::FencedCodeBlock:
            blockSeg.type = MarkdownSegment::FencedCodeBlock;
            break;
        case TreeSitterParser::BlockBoundary::Image:
            blockSeg.type = MarkdownSegment::Image;
            break;
        }
```

Also fix the text-before-block logic: when we `continue` past a Table boundary, the `pos` variable must NOT advance. The `pos = boundary.endChar` at line 85 should only execute when we actually emit a block segment. Move it inside the block emission:

```cpp
        blockSeg.text = markdown.mid(boundary.startChar,
                                      boundary.endChar - boundary.startChar);
        if (blockSeg.text.endsWith(QLatin1Char('\n')))
            blockSeg.text.chop(1);
        blockSeg.sourceStart = boundary.startChar;
        blockSeg.sourceEnd = boundary.endChar;
        segments.append(blockSeg);

        pos = boundary.endChar;
```

- [ ] **Step 5: Run tests**

Run: build and execute. Expected: All 3 splitter tests pass. Also run existing markoff tests to check for regressions:

`cd /home/clinton/dev/Corbomite/build && ctest -R markoff --output-on-failure`

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-parser/src/MarkdownSplitter.cpp \
       libs/markoff-parser/tests/tst_splitter_tables.cpp \
       libs/markoff-parser/tests/CMakeLists.txt
git commit -m "feat(markoff-parser): tables stay in text segments for QTextTable conversion"
```

---

### Task 5: Wire TableConverter into SceneCoordinator

**Files:**
- Modify: `libs/markoff/src/SceneCoordinator.h`
- Modify: `libs/markoff/src/SceneCoordinator.cpp:98-122` (loadMarkdown) and `526-604` (reparse)

After `loadMarkdown()` creates `MarkdownTextItem`s (which now contain pipe text for tables), run `TableConverter` to replace pipe text with `QTextTable`s. On reparse, reconcile.

- [ ] **Step 1: Add TableConverter member to SceneCoordinator**

In `SceneCoordinator.h`, add:

```cpp
#include "TableConverter.h"
```

Add private member:

```cpp
    QHash<MarkdownTextItem *, TableConverter> m_tableConverters;
```

- [ ] **Step 2: Build a helper to detect table regions from parser output**

Add a private method to `SceneCoordinator`:

```cpp
    QList<TableConverter::TableRegion> detectTableRegions(
        const QString &markdown) const;
```

Implementation in `.cpp` — uses `TreeSitterParser::findBlockBoundaries()` plus `TableHandler` to parse the pipe text:

```cpp
#include <markoff-parser/TableHandler.h>
#include "TableSerializer.h"

QList<TableConverter::TableRegion>
SceneCoordinator::detectTableRegions(const QString &markdown) const
{
    QList<TableConverter::TableRegion> regions;
    if (!m_parser->parse(markdown))
        return regions;

    auto boundaries = m_parser->findBlockBoundaries();
    for (const auto &b : boundaries) {
        if (b.type != TreeSitterParser::BlockBoundary::Table)
            continue;

        QString tableText = markdown.mid(b.startChar, b.endChar - b.startChar);
        if (tableText.endsWith(QLatin1Char('\n')))
            tableText.chop(1);

        QStringList lines = tableText.split(QLatin1Char('\n'));
        if (lines.size() < 2)
            continue;

        TableConverter::TableRegion region;
        region.startPos = b.startChar;
        region.endPos = b.endChar;
        region.headers = TableHandler::parseRow(lines[0]);
        region.cols = region.headers.size();

        // Line 1 is the separator — parse alignments
        region.alignments = TableSerializer::parseAlignments(lines[1]);

        // Lines 2+ are data rows
        for (int i = 2; i < lines.size(); ++i) {
            QStringList row = TableHandler::parseRow(lines[i]);
            // Pad or truncate to match column count
            while (row.size() < region.cols)
                row.append(QString());
            while (row.size() > region.cols)
                row.removeLast();
            region.dataRows.append(row);
        }
        region.rows = 1 + region.dataRows.size(); // header + data

        regions.append(region);
    }
    return regions;
}
```

- [ ] **Step 3: Call TableConverter in loadMarkdown()**

After `createTextItem(seg.text)` in `loadMarkdown()`, if the text item might contain tables, detect and convert:

```cpp
void SceneCoordinator::loadMarkdown(const QString &markdown)
{
    clearItems();
    m_tableConverters.clear();

    auto segments = MarkdownSplitter::split(markdown, *m_parser);

    for (const auto &seg : segments) {
        if (seg.type == MarkdownSegment::Text) {
            auto *item = createTextItem(seg.text);

            // Detect and convert any pipe tables within this text item
            auto regions = detectTableRegions(seg.text);
            if (!regions.isEmpty()) {
                TableConverter &converter = m_tableConverters[item];
                converter.convert(item->document(), regions);
            }
        } else if (seg.type == MarkdownSegment::Image) {
            auto *item = new ImageBlockItem(seg.text, m_itemWidth, m_resourceProvider);
            m_scene->addItem(item);
            m_items.append(item);
        }
        // Table segments no longer created here — they stay in text items
    }

    repositionItems();
    m_scene->setSelectableItems(m_items);
    m_headingMapDirty = true;
    emit reparsed();
}
```

- [ ] **Step 4: Remove TableBlockItem from reparse and other references**

In `reparse()`, remove the `TableBlockItem` creation branch. The same pattern as `loadMarkdown()` applies — after creating text items, convert tables. Also remove the `#include "TableBlockItem.h"` and the `dynamic_cast<TableBlockItem *>` in `setFont()`.

- [ ] **Step 5: Build and run existing tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . && ctest -R markoff --output-on-failure`

Expected: Existing tests pass. Some tests that depend on `TableBlockItem` being a separate scene item may need updating — check test output and fix.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/SceneCoordinator.h libs/markoff/src/SceneCoordinator.cpp
git commit -m "feat(markoff): wire TableConverter into SceneCoordinator load and reparse"
```

---

### Task 6: Fix allMarkdown() to Serialize Table Frames

**Files:**
- Modify: `libs/markoff/src/MarkdownTextItem.cpp:174-206` (allMarkdown)
- Create: `libs/markoff/tests/tst_table_integration.cpp`

The `allMarkdown()` method iterates `doc->begin()` to `doc->end()`, which skips blocks inside `QTextTable` frames. We must detect table frames and serialize them via `TableSerializer`.

- [ ] **Step 1: Write failing test — round-trip table through editor**

Create `tests/tst_table_integration.cpp`:

```cpp
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
};

void TestTableIntegration::roundTripBasicTable()
{
    Editor editor;
    editor.resize(400, 300);
    editor.setPlainText(
        QStringLiteral("| Name | Age |\n| --- | --- |\n| Alice | 30 |"));
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    // Should contain a well-formed pipe table
    QVERIFY(output.contains(QStringLiteral("| Name")));
    QVERIFY(output.contains(QStringLiteral("| Alice")));
    QVERIFY(output.contains(QStringLiteral("---")));
}

void TestTableIntegration::roundTripTableWithSurroundingText()
{
    Editor editor;
    editor.resize(400, 300);
    QString input = QStringLiteral(
        "# Title\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nParagraph");
    editor.setPlainText(input);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    QVERIFY(output.contains(QStringLiteral("# Title")));
    QVERIFY(output.contains(QStringLiteral("Paragraph")));
    QVERIFY(output.contains(QStringLiteral("| A")));
}

void TestTableIntegration::roundTripMultipleTables()
{
    Editor editor;
    editor.resize(400, 300);
    QString input = QStringLiteral(
        "| A |\n| --- |\n| 1 |\n\nText\n\n| B |\n| --- |\n| 2 |");
    editor.setPlainText(input);
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    QString output = editor.toPlainText();
    QVERIFY(output.contains(QStringLiteral("| A")));
    QVERIFY(output.contains(QStringLiteral("| B")));
    QVERIFY(output.contains(QStringLiteral("Text")));
}

void TestTableIntegration::insertTableViaApi()
{
    Editor editor;
    editor.resize(400, 300);
    editor.setPlainText(QStringLiteral("Hello"));
    editor.show();
    QTest::qWaitForWindowExposed(&editor);

    editor.insertTable(2, 3);
    QString output = editor.toPlainText();
    // Should contain a pipe table with 3 columns
    int pipeCount = output.count(QLatin1Char('|'));
    QVERIFY(pipeCount >= 12); // at least 4 pipes per row × 3 rows
}

QTEST_MAIN(TestTableIntegration)
#include "tst_table_integration.moc"
```

- [ ] **Step 2: Register test in CMakeLists**

Add to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_table_integration tst_table_integration.cpp)
add_test(NAME tst_markoff_table_integration COMMAND tst_markoff_table_integration)
target_link_libraries(tst_markoff_table_integration PRIVATE Qt6::Test Qt6::Widgets markoff)
set_tests_properties(tst_markoff_table_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Fix allMarkdown() to handle table frames**

In `libs/markoff/src/MarkdownTextItem.cpp`, replace the `allMarkdown()` method. The new version must: (a) iterate the root frame's children to find table frames, (b) serialize regular blocks normally, (c) serialize table frames via `TableSerializer`. Add include for `TableSerializer.h` and `TableConverter.h`:

```cpp
#include "TableSerializer.h"
#include <QTextTable>
#include <QTextFrame>

QString MarkdownTextItem::allMarkdown() const
{
    // Collect table frames sorted by document position
    QList<QTextTable *> tables;
    for (auto *frame : m_document->rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(frame))
            tables.append(t);
    }

    // Build a set of positions inside table frames (to skip those blocks)
    auto isInTable = [&](int pos) -> QTextTable * {
        for (auto *t : tables) {
            if (pos >= t->firstPosition() && pos <= t->lastPosition())
                return t;
        }
        return nullptr;
    };

    QString out;
    out.reserve(m_document->characterCount());
    QSet<QTextTable *> serializedTables;

    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        int blockPos = block.position();

        if (auto *table = isInTable(blockPos)) {
            // Serialize this table once (skip subsequent blocks in same table)
            if (!serializedTables.contains(table)) {
                serializedTables.insert(table);
                if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
                    out += QLatin1Char('\n');

                // Retrieve alignments from the TableConverter if available
                // For now, serialize without alignments (none stored yet)
                out += TableSerializer::serialize(table);
            }
            continue;
        }

        // Normal block — expand U+FFFC objects back to source
        const QString blockText = block.text();
        if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
            out += blockText;
        } else {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                const QTextCharFormat fmt = frag.charFormat();
                const QString text = frag.text();
                const QString raw = fmt.property(MathTextObject::RawProperty).toString();
                if (!raw.isEmpty() && text.size() == 1
                    && text.at(0) == QChar::ObjectReplacementCharacter) {
                    out += raw;
                } else {
                    for (QChar c : text) {
                        if (c != QChar::ObjectReplacementCharacter)
                            out += c;
                    }
                }
            }
        }
        if (block.next().isValid() && !isInTable(block.next().position()))
            out += QLatin1Char('\n');
    }
    return out;
}
```

- [ ] **Step 4: Also fix selectedMarkdown() similarly**

The `selectedMarkdown()` method at lines 130-172 also iterates blocks and needs the same table-frame awareness. When the selection spans a table frame, serialize the entire table (or the selected sub-range if partial selection is inside the table).

- [ ] **Step 5: Build and run tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake --build . && ctest -R markoff --output-on-failure`

Expected: New integration tests pass. Existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/MarkdownTextItem.cpp \
       libs/markoff/tests/tst_table_integration.cpp \
       libs/markoff/tests/CMakeLists.txt
git commit -m "fix(markoff): allMarkdown() serializes QTextTable frames via TableSerializer"
```

---

### Task 7: TextControl Navigation Revisions

**Files:**
- Modify: `libs/markoff/src/TextControl.cpp:914-969`
- Create: `libs/markoff/tests/tst_table_navigation.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt`

Revise the existing table navigation in TextControl:
1. Enter creates row only at last row; otherwise moves down and selects cell content.
2. Up/down arrow cross cell boundaries within the table.
3. Smart column memory (`m_preferredColumn`).
4. Escape inserts blank line after table if none exists.

- [ ] **Step 1: Write failing test — Enter navigates down, not insert**

Create `tests/tst_table_navigation.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QTextDocument>
#include <QTextTable>
#include <QTextCursor>

#include "TextControl.h"

using namespace Markoff;

class TestTableNavigation : public QObject {
    Q_OBJECT

private:
    QTextTable *setupTable(TextControl *control);

private slots:
    void enterMovesDownInMiddleRow();
    void enterInsertsRowAtLastRow();
    void tabMovesToNextCell();
    void shiftTabMovesToPreviousCell();
    void tabAtLastCellInsertsRow();
    void escapeExitsTable();
};

QTextTable *TestTableNavigation::setupTable(TextControl *control)
{
    auto *doc = new QTextDocument;
    control->setDocument(doc);
    control->setTextInteractionFlags(Qt::TextEditorInteraction);

    QTextCursor cursor(doc);
    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    auto *table = cursor.insertTable(3, 2, fmt);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("H1"));
    table->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("H2"));
    table->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("A"));
    table->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("B"));
    table->cellAt(2, 0).firstCursorPosition().insertText(QStringLiteral("C"));
    table->cellAt(2, 1).firstCursorPosition().insertText(QStringLiteral("D"));

    // Position cursor in cell (1, 0)
    QTextCursor c = table->cellAt(1, 0).firstCursorPosition();
    control->setTextCursor(c);

    return table;
}

void TestTableNavigation::enterMovesDownInMiddleRow()
{
    TextControl control;
    auto *table = setupTable(&control);
    auto *doc = control.document();

    // Cursor in row 1 (middle row) — Enter should move to row 2, not insert
    int rowsBefore = table->rows();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    control.processEvent(&enter);

    QCOMPARE(table->rows(), rowsBefore); // no new row
    QTextTableCell cell = table->cellAt(control.textCursor());
    QCOMPARE(cell.row(), 2);
    QCOMPARE(cell.column(), 0);
}

void TestTableNavigation::enterInsertsRowAtLastRow()
{
    TextControl control;
    auto *table = setupTable(&control);

    // Move cursor to last row
    QTextCursor c = table->cellAt(2, 0).firstCursorPosition();
    control.setTextCursor(c);

    int rowsBefore = table->rows();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    control.processEvent(&enter);

    QCOMPARE(table->rows(), rowsBefore + 1); // new row added
    QTextTableCell cell = table->cellAt(control.textCursor());
    QCOMPARE(cell.row(), 3);
}

void TestTableNavigation::tabMovesToNextCell()
{
    TextControl control;
    auto *table = setupTable(&control);

    // Cursor in (1,0) — Tab should move to (1,1)
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    control.processEvent(&tab);

    QTextTableCell cell = table->cellAt(control.textCursor());
    QCOMPARE(cell.row(), 1);
    QCOMPARE(cell.column(), 1);
}

void TestTableNavigation::shiftTabMovesToPreviousCell()
{
    TextControl control;
    auto *table = setupTable(&control);

    // Move to (1,1) first
    QTextCursor c = table->cellAt(1, 1).firstCursorPosition();
    control.setTextCursor(c);

    QKeyEvent shiftTab(QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier);
    control.processEvent(&shiftTab);

    QTextTableCell cell = table->cellAt(control.textCursor());
    QCOMPARE(cell.row(), 1);
    QCOMPARE(cell.column(), 0);
}

void TestTableNavigation::tabAtLastCellInsertsRow()
{
    TextControl control;
    auto *table = setupTable(&control);

    // Move to last cell (2, 1)
    QTextCursor c = table->cellAt(2, 1).firstCursorPosition();
    control.setTextCursor(c);

    int rowsBefore = table->rows();
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    control.processEvent(&tab);

    QCOMPARE(table->rows(), rowsBefore + 1);
}

void TestTableNavigation::escapeExitsTable()
{
    TextControl control;
    auto *table = setupTable(&control);

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    control.processEvent(&esc);

    // Cursor should be outside the table
    QTextTable *cursorTable = control.textCursor().currentTable();
    QVERIFY(!cursorTable);
}

QTEST_MAIN(TestTableNavigation)
#include "tst_table_navigation.moc"
```

- [ ] **Step 2: Register test in CMakeLists**

```cmake
add_executable(tst_markoff_table_navigation tst_table_navigation.cpp)
add_test(NAME tst_markoff_table_navigation COMMAND tst_markoff_table_navigation)
target_link_libraries(tst_markoff_table_navigation PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_table_navigation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_table_navigation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run tests to see which fail**

The `enterMovesDownInMiddleRow` test should fail because the current code always inserts a row.

- [ ] **Step 4: Revise Enter behavior in TextControl.cpp**

Replace lines 951-969 in `TextControl.cpp`:

```cpp
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            QTextTableCell cell = table->cellAt(cursor);
            int row = cell.row();
            int col = cell.column();
            if (row == table->rows() - 1) {
                // Last row — insert new row and move there
                table->insertRows(row + 1, 1);
                cursor = table->cellAt(row + 1, col).firstCursorPosition();
            } else {
                // Middle row — move to same column in next row, select content
                QTextTableCell target = table->cellAt(row + 1, col);
                cursor = target.firstCursorPosition();
                cursor.setPosition(target.lastCursorPosition().position(),
                                   QTextCursor::KeepAnchor);
            }
            q->ensureCursorVisible();
            e->accept();
            goto accept;
        }
```

- [ ] **Step 5: Fix Escape to insert blank line if needed**

Replace the Escape handler:

```cpp
        if (e->key() == Qt::Key_Escape) {
            QTextTableCell lastCell = table->cellAt(table->rows() - 1,
                                                     table->columns() - 1);
            cursor = lastCell.lastCursorPosition();
            cursor.movePosition(QTextCursor::NextBlock);
            // If we're still in the table or at document end, insert a blank line
            if (cursor.currentTable() || cursor.atEnd()) {
                cursor.setPosition(table->lastPosition() + 1);
                cursor.insertBlock();
            }
            q->ensureCursorVisible();
            e->accept();
            goto accept;
        }
```

- [ ] **Step 6: Run all tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake --build . && ctest -R markoff --output-on-failure`

Expected: All navigation tests pass.

- [ ] **Step 7: Add smart cursor entry (x-position → column)**

When `cursorMoveKeyEvent` moves the cursor from outside a table into a table frame (up/down arrow), detect the x-position and map to the nearest column. In `TextControl.cpp`, after `cursorMoveKeyEvent(e)` returns, check if the cursor just entered a table:

```cpp
// After cursorMoveKeyEvent, if cursor is now in a table and wasn't before:
QTextTable *tableAfterMove = cursor.currentTable();
if (tableAfterMove && !tableBeforeMove) {
    // Map cursor x-position to nearest column
    QRectF cursorR = q->blockBoundingRect(cursor.block());
    qreal cursorX = cursorR.left() + cursor.columnNumber() * 8; // approximate
    for (int c = 0; c < tableAfterMove->columns(); ++c) {
        QTextTableCell cell = tableAfterMove->cellAt(
            cursor.atEnd() ? tableAfterMove->rows() - 1 : 0, c);
        // Use cellAt position to find x-range
        // If cursorX falls in this column's range, position cursor there
    }
}
```

If the column geometry detection is too complex at this stage, fall back to landing in the first/last cell of the entry row and add a comment:

```cpp
// TODO(scope-b): smart cursor entry — map x-position to nearest column
// using QTextTableData column geometries. For now, land in first cell.
```

- [ ] **Step 8: Commit**

```bash
git add libs/markoff/src/TextControl.cpp \
       libs/markoff/tests/tst_table_navigation.cpp \
       libs/markoff/tests/CMakeLists.txt
git commit -m "fix(markoff): table navigation — Enter moves down, inserts row only at edge"
```

---

### Task 8: Editor Public API — Signals and Operation Slots

**Files:**
- Modify: `libs/markoff/include/markoff/Editor.h`
- Modify: `libs/markoff/src/Editor.cpp`
- Create: `libs/markoff/tests/tst_table_operations.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt`

Add table-awareness signals and structural operation slots to the public `Editor` API.

- [ ] **Step 1: Write failing test — table operations**

Create `tests/tst_table_operations.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QTextTable>
#include <QTextDocument>
#include <markoff/Editor.h>

using namespace Markoff;

class TestTableOperations : public QObject {
    Q_OBJECT
private:
    Editor *makeEditorWithTable();

private slots:
    void tableEnteredSignal();
    void insertRowBelow();
    void insertColumnRight();
    void deleteRow();
    void deleteColumn();
    void cannotDeleteLastRow();
    void cannotDeleteLastColumn();
    void undoGroupsStructuralOp();
};

Editor *TestTableOperations::makeEditorWithTable()
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(
        QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |"));
    editor->show();
    QTest::qWaitForWindowExposed(editor);
    return editor;
}

void TestTableOperations::tableEnteredSignal()
{
    auto *editor = makeEditorWithTable();
    QSignalSpy spy(editor, &Editor::tableEntered);
    // Click in the table area — this requires the table to exist
    // For now, just verify the signal exists and is connectable
    QVERIFY(spy.isValid());
    delete editor;
}

void TestTableOperations::insertRowBelow()
{
    auto *editor = makeEditorWithTable();
    // Need cursor in the table to operate
    editor->goToLine(1); // should land in table
    editor->tableInsertRowBelow();
    QString out = editor->toPlainText();
    // Should now have 4 data rows (3 original + 1 inserted)
    QVERIFY(out.count(QLatin1Char('\n')) >= 4);
    delete editor;
}

void TestTableOperations::insertColumnRight()
{
    auto *editor = makeEditorWithTable();
    editor->goToLine(1);
    editor->tableInsertColumnRight();
    QString out = editor->toPlainText();
    // Each row should now have 3 columns (3 pipes + closing = 4 pipes per row)
    QStringList lines = out.split(QLatin1Char('\n'));
    for (const auto &line : lines) {
        if (line.contains(QLatin1Char('|')))
            QVERIFY(line.count(QLatin1Char('|')) >= 4);
    }
    delete editor;
}

void TestTableOperations::deleteRow()
{
    auto *editor = makeEditorWithTable();
    editor->goToLine(3); // data row
    editor->tableDeleteRow();
    QString out = editor->toPlainText();
    // Should have fewer rows
    QStringList lines = out.split(QLatin1Char('\n'));
    int tableLines = 0;
    for (const auto &l : lines)
        if (l.contains(QLatin1Char('|'))) tableLines++;
    QCOMPARE(tableLines, 3); // header + separator + 1 data row
    delete editor;
}

void TestTableOperations::deleteColumn()
{
    auto *editor = makeEditorWithTable();
    editor->goToLine(1);
    editor->tableDeleteColumn();
    QString out = editor->toPlainText();
    // Should now have 1 column
    QStringList lines = out.split(QLatin1Char('\n'));
    for (const auto &line : lines) {
        if (line.contains(QLatin1Char('|')))
            QCOMPARE(line.count(QLatin1Char('|')), 2); // | content |
    }
    delete editor;
}

void TestTableOperations::cannotDeleteLastRow()
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(QStringLiteral("| A |\n| --- |\n| 1 |"));
    editor->show();
    QTest::qWaitForWindowExposed(editor);

    editor->goToLine(1);
    // Delete one row — should succeed (header remains)
    editor->tableDeleteRow();
    // Try to delete again — should be a no-op (1 row minimum)
    editor->tableDeleteRow();
    QString out = editor->toPlainText();
    QVERIFY(out.contains(QLatin1Char('|'))); // table still exists
    delete editor;
}

void TestTableOperations::cannotDeleteLastColumn()
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(QStringLiteral("| A |\n| --- |\n| 1 |"));
    editor->show();
    QTest::qWaitForWindowExposed(editor);

    editor->goToLine(1);
    // Try to delete the only column — should be a no-op
    editor->tableDeleteColumn();
    QString out = editor->toPlainText();
    QVERIFY(out.contains(QLatin1Char('|'))); // table still exists
    delete editor;
}

void TestTableOperations::undoGroupsStructuralOp()
{
    auto *editor = makeEditorWithTable();
    editor->goToLine(1);
    QString before = editor->toPlainText();
    editor->tableInsertRowBelow();
    QString after = editor->toPlainText();
    QVERIFY(before != after);
    editor->undo();
    QCOMPARE(editor->toPlainText(), before);
    delete editor;
}

QTEST_MAIN(TestTableOperations)
#include "tst_table_operations.moc"
```

- [ ] **Step 2: Add signals and slots to Editor.h**

In `include/markoff/Editor.h`, add after the existing formatting actions section:

```cpp
    // --- Table operations ---
    void tableInsertRowAbove();
    void tableInsertRowBelow();
    void tableInsertColumnLeft();
    void tableInsertColumnRight();
    void tableDeleteRow();
    void tableDeleteColumn();
    void tableSelectRow();
    void tableSelectColumn();
```

Add signals:

```cpp
    void tableEntered(int rows, int cols);
    void tableExited();
    void tableCursorMoved(int row, int col);
```

- [ ] **Step 3: Implement table operations in Editor.cpp**

Add a private helper to find the current table:

```cpp
static QTextTable *currentTable(Editor *editor)
{
    auto *item = editor->focusedTextItem();
    if (!item) return nullptr;
    QTextCursor cursor = item->textControl()->textCursor();
    return cursor.currentTable();
}
```

Implement each slot:

```cpp
void Editor::tableInsertRowAbove()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertRows(cell.row(), 1);
    cursor.endEditBlock();
}

void Editor::tableInsertRowBelow()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertRows(cell.row() + 1, 1);
    cursor.endEditBlock();
}

void Editor::tableInsertColumnLeft()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertColumns(cell.column(), 1);
    cursor.endEditBlock();
}

void Editor::tableInsertColumnRight()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->insertColumns(cell.column() + 1, 1);
    cursor.endEditBlock();
}

void Editor::tableDeleteRow()
{
    auto *table = currentTable(this);
    if (!table || table->rows() <= 1) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->removeRows(cell.row(), 1);
    cursor.endEditBlock();
}

void Editor::tableDeleteColumn()
{
    auto *table = currentTable(this);
    if (!table || table->columns() <= 1) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    cursor.beginEditBlock();
    table->removeColumns(cell.column(), 1);
    cursor.endEditBlock();
}

void Editor::tableSelectRow()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    int row = cell.row();
    QTextCursor sel = table->cellAt(row, 0).firstCursorPosition();
    sel.setPosition(table->cellAt(row, table->columns() - 1).lastCursorPosition().position(),
                    QTextCursor::KeepAnchor);
    item->textControl()->setTextCursor(sel);
}

void Editor::tableSelectColumn()
{
    auto *table = currentTable(this);
    if (!table) return;
    auto *item = focusedTextItem();
    QTextCursor cursor = item->textControl()->textCursor();
    QTextTableCell cell = table->cellAt(cursor);
    int col = cell.column();
    QTextCursor sel = table->cellAt(0, col).firstCursorPosition();
    sel.setPosition(table->cellAt(table->rows() - 1, col).lastCursorPosition().position(),
                    QTextCursor::KeepAnchor);
    item->textControl()->setTextCursor(sel);
}
```

- [ ] **Step 4: Register test in CMakeLists, build, and run**

```cmake
add_executable(tst_markoff_table_operations tst_table_operations.cpp)
add_test(NAME tst_markoff_table_operations COMMAND tst_markoff_table_operations)
target_link_libraries(tst_markoff_table_operations PRIVATE Qt6::Test Qt6::Widgets markoff)
set_tests_properties(tst_markoff_table_operations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Run: `cd /home/clinton/dev/Corbomite/build && cmake --build . && ctest -R markoff_table --output-on-failure`

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp \
       libs/markoff/tests/tst_table_operations.cpp libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): add table operation slots and signals to Editor API"
```

---

### Task 9: Table Context Menu

**Files:**
- Modify: `libs/markoff/src/Editor.cpp` (contextMenuEvent)

Wire the context menu to show table operations when right-clicking inside a table.

- [ ] **Step 1: Add table context menu to Editor::contextMenuEvent**

In `libs/markoff/src/Editor.cpp`, find `contextMenuEvent` and add a table-specific menu branch. The current implementation is in `Editor::contextMenuEvent()` — add a check at the top:

```cpp
void Editor::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_readOnly) {
        e->ignore();
        return;
    }

    // Check if click is inside a table
    auto *item = focusedTextItem();
    if (item) {
        QTextCursor cursor = item->textControl()->textCursor();
        QTextTable *table = cursor.currentTable();
        if (table) {
            QMenu menu(this);
            menu.addAction(tr("Insert Row Above"), this, &Editor::tableInsertRowAbove);
            menu.addAction(tr("Insert Row Below"), this, &Editor::tableInsertRowBelow);
            menu.addSeparator();
            menu.addAction(tr("Insert Column Left"), this, &Editor::tableInsertColumnLeft);
            menu.addAction(tr("Insert Column Right"), this, &Editor::tableInsertColumnRight);
            menu.addSeparator();
            auto *delRow = menu.addAction(tr("Delete Row"), this, &Editor::tableDeleteRow);
            delRow->setEnabled(table->rows() > 1);
            auto *delCol = menu.addAction(tr("Delete Column"), this, &Editor::tableDeleteColumn);
            delCol->setEnabled(table->columns() > 1);
            menu.addSeparator();
            menu.addAction(tr("Select Row"), this, &Editor::tableSelectRow);
            menu.addAction(tr("Select Column"), this, &Editor::tableSelectColumn);
            menu.exec(e->globalPos());
            e->accept();
            return;
        }
    }

    // Fall through to default context menu
    QGraphicsView::contextMenuEvent(e);
}
```

- [ ] **Step 2: Build and manually test**

Run: `cd /home/clinton/dev/Corbomite && cmake --build build && ./build/bin/markoff-testapp`

Load a file with a table. Right-click inside the table — the context menu should appear. Test each operation.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff/src/Editor.cpp
git commit -m "feat(markoff): table context menu with insert/delete/select operations"
```

---

### Task 10: Revise Editor::insertTable() for Direct QTextTable Insertion

**Files:**
- Modify: `libs/markoff/src/Editor.cpp:797-812`

The current `insertTable()` generates pipe markdown text. Change it to insert a `QTextTable` directly.

- [ ] **Step 1: Replace insertTable implementation**

```cpp
void Editor::insertTable(int rows, int cols)
{
    auto *item = focusedTextItem();
    if (!item) return;

    QTextCursor cursor = item->textControl()->textCursor();
    cursor.beginEditBlock();

    // Ensure blank line before table
    if (!cursor.atBlockStart())
        cursor.insertBlock();

    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    fmt.setCellPadding(8);
    fmt.setCellSpacing(0);
    fmt.setBorder(1);

    auto *table = cursor.insertTable(rows + 1, cols, fmt); // +1 for header

    // Populate header with placeholder text
    for (int c = 0; c < cols; ++c) {
        QTextCursor cell = table->cellAt(0, c).firstCursorPosition();
        cell.insertText(QStringLiteral("Col%1").arg(c + 1));
    }

    // Position cursor in first data cell
    QTextCursor firstData = table->cellAt(1, 0).firstCursorPosition();
    item->textControl()->setTextCursor(firstData);

    cursor.endEditBlock();
}
```

- [ ] **Step 2: Run integration test**

The `insertTableViaApi` test from Task 6 should now pass correctly.

Run: `cd /home/clinton/dev/Corbomite/build && ctest -R markoff_table_integration --output-on-failure`

- [ ] **Step 3: Commit**

```bash
git add libs/markoff/src/Editor.cpp
git commit -m "feat(markoff): insertTable() creates QTextTable directly, no pipe text"
```

---

### Task 11: Remove TableBlockItem

**Files:**
- Delete: `libs/markoff/src/TableBlockItem.h`
- Delete: `libs/markoff/src/TableBlockItem.cpp`
- Modify: `libs/markoff/CMakeLists.txt` (remove from source list)
- Modify: `libs/markoff/src/SceneCoordinator.cpp` (remove include and remaining references)

- [ ] **Step 1: Remove #include and all references in SceneCoordinator**

Grep for `TableBlockItem` across the codebase and remove every reference:

```bash
grep -rn "TableBlockItem" libs/markoff/src/
```

Remove the include, the creation in `loadMarkdown`/`reparse` (already done in Task 5), and the `dynamic_cast` in `setFont()`.

- [ ] **Step 2: Remove from CMakeLists**

Remove these lines from `libs/markoff/CMakeLists.txt`:

```cmake
    src/TableBlockItem.h
    src/TableBlockItem.cpp
```

- [ ] **Step 3: Delete the files**

```bash
rm libs/markoff/src/TableBlockItem.h libs/markoff/src/TableBlockItem.cpp
```

- [ ] **Step 4: Build and run all tests**

Run: `cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . && ctest -R markoff --output-on-failure`

Expected: Clean build, all tests pass.

- [ ] **Step 5: Commit**

```bash
git add -u libs/markoff/
git commit -m "refactor(markoff): remove TableBlockItem — tables now live in QTextDocument"
```

---

### Task 12: Table Signals — Emit on Cursor Movement

**Files:**
- Modify: `libs/markoff/src/Editor.cpp`

Wire cursor position changes to emit `tableEntered`, `tableExited`, and `tableCursorMoved` signals.

- [ ] **Step 1: Track table state and emit signals**

Add private members to `Editor`:

```cpp
    bool m_inTable = false;
```

In the cursor position change handler (or add one if it doesn't exist for this purpose), check table state:

```cpp
void Editor::onCursorPositionChanged()
{
    // ... existing cursor position logic ...

    auto *item = focusedTextItem();
    if (item) {
        QTextCursor cursor = item->textControl()->textCursor();
        QTextTable *table = cursor.currentTable();
        if (table) {
            QTextTableCell cell = table->cellAt(cursor);
            if (!m_inTable) {
                m_inTable = true;
                emit tableEntered(table->rows(), table->columns());
            }
            emit tableCursorMoved(cell.row(), cell.column());
        } else if (m_inTable) {
            m_inTable = false;
            emit tableExited();
        }
    }
}
```

- [ ] **Step 2: Build and test**

Run: `cd /home/clinton/dev/Corbomite/build && cmake --build . && ctest -R markoff --output-on-failure`

- [ ] **Step 3: Commit**

```bash
git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp
git commit -m "feat(markoff): emit tableEntered/tableExited/tableCursorMoved signals"
```

---

### Task 13: Full Test Suite Run and Fixups

**Files:** Various — depends on what breaks.

- [ ] **Step 1: Run the complete test suite**

```bash
cd /home/clinton/dev/Corbomite/build && cmake .. -DCORBOMITE_DEV_BUILD=ON && cmake --build . && ctest --output-on-failure
```

- [ ] **Step 2: Fix any failures**

Common expected issues:
- Tests that loaded markdown with tables and checked for specific scene item counts (they'll find fewer items now since tables aren't separate BlockItems)
- `globalPositionOf` / `itemAtGlobalLine` in SceneCoordinator may need table-frame-aware line counting
- Audit `allMarkdown()` in `SceneCoordinator::toMarkdown()` — it concatenates items; table-containing items now serialize differently

Fix each failure. Do not modify test expectations unless the test was testing `TableBlockItem`-specific behavior that is no longer relevant.

- [ ] **Step 3: Manual smoke test**

```bash
./build/bin/markoff-testapp testvaults/starter-vault/PKM\ LM/Start\ Here.md
```

Or any file with a table. Verify:
- Table renders with grid lines and header styling
- Click into a cell and type — text appears
- Tab moves between cells
- Enter moves down / creates row at bottom
- Right-click shows context menu
- Insert/delete row/column works
- Escape exits table
- Save and reload preserves table content

- [ ] **Step 4: Commit all fixups**

```bash
git add -u
git commit -m "fix(markoff): test suite fixups for editable tables migration"
```

---

## Dependency Order

```
Task 1 (TableSerializer)
Task 2 (TableConverter) ← depends on Task 1 (uses parseAlignments)
Task 3 (TableStyle)
Task 4 (MarkdownSplitter) 
Task 5 (SceneCoordinator wiring) ← depends on Tasks 1, 2, 4
Task 6 (allMarkdown fix) ← depends on Task 1
Task 7 (TextControl nav)
Task 8 (Editor API) ← depends on Task 5
Task 9 (Context menu) ← depends on Task 8
Task 10 (insertTable) ← depends on Task 5
Task 11 (Remove TableBlockItem) ← depends on Tasks 5, 6, 8
Task 12 (Signals) ← depends on Task 8
Task 13 (Full test run) ← depends on all above
```

Tasks 1, 3, 4, 7 can run in parallel. Tasks 2, 6 depend on 1. Task 5 is the big integration point.
