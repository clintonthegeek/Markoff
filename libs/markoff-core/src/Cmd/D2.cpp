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

PasteMarkdownResult pasteMarkdown(MarkoffDocument &doc, BlockId targetBlock,
                   uint32_t byteOffset, const QByteArray &source)
{
    const PasteMarkdownResult noop{targetBlock, byteOffset};

    // Strip clipboard NULs (LibreOffice's text/markdown mime appends one)
    // before parsing — a trailing NUL would otherwise survive into the
    // last pasted block's buffer.
    QByteArray cleaned;
    cleaned.reserve(source.size());
    for (char c : source) {
        if (c != '\0')
            cleaned.append(c);
    }
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty())
        return noop;

    // Parse synchronously — same TLB pipeline loadFromMarkdown uses, so
    // pipe tables / headings / lists keep their kinds (applyFlatEdit would
    // naively split on newlines into Paragraphs and lose Table).
    auto parsedDoc = Markoff::Document::fromMarkdown(QString::fromUtf8(cleaned));
    if (!parsedDoc) return noop;
    auto pastedBlocks = parsedDoc->topLevelBlocks();
    if (pastedBlocks.empty()) return noop;

    // TLB byte offsets are body-relative; `cleaned` == the parsed body as
    // long as it carries no YAML frontmatter (paste sources never do).
    const QByteArray &parseBody = cleaned;

    auto mapKind = [](Markoff::TopLevelBlock::Kind k) -> BlockKind {
        using K = Markoff::TopLevelBlock::Kind;
        switch (k) {
        case K::Paragraph:         return BlockKind::Paragraph;
        case K::AtxHeading:
        case K::SetextHeading:     return BlockKind::Heading;
        case K::FencedCodeBlock:
        case K::IndentedCodeBlock: return BlockKind::CodeBlock;
        case K::BlockQuote:        return BlockKind::BlockQuote;
        case K::ThematicBreak:     return BlockKind::HorizontalRule;
        case K::HtmlBlock:         return BlockKind::HtmlBlock;
        case K::Table:             return BlockKind::Table;
        case K::ListItem:          return BlockKind::ListItem;
        default:                   return BlockKind::Paragraph;
        }
    };

    UndoLog::Transaction t(doc.d2UndoLog());

    // Split current block at byteOffset — remove tail
    QByteArray tail = doc.blockText(targetBlock).mid(static_cast<int>(byteOffset));
    if (!tail.isEmpty())
        doc.d2ApplyBufferEdit(targetBlock, byteOffset,
                              static_cast<uint32_t>(tail.size()), {}, t);

    // Reuse an empty target block for the first pasted TLB so we don't leave
    // a blank paragraph above a pasted table.
    bool reuseTarget = (byteOffset == 0 && doc.blockText(targetBlock).isEmpty());

    BlockId after = targetBlock;
    bool first = true;
    for (const auto &pb : pastedBlocks) {
        if (pb.kind == Markoff::TopLevelBlock::Kind::LinkReferenceDefinition)
            continue;

        BlockKind kind = mapKind(pb.kind);
        // Quoted paragraphs arrive as Paragraph + blockQuoteDepth > 0.
        if (pb.blockQuoteDepth > 0 && kind == BlockKind::Paragraph)
            kind = BlockKind::BlockQuote;

        BlockId newId;
        if (first && reuseTarget) {
            newId = targetBlock;
            doc.d2SetBlockKind(newId, kind, t);
            // Clear any leftover buffer before writing content.
            const QByteArray cur = doc.blockText(newId);
            if (!cur.isEmpty())
                doc.d2ApplyBufferEdit(newId, 0, uint32_t(cur.size()), {}, t);
        } else {
            newId = doc.d2InsertBlock(after, kind, t);
        }
        first = false;

        if (kind == BlockKind::Heading) {
            doc.d2SetBlockAttr(newId, AttrNames::Level,
                               qBound(1, pb.headingLevel, 6), t);
            doc.d2SetBlockAttr(newId, AttrNames::HeadingForm,
                               pb.kind == Markoff::TopLevelBlock::Kind::SetextHeading
                                   ? QStringLiteral("setext")
                                   : QStringLiteral("atx"),
                               t);
        } else if (kind == BlockKind::ListItem) {
            if (!pb.markerStyle.isEmpty())
                doc.d2SetBlockAttr(newId, AttrNames::MarkerStyle, pb.markerStyle, t);
            if (pb.markerNumber > 0)
                doc.d2SetBlockAttr(newId, AttrNames::MarkerNumber, pb.markerNumber, t);
            doc.d2SetBlockAttr(newId, AttrNames::IndentLevel, pb.indentDepth, t);
            doc.d2SetBlockAttr(newId, AttrNames::LooseRun, pb.looseRun, t);
            if (pb.markerStyle == QStringLiteral("task"))
                doc.d2SetBlockAttr(newId, AttrNames::Checked, pb.checked, t);
        } else if (kind == BlockKind::BlockQuote) {
            doc.d2SetBlockAttr(newId, AttrNames::BlockQuoteDepth,
                               qMax(1, pb.blockQuoteDepth), t);
            if (pb.blockQuoteRunId > 0)
                doc.d2SetBlockAttr(newId, AttrNames::BlockQuoteRunId,
                                   pb.blockQuoteRunId, t);
        } else if (kind == BlockKind::CodeBlock) {
            if (!pb.codeLanguage.isEmpty())
                doc.d2SetBlockAttr(newId, AttrNames::InfoString, pb.codeLanguage, t);
        }

        // Populate buffer with parsed block content (body-relative bytes).
        int contentLen = pb.byteEnd - pb.byteStart;
        if (contentLen > 0) {
            QByteArray content = parseBody.mid(pb.byteStart, contentLen);
            // Table / code blocks keep their source shape (incl. internal \n).
            if (kind == BlockKind::CodeBlock && !pb.codeText.isEmpty())
                content = pb.codeText.toUtf8();
            doc.d2ApplyBufferEdit(newId, 0, 0, content, t);
        }
        after = newId;
    }

    // Caret lands right after the pasted content, before the tail below.
    const PasteMarkdownResult result{after, uint32_t(doc.blockText(after).size())};

    // Append original tail to last pasted block
    if (!tail.isEmpty())
        doc.d2ApplyBufferEdit(after,
                              static_cast<uint32_t>(doc.blockText(after).size()),
                              0, tail, t);

    return result;
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
