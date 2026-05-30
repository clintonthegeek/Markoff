// SPDX-License-Identifier: GPL-3.0-or-later
//
// Read-only enforcement: a materialized table frame is not editable in the
// styled view. Typing / Backspace / Delete / Enter inside a cell must not
// mutate the model or change the frame; navigation keys still move the caret.
// Drives the real StructuralTextEdit::keyPressEvent.
#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

using namespace Markoff;

static void pumpEvents() { QCoreApplication::processEvents(); }

namespace {
QTextTable *firstTable(QTextDocument *doc) {
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f)) return t;
    return nullptr;
}
}  // namespace

class TstStyledTableReadonly : public QObject {
    Q_OBJECT
private:
    static void placeCaretInCell(Styled::Editor &editor, QTextTable *t,
                                 int row, int col) {
        QTextCursor cc = t->cellAt(row, col).firstCursorPosition();
        editor.textEdit()->setTextCursor(cc);
    }

private slots:
    void typing_in_cell_does_not_mutate_model() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextDocument *qdoc = editor.textEdit()->document();
        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);
        const QString cellBefore =
            t->cellAt(1, 0).firstCursorPosition().block().text();

        placeCaretInCell(editor, t, 1, 0);
        const quint64 seqBefore = doc.d2EditSequence();
        QTest::keyClicks(editor.textEdit(), QStringLiteral("ZZZ"));
        pumpEvents();

        // Model untouched; frame + cell content unchanged.
        QCOMPARE(doc.d2EditSequence(), seqBefore);
        QTextTable *t2 = firstTable(qdoc);
        QVERIFY(t2 != nullptr);
        QCOMPARE(t2->cellAt(1, 0).firstCursorPosition().block().text(),
                 cellBefore);
    }

    void backspace_delete_enter_in_cell_inert() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextDocument *qdoc = editor.textEdit()->document();
        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);

        // Caret mid-cell so Backspace/Delete would delete a char if not blocked.
        QTextCursor cc = t->cellAt(0, 0).firstCursorPosition();
        cc.movePosition(QTextCursor::EndOfBlock);
        editor.textEdit()->setTextCursor(cc);

        const quint64 seqBefore = doc.d2EditSequence();
        QTest::keyClick(editor.textEdit(), Qt::Key_Backspace);
        QTest::keyClick(editor.textEdit(), Qt::Key_Delete);
        QTest::keyClick(editor.textEdit(), Qt::Key_Return);
        pumpEvents();

        QCOMPARE(doc.d2EditSequence(), seqBefore);
        QVERIFY(firstTable(qdoc) != nullptr);
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("A"));
    }

    void arrow_keys_still_move_caret() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextDocument *qdoc = editor.textEdit()->document();
        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);

        placeCaretInCell(editor, t, 0, 0);
        const int posBefore = editor.textEdit()->textCursor().position();
        QTest::keyClick(editor.textEdit(), Qt::Key_Right);
        const int posAfter = editor.textEdit()->textCursor().position();
        QVERIFY2(posAfter != posBefore, "arrow key did not move the caret");
    }
};

QTEST_MAIN(TstStyledTableReadonly)
#include "tst_styled_table_readonly.moc"
