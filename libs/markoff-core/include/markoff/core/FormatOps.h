// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

/// Widget-free markdown format operations over the single-`\n`
/// widgetFlatView coordinate space (spec
/// docs/specs/2026-06-09-markdownview-contract-v2-design.md §5).
/// Lifted from markoff-source's Editor (queue #8.6-hardened,
/// block-aware via Detail::findBlockAtSepByte). Each op mutates the
/// model through d2 primitives and returns the UTF-16 caret/selection
/// the caller should re-apply after its binding's reverse sync.
///
/// `flatText` is the widget's toPlainText() — which IS
/// widgetFlatView() decoded — at the time of the call; it must be
/// re-fetched between consecutive ops (each op changes the view).
/// A `std::nullopt` return means no edit was performed and the caller
/// must leave its cursor untouched (matching the donor's early-return
/// paths, which never called setTextCursor — e.g. a setHeadingLevel
/// no-op must not collapse an existing selection).
namespace FormatOps {

/// UTF-16 positions over widgetFlatView. start == end is a caret.
struct QtRange { int start = 0; int end = 0; };

MARKOFF_CORE_EXPORT std::optional<QtRange> wrapToggle(
        MarkoffDocument *doc,
        const QString &flatText,
        QtRange sel,
        const QByteArray &delim);   // ** _ ~~ `
MARKOFF_CORE_EXPORT std::optional<QtRange> insertLink(
        MarkoffDocument *doc,
        const QString &flatText,
        QtRange sel);
MARKOFF_CORE_EXPORT std::optional<QtRange> setHeadingLevel(
        MarkoffDocument *doc,
        const QString &flatText,
        int caretQtPos, int level);

/// Per-block, UTF-8-byte-offset counterpart of the flat ops above (canvas
/// production plan P4.3): same algorithms — surrounded-outside/inside-
/// markers/wrap detection for `wrapToggleInBlock`, the `[](url)`
/// templating for `insertLinkInBlock`, the `#`-prefix counting/no-op
/// rules for `setHeadingLevelInBlock` — but scoped to ONE block's own
/// buffer, with no flat/`widgetFlatView` resolution step at all. This is
/// the seam markoff-canvas's C4 constitution (no flat/global byte
/// offsets, no cross-block byte arithmetic) requires: the caller already
/// knows which block and what byte range within it, so there is nothing
/// left to resolve. A selection that spans multiple canvas blocks is the
/// CALLER's concern — invoke the relevant op once per covered block, each
/// with that block's own local byte sub-range; these functions never see
/// more than one block and never sum bytes across blocks.
///
/// `start == end` is a caret (no selection) for `sel`. A `std::nullopt`
/// return means no edit was performed (same contract as the flat
/// versions) — e.g. an unknown/null `block`, or a `setHeadingLevelInBlock`
/// no-op (already at the requested level).

/// UTF-8 byte positions within a single block's own buffer. `start == end`
/// is a caret.
struct ByteRange { int start = 0; int end = 0; };

MARKOFF_CORE_EXPORT std::optional<ByteRange> wrapToggleInBlock(
        MarkoffDocument *doc,
        BlockId block,
        ByteRange sel,
        const QByteArray &delim);   // ** _ ~~ `

MARKOFF_CORE_EXPORT std::optional<ByteRange> insertLinkInBlock(
        MarkoffDocument *doc,
        BlockId block,
        ByteRange sel);

/// Returns the new caret byte offset (always a caret — headings never
/// carry a restored selection, matching the flat version).
MARKOFF_CORE_EXPORT std::optional<int> setHeadingLevelInBlock(
        MarkoffDocument *doc,
        BlockId block,
        int caretByteOffset, int level);

}  // namespace FormatOps
}  // namespace Markoff
