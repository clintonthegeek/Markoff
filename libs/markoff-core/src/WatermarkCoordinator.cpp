// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/WatermarkCoordinator.h>
#include <markoff/core/MarkoffDocument.h>
#include <QSet>

// Full Markoff::Document (parser type) must be complete before MarkoffDocumentPrivate.h
// instantiates std::unique_ptr<const Markoff::Document>'s destructor.
#include <markoff/parser/Document.h>

#include "MarkoffDocumentPrivate.h"

namespace Markoff {

WatermarkCoordinator::WatermarkCoordinator(MarkoffDocument &doc)
    : m_doc(doc)
{}

bool WatermarkCoordinator::onSaveSucceeded()
{
    if (m_doc.d->undoLog.isTransactionOpen()) return false;
    advanceAndCompact();
    disposeOrphans();
    return true;
}

void WatermarkCoordinator::advanceAndCompact()
{
    auto &d = *m_doc.d;

    // Compact per-block Buffers (remove tombstones observed by this replica)
    for (auto &[id, buf] : d.blockBuffers)
        buf->compact(buf->version());

    // Compact IdList
    d.idList.compact(d.idList.version());

    // Compact CausalLwwMaps — pass current stamp as watermark
    auto kmStamp = d.kindTagMap.currentStamp();
    d.kindTagMap.compact(kmStamp);

    auto baStamp = d.blockAttrsMap.currentStamp();
    d.blockAttrsMap.compact(baStamp);

    auto fmStamp = d.frontmatterMap.currentStamp();
    d.frontmatterMap.compact(fmStamp);

    auto lrStamp = d.linkRefMap.currentStamp();
    d.linkRefMap.compact(lrStamp);

    auto fdStamp = d.footnoteDefMap.currentStamp();
    d.footnoteDefMap.compact(fdStamp);

    // Discard all undo entries — all ops have been observed and compacted
    d.undoLog.compact([](const UndoCrdtTarget &, OpId) { return true; });

    // Snapshot post-compact state directly (D2 single-user: idListVersion stays 0;
    // Crdt::Global is a vector type and doesn't map cleanly to quint64)
    m_watermark.kindTagMapSeq  = kmStamp.counter;
    m_watermark.blockAttrsSeq  = baStamp.counter;
    m_watermark.frontmatterSeq = fmStamp.counter;
    m_watermark.linkRefSeq     = lrStamp.counter;
    m_watermark.footnoteDefSeq = fdStamp.counter;
}

void WatermarkCoordinator::disposeOrphans()
{
    auto &d = *m_doc.d;
    auto liveIds = m_doc.iterateBlocks();
    QSet<BlockId> liveSet(liveIds.begin(), liveIds.end());

    for (auto it = d.blockBuffers.begin(); it != d.blockBuffers.end(); ) {
        BlockId id = it->first;
        if (!liveSet.contains(id) && !d.undoLog.isBlockReferenced(id)) {
            // Remove all per-block housekeeping data
            d.bufferProxies.remove(id);
            d.blockLoadTimeBytes.remove(id);
            d.blockEditSequences.remove(id);
            it = d.blockBuffers.erase(it);
        } else {
            ++it;
        }
    }
}

bool WatermarkCoordinator::compactNow()
{
    if (m_doc.d->undoLog.isTransactionOpen()) return false;
    advanceAndCompact();
    disposeOrphans();
    return true;
}

Watermark WatermarkCoordinator::currentWatermark() const
{
    return m_watermark;
}

}  // namespace Markoff
