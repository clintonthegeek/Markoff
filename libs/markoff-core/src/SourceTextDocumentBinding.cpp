// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <optional>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>
#include <markoff/core/StructuralKeyHandler.h>
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

void SourceTextDocumentBinding::onSessionPrimarySelectionChanged(const Markoff::Selection &)
{
    syncFromSession();
}

void SourceTextDocumentBinding::syncFromSession()
{
    if (!m_session || !m_markoffDocument || !m_textDocument) return;
    // Do not fight a local edit mid-flight; the structural path re-asserts the
    // caret from m_pendingCaret at the tail of onD2DocumentChanged instead.
    if (m_applyingLocalEdit) return;

    const Markoff::Selection sel = m_session->primarySelection();
    const quint32 anchorByte = m_markoffDocument->resolveTextAnchor(sel.anchor);
    const quint32 activeByte = m_markoffDocument->resolveTextAnchor(sel.active);
    // resolveTextAnchor returns NO-SEPARATOR global bytes; map each to a
    // sep-view QTextDocument position (the prior implementation concatenated
    // blockText without separators — off by one separator per crossed boundary).
    emitCaret(noSepByteToSepViewPos(anchorByte),
              noSepByteToSepViewPos(activeByte));
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
    // Use the widget flat view (single '\n' between blocks) so the inner
    // QTextDocument mirrors the WYSIWYG paragraph structure rather than the
    // save form. `applyFlatEdit`'s coordinate space (no-separator) is
    // translated in `onQtContentsChange`.
    const QString text = QString::fromUtf8(m_subscribedDoc->widgetFlatView());
    if (m_textDocument->toPlainText() == text) return;  // already in sync
    m_applyingRemoteEdit = true;
    m_textDocument->setPlainText(text);
    m_applyingRemoteEdit = false;
}

int SourceTextDocumentBinding::sepViewPosOf(Markoff::BlockId block,
                                            int byteInBlock) const
{
    if (!m_markoffDocument) return 0;
    int pos = 0;
    for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
        const QByteArray text = m_markoffDocument->blockText(id);
        if (id == block) {
            return pos + byteOffsetToQtPos(text, static_cast<quint32>(byteInBlock));
        }
        pos += QString::fromUtf8(text).size();  // UTF-16 code units
        pos += 1;                                // WP unification: single '\n' separator
    }
    return pos;  // block not found (defensive) -> end of document
}

int SourceTextDocumentBinding::noSepByteToSepViewPos(quint32 noSepByte) const
{
    if (!m_markoffDocument) return 0;
    quint32 cursor = 0;
    const auto blocks = m_markoffDocument->iterateBlocks();
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const Markoff::BlockId id = blocks[static_cast<size_t>(i)];
        const quint32 sz =
            static_cast<quint32>(m_markoffDocument->blockText(id).size());
        // A byte strictly within this block, OR at the end of the LAST block.
        if (noSepByte < cursor + sz
                || (i == static_cast<int>(blocks.size()) - 1)) {
            return sepViewPosOf(id, static_cast<int>(noSepByte - cursor));
        }
        // noSepByte == cursor + sz means the byte is at the boundary between
        // this block and the next — i.e. the START of the next block (no-sep
        // space has no gap between blocks; the "\n\n" separator exists only in
        // sep-view). Fall through to the next block.
        cursor += sz;
    }
    return 0;  // empty document
}

void SourceTextDocumentBinding::emitCaret(int start, int active)
{
    Q_EMIT caretResolved(start, active);
}

