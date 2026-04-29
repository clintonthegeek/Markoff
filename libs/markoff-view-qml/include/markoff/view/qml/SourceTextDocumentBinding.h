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

    /// Convert a UTF-16 code-unit offset within `text` to a UTF-8 byte offset.
    /// Returns the byte offset such that `text.left(qtOffset).toUtf8().size() == byteOffset`.
    static quint32 qtPosToByteOffset(const QString &text, int qtOffset);

    /// Convert a UTF-8 byte offset within `utf8` to a UTF-16 code-unit offset.
    /// Returns the code-unit position such that the byte at `byteOffset` lies
    /// at or just before that code unit.
    static int byteOffsetToQtPos(const QByteArray &utf8, quint32 byteOffset);

    EditorBackend     *editorBackend() const;
    void               setEditorBackend(EditorBackend *);

    QQuickTextDocument *qtQuickDocument() const;
    void                setQtQuickDocument(QQuickTextDocument *);

Q_SIGNALS:
    void editorBackendChanged();
    void qtQuickDocumentChanged();

private Q_SLOTS:
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    /// Called whenever both pointers are non-null and either changed.
    /// Captures m_qtDoc and disables QTextDocument's own undo stack.
    void tryCaptureQtDocument();

    EditorBackend      *m_editorBackend = nullptr;
    QQuickTextDocument *m_qtQuickDoc    = nullptr;
    QTextDocument      *m_qtDoc         = nullptr;  ///< the captured QTextDocument

    bool m_applyingLocalEdit  = false;  ///< T12: set during applyLocalEdit ingestion
    bool m_applyingRemoteEdit = false;  ///< T13: set during reverse edit application
};

}  // namespace Markoff::View::Qml
