// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration>
#include <QQuickTextDocument>
#include <QTextDocument>

#include <markoff/view/qml/EditorBackend.h>

namespace Markoff::View::Qml {

class SourceTextDocumentBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Markoff::View::Qml::EditorBackend *editorBackend
               READ editorBackend
               WRITE setEditorBackend
               NOTIFY editorBackendChanged)
    Q_PROPERTY(QQuickTextDocument *qtQuickDocument
               READ qtQuickDocument
               WRITE setQtQuickDocument
               NOTIFY qtQuickDocumentChanged)
public:
    explicit SourceTextDocumentBinding(QObject *parent = nullptr);
    ~SourceTextDocumentBinding() override;

    EditorBackend     *editorBackend() const;
    void               setEditorBackend(EditorBackend *);

    QQuickTextDocument *qtQuickDocument() const;
    void                setQtQuickDocument(QQuickTextDocument *);

Q_SIGNALS:
    void editorBackendChanged();
    void qtQuickDocumentChanged();

private:
    /// Called whenever both pointers are non-null and either changed.
    /// Captures m_qtDoc and disables QTextDocument's own undo stack.
    void tryCaptureQtDocument();

    EditorBackend      *m_editorBackend = nullptr;
    QQuickTextDocument *m_qtQuickDoc    = nullptr;
    QTextDocument      *m_qtDoc         = nullptr;  ///< the captured QTextDocument
};

}  // namespace Markoff::View::Qml
