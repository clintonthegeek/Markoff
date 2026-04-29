// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::View::Qml {

class EditorBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Markoff::MarkoffDocument *document
               READ document
               WRITE setDocument
               NOTIFY documentChanged)
public:
    explicit EditorBackend(QObject *parent = nullptr);
    ~EditorBackend() override;

    Markoff::MarkoffDocument *document() const;
    void                       setDocument(Markoff::MarkoffDocument *);

Q_SIGNALS:
    void documentChanged();

private:
    Markoff::MarkoffDocument *m_document = nullptr;
};

}  // namespace Markoff::View::Qml
