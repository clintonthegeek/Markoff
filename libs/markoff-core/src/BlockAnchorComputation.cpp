// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockAnchorComputation.h"

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-parser/Document.h>

namespace Markoff::Detail {

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                      const Markoff::Document *parsed)
{
    BlockAnchorBundle bundle;
    if (!parsed) return bundle;

    const QList<Markoff::TopLevelBlock> blocks = parsed->topLevelBlocks();

    // Translate body-relative byte offsets to full-source coordinates,
    // which is what the CRDT buffer (and `textAnchorAt`) operate in.
    // `topLevelBlocks()` returns offsets in the post-frontmatter body;
    // the foundation's CRDT buffer holds the full source including
    // any frontmatter prefix.
    int frontmatterBytes = 0;
    if (const auto fmSpan = parsed->frontmatterSpan(); fmSpan.has_value()) {
        frontmatterBytes = fmSpan->second;
    }

    bundle.anchors.reserve(blocks.size());
    bundle.ranges.reserve(blocks.size());
    for (const Markoff::TopLevelBlock &b : blocks) {
        BlockByteRange r;
        r.startByte = static_cast<quint32>(b.byteStart + frontmatterBytes);
        r.endByte   = static_cast<quint32>(b.byteEnd   + frontmatterBytes);
        bundle.ranges.append(r);

        const TextAnchor t = doc.textAnchorAt(r.startByte, /*rightBias*/ false);
        bundle.anchors.append(BlockId::fromRaw(t.charValue()));
    }
    return bundle;
}

}  // namespace Markoff::Detail
