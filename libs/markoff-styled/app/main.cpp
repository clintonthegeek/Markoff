// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMainWindow>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("markoff-styled-app");

    QCommandLineParser parser;
    parser.addPositionalArgument("file", "Markdown file to open (optional).");
    parser.addHelpOption();
    parser.process(app);

    QByteArray initial;
    if (!parser.positionalArguments().isEmpty()) {
        QFile f(parser.positionalArguments().first());
        if (f.open(QIODevice::ReadOnly)) initial = f.readAll();
    }

    QMainWindow window;
    auto *editor = new Markoff::Styled::Editor(&window);
    auto *doc = new Markoff::MarkoffDocument(1, nullptr, &window);
    doc->loadFromMarkdown(initial);
    auto *session = doc->createSession();
    Q_UNUSED(session);
    editor->setSession(session);
    editor->setDocument(doc);
    window.setCentralWidget(editor);
    window.resize(900, 700);
    window.show();
    return app.exec();
}
