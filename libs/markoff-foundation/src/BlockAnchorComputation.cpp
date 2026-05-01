// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockAnchorComputation.h"

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::Detail {

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                       const QByteArray &body)
{
    BlockAnchorBundle bundle;
    const QList<BlockByteRange> ranges = scanTopLevelBlockRanges(body);
    bundle.anchors.reserve(ranges.size());
    bundle.ranges = ranges;
    for (const BlockByteRange &r : ranges) {
        const TextAnchor t = doc.textAnchorAt(r.startByte, /*rightBias*/ false);
        bundle.anchors.append(BlockAnchor{t});
    }
    return bundle;
}

}  // namespace Markoff::Detail
