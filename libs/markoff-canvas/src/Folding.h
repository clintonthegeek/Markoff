// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff/core/BlockId.h>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::Canvas::Detail {

/// Minimum number of TOP-LEVEL items (`AttrNames::IndentLevel == 0`) a
/// list run must have before its first item counts as a fold head (plan
/// P5.6 "long lists"). Below this, a list is short enough that folding it
/// buys nothing — no affordance is painted and `resolveFoldable` reports
/// `Kind::None` for it.
constexpr int kLongListFoldThreshold = 6;

/// What kind of foldable unit (if any) a block heads, purely a function of
/// current document structure — no fold *state* (which blocks are
/// currently folded) is consulted here. Mirrors `FoldRef::Kind` (core) for
/// the two cases core itself can define (Heading is core-computable from
/// BlockKind + level alone); `LongList`/`Callout` are canvas-local
/// interpretations of core's generic `FoldRef::Kind::Block` — core has no
/// concept of a callout or a list run, so this leaf is the one place that
/// knows what "a specific block range" (the core doc comment's wording)
/// means for those two.
enum class FoldKind { None, Heading, LongList, Callout };

/// A foldable block's kind plus the body blocks that fold away when it is
/// folded (the head itself is never included). `kind == None` iff `body`
/// is empty.
struct FoldInfo {
    FoldKind        kind = FoldKind::None;
    QList<BlockId>  body;
};

/// Resolves what folding `id` would hide, if anything:
///  - Heading: every block after `id` up to (not including) the next
///    Heading at level <= `id`'s own level, or the end of the document.
///    Empty body (kind stays None) for a heading with nothing following it
///    — an empty fold has no reason to exist.
///  - BlockQuote shaped as an Obsidian callout (`CalloutBlocks::
///    parseCallout`, matches the head paragraph only — see its own doc
///    comment on why a continuation paragraph never matches): every
///    subsequent block sharing its `AttrNames::BlockQuoteRunId` (the
///    load-time split's grouping id for one logical multi-paragraph
///    quote — core/CLAUDE.md "Buffer buffer convention").
///  - ListItem at `AttrNames::IndentLevel == 0` that is the FIRST item of
///    its run (previous block is not a ListItem) AND the run has at least
///    `kLongListFoldThreshold` top-level items: every ListItem block
///    (any indent) following it in the same run.
///  - Anything else, or a foldable shape with nothing to hide: `Kind::None`,
///    empty body.
FoldInfo resolveFoldable(const MarkoffDocument &doc, BlockId id);

}  // namespace Markoff::Canvas::Detail
