// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRandomGenerator>
#include <QVariant>

#include <memory>

#include <markoff-foundation/CodeBlockProcessorRegistry.h>
#include <markoff-foundation/CompletionRegistry.h>
#include <markoff-foundation/DefaultLinkService.h>
#include <markoff-foundation/EmojiCompletionProvider.h>
#include <markoff-foundation/Kf6SyntaxHighlightService.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

#include <markoff/view/qml/CompletionPopupModel.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    if (argc < 2) {
        qWarning("Usage: %s <markdown-file>", argv[0]);
        return 1;
    }

    // Read the file.
    QFile in(QString::fromLocal8Bit(argv[1]));
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("Failed to open %s: %s",
                 argv[1], qUtf8Printable(in.errorString()));
        return 1;
    }
    const QByteArray content = in.readAll();
    in.close();

    // Build foundation services (heap-owned via unique_ptr; held alive for app lifetime).
    auto syntax         = std::make_unique<Markoff::Kf6SyntaxHighlightService>();
    auto codeProcessors = std::make_unique<Markoff::CodeBlockProcessorRegistry>();
    auto links          = std::make_unique<Markoff::DefaultLinkService>();
    auto completion     = std::make_unique<Markoff::CompletionRegistry>();
    completion->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    // Document (random replica id for first-open).
    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->resetContent(content, Markoff::Origin::FirstOpen);

    // CompletionPopupModel (heap-owned; QML uses it via context property).
    auto popupModel = std::make_unique<Markoff::View::Qml::CompletionPopupModel>();
    popupModel->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("ctxDocument", doc.get());
    engine.rootContext()->setContextProperty(
        "ctxTheme", QVariant::fromValue(Markoff::Theme::defaultLight()));
    engine.rootContext()->setContextProperty("ctxCompletionModel", popupModel.get());

    engine.loadFromModule("org.markoff.view.qml.app", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    return app.exec();
}
