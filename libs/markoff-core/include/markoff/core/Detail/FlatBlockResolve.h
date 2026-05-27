// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <QList>
#include <markoff/core/BlockId.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Detail {

struct BlockHit {
    Markoff::BlockId blockId;
    quint32          byteInBlock;
    int              blockIndex;
};

/// Resolve a separator-view byte offset to (blockId, byteInBlock). When
/// sepOff lands exactly at a block boundary, biasForward=true picks the next
/// block's start; biasForward=false picks the previous block's end.
std::optional<BlockHit> findBlockAtSepByte(const Markoff::MarkoffDocument *doc,
                                           quint32 sepOff,
                                           bool biasForward);

struct BlockSlice {
    Markoff::BlockId blockId;
    quint32          byteLo;   // inclusive
    quint32          byteHi;   // exclusive
};

/// Slice a sep-view byte range [sepLo, sepHi) into per-block sub-ranges.
/// Empty ranges (sepLo == sepHi) yield no slices.
QList<BlockSlice> sliceByBlocks(const Markoff::MarkoffDocument *doc,
                                quint32 sepLo, quint32 sepHi);

}  // namespace Markoff::Detail
