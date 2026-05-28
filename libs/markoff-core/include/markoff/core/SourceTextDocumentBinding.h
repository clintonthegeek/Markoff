// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTextDocument>
#include <QtGlobal>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/Session.h>

namespace Markoff {

/// Bidirectional bridge between a Qt `QTextDocument` (the buffer behind a
/// QML `TextArea` or a widget-side `QPlainTextEdit`) and a foundation
/// `MarkoffDocument` + `Session`.
///
/// Forward path: `QTextDocument::contentsChange` → `MarkoffDocument::applyFlatEdit`.
/// Reverse path: `MarkoffDocument::d2DocumentChanged` → full-replace
/// `QTextDocument::setPlainText` with block-buffer flat text.
///
/// Cursor + selection are lifted to `Session::primarySelection()` (anchors,
/// not ints) so they survive concurrent edits and round-trip through the
/// CRDT layer. Two cycle guards (`m_applyingLocalEdit` /
/// `m_applyingRemoteEdit`) prevent forward/reverse bounceback. A third
/// (`m_applyingBackendCursor`) guards the int↔anchor cursor bridge.
///
/// This class is QML-free; the QML view library registers it as
/// `QML_FOREIGN` to expose it to QML without polluting the foundation header.
class MARKOFF_CORE_EXPORT SourceTextDocumentBinding : public QObject {
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
    /// The binding-resolved caret, in sep-view (QTextDocument) coordinates.
    /// The owning widget applies this to its real caret. start==active is a
    /// collapsed caret. This is the SOLE caret-output of the binding.
    void caretResolved(int start, int active);

private Q_SLOTS:
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);
    void onD2DocumentChanged();
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

    /// Sep-view (QTextDocument, UTF-16) position of `byteInBlock` within
    /// `block`: sum of each preceding block's UTF-16 length + 2 per "\n\n"
    /// separator, plus the in-block UTF-16 offset.
    int sepViewPosOf(Markoff::BlockId block, int byteInBlock) const;

    /// Map a no-separator global byte offset (the space resolveTextAnchor
    /// returns) to a sep-view QTextDocument position.
    int noSepByteToSepViewPos(quint32 noSepByte) const;

    /// Single emit point for the resolved caret.
    void emitCaret(int start, int active);

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

    struct PendingCaret { Markoff::BlockId block; int offsetInBlock = 0; };
    /// Set by a structural op to declare the intended post-edit caret; resolved
    /// + emitted at the tail of onD2DocumentChanged once the reverse diff settles.
    std::optional<PendingCaret> m_pendingCaret;

    int m_cursorPosition = 0;   ///< T14: mirrors TextArea.cursorPosition
    int m_selectionStart = 0;   ///< T14: mirrors TextArea.selectionStart
    int m_selectionEnd   = 0;   ///< T14: mirrors TextArea.selectionEnd
};

}  // namespace Markoff
