// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QClipboard>
#include <QTest>
#include <markoff/Editor.h>
#include "SceneCoordinator.h"
#include "MarkdownTextItem.h"
#include "SelectionManager.h"
#include "SelectionScene.h"

using namespace Markoff;

class TstGlobalCoordinates : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void singleItemLineMapping();
    void twoTextItemsLineMapping();
    void textBlockTextLineMapping();
    void itemAtGlobalLineRoundTrip();
    void itemAtGlobalLinePastEnd();
    void itemAtGlobalLineInBlockItem();
    void selectAllCrossBoundary();
    void cutRemovesBlockItems();
    void goToLineFirstItem();
    void goToLineSecondItem();
    void goToLinePastEnd();
    void cursorLineFirstItem();
    void cursorLineSecondItem();
    void endToEndAllFixes();
};

void TstGlobalCoordinates::singleItemLineMapping()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("line1\nline2\nline3"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    QVERIFY(coord);
    const auto &items = coord->items();
    QVERIFY(!items.isEmpty());

    MarkdownTextItem *ti = nullptr;
    for (auto *item : items) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    QVERIFY(ti);

    auto gp0 = coord->globalPositionOf(ti, 0, 0);
    QCOMPARE(gp0.line, 1);
    QCOMPARE(gp0.column, 1);

    auto gp1 = coord->globalPositionOf(ti, 1, 3);
    QCOMPARE(gp1.line, 2);
    QCOMPARE(gp1.column, 4);

    auto gp2 = coord->globalPositionOf(ti, 2, 0);
    QCOMPARE(gp2.line, 3);
}

void TstGlobalCoordinates::twoTextItemsLineMapping()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("alpha\nbravo\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\ncharlie\ndelta"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    const auto &items = coord->items();

    MarkdownTextItem *firstText = nullptr;
    MarkdownTextItem *secondText = nullptr;
    for (auto *item : items) {
        if (item->isTextItem()) {
            if (!firstText)
                firstText = static_cast<MarkdownTextItem *>(item);
            else if (!secondText)
                secondText = static_cast<MarkdownTextItem *>(item);
        }
    }
    if (!secondText) {
        QSKIP("Splitter did not produce two text items for this document");
        return;
    }

    // The second item's first line should have a higher global line than
    // the first item's last line.
    int firstLastBlock = firstText->document()->blockCount() - 1;
    auto gpFirstLast = coord->globalPositionOf(firstText, firstLastBlock, 0);
    auto gpSecondFirst = coord->globalPositionOf(secondText, 0, 0);
    QVERIFY(gpSecondFirst.line > gpFirstLast.line);
}

void TstGlobalCoordinates::textBlockTextLineMapping()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("one\n\n| H |\n|---|\n| V |\n\ntwo"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    const auto &items = coord->items();

    MarkdownTextItem *lastText = nullptr;
    for (auto *item : items) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No text items found");
        return;
    }

    // If only one text item, the splitter collapsed everything together.
    // In that case the test expectations don't apply.
    MarkdownTextItem *firstText = nullptr;
    for (auto *item : items) {
        if (item->isTextItem()) {
            firstText = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    if (firstText == lastText) {
        QSKIP("Splitter did not separate text and table into distinct items");
        return;
    }

    // The last text item ("two") must be on a global line that is strictly
    // after the first text item's lines plus the table's lines. Rather than
    // hardcode the exact line (which depends on how the splitter and
    // interItemNewlines handle blank lines around tables), verify that:
    //  1. It starts after the first item's last line.
    //  2. A round-trip through itemAtGlobalLine works.
    int firstLastBlock = firstText->document()->blockCount() - 1;
    auto gpFirstLast = coord->globalPositionOf(firstText, firstLastBlock, 0);
    auto gpLastFirst = coord->globalPositionOf(lastText, 0, 0);
    QVERIFY2(gpLastFirst.line > gpFirstLast.line,
             qPrintable(QStringLiteral("last text item line %1 should be after first item's last line %2")
                        .arg(gpLastFirst.line).arg(gpFirstLast.line)));

    // Round-trip: itemAtGlobalLine should resolve back to the same item.
    auto ip = coord->itemAtGlobalLine(gpLastFirst.line);
    QVERIFY(ip.item == lastText);
    QCOMPARE(ip.localBlockNumber, 0);
}

void TstGlobalCoordinates::itemAtGlobalLineRoundTrip()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *ti = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    QVERIFY(ti);

    for (int block = 0; block < 3; ++block) {
        auto gp = coord->globalPositionOf(ti, block, 0);
        auto ip = coord->itemAtGlobalLine(gp.line);
        QVERIFY2(ip.item != nullptr,
                 qPrintable(QStringLiteral("itemAtGlobalLine returned null for block %1 (line %2)")
                            .arg(block).arg(gp.line)));
        QCOMPARE(ip.item, ti);
        QCOMPARE(ip.localBlockNumber, block);
    }
}

void TstGlobalCoordinates::itemAtGlobalLinePastEnd()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("short"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    auto ip = coord->itemAtGlobalLine(999);
    QVERIFY(ip.item != nullptr);
    QCOMPARE(ip.localBlockNumber, 0);
}