std::optional<SourceTextDocumentBinding::PendingCaret>
SourceTextDocumentBinding::deleteSepRange(quint32 sepLo, quint32 sepHi)
{
    Markoff::MarkoffDocument *doc = m_markoffDocument;
    const auto hitStart = Markoff::Detail::findBlockAtSepByte(doc, sepLo, /*biasForward=*/false);
    const auto hitEnd   = Markoff::Detail::findBlockAtSepByte(doc, sepHi, /*biasForward=*/true);
    if (!hitStart || !hitEnd) return std::nullopt;

    if (hitStart->blockId == hitEnd->blockId) {
        // Within one block: plain delete.
        UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock,
                               hitEnd->byteInBlock - hitStart->byteInBlock,
                               QByteArray(), t);
        return PendingCaret{ hitStart->blockId, static_cast<int>(hitStart->byteInBlock) };
    }

    const auto allBlocks = doc->iterateBlocks();
    const QByteArray endTail = doc->blockText(hitEnd->blockId)
                                   .mid(static_cast<int>(hitEnd->byteInBlock));
    UndoLog::Transaction t(doc->d2UndoLog());
    const uint32_t startBlockSize =
        static_cast<uint32_t>(doc->blockText(hitStart->blockId).size());
    const uint32_t trimLen = startBlockSize - hitStart->byteInBlock;
    if (trimLen > 0)
        doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock, trimLen, QByteArray(), t);
    for (int i = hitStart->blockIndex + 1; i < hitEnd->blockIndex; ++i)
        doc->d2RemoveBlock(allBlocks[size_t(i)], t);
    doc->d2RemoveBlock(hitEnd->blockId, t);
    doc->d2ApplyBufferEdit(hitStart->blockId, hitStart->byteInBlock, 0, endTail, t);
    return PendingCaret{ hitStart->blockId, static_cast<int>(hitStart->byteInBlock) };
}

bool SourceTextDocumentBinding::handleStructuralKey(int key, int modifiers,
                                                    int qtPos, int qtAnchor)
{
    if (!m_markoffDocument || !m_textDocument) return false;
    Markoff::MarkoffDocument *doc = m_markoffDocument;

    const QByteArray preBytes = doc->widgetFlatView();
    const QString    preText  = QString::fromUtf8(preBytes);

    // ── Non-empty selection: collapse then apply ─────────────────────────
    // Delete the selected range through the model (reusing the cross-block
    // delete primitive), collapse the caret to the join point, then dispatch
    // the structural op at that point. This avoids ever letting Qt's native
    // editing restructure a QTextList across the selection.
    if (qtPos != qtAnchor) {
        const int lo = std::min(qtPos, qtAnchor);
        const int hi = std::max(qtPos, qtAnchor);
        const quint32 sepLo = qtPosToByteOffset(preText, lo);
        const quint32 sepHi = qtPosToByteOffset(preText, hi);

        m_applyingLocalEdit = true;
        std::optional<PendingCaret> collapse = deleteSepRange(sepLo, sepHi);
        m_applyingLocalEdit = false;
        if (!collapse) return false;

        m_applyingLocalEdit = true;
        Markoff::StructuralResult r = Markoff::StructuralKeyHandler::handle(
            *doc, collapse->block, key, modifiers,
            static_cast<uint32_t>(collapse->offsetInBlock));
        m_applyingLocalEdit = false;

        if (!r.handled) {
            // Selection was still deleted; land the caret at the collapse point.
            m_pendingCaret = collapse;
            return true;
        }
        m_pendingCaret = PendingCaret{ r.caretBlock, static_cast<int>(r.caretByteInBlock) };
        return true;
    }

    // ── Empty selection ──────────────────────────────────────────────────
    const quint32 sepPos = qtPosToByteOffset(preText, qtPos);
    const auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepPos, /*biasForward=*/false);
    if (!hit) return false;

    m_applyingLocalEdit = true;
    Markoff::StructuralResult r =
        Markoff::StructuralKeyHandler::handle(*doc, hit->blockId, key, modifiers,
                                              hit->byteInBlock);
    m_applyingLocalEdit = false;

    if (!r.handled) return false;

    // The handler returns the correct within-block caret byte for every key
    // (including Tab, which preserves the caret offset), so no per-key special
    // casing is needed here.
    m_pendingCaret = PendingCaret{ r.caretBlock, static_cast<int>(r.caretByteInBlock) };
    return true;
}

