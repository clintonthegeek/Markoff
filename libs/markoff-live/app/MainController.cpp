// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainController.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

MainController::MainController(Markoff::MarkoffDocument *doc,
                               QString filePath,
                               QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_filePath(std::move(filePath))
    , m_linkService(new MarkdownLinkService(this))
{
    connect(m_doc, &Markoff::MarkoffDocument::dirtyChanged,
            this, &MainController::updateTitle);
    connectLinkService();
    updateTitle();
}

QString MainController::title() const
{
    return m_title;
}

MarkdownLinkService *MainController::linkService() const
{
    return m_linkService;
}

QString MainController::statusMessage() const
{
    return m_statusMessage;
}

QString MainController::filePath() const
{
    return m_filePath;
}

void MainController::save()
{
    const quint64 seq = m_doc->d2EditSequence();
    // `toMarkdownUtf8()` reads the legacy buffer that D2 edits don't
    // populate — using it as a save source silently zeros files. Use
    // the D2-aware `serializeForSave()` (per markoff-core CLAUDE.md +
    // docs/handoff/2026-05-21-save-path-data-loss.md). Corbomite's
    // Vault was migrated 2026-05-21; this app's MainController was
    // missed at the time.
    const QByteArray bytes = m_doc->serializeForSave();

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("MainController::save: cannot open '%s' for writing: %s",
                 qUtf8Printable(m_filePath),
                 qUtf8Printable(f.errorString()));
        return;
    }
    f.write(bytes);
    if (!f.commit()) {
        qWarning("MainController::save: commit failed for '%s': %s",
                 qUtf8Printable(m_filePath),
                 qUtf8Printable(f.errorString()));
        return;
    }

    m_doc->markSaved(seq);
}

void MainController::loadDocumentFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("MainController::loadDocumentFromPath: cannot open '%s': %s",
                 qUtf8Printable(path),
                 qUtf8Printable(file.errorString()));
        return;
    }
    const QByteArray content = file.readAll();
    m_doc->loadFromMarkdown(content);
    m_doc->markSaved(m_doc->d2EditSequence());
    m_filePath = path;
    Q_EMIT filePathChanged();
    Q_EMIT fromContextChanged(path);
    updateTitle();
}

void MainController::updateTitle()
{
    const QString base = m_filePath.isEmpty()
        ? tr("Untitled") : QFileInfo(m_filePath).fileName();
    const QString dirty = m_doc->dirty() ? QStringLiteral("* ") : QString{};
    m_title = dirty + base + QStringLiteral(" — markoff-live");
    emit titleChanged();
}

void MainController::connectLinkService()
{
    connect(m_linkService, &MarkdownLinkService::statusMessage,
            this, [this](const QString &msg) {
        if (m_statusMessage == msg) return;
        m_statusMessage = msg;
        Q_EMIT statusMessageChanged();
    });
}
