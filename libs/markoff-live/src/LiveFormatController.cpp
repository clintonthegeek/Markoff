// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveFormatController.h>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/live/BlockKind.h>
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

void LiveFormatController::setHeadingLevel(int level)
{
    if (!m_document || !m_model || !m_selection) return;
    if (level < 0 || level > 6) return;

    const auto allIds  = m_document->iterateBlocks();
    const int rowCount = m_model->rowCount();

    // Collect target rows: every block touched by the selection, or just the
    // focused block if no selection.
    QList<int> targetRows;
    if (m_selection->hasSelection()) {
        for (int i = 0; i < rowCount && i < static_cast<int>(allIds.size()); ++i) {
            const QPoint r = m_selection->rangeForBlock(i);
            if (r.x() >= 0) targetRows.append(i);
        }
    } else {
        // No selection — use the focused block (TextCaret variant) directly.
        // anchorBlock() returns -1 when there's no selection anchor, so we
        // can't use it here; activeBlock() reads from currentTextCaret().
        int row = m_selection->activeBlock();
        if (row < 0) {
            // Fallback: focused-anchor-row (covers cursor variants that don't
            // have a TextCaret, e.g. BlockSelected on an HR/Image).
            row = m_selection->focusedAnchorRow();
        }
        if (row >= 0 && row < rowCount) targetRows.append(row);
    }
    if (targetRows.isEmpty()) return;

    // Single outer transaction bundles all edits into one undo entry.
    Markoff::UndoLog::Transaction t(m_document->d2UndoLog());

    // Helper: count the existing leading ATX-marker bytes (`#`...`######`
    // optionally followed by one space). Mirrors BlockSerializers::
    // stripLeadingHashes / KindTransition::countLeadingHashes byte semantics.
    auto countLeadingMarkerBytes = [](const QByteArray &content) -> int {
        int hashes = 0;
        while (hashes < 6 && hashes < content.size()
               && content[hashes] == '#') {
            ++hashes;
        }
        if (hashes == 0) return 0;
        int total = hashes;
        if (total < content.size() && content[total] == ' ') ++total;
        return total;
    };

    // Helper: replace the leading ATX marker bytes of a block's buffer with
    // `level` hashes + a space (or strip them when level == 0). Drives the
    // onD2Changed auto-inference (countLeadingHashes vs stored level) so
    // the kind transition holds across the model rebuild.
    auto setAtxMarkers = [&](const Markoff::BlockId &blockId,
                             const QByteArray &content, int level) {
        const int oldBytes = countLeadingMarkerBytes(content);
        const QByteArray newPrefix = (level == 0)
            ? QByteArray()
            : QByteArray(level, '#') + ' ';
        if (oldBytes == 0 && newPrefix.isEmpty()) return;
        if (oldBytes > 0 && newPrefix == content.left(oldBytes)) return;
        m_document->d2ApplyBufferEdit(blockId, 0,
                                      static_cast<quint32>(oldBytes),
                                      newPrefix, t);
    };

    // Process in reverse so byte-offset shifts within one block don't affect
    // others (though kind changes here don't shift cross-block offsets, this
    // keeps the pattern consistent with wrapPerBlock).
    for (int n = targetRows.size() - 1; n >= 0; --n) {
        const int i = targetRows[n];
        const Markoff::BlockId blockId = allIds[i];
        const auto &rec = m_model->recordAt(i);

        if (level == 0) {
            // Demote to Paragraph. No-op if already paragraph.
            if (rec.kind == Markoff::Live::BlockKind::Paragraph) continue;
            // ATX headings carry "# " markers in their buffer per the load
            // convention; strip them so the paragraph buffer is content-only.
            if (rec.kind == Markoff::Live::BlockKind::Heading) {
                setAtxMarkers(blockId, rec.text.toUtf8(), 0);
            }
            Markoff::Cmd::changeKind(*m_document, blockId,
                                     Markoff::BlockKind::Paragraph);
            continue;
        }

        // Promote/change to Heading at `level`. Crucially, BOTH the kind+attr
        // AND the buffer's ATX prefix must be set, otherwise the onD2Changed
        // auto-inference (which compares countLeadingHashes(buffer) against
        // the stored level) will revert the kind change on the next event-
        // loop tick: a Heading with no `# ` in its buffer trips the
        // `atxLost` demote-to-Paragraph path. setAtxMarkers keeps the buffer
        // and attr in lockstep.
        if (rec.kind == Markoff::Live::BlockKind::Heading
                || rec.kind == Markoff::Live::BlockKind::Paragraph) {
            setAtxMarkers(blockId, rec.text.toUtf8(), level);
            Markoff::Cmd::changeKind(*m_document, blockId,
                                     Markoff::BlockKind::Heading,
                                     {Markoff::AttrNames::Level},
                                     {Markoff::AttrValue(level)});
        } else {
            // Other kinds (CodeBlock, ListItem, Blockquote, etc.): change
            // kind without buffer rewrite. Per-kind content-strip is out of
            // MVP scope; the user can demote to paragraph first if needed.
            Markoff::Cmd::changeKind(*m_document, blockId,
                                     Markoff::BlockKind::Heading,
                                     {Markoff::AttrNames::Level},
                                     {Markoff::AttrValue(level)});
        }
    }
}

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
