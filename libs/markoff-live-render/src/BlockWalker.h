// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <markoff/live-render/BlockRecord.h>

namespace Markoff { class Document; }

namespace Markoff::LiveRender {

/// Convert a parsed `Markoff::Document` into a flat list of `BlockRecord`s
/// in document order. Reads `Document::topLevelBlocks()` (no re-parsing)
/// and `Document::markdownContent()` for the body text. Populates
/// `BlockRecord::inlineSpans` from `TopLevelBlock::inlineSpans` (R1B).
/// `blockAnchor` is left default-constructed; LiveListModelBinding fills it
/// in from the `blockAnchors` list received via `parseUpdated`.
class BlockWalker {
public:
    static QList<BlockRecord> walk(const Markoff::Document *parsed);
};

}  // namespace Markoff::LiveRender
