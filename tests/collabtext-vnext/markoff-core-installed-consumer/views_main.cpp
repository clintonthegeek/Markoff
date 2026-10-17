// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QCoreApplication>
#include <QString>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/source/Editor.h>
#include <markoff/styled/Editor.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) return 1;
    auto provider = std::move(providerResult).value();

    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("installed-views-consumer"), 1};
    auto documentResult = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::ephemeral);
    if (!documentResult.ok()) return 2;
    auto document = std::move(documentResult).value();
    const auto imported = document->importInitialMarkdown(
        QByteArrayLiteral("# Installed view consumer\n\nA paragraph.\n"));
    if (imported.state != Markoff::VNext::ActionPublicationState::committed)
        return 3;

    auto* session = document->createSession();
    if (!session) return 4;

    Markoff::Source::Editor sourceEditor;
    sourceEditor.setDocument(document.get());

    Markoff::Styled::Editor styledEditor;
    styledEditor.setSession(session);
    styledEditor.setDocument(document.get());

    Markoff::Canvas::EditorWidget canvasEditor;
    canvasEditor.setDocument(document.get());

    sourceEditor.resize(640, 480);
    styledEditor.resize(640, 480);
    canvasEditor.resize(640, 480);
    sourceEditor.show();
    styledEditor.show();
    canvasEditor.show();
    QCoreApplication::processEvents();

    return document->iterateBlocks().size() == 2 ? 0 : 5;
}
