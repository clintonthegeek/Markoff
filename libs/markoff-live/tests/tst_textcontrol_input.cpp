// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "MarkdownTextItem.h"
#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlInput : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // Direct IME tests — bare TextControl
    void preedit_isDisplayedButNotCommitted();
    void preedit_update_replacesPrior();
    void preedit_commit_clearsPreeditAndInsertsText();
    void inputMethodQuery_returnsCursorPosition();
    void preedit_withStyleAttribute_accepted();

    // CJK autocorrect — migrated from tst_cjk_autocorrect.cpp
    void fullWidthDoubleBracketOpenReplacesWithWikilink();
    void fullWidthDoubleBracketCloseReplacesWithClose();
    void fullWidthExclamationBracketReplacesWithEmbed();
    void singleFullWidthBracketDoesNotReplace();
    void replacementIsUndoable();

private:
    MarkdownTextItem *createItem();
    void typeText(const QString &text);
    QGraphicsScene m_scene;
    QGraphicsView m_view{&m_scene};
};

// --- Direct IME slots ---

void TstTextControlInput::preedit_isDisplayedButNotCommitted()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
}

void TstTextControlInput::preedit_update_replacesPrior()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("X"));
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    QVERIFY(fx.control.textCursor().position() == 1);
}

void TstTextControlInput::preedit_commit_clearsPreeditAndInsertsText()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    sendInputMethod(fx.control, QStringLiteral("ZZ"), QString());
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("aZZb"));
}

void TstTextControlInput::inputMethodQuery_returnsCursorPosition()
{
    auto fx = makeFixture(QStringLiteral("hello"), 3);
    QVariant v = fx.control.inputMethodQuery(Qt::ImCursorPosition, QVariant());
    QCOMPARE(v.toInt(), 3);
}

void TstTextControlInput::preedit_withStyleAttribute_accepted()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    QList<QInputMethodEvent::Attribute> attrs;
    QTextCharFormat fmt;
    fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    attrs.append(QInputMethodEvent::Attribute(
        QInputMethodEvent::TextFormat, 0, 2, QVariant::fromValue(fmt)));
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"), attrs);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
}

// --- Migrated CJK autocorrect slots ---

MarkdownTextItem *TstTextControlInput::createItem()
{
    auto *item = new MarkdownTextItem;
    m_scene.addItem(item);
    item->setFlag(QGraphicsItem::ItemIsFocusable);
    item->setFocus();
    item->setPlainText({});
    return item;
}

void TstTextControlInput::typeText(const QString &text)
{
    for (const QChar &ch : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(&m_scene, &press);
        QApplication::sendEvent(&m_scene, &release);
    }
}

void TstTextControlInput::fullWidthDoubleBracketOpenReplacesWithWikilink()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));
}

void TstTextControlInput::fullWidthDoubleBracketCloseReplacesWithClose()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3011\u3011"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("]]"));
}

void TstTextControlInput::fullWidthExclamationBracketReplacesWithEmbed()
{
    auto *item = createItem();
    typeText(QStringLiteral("\uff01\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("![["));
}

void TstTextControlInput::singleFullWidthBracketDoesNotReplace()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("\u3010"));
}

void TstTextControlInput::replacementIsUndoable()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));
    item->document()->undo();
    QVERIFY(item->document()->toPlainText() != QStringLiteral("[["));
}

QTEST_MAIN(TstTextControlInput)
#include "tst_textcontrol_input.moc"
