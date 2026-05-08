// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/source/Editor.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

class TestSourceEditorViewContract : public QObject {
    Q_OBJECT
private slots:
    void inherits_markdown_view() {
        Markoff::Source::Widget::Editor e;
        auto* asView = qobject_cast<Markoff::MarkdownView*>(&e);
        QVERIFY(asView != nullptr);
    }
    void hasCursor_true_hasEditing_when_writable() {
        Markoff::Source::Widget::Editor e;
        QCOMPARE(e.hasCursor(), true);
        QCOMPARE(e.hasEditing(), true);
    }
    void readonly_disables_hasEditing() {
        Markoff::Source::Widget::Editor e;
        e.setReadOnly(true);
        QCOMPARE(e.hasCursor(), true);
        QCOMPARE(e.hasEditing(), false);
    }
    void setDocument_emits_documentChanged() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        QSignalSpy spy(&e, &Markoff::MarkdownView::documentChanged);
        e.setDocument(&doc);
        QCOMPARE(e.document(), &doc);
        QCOMPARE(spy.count(), 1);
    }
    void cursor_position_round_trips() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# A\n\n# B\n\n# C\n"));
        e.setDocument(&doc);
        // Wait for the binding to seed the inner QPlainTextEdit.
        QTest::qWait(50);
        e.setCursorPosition({3, 1});
        QCOMPARE(e.cursorPosition().line, 3);
        QCOMPARE(e.cursorPosition().column, 1);
    }
};
QTEST_MAIN(TestSourceEditorViewContract)
#include "tst_v10_source_editor_view_contract.moc"
