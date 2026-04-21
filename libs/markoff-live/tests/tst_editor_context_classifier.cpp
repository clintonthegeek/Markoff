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
    void bold_flagDetected();
    void italic_flagDetected();
    void strike_flagDetected();
    void inlineCode_flagDetected();
    void selection_hasSelectionFlagPropagates();
    void selection_widePredicate();
    void heading_sevenHashIsParagraph();
    void heading_hashWithoutSpaceIsParagraph();
    void table_cellWithHashStillTable();
    void atBlockEnd_worksForTerminalBlock();
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

void insertFormattedRun(QTextDocument &doc,
                         const QString &text,
                         const QTextCharFormat &fmt)
{
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);
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

void TstEditorContextClassifier::bold_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontWeight(QFont::Bold);
    insertFormattedRun(doc, QStringLiteral("BoldText"), fmt);

    QTextCursor c(&doc);
    c.setPosition(4);  // inside "BoldText"
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inBold);
    QVERIFY(!ctx.inItalic);
}

void TstEditorContextClassifier::italic_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontItalic(true);
    insertFormattedRun(doc, QStringLiteral("Italic"), fmt);

    QTextCursor c(&doc);
    c.setPosition(3);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inItalic);
    QVERIFY(!ctx.inBold);
}

void TstEditorContextClassifier::strike_flagDetected()
{
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(true);
    insertFormattedRun(doc, QStringLiteral("Strike"), fmt);

    QTextCursor c(&doc);
    c.setPosition(3);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inStrikethrough);
}

void TstEditorContextClassifier::inlineCode_flagDetected()
{
    // Storage mechanism (Step 1 outcome (d)): MarkdownHighlighter tags
    // inline-code runs with the kInlineCodeProperty custom QTextCharFormat
    // property. Fixture mirrors that storage.
    QTextDocument doc;
    QTextCharFormat fmt;
    fmt.setProperty(Markoff::Internal::kInlineCodeProperty, true);
    insertFormattedRun(doc, QStringLiteral("CodeRun"), fmt);

    QTextCursor c(&doc);
    c.setPosition(3);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.inInlineCode);
}

void TstEditorContextClassifier::selection_hasSelectionFlagPropagates()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("hello"));
    QTextCursor c(&doc);
    c.setPosition(0);
    c.setPosition(3, QTextCursor::KeepAnchor);
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.hasSelection);
}

void TstEditorContextClassifier::selection_widePredicate()
{
    QTextDocument doc;
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    insertFormattedRun(doc, QStringLiteral("Bold"), boldFmt);
    QTextCharFormat plainFmt;
    insertFormattedRun(doc, QStringLiteral(" plain"), plainFmt);

    QTextCursor c(&doc);
    c.setPosition(0);
    c.setPosition(10, QTextCursor::KeepAnchor);  // spans bold + plain
    EditorContext ctx;
    classifyInlineAtCursor(c, ctx);
    QVERIFY(ctx.hasSelection);
    QVERIFY(!ctx.inBold);  // selection leaves bold span → predicate false
}

void TstEditorContextClassifier::heading_sevenHashIsParagraph()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("####### seven"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Paragraph);
    QCOMPARE(ctx.headingLevel, 0);
}

void TstEditorContextClassifier::heading_hashWithoutSpaceIsParagraph()
{
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("#notheading"));
    EditorContext ctx;
    classifyBlockAtCursor(cursorAtBlock(doc, 0), ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Paragraph);
}

void TstEditorContextClassifier::table_cellWithHashStillTable()
{
    QTextDocument doc;
    QTextCursor c(&doc);
    QTextTable *tbl = c.insertTable(2, 2);
    QTextTableCell cell = tbl->cellAt(0, 0);
    QTextCursor cellCursor = cell.firstCursorPosition();
    cellCursor.insertText(QStringLiteral("# fake"));

    EditorContext ctx;
    classifyBlockAtCursor(cellCursor, ctx);
    QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Table);
    QVERIFY(ctx.table.has_value());
}

void TstEditorContextClassifier::atBlockEnd_worksForTerminalBlock()
{
    // Single-block doc — no trailing separator; length() equals text length.
    QTextDocument doc;
    doc.setPlainText(QStringLiteral("hello"));
    QTextCursor c(&doc);
    c.movePosition(QTextCursor::End);
    EditorContext ctx;
    classifyBlockAtCursor(c, ctx);
    QVERIFY(ctx.atBlockEnd);

    // And for start position
    c.movePosition(QTextCursor::Start);
    EditorContext ctx2;
    classifyBlockAtCursor(c, ctx2);
    QVERIFY(ctx2.atBlockStart);
}

QTEST_APPLESS_MAIN(TstEditorContextClassifier)
#include "tst_editor_context_classifier.moc"
