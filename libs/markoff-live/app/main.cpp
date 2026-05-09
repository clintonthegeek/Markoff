// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QFileInfo>
#include <QQuickStyle>

#include <markoff/core/MarkoffDocument.h>

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

    // PERF WORKAROUND (2026-05-09): pinned replicaId=1.
    // CollabText::Crdt::Buffer's load path is O(replicaId) in both time and
    // memory. Random uint16 replicaIds (the obvious "give every replica a
    // fresh id" approach) yield 50 s+ loads and ~1 GB RSS for a 73 kB doc.
    // replicaId=1 takes ~500 ms for the same doc.
    // Findings + reproducer: docs/handoff/2026-05-09-collabtext-replica-id-perf.md
    // Upstream fix needed before D5 (collab) ships — multi-user requires
    // distinct replicaIds, so the workaround is single-user only.
    const quint16 replicaId = 1;
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->loadFromMarkdown(content);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxDocument"), doc.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("ctxTitle"),
        QFileInfo(argv[1]).fileName());

    engine.loadFromModule("org.markoff.live.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
