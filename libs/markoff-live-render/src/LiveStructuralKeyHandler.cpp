// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveStructuralKeyHandler.h>

#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveProxyBlockModel.h>

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
    LiveHoleLayer            *holeLayer,
    LiveProxyBlockModel      *proxyModel,
    QObject                  *parent)
    : QObject(parent)
    , m_document(document)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_registry(registry)
    , m_undoCoalescer(undoCoalescer)
    , m_holeLayer(holeLayer)
    , m_proxyModel(proxyModel)
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
                      << "proxyBlockIndex=" << blockIndex
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

    // Front-load hole-row dispatch before any inner-model lookup.
    // blockIndex here is a PROXY row index (QML binds to proxyModel since
    // Task 10). If the proxy row is a hole, delegate to handleHoleRowEnter.
    if (m_proxyModel && m_proxyModel->proxyRowIsHole(blockIndex)) {
        const quint64 holeId = m_proxyModel->holeAtProxyRow(blockIndex);
        return handleHoleRow(holeId, key, modifiers, qtPos) == HandleResult::Handled;
    }

    // Translate proxy row → inner row for anchor-side handlers.
    // At zero holes, innerRowForProxy(N) == N, so behavior is unchanged.
    // With holes present the proxy and inner indices diverge; we need the
    // inner row to look up BlockRecord and feed requestTextCaretAtRow.
    // NOTE: existing anchor-side handlers' requestTextCaretAtRow calls take
    // inner-row indices — coherent at zero holes; row-space rework deferred
    // to Task 14+ once holes coexist with inner-row edits.
    const int innerRow = m_proxyModel
                           ? m_proxyModel->innerRowForProxy(blockIndex)
                           : blockIndex;
    if (innerRow < 0 || innerRow >= m_model->rowCount()) return false;

    const BlockRecord &rec = m_model->recordAt(innerRow);
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
    ctx.holeLayer         = m_holeLayer;
    ctx.proxyModel        = m_proxyModel;
    // blockIndex (inner) drives byte arithmetic against the inner model;
    // proxyBlockIndex drives cursor delivery via LiveCursorState (which now
    // listens on the proxy). At zero holes the two are equal.
    ctx.blockIndex        = innerRow;
    ctx.proxyBlockIndex   = blockIndex;
    ctx.blockAnchor       = rec.blockAnchor;
    ctx.currentBlockStart = m_document->resolveTextAnchor(rec.blockAnchor.firstByte);
    ctx.currentBlockEnd   = ctx.currentBlockStart
                          + static_cast<quint32>(blockText.toUtf8().size());
    ctx.qtPos             = qtPos;
    ctx.modifiers         = modifiers;
    ctx.blockText         = blockText;

    return keyIt.value()(ctx) == HandleResult::Handled;
}

