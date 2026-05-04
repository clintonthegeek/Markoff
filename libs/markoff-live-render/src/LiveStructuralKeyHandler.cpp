// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/Marker.h>
#include <markoff/live-render/MarkerScrubber.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcStruct, "markoff.live.struct", QtWarningMsg)

namespace Markoff::LiveRender {

namespace {

/// Marker design (spec §0) is bounded to "paragraph holes only" — list items,
/// blockquotes, and similar block-likes are out of scope. R2's BlockWalker
/// collapses lists/blockquotes/HTML/etc. into a single row whose `kind` is
/// `BlockKind::Paragraph` but whose `text` carries the source-faithful
/// markdown including the list/quote markers. Without a guard, paragraphEnter
/// fires its marker-insertion / mid-block-split logic on that row and
/// destructively rewrites the source (e.g. injects `\n\n` between two list
/// items, splitting the list into two with an empty paragraph in between —
/// the dogfood Bug 1 symptom).
///
/// Strategy B (text-pattern heuristic): treat a row as non-paragraph if its
/// text starts with a markdown list marker (`-`, `*`, `+`, `1.`, `1)`) or a
/// blockquote marker (`>`). Returning `HR::NotHandled` from paragraphEnter in
/// that case lets QTextEdit's default Enter handling apply — a literal `\n`
/// soft-break inside the row, which the parser keeps as a soft line break
/// inside the existing list/quote. Non-destructive; the user sees something
/// happen rather than silent swallowing.
///
/// This is a heuristic, not a parser query. False-positive risk: a true
/// paragraph whose first line happens to start with `- ` (e.g. a literal
/// dash-space at column 0) would be mis-gated. CommonMark would also parse
/// such a paragraph as a list, so the heuristic agrees with the parser's
/// classification — there is no observable false-positive in standard
/// markdown. False-negative risk: indented list items (≥4 leading spaces
/// without a list parent) — not a concern at top level.
///
/// Future work (spec §17): a parser-side "is this row a list/quote
/// container?" query would let us swap this for Strategy D and add proper
/// list-item Enter UX (split items, exit on empty item).
bool rowIsListOrQuoteContent(const QString &blockText)
{
    static const QRegularExpression kListOrQuotePrefix(
        QStringLiteral(R"(^[ \t]{0,3}(?:[-*+]|\d{1,9}[.)])\s)"));
    static const QRegularExpression kBlockQuotePrefix(
        QStringLiteral(R"(^[ \t]{0,3}>)"));
    return kListOrQuotePrefix.match(blockText).hasMatch()
        || kBlockQuotePrefix.match(blockText).hasMatch();
}

}  // namespace

LiveStructuralKeyHandler::LiveStructuralKeyHandler(
    Markoff::MarkoffDocument *document,
    LiveBlockModel           *model,
    LiveCursorState          *cursorState,
    const BlockKindRegistry  *registry,
    UndoCoalescer            *undoCoalescer,
    QObject                  *parent)
    : QObject(parent)
    , m_document(document)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_registry(registry)
    , m_undoCoalescer(undoCoalescer)
{
    registerBuiltins();
}

void LiveStructuralKeyHandler::registerHandler(const QString &kind, int key, HandlerFn fn)
{
    m_handlers[kind][key] = std::move(fn);
}

bool LiveStructuralKeyHandler::tryHandle(int key,
                                         int modifiers,
                                         int blockIndex,
                                         int qtPos,
                                         bool selectionEmpty,
                                         const QString &blockText)
{
    qInfo().noquote() << "[dogfood] StructHandler: tryHandle key=" << key
                      << "mod=" << modifiers
                      << "blockIndex=" << blockIndex
                      << "qtPos=" << qtPos
                      << "blockTextLen=" << blockText.length()
                      << "selEmpty=" << selectionEmpty;
    if (!m_document || !m_model || !m_cursorState || !m_registry) return false;
    if (!selectionEmpty) {
        // R5 limitation: non-empty selection defers to TextEdit's native
        // selection-replacement (which routes through LiveEditBinding's
        // contentsChange path). Documented in plan scope notes.
        return false;
    }

    if (blockIndex < 0 || blockIndex >= m_model->rowCount()) return false;

    const BlockRecord &rec = m_model->recordAt(blockIndex);
    const auto *desc = m_registry->find(rec.kind);
    if (!desc) return false;
    if (!desc->consumedStructuralKeys.contains(key)) return false;

    auto kindIt = m_handlers.constFind(rec.kind);
    if (kindIt == m_handlers.constEnd()) return false;
    auto keyIt = kindIt.value().constFind(key);
    if (keyIt == kindIt.value().constEnd()) return false;

    // Sanity: block must resolve in the foundation.
    const auto blockRangeOpt = m_document->blockByteRange(rec.blockAnchor);
    if (!blockRangeOpt) return false;

    Ctx ctx;
    ctx.document          = m_document.data();
    ctx.model             = m_model;
    ctx.cursorState       = m_cursorState;
    ctx.undoCoalescer     = m_undoCoalescer;
    ctx.blockIndex        = blockIndex;
    ctx.blockAnchor       = rec.blockAnchor;
    ctx.currentBlockStart = blockRangeOpt->first;
    ctx.currentBlockEnd   = ctx.currentBlockStart
                          + static_cast<quint32>(blockText.toUtf8().size());
    ctx.qtPos             = qtPos;
    ctx.modifiers         = modifiers;
    ctx.blockText         = blockText;

    return keyIt.value()(ctx) == HandleResult::Handled;
}

void LiveStructuralKeyHandler::registerBuiltins()
{
    using HR = HandleResult;

    // ---------- paragraph: Enter (all positions, Shift-aware) ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        const bool isShift = (c.modifiers & Qt::ShiftModifier) != 0;

        // Bug 1 (Task 18 dogfood) gate. Spec §0: marker design is bounded
        // to top-level paragraphs. R2's BlockWalker collapses lists and
        // blockquotes into a single Paragraph-kinded row; without this
        // guard, mid-block Enter splits the row with `\n\n`, splitting
        // the list into two and scattering cursor delivery. Returning
        // NotHandled lets QTextEdit's default Enter (a literal `\n`)
        // apply — non-destructive soft-break inside the list/quote.
        // Shift-Enter is also gated: it inserts a `\n` too, which we'd
        // otherwise duplicate; default Qt Shift-Enter does the same.

        if (rowIsListOrQuoteContent(c.blockText)) {
            return HR::NotHandled;
        }

        // Marker design §4.5: plain (unmodified) Enter on a marker-only
        // block is a no-op. CommonMark collapses consecutive blank lines
        // anyway; visual "gap" can't survive a save/load cycle.
        // Spec §4.4: Shift-Enter on a marker block is NOT covered by this
        // rule — it still inserts a soft-break newline (handled below).
        if (!isShift && Markoff::LiveRender::MarkerScrubber::isMarkerOnly(c.blockText)) {
            return HR::Handled;
        }

        if (isShift) {
            // Soft break — insert \n, stay in block.
            const QByteArray prefixUtf8 = c.blockText.left(c.qtPos).toUtf8();
            const quint32 byteOffset =
                c.currentBlockStart + static_cast<quint32>(prefixUtf8.size());
            Markoff::MarkoffEdit ed;
            ed.oldStart = byteOffset;
            ed.oldEnd   = byteOffset;
            ed.newText  = QByteArrayLiteral("\n");
            c.document->applyLocalEdit({ ed });
            c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
            c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos + 1);
            if (c.undoCoalescer) c.undoCoalescer->recordStructural();
            return HR::Handled;
        }

