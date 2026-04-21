// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "EditorContextClassifier.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>

namespace Markoff::Internal {

namespace {

int headingLevelFromBlockText(const QString &line)
{
    int level = 0;
    while (level < line.size() && line.at(level) == QLatin1Char('#'))
        ++level;
    if (level == 0 || level > 6) return 0;
    // Require the canonical "# " form: leading #'s followed by a space.
    if (level >= line.size() || line.at(level) != QLatin1Char(' '))
        return 0;
    return level;
}

bool lineLooksLikeListItem(const QString &line)
{
    // Matches "- ", "* ", "+ ", "1. " with optional leading whitespace.
    // Also covers task-list "- [ ] " and "- [x] " via the bullet prefix.
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:[-*+]|\\d+\\.)\\s"));
    return re.match(line).hasMatch();
}

} // namespace

void classifyBlockAtCursor(const QTextCursor &cursor, EditorContext &ctx)
{
    using BK = EditorContext::BlockKind;
    if (cursor.isNull()) {
        ctx.blockKind = BK::Empty;
        return;
    }

    const QTextBlock block = cursor.block();
    const QString line = block.text();

    ctx.atBlockStart = cursor.positionInBlock() == 0;
    // length() includes the trailing block separator; cursor at the end
    // of the actual text is at length()-1. An empty block has length 1.
    ctx.atBlockEnd = cursor.positionInBlock() == qMax(0, block.length() - 1);

    // Table — check first; tables own their own QTextBlock tree so
    // heading/list heuristics on block text don't apply.
    if (QTextTable *tbl = cursor.currentTable()) {
        const QTextTableCell cell = tbl->cellAt(cursor);
        EditorContext::TableContext tc;
        tc.row = cell.row();
        tc.col = cell.column();
        tc.rows = tbl->rows();
        tc.cols = tbl->columns();
        tc.isHeaderRow = (cell.row() == 0);
        // columnAlignment left default — populated in a future phase.
        ctx.table = tc;
        ctx.blockKind = BK::Table;
        return;
    }

    // Heading
    const int lvl = headingLevelFromBlockText(line);
    if (lvl > 0) {
        ctx.blockKind = BK::Heading;
        ctx.headingLevel = lvl;
        return;
    }

    // Empty
    if (line.isEmpty()) {
        ctx.blockKind = BK::Empty;
        return;
    }

    // ListItem (bullet or ordered, including task-list bullet prefix)
    if (lineLooksLikeListItem(line)) {
        ctx.blockKind = BK::ListItem;
        // listMarker / taskState / listNestingLevel left default —
        // filled in a future phase when a consumer needs them.
        return;
    }

    ctx.blockKind = BK::Paragraph;
}

void classifyInlineAtCursor(const QTextCursor &cursor, EditorContext &ctx)
{
    // Commit B stub — real body lands in Commit C.
    ctx.hasSelection = cursor.hasSelection();
}

} // namespace Markoff::Internal
