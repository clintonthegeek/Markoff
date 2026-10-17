// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/StructuralKeyHandler.h>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralOp.h>

#include <algorithm>

namespace Markoff {
namespace {
bool accepted(const VNext::ActionPublicationResult& result)
{
    return result.state == VNext::ActionPublicationState::committed
        || result.state == VNext::ActionPublicationState::committedHistoryPending;
}

StructuralResult split(MarkoffDocument& doc, BlockId block, BlockKind kind,
                       uint32_t offset, uint32_t nextOffset = 0)
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.end()) return {};
    const auto text = doc.blockText(block);
    if (offset == 0 && at == blocks.begin()) {
        const auto result = doc.applyStructural(
            StructuralOp{StructuralOp::InsertEntry{BlockId{}, kind}});
        if (!accepted(result)) return {};
        return {true, doc.iterateBlocks().front(), nextOffset};
    }
    if (offset == 0) {
        const BlockId previous = *(at - 1);
        const uint32_t end = static_cast<uint32_t>(doc.blockText(previous).size());
        const auto result = doc.applyBlockSplit({previous, end, 0, {}}, kind);
        if (!accepted(result)) return {};
        const auto after = doc.iterateBlocks();
        const auto previousIt = std::find(after.begin(), after.end(), previous);
        if (previousIt == after.end() || previousIt + 1 == after.end()) return {};
        return {true, *(previousIt + 1), nextOffset};
    }
    const auto result = doc.applyBlockSplit({block, offset,
        static_cast<uint32_t>(text.size()) - offset, {}}, kind);
    if (!accepted(result)) return {};
    const auto after = doc.iterateBlocks();
    const auto current = std::find(after.begin(), after.end(), block);
    if (current == after.end() || current + 1 == after.end()) return {};
    return {true, *(current + 1), nextOffset};
}

StructuralResult mergePrevious(MarkoffDocument& doc, BlockId block,
                               uint32_t caretOffset)
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.begin() || at == blocks.end()) return {};
    const BlockId previous = *(at - 1);
    const uint32_t offset = static_cast<uint32_t>(doc.blockText(previous).size());
    if (!accepted(doc.applyBlockMerge({previous, offset, 0, doc.blockText(block)}, {block})))
        return {};
    return {true, previous, offset + caretOffset};
}

StructuralResult mergeNext(MarkoffDocument& doc, BlockId block, uint32_t offset)
{
    const auto blocks = doc.iterateBlocks();
    const auto at = std::find(blocks.begin(), blocks.end(), block);
    if (at == blocks.end() || at + 1 == blocks.end()) return {};
    const BlockId next = *(at + 1);
    if (!accepted(doc.applyBlockMerge({block,
            static_cast<uint32_t>(doc.blockText(block).size()), 0,
            doc.blockText(next)}, {next}))) return {};
    return {true, block, offset};
}
}

