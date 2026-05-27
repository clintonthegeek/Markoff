// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>
#include <markoff/core/Detail/FlatBlockResolve.h>

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

// Translate a sep-view byte offset to no-separator coordinates for
// applyFlatEdit, via findBlockAtSepByte (so a boundary/in-separator position
// resolves to a real block edge; biasForward picks next-start vs prev-end).
// Unlike the old clamp, a range straddling a separator yields distinct no-sep
// offsets, so applyFlatEdit sees a real cross-block range and merges blocks.
static quint32 sepViewToNoSepByteForEdit(const Markoff::MarkoffDocument *doc,
                                         quint32 sepOff, bool biasForward)
{
    const auto blocks = doc->iterateBlocks();
    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepOff, biasForward);
    if (!hit) {  // past end → total no-sep length
        quint32 total = 0;
        for (auto id : blocks) total += quint32(doc->blockText(id).size());
        return total;
    }
    quint32 noSep = 0;
    for (int i = 0; i < hit->blockIndex; ++i)
        noSep += quint32(doc->blockText(blocks[size_t(i)]).size());
    return noSep + hit->byteInBlock;
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
    if (!m_markoffDocument || !m_textDocument) return;
    Markoff::MarkoffDocument *doc = m_markoffDocument;

    // The QTextDocument mirrors flatView() (separator-bearing), so its plain
    // text IS the sep-view. Compute sep-view byte offsets against PRE-change
    // state. NOTE: qtPos/charsRemoved are in the PRE-change document; read the
    // pre-change text from flatView() (the doc hasn't been mutated yet on the
    // forward path), and the inserted text from the POST-change QTextDocument.
    const QByteArray preBytesSep = doc->flatView();
    const QString    preTextSep  = QString::fromUtf8(preBytesSep);
    const quint32 sepStart = qtPosToByteOffset(preTextSep, qtPos);
    const quint32 sepEnd   = qtPosToByteOffset(preTextSep, qtPos + charsRemoved);

    const QString postPlain       = m_textDocument->toPlainText();
    const QByteArray insertedUtf8 = postPlain.mid(qtPos, charsAdded).toUtf8();
    const bool insertedHasNewline = insertedUtf8.contains('\n');

    m_applyingLocalEdit = true;

    // Resolve both endpoints in sep-view (biasForward=false = previous-block bias
    // for both; the structural split below handles differing blocks).
    const auto hitStart = Markoff::Detail::findBlockAtSepByte(
        doc, sepStart, /*biasForward=*/false);
    const auto hitEnd = (charsRemoved == 0)
        ? hitStart
        : Markoff::Detail::findBlockAtSepByte(doc, sepEnd, /*biasForward=*/true);

    // ── Fast path: single-block, structure-neutral edit (the common case) ──
    // Typing or deleting within one block, no embedded newline, no separator-
    // spanning range. Apply directly via d2ApplyBufferEdit with an explicit
    // block ID — sidesteps applyFlatEdit's no-separator boundary ambiguity so
    // end-of-block typing lands in the previous block (matches QTextEdit bias).
    if (!insertedHasNewline && hitStart && hitEnd && hitStart->blockId == hitEnd->blockId) {
        const uint32_t removeBytes = (charsRemoved == 0)
            ? 0u : (hitEnd->byteInBlock - hitStart->byteInBlock);
        UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock,
                               removeBytes, insertedUtf8, t);
        m_applyingLocalEdit = false;
        return;
    }

    // ── Cross-block or structural edit ────────────────────────────────────
    // Either: (a) insertedHasNewline (structural split/replace) or
    //         (b) the edit spans a separator (hitStart and hitEnd in different
    //             blocks) — including the pure separator-delete (backspace
    //             across "\n\n") that must merge two blocks.
    //
    // For (b) without newlines we dispatch directly via d2 primitives so we
    // can express "merge block0+block1 without deleting their content" — which
    // applyFlatEdit cannot represent because in no-sep coordinates the end of
    // block0 and start of block1 share the same byte offset.
    //
    // For (a) (structural, embedded newline), route to applyFlatEdit via the
    // bias-aware no-sep translator which at least avoids the old clamping bug.
    if (!insertedHasNewline && hitStart && hitEnd && hitStart->blockId != hitEnd->blockId) {
        // Cross-block merge without structural newlines: directly apply via
        // d2 primitives (mirrors applyFlatEdit's cross-block path).
        const auto allBlocks = doc->iterateBlocks();
        const QByteArray endTail = doc->blockText(hitEnd->blockId)
                                       .mid(static_cast<int>(hitEnd->byteInBlock));

        UndoLog::Transaction t(doc->d2UndoLog());

        // Trim start block from hitStart->byteInBlock to its end.
        const uint32_t startBlockSize = static_cast<uint32_t>(
            doc->blockText(hitStart->blockId).size());
        const uint32_t trimLen = startBlockSize - hitStart->byteInBlock;
        if (trimLen > 0) {
            doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock,
                                   trimLen, QByteArray(), t);
        }

        // Remove any intermediate blocks (between hitStart+1 and hitEnd-1).
        for (int i = hitStart->blockIndex + 1; i < hitEnd->blockIndex; ++i)
            doc->d2RemoveBlock(allBlocks[size_t(i)], t);

        // Remove the end block (its surviving tail is stitched below).
        doc->d2RemoveBlock(hitEnd->blockId, t);

        // Append inserted content + end-block tail to start block.
        const QByteArray toAppend = insertedUtf8 + endTail;
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock,
                               0, toAppend, t);

        m_applyingLocalEdit = false;
        return;
    }

    // Structural edit (insertedHasNewline=true) or empty document: route
    // through applyFlatEdit. Use the bias-aware no-sep translator (not the
    // old clamp) so partial separator-spanning ranges at least get sensible
    // coordinates; applyFlatEdit's RT2 normalization handles the rest.
    const quint32 noSepStart = sepViewToNoSepByteForEdit(doc, sepStart, /*biasForward=*/false);
    const quint32 noSepEnd   = sepViewToNoSepByteForEdit(doc, sepEnd,   /*biasForward=*/true);
    doc->applyFlatEdit(noSepStart, noSepEnd, insertedUtf8, Markoff::Origin::UserEdit);

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
