// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
#pragma once

#include <QString>
#include <Qt>

#include <optional>

namespace Markoff {

/// A snapshot of the editor's per-cursor contextual state, suitable
/// for driving menu/toolbar enable + check state in the host app.
/// Fields are value-typed; copying is cheap.
///
/// Phase C6 populates a subset of fields (blockKind, headingLevel,
/// table, inBold/Italic/Strikethrough/InlineCode, hasSelection,
/// atBlockStart, atBlockEnd, readOnly). Other fields are declared
/// but return default values until a later C-phase's classifier
/// work fills them in. Consumers should write forward-compatible
/// code: `if (ctx.link) { /* use ctx.link->url */ }`.
struct EditorContext {
    // ---- Block context ----
    enum class BlockKind {
        Paragraph, Heading, BlockQuote, Callout, CodeBlock,
        Table, ListItem, HorizontalRule, FrontMatter, MathDisplay,
        Empty,
    };
    BlockKind blockKind = BlockKind::Paragraph;

    int headingLevel = 0;          ///< 1..6 when blockKind == Heading, else 0.

    // Populated when blockKind == Callout (deferred — default-empty in C6).
    QString calloutType;
    QString calloutTitle;

    // Populated when blockKind == CodeBlock (deferred — default in C6).
    QString codeBlockLanguage;
    bool    codeBlockFenced = false;

    // Populated when blockKind == ListItem (taskState + listMarker deferred).
    enum class ListMarker { Unordered, Ordered, Task };
    ListMarker listMarker = ListMarker::Unordered;
    enum class TaskState { NotATask, Unchecked, Checked, Cancelled };
    TaskState  taskState = TaskState::NotATask;
    int        listNestingLevel = 0;

    // Populated when blockKind == BlockQuote (deferred).
    int blockquoteDepth = 0;

    // Populated when blockKind == Table — C6 populates row/col/rows/cols/isHeaderRow.
    // columnAlignment left default (future phase).
    struct TableContext {
        int row = 0, col = 0;
        int rows = 0, cols = 0;
        Qt::Alignment columnAlignment;
        bool isHeaderRow = false;
    };
    std::optional<TableContext> table;

    // ---- Inline-span context ----
    // `true` when the selection (or cursor if empty) lies entirely
    // within a span of the named kind.
    bool inBold          = false;
    bool inItalic        = false;
    bool inStrikethrough = false;
    bool inInlineCode    = false;
    bool inHighlight     = false;   ///< deferred — always false in C6
    bool inMathInline    = false;   ///< deferred — always false in C6

    // `true` when the cursor sits inside the corresponding span.
    // All three optional<> are `std::nullopt` in C6 — future phases fill them.
    struct LinkContext {
        QString url;
        QString text;
        bool isWikiLink = false;
        bool isEmbed    = false;
    };
    std::optional<LinkContext> link;

    struct TagContext { QString tag; };
    std::optional<TagContext> tag;

    struct FootnoteContext { QString id; };
    std::optional<FootnoteContext> footnote;

    // ---- Convenience flags ----
    bool hasSelection = false;
    bool atBlockStart = false;
    bool atBlockEnd   = false;
    bool readOnly     = false;
};

} // namespace Markoff
