// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff/core/BlockAnchor.h>

namespace Markoff {
class MarkoffDocument;
class Document;
}  // namespace Markoff

namespace Markoff::Detail {

/// Half-open byte range [startByte, endByte) in *full-source* UTF-8
/// coordinates (i.e. with frontmatter offset already applied so the
/// values are usable directly with `MarkoffDocument::textAnchorAt`).
struct BlockByteRange {
    quint32 startByte = 0;
    quint32 endByte   = 0;
};

/// For each top-level block in `parsed` (as enumerated by
/// `Markoff::Document::topLevelBlocks()`), produce a BlockAnchor at its
/// first byte (Left bias), using `doc` as the CRDT-buffer source for
/// anchor lookup.
///
/// `topLevelBlocks()` returns byte ranges in the *body* (post-
/// frontmatter) coordinate space; this routine translates them into
/// the full-source coordinate space the CRDT buffer uses by adding
/// the frontmatter span's end byte. Ranges in the returned bundle are
/// in full-source coordinates.
///
/// The enumeration is identical to the one view-qml's `BlockWalker`
/// consumes, so the foundation's `blockAnchors[i]` and view-qml's
/// `BlockRecord[i]` describe the same block. Misalignment between the
/// two used to corrupt block-anchored typing (typing into row N
/// writing bytes at the offset of a different block) — see the bug
/// notes in the C-7 task brief.
struct BlockAnchorBundle {
    QList<BlockAnchor>    anchors;
    QList<BlockByteRange> ranges;
};

BlockAnchorBundle computeBlockAnchors(const MarkoffDocument &doc,
                                      const Markoff::Document *parsed);

}  // namespace Markoff::Detail
