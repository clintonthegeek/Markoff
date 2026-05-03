// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/UndoCoalescer.h>

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

    // ---------- paragraph: Enter (all positions) ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        // Compute the byte offset for the cursor's qtPos.
        const QByteArray prefixUtf8 =
            c.blockText.left(c.qtPos).toUtf8();
        const quint32 byteOffset =
            c.currentBlockStart + static_cast<quint32>(prefixUtf8.size());

        Markoff::MarkoffEdit ed;
        ed.oldStart = byteOffset;
        ed.oldEnd   = byteOffset;
        ed.newText  = QByteArrayLiteral("\n\n");
        c.document->applyLocalEdit({ ed });
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());

        // Cursor placement:
        //   qtPos == 0 (start-of-block):
        //     The empty leading paragraph the parser will see may or may
        //     not materialise as its own row (depends on Markdown
        //     normalisation). Park the cursor at the original block, now
        //     at row blockIndex + 1 in the post-parse layout, qtPos 0.
        //   0 < qtPos < length (mid-block):
        //     Original block keeps the prefix at row blockIndex; the
        //     suffix appears as a new row at blockIndex + 1. Caret to
        //     qtPos 0 of blockIndex + 1.
        //   qtPos == length (end-of-block):
        //     Empty new paragraph appears at blockIndex + 1. Caret to
        //     qtPos 0 of blockIndex + 1.
        //
        // All three cases land on row blockIndex + 1 qtPos 0.
        c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);

        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Return] = paragraphEnter;
    m_handlers[BlockKind::Paragraph][Qt::Key_Enter]  = paragraphEnter;

    auto paragraphBackspace = [](const Ctx &c) -> HR {
        if (c.qtPos != 0) return HR::NotHandled;     // not at row-start
        if (c.blockIndex == 0) return HR::NotHandled; // first block
        if (c.currentBlockStart == 0) return HR::NotHandled;

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
}

}  // namespace Markoff::LiveRender
