// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QTextTableFormat>

#include "../src/EditorContextClassifier.h"

using namespace Markoff;
using namespace Markoff::Internal;

class TstEditorContextClassifier : public QObject
{
    Q_OBJECT

private slots:
    void emptyDocument_isEmpty();
    void plainParagraph_isParagraph();
    void heading_levels();
    void listItem_bulletDetected();
    void listItem_orderedDetected();
    void table_detected();
    void nullCursor_isEmpty();
};

namespace {
QTextCursor cursorAtBlock(QTextDocument &doc, int blockIdx, int offset = 0)
{
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::Start);
    for (int i = 0; i < blockIdx; ++i)
        c.movePosition(QTextCursor::NextBlock);
    c.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, offset);
    return c;
}
} // namespace

void TstEditorContextClassifier::emptyDocument_isEmpty()
{
    QTextDocument doc;
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Empty);
    QCOMPARE(ctx.headingLevel, 0);
}

void TstEditorContextClassifier::plainParagraph_isParagraph()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("hello world"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Paragraph);
    QCOMPARE(ctx.headingLevel, 0);
    QVERIFY(!ctx.table.has_value());
}

void TstEditorContextClassifier::heading_levels()
{
    for (int level = 1; level <= 6; ++level) {
        QTextDocument doc;
        const QString line = QString(level, QLatin1Char('#'))
                             + QStringLiteral(" Title");
        doc.setPlainText(line);
        EditorContext ctx;
        classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
        QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Heading);
        QCOMPARE(ctx.headingLevel, level);
    }
}

void TstEditorContextClassifier::listItem_bulletDetected()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("- bullet"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::ListItem);
}

void TstEditorContextClassifier::listItem_orderedDetected()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("1. first"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::ListItem);
}

void TstEditorContextClassifier::table_detected()
{
    QTextDocument doc;
    QTextCursor c(&doc);
    QTextTable *tbl = c.insertTable(2, 3);
    // Place cursor in cell (1, 1).
    QTextTableCell cell = tbl->cellAt(1, 1);
    QTextCursor cellCursor = cell.firstCursorPosition();

    EditorContext ctx;
    classifyBlockAtCursor(cellCursor, ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Table);
    QVERIFY(ctx.table.has_value());
    QCOMPARE(ctx.table->row, 1);
    QCOMPARE(ctx.table->col, 1);
    QCOMPARE(ctx.table->rows, 2);
    QCOMPARE(ctx.table->cols, 3);
    QVERIFY(!ctx.table->isHeaderRow);
}

void TstEditorContextClassifier::nullCursor_isEmpty()
{
    QTextCursor c;  // default-constructed → null
    EditorContext ctx;
    classifyBlockAtCursor(c, ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Empty);
}

QTEST_APPLESS_MAIN(TstEditorContextClassifier)
#include "tst_editor_context_classifier.moc"
