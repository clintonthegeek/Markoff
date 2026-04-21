// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
#pragma once

#include <markoff/EditorContext.h>

#include <QTextCursor>

namespace Markoff::Internal {

/// Classify the block at the given cursor position. Populates
/// `ctx.blockKind`, `ctx.headingLevel`, `ctx.table`, `ctx.atBlockStart`,
/// `ctx.atBlockEnd`. Other fields untouched. No-op when cursor is null.
void classifyBlockAtCursor(const QTextCursor &cursor, EditorContext &ctx);

/// Classify the inline spans at / around the cursor (or across the
/// selection if non-empty). Populates `ctx.inBold`, `ctx.inItalic`,
/// `ctx.inStrikethrough`, `ctx.inInlineCode`, `ctx.hasSelection`.
/// Other fields untouched.
///
/// Stub in Commit B — real body lands in Commit C.
void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx);

} // namespace Markoff::Internal
