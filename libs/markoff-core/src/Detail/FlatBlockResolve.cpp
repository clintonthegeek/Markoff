// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/MarkoffDocument.h>

#include <algorithm>

namespace Markoff::Detail {

std::optional<BlockHit> findBlockAtSepByte(const Markoff::MarkoffDocument *doc,
                                           quint32 sepOff,
                                           bool biasForward) {
    const auto blocks = doc->iterateBlocks();
    if (blocks.empty()) return std::nullopt;
    constexpr quint32 SEP_LEN = 2;
    quint32 sepCursor = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const quint32 sz = static_cast<quint32>(doc->blockText(blocks[i]).size());
        const quint32 blkEnd = sepCursor + sz;
        if (sepOff < blkEnd) {
            return BlockHit{blocks[i], sepOff - sepCursor, static_cast<int>(i)};
        }
        if (sepOff == blkEnd) {
            if (!biasForward || i + 1 == blocks.size()) {
                return BlockHit{blocks[i], sz, static_cast<int>(i)};
            }
            const size_t next = i + 1;
            return BlockHit{blocks[next], 0, static_cast<int>(next)};
        }
        sepCursor = blkEnd;
        if (i + 1 < blocks.size()) sepCursor += SEP_LEN;
    }
    return std::nullopt;
}

QList<BlockSlice> sliceByBlocks(const Markoff::MarkoffDocument *doc,
                                quint32 sepLo, quint32 sepHi) {
    QList<BlockSlice> out;
    if (sepLo >= sepHi) return out;
    const auto blocks = doc->iterateBlocks();
    constexpr quint32 SEP_LEN = 2;
    quint32 sepCursor = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const quint32 sz = static_cast<quint32>(doc->blockText(blocks[i]).size());
        const quint32 blkEnd = sepCursor + sz;
        const quint32 sLo = std::max(sepLo, sepCursor);
        const quint32 sHi = std::min(sepHi, blkEnd);
        if (sLo < sHi) {
            out.append({blocks[i], sLo - sepCursor, sHi - sepCursor});
        }
        sepCursor = blkEnd;
        if (i + 1 < blocks.size()) sepCursor += SEP_LEN;
        if (sepCursor >= sepHi) break;
    }
    return out;
}

}  // namespace Markoff::Detail
