// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
#pragma once

#include <markoff/EditorContext.h>

#include <QTextCursor>
#include <QTextFormat>

namespace Markoff::Internal {

/// Custom QTextCharFormat property key used by MarkdownHighlighter to tag
/// inline-code runs so EditorContext classification can detect them
/// without guessing from font family. See MarkdownHighlighter.cpp.
///
/// QTextFormat::UserProperty + 100 — intentional gap above the existing
/// MathTextObject (+1..+3), CheckboxTextObject (+4), and ReadingMathObject
/// (+10..+11) allocations. Leaves +5..+99 reserved for future text-object
/// properties without forcing renumbering here.
constexpr int kInlineCodeProperty = QTextFormat::UserProperty + 100;

/// Classify the block at the given cursor position. Populates
/// `ctx.blockKind`, `ctx.headingLevel`, `ctx.table`, `ctx.atBlockStart`,
/// `ctx.atBlockEnd`. Other fields untouched. No-op when cursor is null.
void classifyBlockAtCursor(const QTextCursor &cursor, EditorContext &ctx);

/// Classify the inline spans at / around the cursor (or across the
/// selection if non-empty). Populates `ctx.inBold`, `ctx.inItalic`,
/// `ctx.inStrikethrough`, `ctx.inInlineCode`, `ctx.hasSelection`.
/// Other fields untouched.
void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx);

} // namespace Markoff::Internal
