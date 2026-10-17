// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QMainWindow>
#include <QUuid>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>
#include <markoff/vnext/DocumentStorageProvider.h>

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

    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) return 1;
    auto provider = std::move(providerResult).value();
    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("styled-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)), 1};
    auto documentResult = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::ephemeral);
    if (!documentResult.ok()) return 1;
    auto doc = std::move(documentResult).value();
    if (doc->importInitialMarkdown(initial).state
        != Markoff::VNext::ActionPublicationState::committed) return 1;
    QMainWindow window;
    auto *editor = new Markoff::Styled::Editor(&window);
    auto *document = doc.get();
    auto *session = document->createSession();
    Q_UNUSED(session);
    editor->setSession(session);
    editor->setDocument(document);
    window.setCentralWidget(editor);
    window.resize(900, 700);
    window.show();
    return app.exec();
}
