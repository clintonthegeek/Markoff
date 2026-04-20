// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/SectionRecyclePool.h"

#include <QGraphicsItem>
#include <QGraphicsScene>

namespace Corbomite::ReadingView {

namespace {

// Defensive detach: pooled items should live off-scene, but if the caller
// hands us one still parented we clean that up before stashing.
void detachFromScene(QGraphicsItem *item)
{
    if (!item) return;
    if (auto *parent = item->parentItem())
        item->setParentItem(nullptr);
    if (auto *scene = item->scene())
        scene->removeItem(item);
}

} // namespace

SectionRecyclePool::SectionRecyclePool() = default;

SectionRecyclePool::~SectionRecyclePool()
{
    clear();
}

QGraphicsItem *SectionRecyclePool::take(const QByteArray &renderedShape)
{
    if (renderedShape.isEmpty()) return nullptr;
    auto it = m_pool.find(renderedShape);
    if (it == m_pool.end()) return nullptr;
    QGraphicsItem *item = it.value();
    m_pool.erase(it);
    // Defensive — should already be detached, but the caller is going to
    // re-parent/re-scene this in a moment so ensure a clean slate.
    detachFromScene(item);
    return item;
}

void SectionRecyclePool::offer(const QByteArray &renderedShape,
                               QGraphicsItem *item)
{
    if (!item) return;
    if (renderedShape.isEmpty()) {
        // No viable key — we can't re-use it later; drop it.
        detachFromScene(item);
        delete item;
        return;
    }
    detachFromScene(item);
    auto it = m_pool.find(renderedShape);
    if (it != m_pool.end()) {
        // Duplicate key: first-in wins; delete the new offering.
        delete item;
        return;
    }
    m_pool.insert(renderedShape, item);
}

void SectionRecyclePool::clear()
{
    for (auto it = m_pool.begin(); it != m_pool.end(); ++it) {
        QGraphicsItem *item = it.value();
        detachFromScene(item);
        delete item;
    }
    m_pool.clear();
}

} // namespace Corbomite::ReadingView
