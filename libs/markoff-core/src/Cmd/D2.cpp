// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/AttrNames.h>

#include <markoff/parser/Document.h>

#include <QDateTime>
#include <QString>
#include <algorithm>

namespace {
QHash<Markoff::AttrName, Markoff::AttrValue> copyListItemAttrs(
    Markoff::MarkoffDocument &doc, Markoff::BlockId from)
{
    using namespace Markoff;
    QHash<AttrName, AttrValue> out;
    const auto src = doc.blockAttrs(from);
    for (const AttrName &key : {AttrNames::IndentLevel, AttrNames::MarkerStyle,
                                 AttrNames::LooseRun}) {
        auto it = src.constFind(key);
        if (it != src.constEnd()) out[key] = it.value();
    }
    return out;
}
} // namespace

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

    // B1: block buffers are content. Any trailing '\n' in prevText is
    // user-authored (e.g. a soft break) and must be preserved as
    // content of the merged block. Append cleanly.
    uint32_t joinOffset = static_cast<uint32_t>(prevText.size());
    uint32_t removeLen  = 0;

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

    // B1: see backspaceMerge — buffers are content; append cleanly.
    uint32_t joinOffset = static_cast<uint32_t>(curText.size());
    uint32_t removeLen  = 0;

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

BlockId insertListItemAfter(MarkoffDocument &doc, BlockId currentItem,
                             UndoLog::Transaction &t)
{
    const auto srcAttrs = copyListItemAttrs(doc, currentItem);
    const BlockId newId = doc.d2InsertBlock(currentItem, BlockKind::ListItem, t);
    for (auto it = srcAttrs.constBegin(); it != srcAttrs.constEnd(); ++it)
        doc.d2SetBlockAttr(newId, it.key(), it.value(), t);

    const auto curAttrs = doc.blockAttrs(currentItem);
    const auto *stylePtr = [&]() -> const QString * {
        auto it = curAttrs.constFind(AttrNames::MarkerStyle);
        return (it != curAttrs.constEnd()) ? std::get_if<QString>(&it.value()) : nullptr;
    }();
    if (stylePtr && (*stylePtr == QStringLiteral("dot")
                  || *stylePtr == QStringLiteral("paren"))) {
        auto ni = curAttrs.constFind(AttrNames::MarkerNumber);
        const int n = (ni != curAttrs.constEnd() && std::get_if<int>(&ni.value()))
                      ? std::get<int>(ni.value()) : 1;
        doc.d2SetBlockAttr(newId, AttrNames::MarkerNumber, n + 1, t);
    } else if (stylePtr && *stylePtr == QStringLiteral("task")) {
        doc.d2SetBlockAttr(newId, AttrNames::Checked, false, t);
    }
    return newId;
}

BlockId insertListItemBefore(MarkoffDocument &doc, BlockId currentItem,
                              UndoLog::Transaction &t)
{
    // Find the block before currentItem to use as the insertion anchor
    const auto blocks = doc.iterateBlocks();
    BlockId prev{};  // default-constructed = null/invalid
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i] == currentItem) {
            if (i > 0) prev = blocks[i - 1];
            break;
        }
    }

    const auto srcAttrs = copyListItemAttrs(doc, currentItem);
    const BlockId newId = doc.d2InsertBlock(prev, BlockKind::ListItem, t);
    for (auto it = srcAttrs.constBegin(); it != srcAttrs.constEnd(); ++it)
        doc.d2SetBlockAttr(newId, it.key(), it.value(), t);

    const auto curAttrs = doc.blockAttrs(currentItem);
    const auto *stylePtr = [&]() -> const QString * {
        auto it = curAttrs.constFind(AttrNames::MarkerStyle);
        return (it != curAttrs.constEnd()) ? std::get_if<QString>(&it.value()) : nullptr;
    }();
    if (stylePtr && (*stylePtr == QStringLiteral("dot")
                  || *stylePtr == QStringLiteral("paren"))) {
        auto ni = curAttrs.constFind(AttrNames::MarkerNumber);
        const int n = (ni != curAttrs.constEnd() && std::get_if<int>(&ni.value()))
                      ? std::get<int>(ni.value()) : 1;
        doc.d2SetBlockAttr(newId, AttrNames::MarkerNumber, n, t);
    } else if (stylePtr && *stylePtr == QStringLiteral("task")) {
        doc.d2SetBlockAttr(newId, AttrNames::Checked, false, t);
    }
    return newId;
}

