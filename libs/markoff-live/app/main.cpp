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
#include "MainController.h"

/// Test app for markoff-live. Loads a Markdown file and renders it via
/// LiveListModelBinding + LiveView.
/// Usage:
///   markoff-live-app <markdown-file>      open existing file
///   markoff-live-app --new <output-path>  start with an empty doc; save to <output-path>
int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);

    bool isNew = false;
    QString filePath;
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--new")) {
        isNew = true;
        filePath = QString::fromLocal8Bit(argv[2]);
    } else if (argc == 2) {
        filePath = QString::fromLocal8Bit(argv[1]);
    } else {
        qWarning("Usage:\n  %s <markdown-file>\n  %s --new <output-path>",
                 argv[0], argv[0]);
        return 1;
    }

    QByteArray content;
    if (!isNew) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("Cannot open '%s': %s", qUtf8Printable(filePath),
                     qUtf8Printable(file.errorString()));
            return 1;
        }
        content = file.readAll();
    }

    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->loadFromMarkdown(content);
    doc->markSaved(doc->d2EditSequence());

    Markoff::Session *session = doc->createSession();

    auto ctrl = std::make_unique<MainController>(doc.get(), filePath);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), doc.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxSession"), session);
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxMain"), ctrl.get());

    engine.loadFromModule("org.markoff.live.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