LiveStructuralKeyHandler::HandleResult
LiveStructuralKeyHandler::handleHoleRow(quint64 holeId, int key,
                                        int modifiers, int qtPos)
{
    if (!m_holeLayer || !m_proxyModel) return HandleResult::NotHandled;
    if (!m_holeLayer->exists(holeId)) return HandleResult::NotHandled;

    const QString buf = m_holeLayer->bufferText(holeId);

    // ---- Enter (Task 12 logic, unchanged) ----
    if ((key == Qt::Key_Return || key == Qt::Key_Enter)
        && (modifiers & Qt::ShiftModifier) == 0) {

        // Empty-buffer Enter — no-op per stacked-Enter rule (spec §3.3).
        // Consume the key so QML TextEdit doesn't insert a literal newline
        // into the buffer, but make no source or layer change.
        if (buf.isEmpty()) return HandleResult::Handled;

        // Resolve the reify byte BEFORE commit; after commit the holeId is gone.
        const quint32 reifyByte =
            m_document->resolveTextAnchor(m_holeLayer->reifyAnchor(holeId));

        if (qtPos >= buf.length()) {
            // End-of-buffer: commit the whole buffer, then open a fresh hole
            // positioned right after the just-committed paragraph.
            m_holeLayer->commitBlockHole(holeId);
            // After commit, source contains "\n\n" + buf inserted at reifyByte.
            // The end of the just-committed paragraph is therefore at:
            const quint32 newReifyByte = reifyByte
                + 2  // for "\n\n"
                + static_cast<quint32>(buf.toUtf8().size());
            Markoff::TextAnchor newAnchor =
                m_document->textAnchorAt(newReifyByte, /*rightBias=*/false);
            const quint64 newId = m_holeLayer->createBlockHole(
                HoleKind::Paragraph, newAnchor);

            TextCaret tc;
            tc.block            = BlockId{HoleBlockId{newId}};
            tc.cachedByteOffset = 0;
            m_cursorState->request(Cursor{tc});

            if (m_undoCoalescer) m_undoCoalescer->recordStructural();
            return HandleResult::Handled;
        }

        // Mid-buffer split: set prefix, commit, create new hole with suffix.
        const QString prefix = buf.left(qtPos);
        const QString suffix = buf.mid(qtPos);

        m_holeLayer->setBlockHoleBuffer(holeId, prefix);
        m_holeLayer->commitBlockHole(holeId);

        const quint32 newReifyByte = reifyByte
            + 2  // for "\n\n"
            + static_cast<quint32>(prefix.toUtf8().size());
        Markoff::TextAnchor newAnchor =
            m_document->textAnchorAt(newReifyByte, /*rightBias=*/false);
        const quint64 newId = m_holeLayer->createBlockHole(
            HoleKind::Paragraph, newAnchor);
        m_holeLayer->setBlockHoleBuffer(newId, suffix);

        TextCaret tc;
        tc.block            = BlockId{HoleBlockId{newId}};
        tc.cachedByteOffset = 0;  // cursor at start of suffix
        m_cursorState->request(Cursor{tc});

        if (m_undoCoalescer) m_undoCoalescer->recordStructural();
        return HandleResult::Handled;
    }

    // ---- Esc: abandon, focus previous neighbor ----
    if (key == Qt::Key_Escape) {
        const int holeProxyRow = m_proxyModel->proxyRowForHole(holeId);
        m_holeLayer->abandonBlockHole(holeId);
        routeFocusAfterAbandon(holeProxyRow, /*preferNext=*/false);
        return HandleResult::Handled;
    }

    // ---- Backspace at qtPos 0 ----
    if (key == Qt::Key_Backspace && qtPos == 0) {
        if (buf.isEmpty()) {
            const int holeProxyRow = m_proxyModel->proxyRowForHole(holeId);
            m_holeLayer->abandonBlockHole(holeId);
            routeFocusAfterAbandon(holeProxyRow, /*preferNext=*/false);
            return HandleResult::Handled;
        }
        // Non-empty buffer at qtPos 0: passthrough (no char to delete).
        return HandleResult::NotHandled;
    }

    // ---- Delete at end of empty buffer ----
    if (key == Qt::Key_Delete
        && buf.isEmpty()
        && qtPos == 0) {
        const int holeProxyRow = m_proxyModel->proxyRowForHole(holeId);
        m_holeLayer->abandonBlockHole(holeId);
        routeFocusAfterAbandon(holeProxyRow, /*preferNext=*/true);
        return HandleResult::Handled;
    }

    return HandleResult::NotHandled;
}

