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
/// Cursor + selection authority: the binding owns the post-structural-edit
/// caret. Structural ops stage an intended caret; onD2DocumentChanged resolves
/// it (sep-view) and emits `caretResolved`, which the owning widget applies via
/// setTextCursor. `syncFromSession` resolves an externally-driven
/// Session::primarySelection() (collaborator / undo) through the same signal.
/// Two cycle guards (`m_applyingLocalEdit` / `m_applyingRemoteEdit`) prevent
/// forward/reverse bounceback.
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

    /// Apply a structural key (Enter/Backspace/Delete/Tab/Backtab) at the
    /// given QTextDocument caret. `qtPos`/`qtAnchor` are sep-view UTF-16
    /// positions. A non-empty selection (qtPos != qtAnchor) is collapsed
    /// first: the selected range is deleted via `deleteSepRange`, then the
    /// structural op is dispatched at the collapse point (returns true even
    /// when that post-collapse key is a no-op, since the selection was already
    /// consumed). Returns true if the key was a handled structural op (caller
    /// should consume the key event); false → caller falls through to native
    /// editing. Stages m_pendingCaret on success.
    bool handleStructuralKey(int key, int modifiers, int qtPos, int qtAnchor);

Q_SIGNALS:
    void markoffDocumentChanged();
    void sessionChanged();
    void textDocumentChanged();
    /// The binding-resolved caret, in sep-view (QTextDocument) coordinates.
    /// The owning widget applies this to its real caret. start==active is a
    /// collapsed caret. This is the SOLE caret-output of the binding.
    void caretResolved(int start, int active);

private Q_SLOTS:
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);
    void onD2DocumentChanged();
    void onSessionPrimarySelectionChanged(const Markoff::Selection &);

private:
    struct PendingCaret { Markoff::BlockId block; int offsetInBlock = 0; };

    /// Called whenever the document or text-document pointer changes.
    /// Disables QTextDocument's own undo stack and (re)wires its
    /// contentsChange signal.
    void rewireQtDocument();

    /// Rewire the contentsChanged subscription to the current MarkoffDocument.
    void rebindMarkoffDocumentSubscription();

    /// Rewire the primarySelectionChanged subscription to the current Session.
    void rebindSessionSubscription();

    /// Sync cursor/selection from the current Session::primarySelection() into
    /// sep-view coordinates, routing through emitCaret.
    void syncFromSession();

    /// Sep-view (QTextDocument, UTF-16) position of `byteInBlock` within
    /// `block`: sum of each preceding block's UTF-16 length + 2 per "\n\n"
    /// separator, plus the in-block UTF-16 offset.
    int sepViewPosOf(Markoff::BlockId block, int byteInBlock) const;

    /// Map a no-separator global byte offset (the space resolveTextAnchor
    /// returns) to a sep-view QTextDocument position.
    int noSepByteToSepViewPos(quint32 noSepByte) const;

    /// Delete the sep-view byte range [sepLo, sepHi) from the model via
    /// direct D2 primitives (the cross-block merge path). Returns the
    /// collapsed caret as (block, byteInBlock), or nullopt if the range
    /// does not resolve to real blocks. Shared by onQtContentsChange
    /// (selection delete) and (later) handleStructuralKey.
    std::optional<PendingCaret> deleteSepRange(quint32 sepLo, quint32 sepHi);

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

    /// Set by a structural op to declare the intended post-edit caret; resolved
    /// + emitted at the tail of onD2DocumentChanged once the reverse diff settles.
    std::optional<PendingCaret> m_pendingCaret;
};

}  // namespace Markoff
