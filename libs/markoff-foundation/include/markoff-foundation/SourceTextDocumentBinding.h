// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTextDocument>
#include <QtGlobal>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/Session.h>

namespace Markoff {

/// Bidirectional bridge between a Qt `QTextDocument` (the buffer behind a
/// QML `TextArea` or a widget-side `QPlainTextEdit`) and a foundation
/// `MarkoffDocument` + `Session`.
///
/// Forward path: `QTextDocument::contentsChange` → `MarkoffDocument::applyLocalEdit`.
/// Reverse path: `MarkoffDocument::contentsChanged` → cursor-driven
/// remove/insert on `QTextDocument`.
///
/// Cursor + selection are lifted to `Session::primarySelection()` (anchors,
/// not ints) so they survive concurrent edits and round-trip through the
/// CRDT layer. Two cycle guards (`m_applyingLocalEdit` /
/// `m_applyingRemoteEdit`) prevent forward/reverse bounceback. A third
/// (`m_applyingBackendCursor`) guards the int↔anchor cursor bridge.
///
/// This class is QML-free; the QML view library registers it as
/// `QML_FOREIGN` to expose it to QML without polluting the foundation header.
class MARKOFF_FOUNDATION_EXPORT SourceTextDocumentBinding : public QObject {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *markoffDocument
               READ markoffDocument
               WRITE setMarkoffDocument
               NOTIFY markoffDocumentChanged)
    Q_PROPERTY(Markoff::Session *session
               READ session
               WRITE setSession
               NOTIFY sessionChanged)
    Q_PROPERTY(QTextDocument *textDocument
               READ textDocument
               WRITE setTextDocument
               NOTIFY textDocumentChanged)
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

    Markoff::MarkoffDocument *markoffDocument() const;
    void                       setMarkoffDocument(Markoff::MarkoffDocument *);

    Markoff::Session *session() const;
    void              setSession(Markoff::Session *);

    QTextDocument *textDocument() const;
    void           setTextDocument(QTextDocument *);

    int  cursorPosition() const;
    void setCursorPosition(int pos);

    int  selectionStart() const;
    void setSelectionStart(int pos);

    int  selectionEnd() const;
    void setSelectionEnd(int pos);

Q_SIGNALS:
    void markoffDocumentChanged();
    void sessionChanged();
    void textDocumentChanged();
    void cursorPositionChanged();
    void selectionStartChanged();
    void selectionEndChanged();

private Q_SLOTS:
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);
    void onMarkoffContentsChanged(const QList<Markoff::MarkoffEdit> &edits);
    void onSessionPrimarySelectionChanged(const Markoff::Selection &);

private:
    /// Called whenever the document or text-document pointer changes.
    /// Disables QTextDocument's own undo stack and (re)wires its
    /// contentsChange signal.
    void rewireQtDocument();

    /// Rewire the contentsChanged subscription to the current MarkoffDocument.
    void rebindMarkoffDocumentSubscription();

    /// Rewire the primarySelectionChanged subscription to the current Session.
    void rebindSessionSubscription();

    /// Push the current (m_selectionStart, m_selectionEnd) ints to the session
    /// as a Primary selection (anchors derived from current QTextDocument text).
    void pushSelectionToSession();

    /// Sync int cursor/selection from the current Session::primarySelection().
    void syncFromSession();

    /// Seed m_textDocument with the current content of m_markoffDocument.
    /// Called whenever both pointers become available to handle the case where
    /// MarkoffDocument was populated (e.g. via resetContent) before the binding
    /// subscribed.
    void syncQtDocumentFromMarkoff();

    Markoff::MarkoffDocument *m_markoffDocument = nullptr;
    Markoff::Session         *m_session         = nullptr;
    QTextDocument            *m_textDocument    = nullptr;

    Markoff::MarkoffDocument *m_subscribedDoc     = nullptr;  ///< what we're currently subscribed to
    Markoff::Session         *m_subscribedSession = nullptr;  ///< current session subscription

    bool m_applyingLocalEdit      = false;  ///< T12: set during applyLocalEdit ingestion
    bool m_applyingRemoteEdit     = false;  ///< T13: set during reverse edit application
    bool m_applyingBackendCursor  = false;  ///< T14: cycle guard for int↔anchor sync

    int m_cursorPosition = 0;   ///< T14: mirrors TextArea.cursorPosition
    int m_selectionStart = 0;   ///< T14: mirrors TextArea.selectionStart
    int m_selectionEnd   = 0;   ///< T14: mirrors TextArea.selectionEnd
};

}  // namespace Markoff
