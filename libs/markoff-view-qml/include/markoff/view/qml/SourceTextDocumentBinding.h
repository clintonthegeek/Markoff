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
    Q_PROPERTY(int cursorPosition
               READ cursorPosition
               WRITE setCursorPosition
               NOTIFY cursorPositionChanged)
    Q_PROPERTY(int selectionStart
               READ selectionStart
               WRITE setSelectionStart
               NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd
               READ selectionEnd
               WRITE setSelectionEnd
               NOTIFY selectionEndChanged)
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

    int  cursorPosition() const;
    void setCursorPosition(int pos);

    int  selectionStart() const;
    void setSelectionStart(int pos);

    int  selectionEnd() const;
    void setSelectionEnd(int pos);

Q_SIGNALS:
    void editorBackendChanged();
    void qtQuickDocumentChanged();
    void cursorPositionChanged();
    void selectionStartChanged();
    void selectionEndChanged();

private Q_SLOTS:
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);
    void onMarkoffContentsChanged(const QList<Markoff::MarkoffEdit> &edits);
    void onEditorBackendDocumentChanged();

private:
    /// Called whenever both pointers are non-null and either changed.
    /// Captures m_qtDoc and disables QTextDocument's own undo stack.
    void tryCaptureQtDocument();

    /// Rewire the contentsChanged subscription to the backend's current document.
    void rebindMarkoffDocumentSubscription();

    /// T14: sync cursorPosition from backend's cursorAnchor.
    void syncFromBackendCursor();

    /// T14: sync selectionStart/selectionEnd from backend's selectionAnchor/selectionActive.
    void syncFromBackendSelection();

    /// Seed m_qtDoc with the current content of m_subscribedDoc.
    /// Called whenever both pointers become available to handle the case where
    /// MarkoffDocument was populated (e.g. via resetContent) before the binding subscribed.
    void syncQtDocumentFromMarkoff();

    EditorBackend      *m_editorBackend = nullptr;
    QQuickTextDocument *m_qtQuickDoc    = nullptr;
    QTextDocument      *m_qtDoc         = nullptr;  ///< the captured QTextDocument

    Markoff::MarkoffDocument *m_subscribedDoc = nullptr;  ///< what we're currently subscribed to

    bool m_applyingLocalEdit      = false;  ///< T12: set during applyLocalEdit ingestion
    bool m_applyingRemoteEdit     = false;  ///< T13: set during reverse edit application
    bool m_applyingBackendCursor  = false;  ///< T14: cycle guard for int↔anchor sync

    int m_cursorPosition = 0;   ///< T14: mirrors TextArea.cursorPosition
    int m_selectionStart = 0;   ///< T14: mirrors TextArea.selectionStart
    int m_selectionEnd   = 0;   ///< T14: mirrors TextArea.selectionEnd
};

}  // namespace Markoff::View::Qml
