// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTextDocument>

#include "MarkdownTextItem.h"

using namespace Markoff;

class TestCjkAutocorrect : public QObject {
    Q_OBJECT

private slots:
    void fullWidthDoubleBracketOpenReplacesWithWikilink();
    void fullWidthDoubleBracketCloseReplacesWithClose();
    void fullWidthExclamationBracketReplacesWithEmbed();
    void singleFullWidthBracketDoesNotReplace();
    void replacementIsUndoable();

private:
    MarkdownTextItem *createItem();
    void typeText(MarkdownTextItem *item, const QString &text);
    QGraphicsScene m_scene;
    QGraphicsView m_view{&m_scene};
};

MarkdownTextItem *TestCjkAutocorrect::createItem()
{
    auto *item = new MarkdownTextItem;
    m_scene.addItem(item);
    item->setFlag(QGraphicsItem::ItemIsFocusable);
    item->setFocus();
    item->setPlainText({});
    return item;
}

void TestCjkAutocorrect::typeText(MarkdownTextItem *item, const QString &text)
{
    Q_UNUSED(item);
    for (const QChar &ch : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(&m_scene, &press);
        QApplication::sendEvent(&m_scene, &release);
    }
}

void TestCjkAutocorrect::fullWidthDoubleBracketOpenReplacesWithWikilink()
{
    auto *item = createItem();

    typeText(item, QStringLiteral("\u3010\u3010")); // 【【
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));
}

void TestCjkAutocorrect::fullWidthDoubleBracketCloseReplacesWithClose()
{
    auto *item = createItem();

    typeText(item, QStringLiteral("\u3011\u3011")); // 】】
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("]]"));
}

void TestCjkAutocorrect::fullWidthExclamationBracketReplacesWithEmbed()
{
    auto *item = createItem();

    typeText(item, QStringLiteral("\uff01\u3010\u3010")); // ！【【
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("![["));
}

void TestCjkAutocorrect::singleFullWidthBracketDoesNotReplace()
{
    auto *item = createItem();

    typeText(item, QStringLiteral("\u3010")); // single 【
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("\u3010"));
}

void TestCjkAutocorrect::replacementIsUndoable()
{
    auto *item = createItem();

    typeText(item, QStringLiteral("\u3010\u3010")); // 【【 → [[
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));

    item->document()->undo();
    // Undo should remove the [[ (the replacement was grouped with input)
    QVERIFY(item->document()->toPlainText() != QStringLiteral("[["));
}

QTEST_MAIN(TestCjkAutocorrect)
#include "tst_cjk_autocorrect.moc"
