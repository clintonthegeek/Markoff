// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlEditing : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void keyPress_insertsCharacter();
    void backspace_removesPreviousCharacter();
    void deleteKey_removesNextCharacter();
    void insertPlainText_insertsAtCursor();
    void backspace_atStart_isNoop();
    void deleteKey_atEnd_isNoop();
    void backspace_adjacentToPlaceholder_removesPlaceholder();
    void deleteKey_adjacentToPlaceholder_removesPlaceholder();
    void readOnly_keyPressEvent_rejectsEdit();
    void readOnly_insertPlainText_stillInserts();
};

void TstTextControlEditing::keyPress_insertsCharacter()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendKey(fx.control, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("axb"));
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlEditing::backspace_removesPreviousCharacter()
{
    auto fx = makeFixture(QStringLiteral("abc"), 2);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ac"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::deleteKey_removesNextCharacter()
{
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ac"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::insertPlainText_insertsAtCursor()
{
    auto fx = makeFixture(QStringLiteral("hello"), 5);
    fx.control.insertPlainText(QStringLiteral(" world"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("hello world"));
}

void TstTextControlEditing::backspace_atStart_isNoop()
{
    auto fx = makeFixture(QStringLiteral("abc"), 0);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::deleteKey_atEnd_isNoop()
{
    auto fx = makeFixture(QStringLiteral("abc"), 3);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::backspace_adjacentToPlaceholder_removesPlaceholder()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor pc(fx.document.get());
    pc.setPosition(2);
    fx.control.setTextCursor(pc);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::deleteKey_adjacentToPlaceholder_removesPlaceholder()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor pc(fx.document.get());
    pc.setPosition(1);
    fx.control.setTextCursor(pc);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::readOnly_keyPressEvent_rejectsEdit()
{
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    fx.control.setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    sendKey(fx.control, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::readOnly_insertPlainText_stillInserts()
{
    // Documented behavior: insertPlainText() is a direct API call that
    // bypasses TextInteractionFlags. Read-only mode only filters
    // keyboard/mouse events, not programmatic API calls.
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    fx.control.setTextInteractionFlags(Qt::TextSelectableByMouse);
    fx.control.insertPlainText(QStringLiteral("X"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("aXbc"));
}

QTEST_MAIN(TstTextControlEditing)
#include "tst_textcontrol_editing.moc"
