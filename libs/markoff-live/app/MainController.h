// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>

#include <markoff/core/MarkoffDocument.h>

/// Owns save + window-title logic for the markoff-live-app.
/// Connects to MarkoffDocument::dirtyChanged to produce a reactive
/// title property (basename [* if dirty] + " — markoff-live").
class MainController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
public:
    explicit MainController(Markoff::MarkoffDocument *doc,
                            QString filePath,
                            QObject *parent = nullptr);

    QString title() const;

public Q_SLOTS:
    void save();

Q_SIGNALS:
    void titleChanged();

private:
    void updateTitle();

    Markoff::MarkoffDocument *m_doc;
    QString                   m_filePath;
    QString                   m_title;
};
