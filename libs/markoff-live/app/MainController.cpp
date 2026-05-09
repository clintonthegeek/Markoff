// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainController.h"

#include <QFileInfo>
#include <QSaveFile>

MainController::MainController(Markoff::MarkoffDocument *doc,
                               QString filePath,
                               QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_filePath(std::move(filePath))
{
    connect(m_doc, &Markoff::MarkoffDocument::dirtyChanged,
            this, &MainController::updateTitle);
    updateTitle();
}

QString MainController::title() const
{
    return m_title;
}

void MainController::save()
{
    const quint64 seq = m_doc->d2EditSequence();
    const QByteArray bytes = m_doc->toMarkdownUtf8();

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

void MainController::updateTitle()
{
    const QString base = QFileInfo(m_filePath).fileName();
    const QString dirty = m_doc->dirty() ? QStringLiteral("* ") : QString{};
    m_title = dirty + base + QStringLiteral(" — markoff-live");
    emit titleChanged();
}
