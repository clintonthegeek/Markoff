// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/SourceTextDocumentBinding.h>

#include <algorithm>

#include <QTextCursor>
#include <QTextDocument>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

namespace Markoff {

SourceTextDocumentBinding::SourceTextDocumentBinding(QObject *parent)
    : QObject(parent) {}
SourceTextDocumentBinding::~SourceTextDocumentBinding() = default;

quint32 SourceTextDocumentBinding::qtPosToByteOffset(const QString &text, int qtOffset)
{
    if (qtOffset <= 0) return 0;
    const int n = std::min(qtOffset, static_cast<int>(text.size()));

    // Walk UTF-16 code units in `text`, counting their UTF-8 byte widths.
    // No allocation; constant memory; O(qtOffset) time.
    quint32 bytes = 0;
    int i = 0;
    while (i < n) {
        const ushort u = text.at(i).unicode();
        if (u < 0x80) {
            bytes += 1;
            ++i;
        } else if (u < 0x800) {
            bytes += 2;
            ++i;
        } else if (u >= 0xD800 && u <= 0xDBFF) {
            // High surrogate. UTF-8 encoding of the surrogate pair is 4 bytes;
            // it consumes 2 UTF-16 code units. Only count the pair if both
            // code units are within `n` — if a high surrogate is at position
            // n-1 and we're stopping there, count it as 0 bytes (not yet
            // emitted; matches the original allocating impl which would
            // truncate before emitting).
            if (i + 1 < n) {
                bytes += 4;
                i += 2;
            } else {
                // Trailing lone high surrogate at the boundary; count as 0
                // and stop.
                break;
            }
        } else {
            // BMP non-ASCII (U+0800..U+FFFF excluding surrogates).
            bytes += 3;
            ++i;
        }
    }

    return bytes;
}

int SourceTextDocumentBinding::byteOffsetToQtPos(const QByteArray &utf8, quint32 byteOffset)
{
    if (byteOffset == 0) return 0;

    // Walk UTF-8 bytes counting UTF-16 code units. No allocation.
    int qtPos = 0;
    quint32 currentByte = 0;
    int i = 0;
    const int sz = utf8.size();
    while (i < sz && currentByte < byteOffset) {
        const uchar b = static_cast<uchar>(utf8.at(i));
        int byteCount;
        int codeUnits;
        if ((b & 0x80) == 0)         { byteCount = 1; codeUnits = 1; }   // ASCII
        else if ((b & 0xE0) == 0xC0) { byteCount = 2; codeUnits = 1; }   // 2-byte UTF-8 → 1 UTF-16
        else if ((b & 0xF0) == 0xE0) { byteCount = 3; codeUnits = 1; }   // 3-byte UTF-8 → 1 UTF-16
        else if ((b & 0xF8) == 0xF0) { byteCount = 4; codeUnits = 2; }   // 4-byte UTF-8 → surrogate pair
        else                         { byteCount = 1; codeUnits = 1; }   // invalid; resync

        // If consuming this character would overshoot byteOffset, stop here
        // — we've reached the qt position just before the byte boundary.
        if (currentByte + static_cast<quint32>(byteCount) > byteOffset) break;
        currentByte += static_cast<quint32>(byteCount);
        qtPos       += codeUnits;
        i           += byteCount;
    }
    return qtPos;
}

// ---------------------------------------------------------------------------
// MarkoffDocument property
// ---------------------------------------------------------------------------

Markoff::MarkoffDocument *SourceTextDocumentBinding::markoffDocument() const
{
    return m_markoffDocument;
}

void SourceTextDocumentBinding::setMarkoffDocument(Markoff::MarkoffDocument *doc)
{
    if (m_markoffDocument == doc) return;
    m_markoffDocument = doc;
    Q_EMIT markoffDocumentChanged();
    rebindMarkoffDocumentSubscription();
    rewireQtDocument();
}

// ---------------------------------------------------------------------------
// Session property
// ---------------------------------------------------------------------------

Markoff::Session *SourceTextDocumentBinding::session() const
{
    return m_session;
}

void SourceTextDocumentBinding::setSession(Markoff::Session *s)
{
    if (m_session == s) return;
    m_session = s;
    Q_EMIT sessionChanged();
    rebindSessionSubscription();
}

// ---------------------------------------------------------------------------
// T14: cursor/selection int properties (UTF-16 ↔ CRDT anchor bridge)
// ---------------------------------------------------------------------------

int SourceTextDocumentBinding::cursorPosition() const { return m_cursorPosition; }
int SourceTextDocumentBinding::selectionStart()  const { return m_selectionStart; }
int SourceTextDocumentBinding::selectionEnd()    const { return m_selectionEnd;   }

void SourceTextDocumentBinding::setCursorPosition(int pos)
{
    if (m_cursorPosition == pos) return;
    m_cursorPosition = pos;

    if (!m_applyingBackendCursor && m_session && m_markoffDocument && m_textDocument) {
        const QString text = m_textDocument->toPlainText();
        const quint32 byteOff = qtPosToByteOffset(text, pos);
        const auto anchor = m_markoffDocument->textAnchorAt(byteOff, /*rightBias*/ false);
        // Cursor move => collapse selection.
        Markoff::Selection sel;
        sel.anchor = anchor;
        sel.active = anchor;
        sel.kind   = Markoff::Selection::Kind::Primary;
        m_session->setPrimarySelection(sel);
    }
    Q_EMIT cursorPositionChanged();
}

void SourceTextDocumentBinding::setSelectionStart(int pos)
{
    if (m_selectionStart == pos) return;
    m_selectionStart = pos;

    if (!m_applyingBackendCursor) {
        pushSelectionToSession();
    }
    Q_EMIT selectionStartChanged();
}

void SourceTextDocumentBinding::setSelectionEnd(int pos)
{
    if (m_selectionEnd == pos) return;
    m_selectionEnd = pos;

    if (!m_applyingBackendCursor) {
        pushSelectionToSession();
    }
    Q_EMIT selectionEndChanged();
}

void SourceTextDocumentBinding::pushSelectionToSession()
{
    if (!m_session || !m_markoffDocument || !m_textDocument) return;
    const QString text = m_textDocument->toPlainText();
    const quint32 startByte = qtPosToByteOffset(text, m_selectionStart);
    const quint32 endByte   = qtPosToByteOffset(text, m_selectionEnd);
    const auto anchorA = m_markoffDocument->textAnchorAt(startByte, /*rightBias*/ false);
    const auto anchorB = m_markoffDocument->textAnchorAt(endByte,   /*rightBias*/ true);
    Markoff::Selection sel;
    sel.anchor = anchorA;
    sel.active = anchorB;
    sel.kind   = Markoff::Selection::Kind::Primary;
    m_session->setPrimarySelection(sel);
}

void SourceTextDocumentBinding::onSessionPrimarySelectionChanged(const Markoff::Selection &)
{
    syncFromSession();
}

void SourceTextDocumentBinding::syncFromSession()
{
    if (!m_session || !m_markoffDocument || !m_textDocument) return;

    const Markoff::Selection sel = m_session->primarySelection();
    const quint32 anchorByte = m_markoffDocument->resolveTextAnchor(sel.anchor);
    const quint32 activeByte = m_markoffDocument->resolveTextAnchor(sel.active);
    const QByteArray utf8 = m_markoffDocument->toMarkdownUtf8();
    const int newStart = byteOffsetToQtPos(utf8, anchorByte);
    const int newEnd   = byteOffsetToQtPos(utf8, activeByte);

    m_applyingBackendCursor = true;
    if (m_cursorPosition != newEnd) {
        m_cursorPosition = newEnd;
        Q_EMIT cursorPositionChanged();
    }
    if (m_selectionStart != newStart) {
        m_selectionStart = newStart;
        Q_EMIT selectionStartChanged();
    }
    if (m_selectionEnd != newEnd) {
        m_selectionEnd = newEnd;
        Q_EMIT selectionEndChanged();
    }
    m_applyingBackendCursor = false;
}

void SourceTextDocumentBinding::rebindMarkoffDocumentSubscription()
{
    Markoff::MarkoffDocument *newDoc = m_markoffDocument;

    if (newDoc == m_subscribedDoc) return;

    if (m_subscribedDoc) {
        QObject::disconnect(m_subscribedDoc, &Markoff::MarkoffDocument::contentsChanged,
                            this, &SourceTextDocumentBinding::onMarkoffContentsChanged);
    }

    m_subscribedDoc = newDoc;

    if (m_subscribedDoc) {
        QObject::connect(m_subscribedDoc, &Markoff::MarkoffDocument::contentsChanged,
                         this, &SourceTextDocumentBinding::onMarkoffContentsChanged);
    }

    // If both doc and qtDoc are captured, seed the qtDoc with the doc's
    // current content. This handles the case where MarkoffDocument was
    // populated (e.g. via resetContent) BEFORE the binding subscribed.
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::rebindSessionSubscription()
{
    if (m_session == m_subscribedSession) return;

    if (m_subscribedSession) {
        QObject::disconnect(m_subscribedSession, &Markoff::Session::primarySelectionChanged,
                            this, &SourceTextDocumentBinding::onSessionPrimarySelectionChanged);
    }

    m_subscribedSession = m_session;

    if (m_subscribedSession) {
        QObject::connect(m_subscribedSession, &Markoff::Session::primarySelectionChanged,
                         this, &SourceTextDocumentBinding::onSessionPrimarySelectionChanged);
    }
}

QTextDocument *SourceTextDocumentBinding::textDocument() const
{
    return m_textDocument;
}

void SourceTextDocumentBinding::setTextDocument(QTextDocument *td)
{
    if (m_textDocument == td) return;

    if (m_textDocument) {
        // Detach from old QTextDocument before switching.
        QObject::disconnect(m_textDocument, &QTextDocument::contentsChange,
                            this, &SourceTextDocumentBinding::onQtContentsChange);
    }

    m_textDocument = td;
    Q_EMIT textDocumentChanged();
    rewireQtDocument();
}

void SourceTextDocumentBinding::rewireQtDocument()
{
    if (m_textDocument) {
        // Foundation's CRDT undo (via MarkoffDocument::undo/redo) is canonical.
        // Disable QTextDocument's own undo stack to prevent double-undo behavior.
        m_textDocument->setUndoRedoEnabled(false);
        // Reconnect (Qt::UniqueConnection prevents double-subscription if
        // rewireQtDocument is called multiple times for the same doc).
        QObject::connect(m_textDocument, &QTextDocument::contentsChange,
                         this, &SourceTextDocumentBinding::onQtContentsChange,
                         Qt::UniqueConnection);
    }

    // Seed the qtDoc from the foundation's current content (if both are now ready).
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::syncQtDocumentFromMarkoff()
{
    if (!m_subscribedDoc || !m_textDocument) return;
    const QByteArray utf8 = m_subscribedDoc->toMarkdownUtf8();
    const QString text = QString::fromUtf8(utf8);
    if (m_textDocument->toPlainText() == text) return;  // already in sync
    m_applyingRemoteEdit = true;
    m_textDocument->setPlainText(text);
    m_applyingRemoteEdit = false;
}

void SourceTextDocumentBinding::onQtContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    // Cycle guard: when T13's reverse path is mid-application, don't loop back.
    if (m_applyingRemoteEdit) return;
    if (!m_markoffDocument) return;
    if (!m_textDocument) return;

    Markoff::MarkoffDocument *doc = m_markoffDocument;

    // Compute byte offsets against the PRE-CHANGE document state.
    // doc->toMarkdownUtf8() still holds the pre-change bytes because we haven't
    // called applyLocalEdit yet.
    const QByteArray preBytes = doc->toMarkdownUtf8();
    const QString preText = QString::fromUtf8(preBytes);
    const quint32 oldStart = qtPosToByteOffset(preText, qtPos);
    const quint32 oldEnd   = qtPosToByteOffset(preText, qtPos + charsRemoved);

    // Extract the inserted text from the POST-CHANGE QTextDocument.
    const QString postPlain = m_textDocument->toPlainText();
    const QString insertedText = postPlain.mid(qtPos, charsAdded);
    const QByteArray newText = insertedText.toUtf8();

    Markoff::MarkoffEdit ed;
    ed.oldStart = oldStart;
    ed.oldEnd   = oldEnd;
    ed.newText  = newText;

    m_applyingLocalEdit = true;
    doc->applyLocalEdit({ ed });
    m_applyingLocalEdit = false;
}

void SourceTextDocumentBinding::onMarkoffContentsChanged(const QList<Markoff::MarkoffEdit> &edits)
{
    // Cycle guard: if WE just called applyLocalEdit (T12 forward path), this is
    // the echo of our own change — don't re-apply.
    if (m_applyingLocalEdit) return;
    if (!m_textDocument) return;
    if (!m_subscribedDoc) return;

    // Capture the pre-change plain text from QTextDocument (it hasn't been
    // touched yet) for byte→Qt-pos conversion.
    const QString preText = m_textDocument->toPlainText();
    const QByteArray preBytes = preText.toUtf8();

    m_applyingRemoteEdit = true;
    QTextCursor cursor(m_textDocument);

    // Walk edits in reverse byte-offset order so that earlier edits' positions
    // are not invalidated by the mutations we apply to later (higher-offset) regions.
    // All offsets in the list are OLD-text coordinates (pre-batch state), so
    // reverse application keeps them valid throughout the loop.
    QList<Markoff::MarkoffEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(),
              [](const Markoff::MarkoffEdit &a, const Markoff::MarkoffEdit &b) {
                  return a.oldStart > b.oldStart;
              });

    for (const Markoff::MarkoffEdit &ed : sorted) {
        const int qtStart = byteOffsetToQtPos(preBytes, ed.oldStart);
        const int qtEnd   = byteOffsetToQtPos(preBytes, ed.oldEnd);

        cursor.setPosition(qtStart);
        cursor.setPosition(qtEnd, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        if (!ed.newText.isEmpty()) {
            cursor.insertText(QString::fromUtf8(ed.newText));
        }
    }

    m_applyingRemoteEdit = false;
}

}  // namespace Markoff
