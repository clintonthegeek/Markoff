// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

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
MARKOFF_FOUNDATION_EXPORT void insertCharacter(MarkoffDocument &doc,
                                                BlockId block,
                                                uint32_t byteOffset,
                                                QChar ch);

/// Insert a soft break (literal newline) within the block at byteOffset.
MARKOFF_FOUNDATION_EXPORT void insertSoftBreak(MarkoffDocument &doc,
                                                BlockId block,
                                                uint32_t byteOffset);

/// Create a new Paragraph block immediately after `currentBlock`.
/// Returns the new BlockId.
MARKOFF_FOUNDATION_EXPORT BlockId enterAtEnd(MarkoffDocument &doc,
                                              BlockId currentBlock);

/// Merge `currentBlock` into the block before it.
/// Returns {mergedIntoBlockId, cursorByteOffset}.
/// No-op (returns currentBlock/0) if currentBlock is already the first.
struct BackspaceMergeResult { BlockId mergedInto; uint32_t cursorByteOffset; };
MARKOFF_FOUNDATION_EXPORT BackspaceMergeResult backspaceMerge(MarkoffDocument &doc,
                                                               BlockId currentBlock);

/// Merge the block after `currentBlock` into `currentBlock`.
/// No-op if currentBlock is already the last block.
MARKOFF_FOUNDATION_EXPORT void deleteMerge(MarkoffDocument &doc, BlockId currentBlock);

/// Change the block's kind and optionally set attributes.
/// attrNames and attrValues are parallel arrays; lengths must match.
MARKOFF_FOUNDATION_EXPORT void changeKind(MarkoffDocument &doc,
                                           BlockId block,
                                           BlockKind newKind,
                                           const QList<QByteArray> &attrNames = {},
                                           const QList<AttrValue>  &attrValues = {});

/// Paste markdown source at byteOffset within targetBlock.
/// Parses source synchronously. Splits targetBlock at the offset, inserts
/// parsed blocks after the split, appends the tail to the last inserted block.
MARKOFF_FOUNDATION_EXPORT void pasteMarkdown(MarkoffDocument &doc,
                                              BlockId targetBlock,
                                              uint32_t byteOffset,
                                              const QByteArray &source);

}}  // namespace Markoff::Cmd
