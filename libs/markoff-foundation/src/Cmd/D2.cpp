// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/D2.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockAttrsMap.h>

#include <markoff-parser/Document.h>

#include <QDateTime>
#include <QString>
#include <algorithm>

namespace Markoff::Cmd {

static uint32_t clampByteOffset(const QByteArray &text, uint32_t pos)
{
    return qMin(pos, static_cast<uint32_t>(text.size()));
}

void insertCharacter(MarkoffDocument &doc, BlockId block, uint32_t byteOffset, QChar ch)
{
    CoalesceContext ctx{block, !ch.isNonCharacter() && !ch.isNull() && ch.isPrint(),
                        QDateTime::currentMSecsSinceEpoch()};
    doc.d2UndoLog().maybeCoalesceOrTransaction(ctx, [&](UndoLog::Transaction &t) {
        QByteArray utf8 = QString(ch).toUtf8();
        uint32_t off = clampByteOffset(doc.blockText(block), byteOffset);
        doc.d2ApplyBufferEdit(block, off, 0, utf8, t);
    });
}

void insertSoftBreak(MarkoffDocument &doc, BlockId block, uint32_t byteOffset)
{
    UndoLog::Transaction t(doc.d2UndoLog());
    uint32_t off = clampByteOffset(doc.blockText(block), byteOffset);
    doc.d2ApplyBufferEdit(block, off, 0, "\n", t);
}

BlockId enterAtEnd(MarkoffDocument &doc, BlockId currentBlock)
{
    UndoLog::Transaction t(doc.d2UndoLog());
    return doc.d2InsertBlock(currentBlock, BlockKind::Paragraph, t);
}

BackspaceMergeResult backspaceMerge(MarkoffDocument &doc, BlockId currentBlock)
{
    auto blocks = doc.iterateBlocks();
    auto curIt = std::find(blocks.begin(), blocks.end(), currentBlock);
    if (curIt == blocks.begin())
        return {currentBlock, 0};

    BlockId prev = *(curIt - 1);
    QByteArray prevText = doc.blockText(prev);

    // A trailing '\n' in a block buffer is the structural inter-block delimiter.
    // When a merge destroys the boundary, that delimiter goes with it — replace
    // it with the next block's content rather than appending after it.
    uint32_t joinOffset = static_cast<uint32_t>(prevText.size());
    uint32_t removeLen  = 0;
    if (prevText.endsWith('\n')) {
        --joinOffset;
        removeLen = 1;
    }

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(prev, joinOffset, removeLen, doc.blockText(currentBlock), t);
    doc.d2RemoveBlock(currentBlock, t);

    return {prev, joinOffset};
}

void deleteMerge(MarkoffDocument &doc, BlockId currentBlock)
{
    auto blocks = doc.iterateBlocks();
    auto curIt = std::find(blocks.begin(), blocks.end(), currentBlock);
    if (curIt == blocks.end()) return;
    auto nextIt = std::next(curIt);
    if (nextIt == blocks.end()) return;

    BlockId next = *nextIt;
    QByteArray curText = doc.blockText(currentBlock);

    // Same as backspaceMerge: the trailing '\n' is structural; replace it.
    uint32_t joinOffset = static_cast<uint32_t>(curText.size());
    uint32_t removeLen  = 0;
    if (curText.endsWith('\n')) {
        --joinOffset;
        removeLen = 1;
    }

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(currentBlock, joinOffset, removeLen, doc.blockText(next), t);
    doc.d2RemoveBlock(next, t);
}

void changeKind(MarkoffDocument &doc, BlockId block, BlockKind newKind,
                const QList<QByteArray> &attrNames, const QList<AttrValue> &attrValues)
{
    Q_ASSERT(attrNames.size() == attrValues.size());
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2SetBlockKind(block, newKind, t);
    for (int i = 0; i < attrNames.size(); ++i)
        doc.d2SetBlockAttr(block, attrNames[i], attrValues[i], t);
}

void pasteMarkdown(MarkoffDocument &doc, BlockId targetBlock,
                   uint32_t byteOffset, const QByteArray &source)
{
    // Parse synchronously
    auto parsedDoc = Markoff::Document::fromMarkdown(QString::fromUtf8(source));
    if (!parsedDoc) return;
    auto pastedBlocks = parsedDoc->topLevelBlocks();
    if (pastedBlocks.empty()) return;

    UndoLog::Transaction t(doc.d2UndoLog());

    // Split current block at byteOffset — remove tail
    QByteArray tail = doc.blockText(targetBlock).mid(static_cast<int>(byteOffset));
    if (!tail.isEmpty())
        doc.d2ApplyBufferEdit(targetBlock, byteOffset,
                              static_cast<uint32_t>(tail.size()), {}, t);

    // Insert each parsed block after the last known position
    BlockId after = targetBlock;
    for (const auto &pb : pastedBlocks) {
        // Simplified kind mapping; Phase 7 has the full mapping
        BlockKind kind = BlockKind::Paragraph;

        BlockId newId = doc.d2InsertBlock(after, kind, t);

        // Populate buffer with parsed block content (body-relative byte offsets)
        int contentLen = pb.byteEnd - pb.byteStart;
        if (contentLen > 0) {
            QByteArray content = source.mid(pb.byteStart, contentLen);
            doc.d2ApplyBufferEdit(newId, 0, 0, content, t);
        }
        after = newId;
    }

    // Append original tail to last pasted block
    if (!tail.isEmpty())
        doc.d2ApplyBufferEdit(after,
                              static_cast<uint32_t>(doc.blockText(after).size()),
                              0, tail, t);
}

}  // namespace Markoff::Cmd