void renumberRunStartingAt(MarkoffDocument &doc, BlockId anyItemInRun,
                            UndoLog::Transaction &t)
{
    if (doc.blockKind(anyItemInRun) != BlockKind::ListItem) return;

    const auto seedAttrs = doc.blockAttrs(anyItemInRun);
    const auto *seedStylePtr = [&]() -> const QString * {
        auto it = seedAttrs.constFind(AttrNames::MarkerStyle);
        return (it != seedAttrs.constEnd()) ? std::get_if<QString>(&it.value()) : nullptr;
    }();
    if (!seedStylePtr || (*seedStylePtr != QStringLiteral("dot")
                       && *seedStylePtr != QStringLiteral("paren"))) return;

    const auto *seedIndentPtr = [&]() -> const int * {
        auto it = seedAttrs.constFind(AttrNames::IndentLevel);
        return (it != seedAttrs.constEnd()) ? std::get_if<int>(&it.value()) : nullptr;
    }();
    const int seedIndent = seedIndentPtr ? *seedIndentPtr : 0;
    const QString &seedStyle = *seedStylePtr;

    const auto blocks = doc.iterateBlocks();
    int seedIdx = -1;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i] == anyItemInRun) { seedIdx = static_cast<int>(i); break; }
    }
    if (seedIdx < 0) return;

    // Walk backward to find the run's first item
    int firstIdx = seedIdx;
    while (firstIdx > 0) {
        const BlockId prevId = blocks[firstIdx - 1];
        if (doc.blockKind(prevId) != BlockKind::ListItem) break;
        const auto prevAttrs = doc.blockAttrs(prevId);
        const auto *ps = [&]() -> const QString * {
            auto it = prevAttrs.constFind(AttrNames::MarkerStyle);
            return (it != prevAttrs.constEnd()) ? std::get_if<QString>(&it.value()) : nullptr;
        }();
        if (!ps || *ps != seedStyle) break;
        const auto *pi = [&]() -> const int * {
            auto it = prevAttrs.constFind(AttrNames::IndentLevel);
            return (it != prevAttrs.constEnd()) ? std::get_if<int>(&it.value()) : nullptr;
        }();
        if (!pi || *pi != seedIndent) break;
        --firstIdx;
    }

    // First item's MarkerNumber is the seed for the run
    const auto firstAttrs = doc.blockAttrs(blocks[firstIdx]);
    const auto *firstNumPtr = [&]() -> const int * {
        auto it = firstAttrs.constFind(AttrNames::MarkerNumber);
        return (it != firstAttrs.constEnd()) ? std::get_if<int>(&it.value()) : nullptr;
    }();
    const int seedNumber = firstNumPtr ? *firstNumPtr : 1;

    // Walk forward, fixing numbers
    int expected = seedNumber;
    for (std::size_t i = static_cast<std::size_t>(firstIdx); i < blocks.size(); ++i) {
        const BlockId bid = blocks[i];
        if (doc.blockKind(bid) != BlockKind::ListItem) break;
        const auto attrs = doc.blockAttrs(bid);
        const auto *style = [&]() -> const QString * {
            auto it = attrs.constFind(AttrNames::MarkerStyle);
            return (it != attrs.constEnd()) ? std::get_if<QString>(&it.value()) : nullptr;
        }();
        if (!style || *style != seedStyle) break;
        const auto *indentPtr = [&]() -> const int * {
            auto it = attrs.constFind(AttrNames::IndentLevel);
            return (it != attrs.constEnd()) ? std::get_if<int>(&it.value()) : nullptr;
        }();
        if (!indentPtr || *indentPtr != seedIndent) break;

        const auto *actualPtr = [&]() -> const int * {
            auto it = attrs.constFind(AttrNames::MarkerNumber);
            return (it != attrs.constEnd()) ? std::get_if<int>(&it.value()) : nullptr;
        }();
        const int actual = actualPtr ? *actualPtr : -1;
        if (actual != expected)
            doc.d2SetBlockAttr(bid, AttrNames::MarkerNumber, expected, t);
        ++expected;
    }
}

}  // namespace Markoff::Cmd
