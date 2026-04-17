// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QTextCursor>

#include <markoff/Editor.h>

using namespace Markoff;

class TestEditorFormatting : public QObject {
    Q_OBJECT

private slots:
    void toggleBoldWraps();
    void toggleBoldUnwraps();
    void toggleBoldUnwrapsWhenOuterDelimiters();
    void toggleItalicNoSelectionInsertsPair();

private:
    Editor *makeEditor(const QString &text);
};

Editor *TestEditorFormatting::makeEditor(const QString &text)
{
    auto *editor = new Editor;
    editor->resize(400, 300);
    editor->setPlainText(text);
    editor->show();
    QTest::qWaitForWindowExposed(editor);

    // Live-preview mode may produce multiple MarkdownTextItems; give the
    // first one focus so the editor's formatting actions work.
    const auto items = editor->scene()->items();
    if (!items.isEmpty())
        items.first()->setFocus();

    return editor;
}

// We exercise wrapSelection via the public formatting actions: toggleBold,
// toggleItalic, etc. The verification is on the resulting toPlainText().

void TestEditorFormatting::toggleBoldWraps()
{
    auto *editor = makeEditor(QStringLiteral("hello world"));
    editor->selectAll();
    editor->toggleBold();
    QCOMPARE(editor->toPlainText(), QStringLiteral("**hello world**"));
    delete editor;
}

void TestEditorFormatting::toggleBoldUnwraps()
{
    // Selection IS the wrapped form: **foo** → foo
    auto *editor = makeEditor(QStringLiteral("**foo**"));
    editor->selectAll();
    editor->toggleBold();
    QCOMPARE(editor->toPlainText(), QStringLiteral("foo"));
    delete editor;
}

void TestEditorFormatting::toggleBoldUnwrapsWhenOuterDelimiters()
{
    // Selection is just `foo`, with `**` outside on each side. Toggle should
    // strip the outer delimiters. We need a focused text item first; the
    // selectAll() call wakes up focus on the only text item, then findText
    // can position the selection on the inner word.
    auto *editor = makeEditor(QStringLiteral("**foo**"));
    editor->selectAll();
    QVERIFY(editor->findText(QStringLiteral("foo")));
    editor->toggleBold();
    QCOMPARE(editor->toPlainText(), QStringLiteral("foo"));
    delete editor;
}

void TestEditorFormatting::toggleItalicNoSelectionInsertsPair()
{
    auto *editor = makeEditor(QStringLiteral(""));
    editor->toggleItalic();
    // Empty pair inserted, cursor parked between the asterisks.
    QCOMPARE(editor->toPlainText(), QStringLiteral("**"));
    delete editor;
}

QTEST_MAIN(TestEditorFormatting)
#include "tst_editor_formatting.moc"