StructuralResult StructuralKeyHandler::handle(MarkoffDocument& doc, BlockId block,
                                               int key, int modifiers,
                                               uint32_t caretByteInBlock)
{
    if (block.isNull()) return {};
    if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const auto kind = doc.blockKind(block);
    const auto text = doc.blockText(block);
    const uint32_t length = static_cast<uint32_t>(text.size());
    const uint32_t offset = std::min(caretByteInBlock, length);

    if (enter) {
        if ((modifiers & Qt::ShiftModifier) || kind == BlockKind::CodeBlock
            || (kind == BlockKind::Math && offset < length)) {
            return accepted(doc.applyBlockEdit({block, offset, 0, QByteArrayLiteral("\n")}))
                ? StructuralResult{true, block, offset + 1} : StructuralResult{};
        }
        if (kind == BlockKind::ListItem && text.isEmpty()) {
            const auto result = doc.changeBlockKind(block, BlockKind::Paragraph,
                {AttrNames::MarkerStyle}, {QString{}});
            return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
        }
        BlockKind nextKind = BlockKind::Paragraph;
        if (kind == BlockKind::ListItem || kind == BlockKind::BlockQuote)
            nextKind = kind;
        if (kind == BlockKind::Math && offset != length) return {};
        if (kind == BlockKind::Paragraph || kind == BlockKind::Heading
            || kind == BlockKind::ListItem || kind == BlockKind::BlockQuote
            || kind == BlockKind::Math || kind == BlockKind::HorizontalRule)
            return split(doc, block, nextKind, offset);
        return {};
    }
    if (kind == BlockKind::ListItem && key == Qt::Key_Backspace && offset == 0) {
        int indent = 0;
        const auto attrs = doc.blockAttrs(block);
        if (const auto it = attrs.constFind(AttrNames::IndentLevel);
            it != attrs.cend())
            if (const int* value = std::get_if<int>(&it.value())) indent = *value;
        if (indent > 0) {
            const auto result = doc.changeBlockKind(block, BlockKind::ListItem,
                {AttrNames::IndentLevel}, {indent - 1});
            return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
        }
        const auto result = doc.changeBlockKind(block, BlockKind::Paragraph,
            {AttrNames::MarkerStyle}, {QString{}});
        return accepted(result) ? StructuralResult{true, block, 0} : StructuralResult{};
    }
    if (kind == BlockKind::ListItem && key == Qt::Key_Tab) {
        const bool outdent = (modifiers & Qt::ShiftModifier) != 0;
        const auto attrs = doc.blockAttrs(block);
        int indent = 0;
        if (const auto it = attrs.constFind(AttrNames::IndentLevel);
            it != attrs.cend())
            if (const int* value = std::get_if<int>(&it.value())) indent = *value;
        const int nextIndent = outdent ? std::max(0, indent - 1)
                                       : std::min(6, indent + 1);
        if (nextIndent == indent) return {true, block, offset};
        const auto blocks = doc.iterateBlocks();
        const auto at = std::find(blocks.begin(), blocks.end(), block);
        if (!outdent && at != blocks.end()) {
            bool parentFound = false;
            while (at != blocks.begin()) {
                const BlockId previous = *(at - 1);
                if (doc.blockKind(previous) != BlockKind::ListItem) break;
                const auto previousAttrs = doc.blockAttrs(previous);
                int previousIndent = 0;
                if (const auto it = previousAttrs.constFind(AttrNames::IndentLevel);
                    it != previousAttrs.cend())
                    if (const int* value = std::get_if<int>(&it.value()))
                        previousIndent = *value;
                if (previousIndent == indent) { parentFound = true; break; }
                if (previousIndent < indent) break;
                // Continue searching toward the start of this list run.
                // `at` is const, so use an explicit index below instead.
                break;
            }
            if (!parentFound) {
                int index = static_cast<int>(std::distance(blocks.begin(), at)) - 1;
                while (index >= 0) {
                    const BlockId previous = blocks[static_cast<size_t>(index)];
                    if (doc.blockKind(previous) != BlockKind::ListItem) break;
                    const auto previousAttrs = doc.blockAttrs(previous);
                    int previousIndent = 0;
                    if (const auto it = previousAttrs.constFind(AttrNames::IndentLevel);
                        it != previousAttrs.cend())
                        if (const int* value = std::get_if<int>(&it.value()))
                            previousIndent = *value;
                    if (previousIndent == indent) { parentFound = true; break; }
                    if (previousIndent < indent) break;
                    --index;
                }
            }
            if (!parentFound) return {true, block, offset};
        }
        const auto result = doc.changeBlockKind(block, BlockKind::ListItem,
            {AttrNames::IndentLevel}, {nextIndent});
        return accepted(result) ? StructuralResult{true, block, offset} : StructuralResult{};
    }
    if (key == Qt::Key_Backspace && offset == 0)
        return mergePrevious(doc, block, 0);
    if (key == Qt::Key_Delete && offset == length)
        return mergeNext(doc, block, offset);
    if (key == Qt::Key_Tab && kind == BlockKind::CodeBlock
        && !(modifiers & Qt::ShiftModifier)) {
        return accepted(doc.applyBlockEdit({block, offset, 0, QByteArrayLiteral("    ")}))
            ? StructuralResult{true, block, offset + 4} : StructuralResult{};
    }
    return {};
}

} // namespace Markoff
