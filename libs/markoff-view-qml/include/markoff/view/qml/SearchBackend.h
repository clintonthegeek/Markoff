// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <markoff-foundation/SearchEngine.h>
#include <markoff/view/qml/EditorBackend.h>

namespace Markoff::View::Qml {

class SearchBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Markoff::View::Qml::EditorBackend *editorBackend
               READ editorBackend
               WRITE setEditorBackend
               NOTIFY editorBackendChanged)
    Q_PROPERTY(QString needle
               READ needle
               WRITE setNeedle
               NOTIFY needleChanged)
    Q_PROPERTY(int flags
               READ flags
               WRITE setFlags
               NOTIFY flagsChanged)
    Q_PROPERTY(int matchCount
               READ matchCount
               NOTIFY matchCountChanged)
public:
    explicit SearchBackend(QObject *parent = nullptr);
    ~SearchBackend() override;

    EditorBackend *editorBackend() const;
    void           setEditorBackend(EditorBackend *);

    QString needle() const;
    void    setNeedle(const QString &);

    int  flags() const;
    void setFlags(int);

    int  matchCount() const;

    Q_INVOKABLE int  findAll();
    Q_INVOKABLE bool findNext();
    Q_INVOKABLE bool findPrevious();
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void editorBackendChanged();
    void needleChanged();
    void flagsChanged();
    void matchCountChanged();

private:
    EditorBackend         *m_editorBackend = nullptr;
    QString                m_needle;
    int                    m_flags         = static_cast<int>(Markoff::SearchEngine::NoFlags);
    int                    m_matchCount    = 0;
    Markoff::SearchEngine  m_engine;
};

}  // namespace Markoff::View::Qml
