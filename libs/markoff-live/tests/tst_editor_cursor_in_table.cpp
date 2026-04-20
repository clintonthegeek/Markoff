// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTest>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "TextControl.h"

using namespace Markoff;

class TstEditorCursorInTable : public QObject {
    Q_OBJECT

private:
    Editor *makeEditor(const QString &text) {
        auto *ed = new Editor;
        ed->resize(400, 300);
        ed->setPlainText(text);
        ed->show();
        (void)QTest::qWaitForWindowExposed(ed);
        return ed;
    }

    bool focusInsideTable(Editor *ed) {
        const auto items = ed->scene()->items();
        for (auto *gi : items) {
            auto *ti = dynamic_cast<MarkdownTextItem *>(gi);
            if (!ti) continue;
            QTextFrame *root = ti->document()->rootFrame();
            for (QTextFrame *frame : root->childFrames()) {
                auto *table = qobject_cast<QTextTable *>(frame);
                if (!table) continue;
                ti->setFocus();
                QTextCursor c = table->cellAt(0, 0).firstCursorPosition();
                ti->textControl()->setTextCursor(c);
                return true;
            }
        }
        return false;
    }

    bool focusOnFirstTextItem(Editor *ed) {
        const auto items = ed->scene()->items();
        for (auto *gi : items) {
            auto *ti = dynamic_cast<MarkdownTextItem *>(gi);
            if (!ti) continue;
            ti->setFocus();
            return true;
        }
        return false;
    }

private slots:
    void returnsFalseWithoutFocus() {
        Editor ed;
        QVERIFY(!ed.cursorInTable());
    }

    void returnsFalseForPlainText() {
        auto *ed = makeEditor(QStringLiteral("just a paragraph"));
        QVERIFY(focusOnFirstTextItem(ed));
        QVERIFY(!ed->cursorInTable());
        delete ed;
    }

    void returnsTrueInsideTableCell() {
        const QString md = QStringLiteral(
            "| A | B |\n"
            "| - | - |\n"
            "| 1 | 2 |\n");
        auto *ed = makeEditor(md);
        QVERIFY(focusInsideTable(ed));
        QVERIFY(ed->cursorInTable());
        delete ed;
    }

    void returnsFalseOutsideTableWhenTableExists() {
        const QString md = QStringLiteral(
            "paragraph before\n\n"
            "| A | B |\n"
            "| - | - |\n"
            "| 1 | 2 |\n");
        auto *ed = makeEditor(md);
        // Focus the first text item (the "paragraph before" block), not the table.
        QVERIFY(focusOnFirstTextItem(ed));
        QVERIFY(!ed->cursorInTable());
        delete ed;
    }
};

QTEST_MAIN(TstEditorCursorInTable)
#include "tst_editor_cursor_in_table.moc"
