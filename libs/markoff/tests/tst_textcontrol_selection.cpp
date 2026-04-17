// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void shiftRight_extends_from_anchor();
    void shiftLeft_retracts_selection();
    void selectAll_selectsEntireDocument();
    void shiftClick_setsAnchor();
    void doubleClick_selectsWord();
    void tripleClick_selectsLine();
    void dragSelect_pressMoveRelease_selectsRange();
    void selection_spanning_placeholder_includes_fffc();
};

void TstTextControlSelection::shiftRight_extends_from_anchor()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    for (int i = 0; i < 5; ++i)
        sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().anchor(), 0);
    QCOMPARE(fx.control.textCursor().position(), 5);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::shiftLeft_retracts_selection()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    for (int i = 0; i < 4; ++i)
        sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    sendKey(fx.control, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().position(), 3);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hel"));
}

void TstTextControlSelection::selectAll_selectsEntireDocument()
{
    auto fx = makeFixture(QStringLiteral("line one\nline two"), 0);
    fx.control.selectAll();
    QCOMPARE(fx.control.textCursor().selectedText().length(),
             fx.document->characterCount() - 1);
}

void TstTextControlSelection::shiftClick_setsAnchor()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 2);
    QTextCursor target(fx.document.get());
    target.setPosition(8);
    // Force layout before requesting cursorRect
    fx.document->documentLayout()->documentSize();
    QPointF clickAt = fx.control.cursorRect(target).center();
    sendMousePress(fx.control, clickAt, Qt::LeftButton, Qt::ShiftModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, clickAt, Qt::LeftButton, Qt::ShiftModifier,
                     fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().anchor(), 2);
    QVERIFY(fx.control.textCursor().position() != 2);
}

void TstTextControlSelection::doubleClick_selectsWord()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    QTextCursor probe(fx.document.get());
    probe.setPosition(2);
    fx.document->documentLayout()->documentSize();
    QPointF clickAt = fx.control.cursorRect(probe).center();
    sendMouseDoubleClick(fx.control, clickAt, Qt::LeftButton,
                         Qt::NoModifier, fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::tripleClick_selectsLine()
{
    auto fx = makeFixture(QStringLiteral("one two\nthree"), 0);
    QTextCursor probe(fx.document.get());
    probe.setPosition(3);
    fx.document->documentLayout()->documentSize();
    QPointF clickAt = fx.control.cursorRect(probe).center();
    // Triple-click: DblClick starts the trippleClickTimer; a subsequent
    // MouseButtonPress while the timer is still active triggers the
    // StartOfBlock/EndOfBlock selection path in TextControlPrivate.
    sendMouseDoubleClick(fx.control, clickAt, Qt::LeftButton,
                         Qt::NoModifier, fx.contextWidget.get());
    // Send the third click as a plain press (timer is still active because
    // no event-loop tick has fired to expire it).
    sendMousePress(fx.control, clickAt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, clickAt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    const QString sel = fx.control.textCursor().selectedText();
    QVERIFY2(sel.contains(QStringLiteral("one two")),
             qPrintable(QStringLiteral("expected line selection, got '%1'")
                        .arg(sel)));
}

void TstTextControlSelection::dragSelect_pressMoveRelease_selectsRange()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    QTextCursor startCur(fx.document.get());
    startCur.setPosition(0);
    QTextCursor endCur(fx.document.get());
    endCur.setPosition(5);
    fx.document->documentLayout()->documentSize();
    QPointF startPt = fx.control.cursorRect(startCur).center();
    QPointF endPt = fx.control.cursorRect(endCur).center();
    sendMousePress(fx.control, startPt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseMove(fx.control, endPt, Qt::LeftButton, Qt::NoModifier,
                  fx.contextWidget.get());
    sendMouseRelease(fx.control, endPt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::selection_spanning_placeholder_includes_fffc()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    fx.control.selectAll();
    const QString selected = fx.control.textCursor().selectedText();
    QCOMPARE(selected.length(), 3);
    QCOMPARE(selected.at(1), QChar(QChar::ObjectReplacementCharacter));
}

QTEST_MAIN(TstTextControlSelection)
#include "tst_textcontrol_selection.moc"