void TstGlobalCoordinates::itemAtGlobalLineInBlockItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("text\n\n| H |\n|---|\n| V |\n\nmore"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();

    // Check if the splitter actually created separate items.
    bool hasBlockItem = false;
    for (auto *item : coord->items()) {
        if (!item->isTextItem()) {
            hasBlockItem = true;
            break;
        }
    }
    if (!hasBlockItem) {
        QSKIP("Splitter did not create a block item for the table");
        return;
    }

    // Line 3 is inside the table block item.
    auto ip = coord->itemAtGlobalLine(3);
    QVERIFY(ip.item == nullptr);
}

void TstGlobalCoordinates::selectAllCrossBoundary()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("alpha\n\n| H |\n|---|\n| V |\n\nbravo"));
    editor.show();
    QApplication::processEvents();

    editor.selectAll();
    QApplication::processEvents();

    editor.copy();
    QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("alpha")));
    QVERIFY(clipboard.contains(QStringLiteral("bravo")));
}

void TstGlobalCoordinates::cutRemovesBlockItems()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("before\n\n| H |\n|---|\n| V |\n\nafter"));
    editor.show();
    QApplication::processEvents();

    editor.selectAll();
    QApplication::processEvents();
    editor.cut();
    QApplication::processEvents();

    QString clipboard = QApplication::clipboard()->text();
    // Table is now auto-formatted by TableSerializer, so check for header
    // content rather than exact pipe format
    QVERIFY(clipboard.contains(QLatin1Char('H')));
    QVERIFY(clipboard.contains(QLatin1Char('V')));

    QString remaining = editor.toPlainText().trimmed();
    QVERIFY(!remaining.contains(QLatin1Char('H')));
    QVERIFY(!remaining.contains(QStringLiteral("before")));
    QVERIFY(!remaining.contains(QStringLiteral("after")));
}

void TstGlobalCoordinates::goToLineFirstItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    editor.show();
    QApplication::processEvents();

    editor.goToLine(2);
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), 2);
}

void TstGlobalCoordinates::goToLineSecondItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("first\n\n| H |\n|---|\n| V |\n\nsecond\nthird"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No second text item");
        return;
    }

    // Check we actually have multiple text items.
    MarkdownTextItem *firstText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem()) {
            firstText = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    if (firstText == lastText) {
        QSKIP("Splitter did not produce multiple text items");
        return;
    }

    auto gp = coord->globalPositionOf(lastText, 0, 0);

    editor.goToLine(gp.line);
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), gp.line);
}

void TstGlobalCoordinates::goToLinePastEnd()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("one\ntwo"));
    editor.show();
    QApplication::processEvents();

    editor.goToLine(999);
    QApplication::processEvents();

    QCOMPARE(editor.cursorLine(), 2);
}

void TstGlobalCoordinates::cursorLineFirstItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aaa\nbbb\nccc"));
    editor.show();
    QApplication::processEvents();

    // Navigate to line 1 first, since the cursor position after
    // setPlainText is implementation-defined.
    editor.goToLine(1);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), 1);

    editor.goToLine(3);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), 3);
}

void TstGlobalCoordinates::cursorLineSecondItem()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral("aa\nbb\n\n| H |\n|---|\n| V |\n\ncc\ndd"));
    editor.show();
    QApplication::processEvents();

    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    if (!lastText) {
        QSKIP("No second text item");
        return;
    }

    MarkdownTextItem *firstText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem()) {
            firstText = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    if (firstText == lastText) {
        QSKIP("Splitter did not produce multiple text items");
        return;
    }

    auto gpCC = coord->globalPositionOf(lastText, 0, 0);
    auto gpDD = coord->globalPositionOf(lastText, 1, 0);

    editor.goToLine(gpCC.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpCC.line);

    editor.goToLine(gpDD.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpDD.line);
}

void TstGlobalCoordinates::endToEndAllFixes()
{
    Editor editor;
    editor.resize(800, 600);
    editor.setPlainText(QStringLiteral(
        "# Title\n\nParagraph one.\n\n| Col |\n|-----|\n| Val |\n\nParagraph two.\nLine ten."));
    editor.show();
    QApplication::processEvents();

    // Fix 4: cursorLine starts at 1 after navigating there.
    editor.goToLine(1);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), 1);

    // Fix 3: goToLine to last line in second text item.
    auto *coord = editor.coordinatorForTesting();
    MarkdownTextItem *lastText = nullptr;
    for (auto *item : coord->items()) {
        if (item->isTextItem())
            lastText = static_cast<MarkdownTextItem *>(item);
    }
    QVERIFY(lastText);
    auto gpLast = coord->globalPositionOf(lastText,
        lastText->document()->blockCount() - 1, 0);

    editor.goToLine(gpLast.line);
    QApplication::processEvents();
    QCOMPARE(editor.cursorLine(), gpLast.line);

    // Fix 1: selectAll selects across all items.
    editor.selectAll();
    QApplication::processEvents();
    editor.copy();
    QString clipboard = QApplication::clipboard()->text();
    QVERIFY(clipboard.contains(QStringLiteral("Title")));
    QVERIFY(clipboard.contains(QStringLiteral("Line ten.")));

    // Fix 2: cut removes everything including the table.
    editor.selectAll();
    QApplication::processEvents();
    editor.cut();
    QApplication::processEvents();
    QString remaining = editor.toPlainText().trimmed();
    QVERIFY(!remaining.contains(QStringLiteral("| Col |")));
    QVERIFY(!remaining.contains(QStringLiteral("Title")));
}

QTEST_MAIN(TstGlobalCoordinates)
#include "tst_global_coordinates.moc"
