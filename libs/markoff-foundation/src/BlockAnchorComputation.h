// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff-foundation/BlockAnchor.h>
#include "TopLevelBlockScanner.h"

namespace Markoff {
class MarkoffDocument;
}  // namespace Markoff

namespace Markoff::Detail {

/// For each top-level block in `body`, produce a BlockAnchor at its
/// first byte (Left bias), using `doc` as the CRDT-buffer source for
/// anchor lookup. Ranges are returned in parallel for use by
/// MarkoffDocument's blockAt / offsetInBlock / blockByteRange APIs
/// (added in Task 8).
struct BlockAnchorBundle {
    QList<BlockAnchor>    anchors;
    QList<BlockByteRange> ranges;
};

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                       const QByteArray &body);

}  // namespace Markoff::Detail
