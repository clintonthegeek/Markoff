// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWidget>

#include <markoff/source/Editor.h>
#include <markoff/source/FindBar.h>
#include <markoff/core/MarkoffDocument.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    Markoff::MarkoffDocument doc(1);

    QByteArray seed;
    if (argc > 1) {
        QFile f(QString::fromUtf8(argv[1]));
        if (f.open(QIODevice::ReadOnly)) seed = f.readAll();
    }
    if (!seed.isEmpty()) doc.loadFromMarkdown(seed);
    else doc.loadFromMarkdown(QByteArray());

    QMainWindow win;
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *editor = new Markoff::Source::Editor;
    editor->setDocument(&doc);
    auto *findbar = new Markoff::Source::FindBar(editor);

    layout->addWidget(editor, 1);
    layout->addWidget(findbar);

    win.setCentralWidget(central);
    win.resize(900, 700);
    win.setWindowTitle(QObject::tr("markoff-source"));

    auto *findShortcut = new QShortcut(QKeySequence::Find, &win);
    QObject::connect(findShortcut, &QShortcut::activated, findbar,
                     &Markoff::Source::FindBar::activate);

    win.show();
    return app.exec();
}
