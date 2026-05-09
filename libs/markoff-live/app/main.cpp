// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QFileInfo>
#include <QQuickStyle>
#include <QRandomGenerator>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

/// Test app for markoff-live. Loads a Markdown file and renders it via
/// LiveListModelBinding + LiveView.
/// Usage: markoff-live-app <markdown-file>
int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);

    if (argc < 2) {
        qWarning("Usage: %s <markdown-file>", argv[0]);
        return 1;
    }

    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot open '%s': %s", argv[1],
                 qUtf8Printable(file.errorString()));
        return 1;
    }
    const QByteArray content = file.readAll();

    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->loadFromMarkdown(content);

    Markoff::Session *session = doc->createSession();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), doc.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxSession"), session);
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxTitle"),
        QFileInfo(argv[1]).fileName());

    engine.loadFromModule("org.markoff.live.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
