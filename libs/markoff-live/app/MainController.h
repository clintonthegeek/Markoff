// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>

#include <markoff/core/MarkoffDocument.h>
#include "MarkdownLinkService.h"

/// Owns save + window-title logic for the markoff-live-app.
/// Connects to MarkoffDocument::dirtyChanged to produce a reactive
/// title property (basename [* if dirty] + " — markoff-live").
class MainController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(MarkdownLinkService* linkService READ linkService CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)
public:
    explicit MainController(Markoff::MarkoffDocument *doc,
                            QString filePath,
                            QObject *parent = nullptr);

    QString title() const;
    MarkdownLinkService *linkService() const;
    QString statusMessage() const;
    QString filePath() const;

public Q_SLOTS:
    void save();

Q_SIGNALS:
    void titleChanged();
    void statusMessageChanged();

private:
    void updateTitle();
    void connectLinkService();

    Markoff::MarkoffDocument  *m_doc;
    QString                    m_filePath;
    QString                    m_title;
    QString                    m_statusMessage;
    MarkdownLinkService       *m_linkService;
};