void LiveStructuralKeyHandler::routeFocusAfterAbandon(int holeProxyRow,
                                                      bool preferNext)
{
    // The hole has been abandoned; proxy row count has decreased by 1.
    // The hole's old position is now occupied by what was after it.
    //
    // preferNext == false (Esc/Backspace): target the row just before the
    //   hole's old position → holeProxyRow - 1 (end-of-row qtPos).
    // preferNext == true (Delete): target the row that shifted up into
    //   the hole's old slot → holeProxyRow (qtPos 0).

    const int proxyCount = m_proxyModel->rowCount();

    auto findInnerRow = [&](int proxyRow, int direction) -> int {
        while (proxyRow >= 0 && proxyRow < proxyCount) {
            if (!m_proxyModel->proxyRowIsHole(proxyRow)) return proxyRow;
            proxyRow += direction;
        }
        return -1;
    };

    int targetProxy = -1;
    int qtPos       = 0;

    if (preferNext) {
        // Try the row that stepped into the hole's old slot.
        targetProxy = findInnerRow(holeProxyRow, +1);
        qtPos = 0;
        if (targetProxy < 0) {
            // Fallback: previous neighbor, end-of-row.
            targetProxy = findInnerRow(holeProxyRow - 1, -1);
            if (targetProxy >= 0) {
                const int innerRow = m_proxyModel->innerRowForProxy(targetProxy);
                if (innerRow >= 0 && innerRow < m_model->rowCount())
                    qtPos = m_model->recordAt(innerRow).text.length();
            }
        }
    } else {
        // Try the row just before the hole's old position.
        targetProxy = findInnerRow(holeProxyRow - 1, -1);
        if (targetProxy >= 0) {
            const int innerRow = m_proxyModel->innerRowForProxy(targetProxy);
            if (innerRow >= 0 && innerRow < m_model->rowCount())
                qtPos = m_model->recordAt(innerRow).text.length();
        } else {
            // Fallback: next neighbor.
            targetProxy = findInnerRow(holeProxyRow, +1);
            qtPos = 0;
        }
    }

    if (targetProxy < 0) {
        // F4 single-hole-doc edge case: no inner rows at all.
        m_cursorState->clear();
        return;
    }

    const int innerRow = m_proxyModel->innerRowForProxy(targetProxy);
    if (innerRow < 0) { m_cursorState->clear(); return; }
    // requestTextCaretAtRow indexes the proxy now; pass targetProxy directly.
    m_cursorState->requestTextCaretAtRow(targetProxy, qtPos);
}

void LiveStructuralKeyHandler::registerBuiltins()
{
    using HR = HandleResult;

    // ---------- paragraph: Enter (all positions, Shift-aware) ----------
    auto paragraphEnter = [](const Ctx &c) -> HR {
        const bool isShift = (c.modifiers & Qt::ShiftModifier) != 0;

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
            c.cursorState->requestTextCaretAtRow(c.proxyBlockIndex, c.qtPos + 1);
            if (c.undoCoalescer) c.undoCoalescer->recordStructural();
            return HR::Handled;
        }

        const bool atStart = (c.qtPos == 0);
        const bool atEnd   = (c.qtPos == c.blockText.length());

        if (!atStart && !atEnd) {
            // Mid-block split — applyLocalEdit("\n\n") creates a NEW row at
            // proxyBlockIndex+1. Use requestTextCaretAtNewRow (pure-pending)
            // — requestTextCaretAtRow would resolve against whatever block
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
            c.cursorState->requestTextCaretAtNewRow(c.proxyBlockIndex + 1, 0);
            if (c.undoCoalescer) c.undoCoalescer->recordStructural();
            return HR::Handled;
        }

        // EOB or start-of-block: create hole instead of source edit (R5.5 F5).
        if (!c.holeLayer || !c.proxyModel) return HR::NotHandled;

        const quint32 reifyByte = atStart ? c.currentBlockStart : c.currentBlockEnd;
        qInfo().noquote() << "[dogfood] StructHandler: paragraphEnter EOB-or-start"
                          << "(atStart=" << atStart << "atEnd=" << atEnd
                          << "innerRow=" << c.blockIndex
                          << "proxyRow=" << c.proxyBlockIndex
                          << "reifyByte=" << reifyByte << ")";
        Markoff::TextAnchor anchor = c.document->textAnchorAt(reifyByte, /*rightBias=*/false);
        const quint64 holeId = c.holeLayer->createBlockHole(HoleKind::Paragraph, anchor);

        // Place caret in the hole row at byte offset 0.
        TextCaret tc;
        tc.block             = BlockId{HoleBlockId{holeId}};
        tc.cachedByteOffset  = 0;
        c.cursorState->request(Cursor{tc});

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
        c.cursorState->requestTextCaretAtRow(c.proxyBlockIndex - 1, prevQtPos);

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
        c.cursorState->requestTextCaretAtRow(c.proxyBlockIndex, c.qtPos);

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
