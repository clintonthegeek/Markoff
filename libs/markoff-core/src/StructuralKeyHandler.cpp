// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/StructuralKeyHandler.h>

#include <QtCore/qnamespace.h>
#include <algorithm>
#include <variant>
#include <vector>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

namespace Markoff {

namespace {

int indexOf(const std::vector<BlockId> &blocks, BlockId id) {
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        if (blocks[size_t(i)] == id) return i;
    return -1;
}

int indentOf(MarkoffDocument &doc, BlockId id) {
    const auto attrs = doc.blockAttrs(id);
    auto it = attrs.find(AttrNames::IndentLevel);
    if (it != attrs.end() && std::holds_alternative<int>(it.value()))
        return std::get<int>(it.value());
    return 0;
}

// --- Paragraph / Heading -------------------------------------------------

StructuralResult paragraphEnter(MarkoffDocument &doc, BlockId block,
                                int modifiers, uint32_t caretByte) {
    const QByteArray text = doc.blockText(block);
    const bool isShift = (modifiers & Qt::ShiftModifier) != 0;

    if (isShift) {
        Cmd::insertSoftBreak(doc, block, caretByte);
        return {true, block, caretByte + 1};
    }

    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);

    if (caretByte == static_cast<uint32_t>(text.size())) {
        const BlockId nb = Cmd::enterAtEnd(doc, block);  // new para after
        return {true, nb, 0};
    }
    if (caretByte == 0) {
        BlockId nb;
        if (idx > 0) {
            nb = Cmd::enterAtEnd(doc, blocks[size_t(idx - 1)]);  // insert before `block`
        } else {
            UndoLog::Transaction t(doc.d2UndoLog());
            nb = doc.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        }
        return {true, nb, 0};  // caret in the new empty para (matches live)
    }
    // Mid-block split.
    const QByteArray suffix = text.mid(static_cast<int>(caretByte));
    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2ApplyBufferEdit(block, caretByte,
                          static_cast<uint32_t>(suffix.size()), QByteArray{}, t);
    const BlockId nb = doc.d2InsertBlock(block, BlockKind::Paragraph, t);
    doc.d2ApplyBufferEdit(nb, 0, 0, suffix, t);
    return {true, nb, 0};
}

StructuralResult paragraphBackspace(MarkoffDocument &doc, BlockId block,
                                    uint32_t caretByte) {
    if (caretByte != 0) return {};               // not at start
    const auto blocks = doc.iterateBlocks();
    if (indexOf(blocks, block) <= 0) return {};  // first block / not found
    auto res = Cmd::backspaceMerge(doc, block);
    if (res.mergedInto.isNull()) return {};
    return {true, res.mergedInto, res.cursorByteOffset};
}

StructuralResult paragraphDelete(MarkoffDocument &doc, BlockId block,
                                 uint32_t caretByte) {
    const QByteArray text = doc.blockText(block);
    if (caretByte != static_cast<uint32_t>(text.size())) return {};  // not at end
    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (idx < 0 || idx >= static_cast<int>(blocks.size()) - 1) return {};  // last block
    Cmd::deleteMerge(doc, block);
    return {true, block, caretByte};
}

}  // namespace

StructuralResult StructuralKeyHandler::handle(MarkoffDocument &doc, BlockId block,
                                              int key, int modifiers,
                                              uint32_t caretByteInBlock) {
    if (block.isNull()) return {};
    const BlockKind kind = doc.blockKind(block);

    const bool isEnter = (key == Qt::Key_Return || key == Qt::Key_Enter);

    switch (kind) {
    case BlockKind::Paragraph:
    case BlockKind::Heading:
        if (isEnter)                  return paragraphEnter(doc, block, modifiers, caretByteInBlock);
        if (key == Qt::Key_Backspace) return paragraphBackspace(doc, block, caretByteInBlock);
        if (key == Qt::Key_Delete)    return paragraphDelete(doc, block, caretByteInBlock);
        return {};
    default:
        return {};  // other kinds added in later tasks
    }
}

}  // namespace Markoff
