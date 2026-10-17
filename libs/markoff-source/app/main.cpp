// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QFile>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>
#include <QUuid>

#include <markoff/source/Editor.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QByteArray seed;
    if (argc > 1) {
        QFile f(QString::fromUtf8(argv[1]));
        if (f.open(QIODevice::ReadOnly)) seed = f.readAll();
    }
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) return 1;
    auto provider = std::move(providerResult).value();
    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("source-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces)), 1};
    auto documentResult = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::ephemeral);
    if (!documentResult.ok()) return 1;
    auto document = std::move(documentResult).value();
    if (document->importInitialMarkdown(seed).state
        != Markoff::VNext::ActionPublicationState::committed) return 1;
    auto *documentPtr = document.get();

    QMainWindow win;
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *editor = new Markoff::Source::Editor;
    editor->setDocument(documentPtr);

    layout->addWidget(editor, 1);

    win.setCentralWidget(central);
    win.resize(900, 700);
    win.setWindowTitle(QObject::tr("markoff-source"));

    win.show();
    return app.exec();
}
