// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

#include <markoff/source/Editor.h>
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

    layout->addWidget(editor, 1);

    win.setCentralWidget(central);
    win.resize(900, 700);
    win.setWindowTitle(QObject::tr("markoff-source"));

    win.show();
    return app.exec();
}
