// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/Theme.h>

namespace Markoff::View::Qml {

class EditorBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document
               WRITE setDocument
               NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Session *session
               READ session
               NOTIFY sessionChanged)
    Q_PROPERTY(Markoff::Theme theme
               READ theme
               WRITE setTheme
               NOTIFY themeChanged)
public:
    explicit EditorBackend(QObject *parent = nullptr);
    ~EditorBackend() override;

    Markoff::MarkoffDocument *document() const;
    void                       setDocument(Markoff::MarkoffDocument *);

    Markoff::Session *session() const;

    Markoff::Theme theme() const;
    void           setTheme(const Markoff::Theme &);

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();
    void themeChanged();

private:
    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
    Markoff::Theme            m_theme    = Markoff::Theme::defaultLight();
};

}  // namespace Markoff::View::Qml
