// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWidget>

#include <markoff/source/widget/Editor.h>
#include <markoff/source/widget/FindBar.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    Markoff::MarkoffDocument doc(1);
    doc.setCoalescingIdleMs(80);

    QByteArray seed;
    if (argc > 1) {
        QFile f(QString::fromUtf8(argv[1]));
        if (f.open(QIODevice::ReadOnly)) seed = f.readAll();
    }
    if (!seed.isEmpty()) doc.resetContent(seed, Markoff::Origin::FirstOpen);

    QMainWindow win;
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *editor = new Markoff::Source::Widget::Editor;
    editor->setDocument(&doc);
    auto *findbar = new Markoff::Source::Widget::FindBar(editor);

    layout->addWidget(editor, 1);
    layout->addWidget(findbar);

    win.setCentralWidget(central);
    win.resize(900, 700);
    win.setWindowTitle(QObject::tr("markoff-source-widget"));

    auto *findShortcut = new QShortcut(QKeySequence::Find, &win);
    QObject::connect(findShortcut, &QShortcut::activated, findbar,
                     &Markoff::Source::Widget::FindBar::activate);

    win.show();
    return app.exec();
}
