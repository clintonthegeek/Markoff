// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>

#include <QByteArray>
#include <QChar>
#include <QList>
#include <cstdint>

namespace Markoff {
class MarkoffDocument;

namespace Cmd {

// ============================================================
// D2 per-block editing commands (use with D2 MarkoffDocument)
// ============================================================

/// Insert a single character at byteOffset (byte offset within block text).
/// Consecutive printable characters in the same block within 1000ms coalesce
/// into a single undo entry.
MARKOFF_CORE_EXPORT void insertCharacter(MarkoffDocument &doc,
                                                BlockId block,
                                                uint32_t byteOffset,
                                                QChar ch);

/// Insert a soft break (literal newline) within the block at byteOffset.
MARKOFF_CORE_EXPORT void insertSoftBreak(MarkoffDocument &doc,
                                                BlockId block,
                                                uint32_t byteOffset);

/// Create a new Paragraph block immediately after `currentBlock`.
/// Returns the new BlockId.
MARKOFF_CORE_EXPORT BlockId enterAtEnd(MarkoffDocument &doc,
                                              BlockId currentBlock);

/// Merge `currentBlock` into the block before it.
/// Returns {mergedIntoBlockId, cursorByteOffset}.
/// No-op (returns currentBlock/0) if currentBlock is already the first.
struct BackspaceMergeResult { BlockId mergedInto; uint32_t cursorByteOffset; };
MARKOFF_CORE_EXPORT BackspaceMergeResult backspaceMerge(MarkoffDocument &doc,
                                                               BlockId currentBlock);

/// Merge the block after `currentBlock` into `currentBlock`.
/// No-op if currentBlock is already the last block.
MARKOFF_CORE_EXPORT void deleteMerge(MarkoffDocument &doc, BlockId currentBlock);

/// Change the block's kind and optionally set attributes.
/// attrNames and attrValues are parallel arrays; lengths must match.
MARKOFF_CORE_EXPORT VNext::ActionPublicationResult changeKind(MarkoffDocument &doc,
                                           BlockId block,
                                           BlockKind newKind,
                                           const QList<QByteArray> &attrNames = {},
                                           const QList<AttrValue>  &attrValues = {});

/// Paste markdown source at byteOffset within targetBlock.
/// Parses source synchronously (kept as typed blocks — Table, Heading,
/// ListItem, etc. — not flattened to Paragraph the way applyFlatEdit would).
/// Splits targetBlock at the offset, inserts parsed blocks after the split,
/// appends the tail to the last inserted block.
/// Returns the caret position immediately after the pasted content (before
/// the reappended tail), so a block-local caller (e.g. the canvas leaf,
/// which may not do cross-block byte arithmetic) can reposition its caret
/// without walking iterateBlocks() itself. No-op (returns
/// {targetBlock, byteOffset}) if `source` is empty/whitespace-only or fails
/// to parse.
struct PasteMarkdownResult { BlockId caretBlock; uint32_t caretByteOffset; };
MARKOFF_CORE_EXPORT PasteMarkdownResult pasteMarkdown(MarkoffDocument &doc,
                                              BlockId targetBlock,
                                              uint32_t byteOffset,
                                              const QByteArray &source);

}}  // namespace Markoff::Cmd