void SourceTextDocumentBinding::onQtContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    // Cycle guard: when T13's reverse path is mid-application, don't loop back.
    if (m_applyingRemoteEdit) return;
    if (!m_markoffDocument || !m_textDocument) return;
    Markoff::MarkoffDocument *doc = m_markoffDocument;

    // The QTextDocument mirrors widgetFlatView() (single '\n' between blocks),
    // so its plain text IS the sep-view. Compute sep-view byte offsets against
    // PRE-change state. NOTE: qtPos/charsRemoved are in the PRE-change document;
    // read the pre-change text from widgetFlatView() (the doc hasn't been mutated
    // yet on the forward path), and the inserted text from the POST-change QTextDocument.
    const QByteArray preBytesSep = doc->widgetFlatView();
    const QString    preTextSep  = QString::fromUtf8(preBytesSep);
    const quint32 sepStart = qtPosToByteOffset(preTextSep, qtPos);
    const quint32 sepEnd   = qtPosToByteOffset(preTextSep, qtPos + charsRemoved);

    const QString postPlain       = m_textDocument->toPlainText();
    const QByteArray insertedUtf8 = postPlain.mid(qtPos, charsAdded).toUtf8();
    const bool insertedHasNewline = insertedUtf8.contains('\n');

    m_applyingLocalEdit = true;

    // ── Pure single Enter: interactive newline split (WYSIWYG paragraph) ──
    // A bare Enter (no selection, exactly one "\n" inserted) creates a real
    // paragraph — possibly a transient empty one — via the interactive
    // ingress, and declares the caret target. Everything else (paste,
    // multi-newline, selection+Enter) keeps its existing routing below.
    if (charsRemoved == 0 && insertedUtf8 == QByteArrayLiteral("\n")) {
        const quint32 noSep =
            sepViewToNoSepByteForEdit(doc, sepStart, /*biasForward=*/false);
        const Markoff::BlockId newBlk =
            doc->applyInteractiveNewline(noSep, Markoff::Origin::UserEdit);
        m_pendingCaret = PendingCaret{ newBlk, 0 };
        m_applyingLocalEdit = false;
        return;
    }

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
        m_pendingCaret = deleteSepRange(sepStart, sepEnd);
        // Insert the typed replacement text (if any) at the merge point, in a
        // second transaction. Skipped when the range didn't resolve (nullopt).
        if (!insertedUtf8.isEmpty() && m_pendingCaret) {
            UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(m_pendingCaret->block,
                                   static_cast<uint32_t>(m_pendingCaret->offsetInBlock),
                                   0, insertedUtf8, t);
        }
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

    const QString expected = QString::fromUtf8(m_subscribedDoc->widgetFlatView());
    const QString actual   = m_textDocument->toPlainText();

    if (actual != expected) {
        // ── Incremental diff: longest common prefix + suffix ─────────────────────
        // Replace only the minimal contiguous changed span via QTextCursor so that
        // character formatting outside the changed region is preserved. This also
        // means the view's cursor doesn't jump to the end on a remote edit.

        // Longest common prefix.
        int p = 0;
        const int minLen = std::min(actual.size(), expected.size());
        while (p < minLen && actual.at(p) == expected.at(p)) ++p;
        // Don't split a surrogate pair at the prefix boundary.
        if (p > 0 && p < actual.size() && actual.at(p - 1).isHighSurrogate()) --p;

        // Longest common suffix, not overlapping the prefix.
        int s = 0;
        const int maxS = minLen - p;
        while (s < maxS
               && actual.at(actual.size() - 1 - s) == expected.at(expected.size() - 1 - s))
            ++s;
        // Don't split a surrogate pair at the suffix boundary.
        if (s > 0 && actual.at(actual.size() - s).isLowSurrogate()) --s;

        const int removeFrom = p;
        const int removeTo   = actual.size() - s;   // exclusive
        const QString middle = expected.mid(p, expected.size() - s - p);

        m_applyingRemoteEdit = true;
        QTextCursor c(m_textDocument);
        c.setPosition(removeFrom);
        c.setPosition(removeTo, QTextCursor::KeepAnchor);
        c.insertText(middle);
        m_applyingRemoteEdit = false;
    }

    // ── Re-assert the caret declared by a structural op ─────────────────────
    // The QTextDocument is now settled. No singleShot: d2DocumentChanged is
    // already debounced past the synchronous keystroke (INVARIANTS §6).
    if (m_pendingCaret) {
        const int pos = sepViewPosOf(m_pendingCaret->block,
                                     m_pendingCaret->offsetInBlock);
        emitCaret(pos, pos);
        m_pendingCaret.reset();
    }
}

}  // namespace Markoff
