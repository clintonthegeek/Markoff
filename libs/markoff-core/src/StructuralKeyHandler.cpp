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

// --- ListItem -----------------------------------------------------------------

StructuralResult listItemEnter(MarkoffDocument &doc, BlockId block,
                               uint32_t caretByte) {
    const QByteArray content = doc.blockText(block);
    const int indent = indentOf(doc, block);
    UndoLog::Transaction t(doc.d2UndoLog());

    if (content.isEmpty() && indent > 0) {                      // outdent
        doc.d2SetBlockAttr(block, AttrNames::IndentLevel, indent - 1, t);
        Cmd::renumberRunStartingAt(doc, block, t);
        return {true, block, 0};
    }
    if (content.isEmpty() && indent == 0) {                     // exit list
        doc.d2SetBlockKind(block, BlockKind::Paragraph, t);
        doc.d2SetBlockAttr(block, AttrNames::MarkerStyle, QString{}, t);
        return {true, block, 0};
    }
    if (caretByte == 0) {                                       // item before
        const BlockId nb = Cmd::insertListItemBefore(doc, block, t);
        Cmd::renumberRunStartingAt(doc, nb, t);
        return {true, block, 0};  // follow the original content item
    }
    if (caretByte == static_cast<uint32_t>(content.size())) {   // item after
        const BlockId nb = Cmd::insertListItemAfter(doc, block, t);
        Cmd::renumberRunStartingAt(doc, nb, t);
        return {true, nb, 0};
    }
    // Mid-content split.
    const QByteArray suffix = content.mid(static_cast<int>(caretByte));
    doc.d2ApplyBufferEdit(block, caretByte,
                          static_cast<uint32_t>(suffix.size()), QByteArray{}, t);
    const BlockId nb = Cmd::insertListItemAfter(doc, block, t);
    doc.d2ApplyBufferEdit(nb, 0, 0, suffix, t);
    Cmd::renumberRunStartingAt(doc, nb, t);
    return {true, nb, 0};
}

StructuralResult listItemBackspace(MarkoffDocument &doc, BlockId block,
                                   uint32_t caretByte) {
    if (caretByte != 0) return {};                  // in-line → native
    const int indent = indentOf(doc, block);
    if (indent > 0) {                               // outdent
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2SetBlockAttr(block, AttrNames::IndentLevel, indent - 1, t);
        Cmd::renumberRunStartingAt(doc, block, t);
        return {true, block, 0};
    }
    const auto blocks = doc.iterateBlocks();
    if (indexOf(blocks, block) <= 0) return {};
    auto res = Cmd::backspaceMerge(doc, block);
    if (res.mergedInto.isNull()) return {};
    {   // backspaceMerge self-transacts; renumber in a follow-up (2 undo entries).
        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, res.mergedInto, t);
    }
    return {true, res.mergedInto, res.cursorByteOffset};
}

StructuralResult listItemDelete(MarkoffDocument &doc, BlockId block,
                                uint32_t caretByte) {
    const QByteArray content = doc.blockText(block);
    if (caretByte < static_cast<uint32_t>(content.size())) return {};
    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (idx < 0 || idx >= static_cast<int>(blocks.size()) - 1) return {};
    Cmd::deleteMerge(doc, block);
    {
        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, block, t);
    }
    return {true, block, caretByte};
}

StructuralResult listItemTab(MarkoffDocument &doc, BlockId block, int modifiers,
                             uint32_t caretByte) {
    const int indent = indentOf(doc, block);
    const bool shift = (modifiers & Qt::ShiftModifier) != 0;
    const int newIndent = shift ? std::max(0, indent - 1) : std::min(6, indent + 1);
    if (newIndent == indent) return {true, block, caretByte};  // at boundary; consume key

    const auto blocks = doc.iterateBlocks();
    const int idx = indexOf(blocks, block);
    if (!shift) {
        // Indent only if a preceding ListItem at the current indent exists.
        bool parentFound = false;
        for (int k = idx - 1; k >= 0; --k) {
            const BlockId prev = blocks[size_t(k)];
            if (doc.blockKind(prev) != BlockKind::ListItem) break;
            const int prevIndent = indentOf(doc, prev);
            if (prevIndent == indent) { parentFound = true; break; }
            if (prevIndent < indent) break;
        }
        if (!parentFound) return {true, block, caretByte};  // refuse; consume key
    }

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2SetBlockAttr(block, AttrNames::IndentLevel, newIndent, t);
    Cmd::renumberRunStartingAt(doc, block, t);
    if (idx > 0) Cmd::renumberRunStartingAt(doc, blocks[size_t(idx - 1)], t);
    return {true, block, caretByte};
}

}  // namespace

StructuralResult StructuralKeyHandler::handle(MarkoffDocument &doc, BlockId block,
                                              int key, int modifiers,
                                              uint32_t caretByteInBlock) {
    if (block.isNull()) return {};
    const BlockKind kind = doc.blockKind(block);

    int normKey = key;
    if (key == Qt::Key_Backtab) { normKey = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }

    const bool isEnter = (normKey == Qt::Key_Return || normKey == Qt::Key_Enter);

    switch (kind) {
    case BlockKind::Paragraph:
    case BlockKind::Heading:
        if (isEnter)                      return paragraphEnter(doc, block, modifiers, caretByteInBlock);
        if (normKey == Qt::Key_Backspace) return paragraphBackspace(doc, block, caretByteInBlock);
        if (normKey == Qt::Key_Delete)    return paragraphDelete(doc, block, caretByteInBlock);
        return {};
    case BlockKind::ListItem:
        if (isEnter)                      return listItemEnter(doc, block, caretByteInBlock);
        if (normKey == Qt::Key_Backspace) return listItemBackspace(doc, block, caretByteInBlock);
        if (normKey == Qt::Key_Delete)    return listItemDelete(doc, block, caretByteInBlock);
        if (normKey == Qt::Key_Tab)       return listItemTab(doc, block, modifiers, caretByteInBlock);
        return {};
    default:
        return {};  // other kinds added in later tasks
    }
}

}  // namespace Markoff
