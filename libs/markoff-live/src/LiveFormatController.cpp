// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveFormatController.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/Coordinates.h>

#include <algorithm>
#include <climits>

namespace Markoff::Live {

namespace coords = Detail::Coordinates;

LiveFormatController::LiveFormatController(QObject *parent) : QObject(parent) {}

void LiveFormatController::setDocument(Markoff::MarkoffDocument *doc) { m_document = doc; }
void LiveFormatController::setSelectionView(LiveCursorState *sv)      { m_selection = sv; }
void LiveFormatController::setModel(const LiveBlockModel *m)          { m_model = m; }

void LiveFormatController::wrapPerBlock(const QByteArray &openDelim, const QByteArray &closeDelim)
{
    if (!m_document || !m_model || !m_selection) return;
    if (!m_selection->hasSelection()) return;

    const int rowCount = m_model->rowCount();
    const auto allIds  = m_document->iterateBlocks();

    // Collect selected block indices (to process in reverse so later edits
    // don't shift the byte offsets of earlier blocks).
    QList<int> selectedRows;
    for (int i = 0; i < rowCount && i < static_cast<int>(allIds.size()); ++i) {
        const QPoint r = m_selection->rangeForBlock(i);
        if (r.x() >= 0)
            selectedRows.append(i);
    }
    if (selectedRows.isEmpty()) return;

    // All per-block edits land in one UndoLog transaction so a single
    // undoD2() reverses the whole format toggle.
    Markoff::UndoLog::Transaction t(m_document->d2UndoLog());

    // Process in reverse: highest row first, so edits to later blocks don't
    // shift byte offsets of earlier blocks.
    for (int n = selectedRows.size() - 1; n >= 0; --n) {
        const int i = selectedRows[n];
        const QPoint r = m_selection->rangeForBlock(i);

        // Model text (no trailing '\n') used for qtPos→byte conversion, matching
        // the convention in deleteSelection.
        const QByteArray modelUtf8 = m_model->recordAt(i).text.toUtf8();
        // blockText raw bytes (includes trailing '\n') for wrap detection.
        const QByteArray rawBlock  = m_document->blockText(allIds[i]);

        const int qLo = r.x();
        // INT_MAX means "to end of block text"; clamp to model text length.
        const int qHi = (r.y() == INT_MAX) ? m_model->recordAt(i).text.length() : r.y();
        if (qHi <= qLo) continue;

        // Byte offsets within model text (excludes trailing '\n').
        // These are directly usable with d2ApplyBufferEdit (per-block offsets).
        const quint32 bLo = static_cast<quint32>(coords::qtPosToByte(modelUtf8, qLo));
        const quint32 bHi = static_cast<quint32>(coords::qtPosToByte(modelUtf8, qHi));
        if (bHi <= bLo) continue;

        // Wrap detection: check if bytes just OUTSIDE [bLo, bHi) in rawBlock
        // equal the delimiters. rawBlock includes the trailing '\n' so the
        // block size is rawBlock.size(), but we only check within the content.
        const int openLen  = openDelim.size();
        const int closeLen = closeDelim.size();
        const int preOff   = static_cast<int>(bLo) - openLen;  // in rawBlock
        const int postOff  = static_cast<int>(bHi);             // in rawBlock

        bool alreadyWrapped = false;
        if (preOff >= 0 && postOff + closeLen <= static_cast<int>(rawBlock.size())) {
            alreadyWrapped = (rawBlock.mid(preOff, openLen)  == openDelim &&
                              rawBlock.mid(postOff, closeLen) == closeDelim);
        }

        if (alreadyWrapped) {
            // Remove close delimiter first (higher offset within block), then open.
            m_document->d2ApplyBufferEdit(allIds[i],
                                          bHi,
                                          static_cast<quint32>(closeLen),
                                          QByteArray(), t);
            m_document->d2ApplyBufferEdit(allIds[i],
                                          bLo - static_cast<quint32>(openLen),
                                          static_cast<quint32>(openLen),
                                          QByteArray(), t);
        } else {
            // Insert close first (higher offset within block), then open (lower).
            m_document->d2ApplyBufferEdit(allIds[i], bHi, 0, closeDelim, t);
            m_document->d2ApplyBufferEdit(allIds[i], bLo, 0, openDelim,  t);
        }
    }
}

void LiveFormatController::toggleBold()          { wrapPerBlock("**", "**"); }
void LiveFormatController::toggleItalic()        { wrapPerBlock("_",  "_");  }
void LiveFormatController::toggleStrikethrough() { wrapPerBlock("~~", "~~"); }
void LiveFormatController::toggleInlineCode()    { wrapPerBlock("`",  "`");  }

void LiveFormatController::insertLink()
{
    if (!m_document || !m_model || !m_selection) return;

    const auto allIds = m_document->iterateBlocks();
    const int rowCount = m_model->rowCount();

    const auto byteInBlock = [&](int row, int qtPos) -> quint32 {
        if (row < 0 || row >= rowCount) return 0;
        const QByteArray modelUtf8 = m_model->recordAt(row).text.toUtf8();
        return static_cast<quint32>(coords::qtPosToByte(modelUtf8, qtPos));
    };

    Markoff::UndoLog::Transaction t(m_document->d2UndoLog());

    if (!m_selection->hasSelection()) {
        // Caret-only: insert "[](url)" at caret position within the block.
        const int row   = m_selection->anchorBlock();
        const int qtPos = m_selection->anchorQtPos();
        if (row < 0 || row >= rowCount) return;
        const quint32 byte = byteInBlock(row, qtPos);
        m_document->d2ApplyBufferEdit(allIds[row], byte, 0, "[](url)", t);
        return;
    }

    // Single-block nonempty selection: wrap selected text as [text](url).
    if (m_selection->anchorBlock() == m_selection->activeBlock()) {
        const int row = m_selection->anchorBlock();
        if (row < 0 || row >= rowCount) return;

        int qLo = m_selection->anchorQtPos();
        int qHi = m_selection->activeQtPos();
        if (qLo > qHi) std::swap(qLo, qHi);

        const quint32 lo = byteInBlock(row, qLo);
        const quint32 hi = byteInBlock(row, qHi);
        if (hi <= lo) return;

        // Insert closing bracket+url first (higher offset within block), then open.
        m_document->d2ApplyBufferEdit(allIds[row], hi, 0, "](url)", t);
        m_document->d2ApplyBufferEdit(allIds[row], lo, 0, "[",      t);
    }
    // Multi-block link: not specified in E2.5 — skip.
}

}  // namespace Markoff::Live
