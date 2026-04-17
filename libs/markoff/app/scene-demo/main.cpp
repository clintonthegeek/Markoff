// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelectionScene.h"
#include "MarkdownTextItem.h"
#include "StubBlockItem.h"
#include "SelectionManager.h"

#include <QApplication>
#include <QGraphicsView>
#include <QMainWindow>
#include <QStatusBar>
#include <QLabel>

using namespace Markoff;

static QString modeName(SelectionMode mode)
{
    switch (mode) {
    case SelectionMode::None:          return QStringLiteral("None");
    case SelectionMode::WithinItem:    return QStringLiteral("WithinItem");
    case SelectionMode::CrossBoundary: return QStringLiteral("CrossBoundary");
    }
    return {};
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Scene
    auto *scene = new SelectionScene;

    // Layout items vertically
    constexpr qreal spacing = 8.0;
    constexpr qreal itemWidth = 600.0;
    qreal y = 0;

    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral(
        "# Hello World\n"
        "\n"
        "This is the **first** text region.\n"
        "It has multiple lines of markdown."));
    scene->addItem(text1);
    text1->setPos(0, y);
    y += text1->boundingRect().height() + spacing;

    auto *table = new StubBlockItem(
        QStringLiteral("| Name  | Value |\n|-------|-------|\n| Alpha | 1     |\n| Beta  | 2     |"),
        itemWidth, 80);
    scene->addItem(table);
    table->setPos(0, y);
    y += table->boundingRect().height() + spacing;

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral(
        "## Second Section\n"
        "\n"
        "More text after the table.\n"
        "Try selecting across the boundary!"));
    scene->addItem(text2);
    text2->setPos(0, y);
    y += text2->boundingRect().height() + spacing;

    auto *codeBlock = new StubBlockItem(
        QStringLiteral("```python\nprint('hello world')\n```"),
        itemWidth, 60);
    scene->addItem(codeBlock);
    codeBlock->setPos(0, y);
    y += codeBlock->boundingRect().height() + spacing;

    auto *text3 = new MarkdownTextItem;
    text3->setPlainText(QStringLiteral(
        "Final paragraph with some **bold** and *italic* text.\n"
        "This is the last region in the document."));
    scene->addItem(text3);
    text3->setPos(0, y);

    // Register items with SelectionManager
    QList<SelectableItem *> items = {text1, table, text2, codeBlock, text3};
    scene->setSelectableItems(items);
    scene->setSceneRect(0, 0, itemWidth, y + text3->boundingRect().height());

    // View
    auto *view = new QGraphicsView(scene);
    view->setRenderHint(QPainter::Antialiasing);
    view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    view->setDragMode(QGraphicsView::NoDrag);

    // Window
    auto *window = new QMainWindow;
    window->setWindowTitle(QStringLiteral("Markoff Scene Demo \u2014 try drag-selecting across items"));
    window->setCentralWidget(view);
    window->resize(700, 600);

    // Status bar shows selection mode
    auto *statusLabel = new QLabel;
    window->statusBar()->addPermanentWidget(statusLabel);
    statusLabel->setText(QStringLiteral("Mode: None"));

    QObject::connect(scene->selectionManager(), &SelectionManager::modeChanged,
                     statusLabel, [statusLabel](SelectionMode mode) {
        statusLabel->setText(QStringLiteral("Mode: ") + modeName(mode));
    });

    window->show();
    return app.exec();
}
