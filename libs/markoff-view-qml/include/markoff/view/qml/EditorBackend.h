// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>

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
public:
    explicit EditorBackend(QObject *parent = nullptr);
    ~EditorBackend() override;

    Markoff::MarkoffDocument *document() const;
    void                       setDocument(Markoff::MarkoffDocument *);

    Markoff::Session *session() const;

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();

private:
    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::Session         *m_session  = nullptr;
};

}  // namespace Markoff::View::Qml
