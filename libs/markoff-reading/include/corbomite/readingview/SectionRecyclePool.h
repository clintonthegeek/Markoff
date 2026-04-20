// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_SECTIONRECYCLEPOOL_H
#define CORBOMITE_READINGVIEW_SECTIONRECYCLEPOOL_H

#include <QByteArray>
#include <QHash>

class QGraphicsItem;

namespace Corbomite::ReadingView {

/// Pool of unmounted, already-rendered `QGraphicsItem` subtrees keyed by
/// the `ReadingSection::renderedShape()` SHA-256 digest.
///
/// Ownership: the pool owns items stored in it. `take` transfers ownership
/// to the caller; `offer` transfers ownership to the pool. Items offered
/// with a shape-key that is already present are deleted on the spot (first
/// -in wins — LRU is overkill for Phase 4). The destructor deletes all
/// remaining pooled items.
class SectionRecyclePool
{
public:
    SectionRecyclePool();
    ~SectionRecyclePool();

    SectionRecyclePool(const SectionRecyclePool &) = delete;
    SectionRecyclePool &operator=(const SectionRecyclePool &) = delete;

    /// Return a pooled item matching `renderedShape`, or `nullptr`. The
    /// caller assumes ownership; the hash entry is removed.
    QGraphicsItem *take(const QByteArray &renderedShape);

    /// Hand ownership of `item` to the pool. If an entry with the same
    /// shape already exists, the newly-offered `item` is deleted. If
    /// `item` is parented to a scene the pool first detaches it.
    void offer(const QByteArray &renderedShape, QGraphicsItem *item);

    /// Drop everything. Called on vault close, theme change, etc.
    void clear();

    int size() const { return m_pool.size(); }

private:
    QHash<QByteArray, QGraphicsItem *> m_pool;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_SECTIONRECYCLEPOOL_H
