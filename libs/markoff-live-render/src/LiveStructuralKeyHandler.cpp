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

    // ---------- paragraph: Enter at end of block ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        if (c.qtPos != c.blockText.length()) {
            // Not end-of-block; mid-block / start-of-block handled in a later
            // task. For now, NotHandled falls through to TextEdit's native \n.
            return HR::NotHandled;
        }
        Markoff::MarkoffEdit ed;
        ed.oldStart = c.currentBlockEnd;
        ed.oldEnd   = c.currentBlockEnd;
        ed.newText  = QByteArrayLiteral("\n\n");
        c.document->applyLocalEdit({ ed });
        // Stamp the row's lastEditEditSequence so the freshness rule
        // preserves the row's text on parse-back (spec §4.3).
        c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
        // Caret lands at qtPos 0 of the new (currently non-existent) row.
        c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);
        // Structural edit: break the printable-coalesce chain.
        if (c.undoCoalescer) c.undoCoalescer->recordStructural();
        return HR::Handled;
    };
    m_handlers[BlockKind::Paragraph][Qt::Key_Return] = paragraphEnter;
    m_handlers[BlockKind::Paragraph][Qt::Key_Enter]  = paragraphEnter;
}

}  // namespace Markoff::LiveRender
