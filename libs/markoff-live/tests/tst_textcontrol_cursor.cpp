// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlCursor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void arrowRight_advancesByOne();
    void arrowLeft_retreatsByOne();
    void arrowRight_atEnd_stays();
    void arrowLeft_atStart_stays();
    void home_goesToLineStart();
    void end_goesToLineEnd();
    void ctrlRight_jumpsToNextWord();
    void ctrlLeft_jumpsToPrevWord();
    void shiftRight_extendsSelection();
    void moveCursorRight_matchesKeyboard();
    void cursorRight_overPlaceholder_advancesByOne();
    void cursorLeft_overPlaceholder_retreatsByOne();
    void ctrlRight_acrossPlaceholder_skipsIt();
};

void TstTextControlCursor::arrowRight_advancesByOne()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlCursor::arrowLeft_retreatsByOne()
{
    auto fx = makeFixture(QStringLiteral("hello"), 3);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlCursor::arrowRight_atEnd_stays()
{
    auto fx = makeFixture(QStringLiteral("hi"), 2);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlCursor::arrowLeft_atStart_stays()
{
    auto fx = makeFixture(QStringLiteral("hi"), 0);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 0);
}

void TstTextControlCursor::home_goesToLineStart()
{
    auto fx = makeFixture(QStringLiteral("hello"), 4);
    sendKey(fx.control, Qt::Key_Home);
    QCOMPARE(fx.control.textCursor().position(), 0);
}

void TstTextControlCursor::end_goesToLineEnd()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_End);
    QCOMPARE(fx.control.textCursor().position(), 5);
}

void TstTextControlCursor::ctrlRight_jumpsToNextWord()
{
    auto fx = makeFixture(QStringLiteral("one two three"), 0);
    sendKey(fx.control, Qt::Key_Right, Qt::ControlModifier);
    QVERIFY(fx.control.textCursor().position() >= 3);
    QVERIFY(fx.control.textCursor().position() <= 4);
}

void TstTextControlCursor::ctrlLeft_jumpsToPrevWord()
{
    auto fx = makeFixture(QStringLiteral("one two three"), 13);
    sendKey(fx.control, Qt::Key_Left, Qt::ControlModifier);
    QVERIFY(fx.control.textCursor().position() >= 7);
    QVERIFY(fx.control.textCursor().position() <= 8);
}

void TstTextControlCursor::shiftRight_extendsSelection()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().anchor(), 0);
    QCOMPARE(fx.control.textCursor().position(), 2);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("he"));
}

void TstTextControlCursor::moveCursorRight_matchesKeyboard()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    fx.control.moveCursor(QTextCursor::Right);
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlCursor::cursorRight_overPlaceholder_advancesByOne()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor start(fx.document.get());
    start.setPosition(1);
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlCursor::cursorLeft_overPlaceholder_retreatsByOne()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor start(fx.document.get());
    start.setPosition(2);
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlCursor::ctrlRight_acrossPlaceholder_skipsIt()
{
    auto fx = makeFixture(QStringLiteral("one "));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral(" two"));
    QTextCursor start(fx.document.get());
    start.setPosition(0);
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Right, Qt::ControlModifier);
    QVERIFY(fx.control.textCursor().position() > 0);
}

QTEST_MAIN(TstTextControlCursor)
#include "tst_textcontrol_cursor.moc"
