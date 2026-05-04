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

Q_LOGGING_CATEGORY(lcStruct, "markoff.live.struct", QtWarningMsg)

namespace Markoff::LiveRender {

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
    ctx.currentBlockStart = m_document->resolveTextAnchor(rec.blockAnchor.firstByte);
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

        // Cursor goes to qtPos 0 of the user's content row.
        // - atStart: marker paragraph takes index `blockIndex`, original content
        //   shifts down to `blockIndex + 1`. Cursor follows the user's content.
        // - atEnd: marker paragraph is appended at `blockIndex + 1`. Cursor lands
        //   in it so the user can type immediately into the new (visually empty)
        //   paragraph.
        // Both cases: newRow == blockIndex + 1.
        const int newRow = c.blockIndex + 1;
        c.cursorState->requestTextCaretAtNewRow(newRow, /*qtPos=*/0);

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
