// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

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
    // D2 per-block concatenation in no-separator coordinates (the space
    // `resolveTextAnchor` returns, and the same space `applyFlatEdit`
    // operates in). The legacy `toMarkdownUtf8()` fallback was removed once
    // resetContent started populating D2 blocks (Markoff `861196c`) —
    // iterateBlocks() is now empty only on a genuinely empty document, where
    // the legacy buffer would also be empty.
    QByteArray utf8;
    for (Markoff::BlockId id : m_markoffDocument->iterateBlocks())
        utf8 += m_markoffDocument->blockText(id);
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
        QObject::disconnect(m_subscribedDoc, &Markoff::MarkoffDocument::d2DocumentChanged,
                            this, &SourceTextDocumentBinding::onD2DocumentChanged);
    }

    m_subscribedDoc = newDoc;

    if (m_subscribedDoc) {
        QObject::connect(m_subscribedDoc, &Markoff::MarkoffDocument::d2DocumentChanged,
                         this, &SourceTextDocumentBinding::onD2DocumentChanged);
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

// Translate a byte offset in the separator-bearing flat-view space (what
// `MarkoffDocument::flatView()` returns) to the equivalent offset in the
// no-separator concatenation space used by `applyFlatEdit`. Offsets that
// land inside a separator span are clamped to the no-separator boundary
// between the surrounding blocks.
//
// Known gap (TODO): edits that delete separator bytes (e.g. backspace at
// the start of a block, removing the `\n\n` between two blocks) currently
// translate to a zero-length cursor edit in no-separator space, so the
// model retains both blocks while the QTextDocument has them merged. The
// subsequent `onD2DocumentChanged` then reverts the user's edit. Source
// widget consumers should be aware that separator-zone deletes do not
// merge blocks yet — needs a follow-on for full structural editing parity.
static quint32 sepViewToNoSepByte(const Markoff::MarkoffDocument *doc, quint32 sepOff)
{
    const auto blocks = doc->iterateBlocks();
    constexpr quint32 SEP_LEN = 2;   // "\n\n"
    quint32 sepCursor   = 0;
    quint32 noSepCursor = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const quint32 blkSize = static_cast<quint32>(doc->blockText(blocks[i]).size());
        const quint32 blkEnd  = sepCursor + blkSize;
        if (sepOff <= blkEnd) return noSepCursor + (sepOff - sepCursor);
        sepCursor   = blkEnd;
        noSepCursor += blkSize;
        if (i + 1 < blocks.size()) {
            const quint32 sepEnd = sepCursor + SEP_LEN;
            if (sepOff < sepEnd) return noSepCursor;  // inside separator → clamp
            sepCursor = sepEnd;
        }
    }
    return noSepCursor;
}

void SourceTextDocumentBinding::syncQtDocumentFromMarkoff()
{
    if (!m_subscribedDoc || !m_textDocument) return;
    // Use the separator-bearing flat view so the inner QTextDocument is a
    // 1:1 mirror of the saved markdown — line/column positions match the
    // file. `applyFlatEdit`'s coordinate space (no-separator) is translated
    // in `onQtContentsChange`.
    const QString text = QString::fromUtf8(m_subscribedDoc->flatView());
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

    // Compute byte offsets against the PRE-CHANGE document state in the
    // separator-bearing flat view (what the QTextDocument holds).
    const QByteArray preBytesSep = doc->flatView();
    const QString    preTextSep  = QString::fromUtf8(preBytesSep);
    const quint32 oldStartSep = qtPosToByteOffset(preTextSep, qtPos);
    const quint32 oldEndSep   = qtPosToByteOffset(preTextSep, qtPos + charsRemoved);

    // Translate to no-separator coordinates for applyFlatEdit.
    const quint32 oldStart = sepViewToNoSepByte(doc, oldStartSep);
    const quint32 oldEnd   = sepViewToNoSepByte(doc, oldEndSep);

    // Extract the inserted text from the POST-CHANGE QTextDocument.
    const QString postPlain = m_textDocument->toPlainText();
    const QString insertedText = postPlain.mid(qtPos, charsAdded);
    const QByteArray newText = insertedText.toUtf8();

    m_applyingLocalEdit = true;
    doc->applyFlatEdit(oldStart, oldEnd, newText, Markoff::Origin::UserEdit);
    m_applyingLocalEdit = false;
}

void SourceTextDocumentBinding::onD2DocumentChanged()
{
    // Cycle guard: if WE just called applyFlatEdit (forward path), m_applyingLocalEdit
    // is true for synchronous echoes. d2DocumentChanged is debounced, so by the time
    // it fires m_applyingLocalEdit is already false; primary protection is the
    // equality check below.
    if (m_applyingLocalEdit) return;
    if (!m_textDocument) return;
    if (!m_subscribedDoc) return;

    const QString expectedStr = QString::fromUtf8(m_subscribedDoc->flatView());
    // After a forward edit the QTextDocument already holds the new text;
    // this equality check prevents the unnecessary setPlainText re-application.
    if (m_textDocument->toPlainText() == expectedStr) return;

    m_applyingRemoteEdit = true;
    m_textDocument->setPlainText(expectedStr);
    m_applyingRemoteEdit = false;
}

}  // namespace Markoff
