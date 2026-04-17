// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "TextControl.h"

using namespace Markoff;

class TstCheckboxToggle : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void togglePlainLineInsertsUncheckedMarker();
    void toggleUncheckedBecomesChecked();
    void toggleCheckedRemovesMarker();
    void doesNotAccumulateDashesOnRepeatedToggles();

private:
    /// Build an editor, set text, focus the first text item, and
    /// position the caret at the start of the given 0-based line.
    Editor *makeEditorAtLine(const QString &text, int lineNumber);
};

Editor *TstCheckboxToggle::makeEditorAtLine(const QString &text, int lineNumber)
{
    auto *editor = new Editor;
    editor->resize(600, 400);
    editor->setPlainText(text);
    editor->show();
    QApplication::processEvents();

    MarkdownTextItem *ti = nullptr;
    for (auto *item : editor->coordinatorForTesting()->items()) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    if (ti) {
        ti->setFocus();
        QTextCursor c(ti->document());
        QTextBlock block = ti->document()->findBlockByNumber(lineNumber);
        if (block.isValid())
            c.setPosition(block.position());
        ti->textControl()->setTextCursor(c);
    }
    QApplication::processEvents();
    return editor;
}

void TstCheckboxToggle::togglePlainLineInsertsUncheckedMarker()
{
    auto *editor = makeEditorAtLine(QStringLiteral("hello"), 0);
    editor->toggleCheckbox();
    QCOMPARE(editor->toPlainText(), QStringLiteral("- [ ] hello"));
    delete editor;
}

void TstCheckboxToggle::toggleUncheckedBecomesChecked()
{
    auto *editor = makeEditorAtLine(QStringLiteral("- [ ] hello"), 0);
    editor->toggleCheckbox();
    QCOMPARE(editor->toPlainText(), QStringLiteral("- [x] hello"));
    delete editor;
}

void TstCheckboxToggle::toggleCheckedRemovesMarker()
{
    auto *editor = makeEditorAtLine(QStringLiteral("- [x] hello"), 0);
    editor->toggleCheckbox();
    QCOMPARE(editor->toPlainText(), QStringLiteral("hello"));
    delete editor;
}

void TstCheckboxToggle::doesNotAccumulateDashesOnRepeatedToggles()
{
    // Regression: the pre-fix implementation would see the substituted
    // U+FFFC glyph in the document, fail its "- [ ]" literal match,
    // and fall through to the else branch, prepending "- [ ] " onto
    // the existing "- <glyph>". Each invocation added more dashes.
    // After the fix, toggling through the three-state cycle twice
    // should return to the original text.
    auto *editor = makeEditorAtLine(QStringLiteral("hello"), 0);
    editor->toggleCheckbox();                                  // hello -> - [ ] hello
    QCOMPARE(editor->toPlainText(), QStringLiteral("- [ ] hello"));
    editor->toggleCheckbox();                                  // - [ ] -> - [x]
    QCOMPARE(editor->toPlainText(), QStringLiteral("- [x] hello"));
    editor->toggleCheckbox();                                  // - [x] -> plain
    QCOMPARE(editor->toPlainText(), QStringLiteral("hello"));
    delete editor;
}

QTEST_MAIN(TstCheckboxToggle)
#include "tst_checkbox_toggle.moc"