        const bool atStart = (c.qtPos == 0);
        const bool atEnd   = (c.qtPos == c.blockText.length());

        if (!atStart && !atEnd) {
            // Mid-block split — applyLocalEdit("\n\n") creates a NEW row at
            // blockIndex+1. Use requestTextCaretAtNewRow (pure-pending) —
            // requestTextCaretAtRow would resolve against whatever block
            // currently sits at that index (the block about to be shifted
            // down by the insertion), landing the cursor on the wrong row.
            const QByteArray prefixUtf8 = c.blockText.left(c.qtPos).toUtf8();
            const quint32 byteOffset =
                c.currentBlockStart + static_cast<quint32>(prefixUtf8.size());
            Markoff::MarkoffEdit ed;
            ed.oldStart = byteOffset;
            ed.oldEnd   = byteOffset;
            ed.newText  = QByteArrayLiteral("\n\n");
            c.document->applyLocalEdit({ ed });
            c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
            c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
            if (c.undoCoalescer) c.undoCoalescer->recordStructural();
            return HR::Handled;
        }

        // EOB or start-of-block: insert marker paragraph and let the
        // existing parser-driven row pipeline deliver the new row.
        // Spec §4.1 / §4.2 (R5.5 marker-paragraph design).
        //
        // Byte layout depends on which side the new (empty marker) block
        // sits relative to the existing block:
        //   atEnd:   "<existing>\n\n<marker>"  → newText = "\n\n" + marker
        //   atStart: "<marker>\n\n<existing>"  → newText = marker + "\n\n"
        const quint32 byteOffset = atStart ? c.currentBlockStart
                                            : c.currentBlockEnd;
        Markoff::MarkoffEdit ed;
        ed.oldStart = byteOffset;
        ed.oldEnd   = byteOffset;
        ed.newText  = atStart
                        ? (QByteArray(kMarkerUtf8) + QByteArrayLiteral("\n\n"))
                        : (QByteArrayLiteral("\n\n") + QByteArray(kMarkerUtf8));
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());

        // Cursor delivery diverges by side:
        // - atEnd: the new marker block is born at `blockIndex + 1`. Use the
        //   row-keyed pure-pending request — the cursor MUST land on the new
        //   row (not the existing user content). requestTextCaretAtNewRow
        //   resolves on rowsInserted whose range covers blockIndex+1.
        // - atStart: the new marker block is born at `blockIndex`, and the
        //   existing user content shifts to byte
        //   `currentBlockStart + markerBytes`. Use the byte-keyed pending
        //   request — the cursor MUST follow the user's content, and the
        //   only stable identifier across the parse-back diff is the
        //   post-edit byte position of the user's content. Anchor identity
        //   was tried in commit 3c86b76 but mis-resolved in some mid-doc
        //   cases (dogfood pass 3 / Bug 3 v2): the resolver can match a
        //   row whose anchor coincidentally equals the captured anchor in
        //   a transient state, landing the cursor on the originally-
        //   following paragraph. Byte-keyed resolution sidesteps anchor-
        //   identity quirks entirely by asking the foundation directly:
        //   "which row's byte range now contains target byte X?".
        if (atStart) {
            const quint32 markerBytes = static_cast<quint32>(ed.newText.size());
            const quint32 userContentByte = c.currentBlockStart + markerBytes;
            c.cursorState->requestTextCaretAtByte(c.document, userContentByte, /*qtPos=*/0);
        } else {
            const int newRow = c.blockIndex + 1;
            c.cursorState->requestTextCaretAtNewRow(newRow, /*qtPos=*/0);
        }

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Return] = paragraphEnter;
    m_handlers[BlockKind::Paragraph][Qt::Key_Enter]  = paragraphEnter;

    auto paragraphBackspace = [](const Ctx &c) -> HR {
        if (c.qtPos != 0) return HR::NotHandled;     // not at row-start
        if (c.blockIndex == 0) return HR::NotHandled; // first block
        if (c.currentBlockStart == 0) return HR::NotHandled;

        // Marker design §8.3: if the previous block is a marker-only
        // paragraph, delete it (and its leading "\n\n" separator) instead
        // of running the regular paragraph-merge.
        const auto &prevRec = c.model->recordAt(c.blockIndex - 1);
        if (Markoff::LiveRender::MarkerScrubber::isMarkerOnly(prevRec.text)) {
            const auto prevRange = c.document->blockByteRange(prevRec.blockAnchor);
            if (prevRange) {
                quint32 start = prevRange->first;
                // blockByteRange already includes the block's trailing \n
                // (Task 3 discovery), so we only need to absorb 1 byte of
                // leading separator here.
                const quint32 absorb = (start >= 1) ? 1 : start;
                start -= absorb;
                Markoff::MarkoffEdit ed;
                ed.oldStart = start;
                ed.oldEnd   = prevRange->second;
                ed.newText  = QByteArray();
                c.document->applyLocalEdit({ ed });
                // Cursor lands at qtPos 0 of the user's row, which after the
                // marker block is removed sits at index blockIndex - 1.
                c.cursorState->requestTextCaretAtRow(c.blockIndex - 1, 0);
                if (c.undoCoalescer) c.undoCoalescer->recordStructural();
                return HR::Handled;
            }
        }

        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockStart - 1;
        ed.oldEnd   = c.currentBlockStart;
        ed.newText  = QByteArray();
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        c.model->setRowEditSequence(c.blockIndex - 1, c.document->editSequence());

        // After parse-back: blockIndex - 1 contains the merged content;
        // blockIndex is gone. Cursor lands at qtPos = previous block's
        // text length (the merge point). Compute the qtPos at edit time
        // since the previous block's text is still current in the model.
        const int prevQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
        c.cursorState->requestTextCaretAtRow(c.blockIndex - 1, prevQtPos);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Backspace] = paragraphBackspace;

    auto paragraphDelete = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) return HR::NotHandled;
        if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;
        const quint32 docLen = c.document->visibleLength();
        if (c.currentBlockEnd >= docLen) return HR::NotHandled;

        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockEnd;
        ed.oldEnd   = c.currentBlockEnd + 1;
        ed.newText  = QByteArray();
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        c.model->setRowEditSequence(c.blockIndex + 1, c.document->editSequence());

        // Cursor stays at end of the (now-merged) block — same row, same qtPos.
        // Use requestTextCaretAtRow so it survives the parse-back's row
        // reshuffle even if the block changes identity.
        c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Delete] = paragraphDelete;

    // Heading: same handlers as paragraph. Source-faithful text means the
    // # prefix is part of blockText, so qtPos arithmetic matches paragraph.
    m_handlers[BlockKind::Heading][Qt::Key_Return]    = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Enter]     = paragraphEnter;
    m_handlers[BlockKind::Heading][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::Heading][Qt::Key_Delete]    = paragraphDelete;

    // Code-block: only the merge handlers. Enter is NOT consumed —
    // TextEdit's native \n insertion routes through LiveEditBinding as
    // a regular text edit, producing the literal newline the user
    // expects inside fenced code.
    //
    // Limitation: blockText excludes the fences, so currentBlockEnd
    // underestimates the true block end. Delete-at-body-end therefore
    // deletes a fence byte rather than the inter-block separator. Not
    // exercised by the R5 dogfood script; full fix lands in R6 with
    // proper code-block byte arithmetic.
    m_handlers[BlockKind::CodeBlock][Qt::Key_Backspace] = paragraphBackspace;
    m_handlers[BlockKind::CodeBlock][Qt::Key_Delete]    = paragraphDelete;
}

}  // namespace Markoff::LiveRender
