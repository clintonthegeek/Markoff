// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/VirtualScrollController.h"

#include "corbomite/readingview/ReadingSection.h"

#include <QGraphicsItem>

namespace Corbomite::ReadingView {

VirtualScrollController::VirtualScrollController(QObject *parent)
    : QObject(parent)
{
}

VirtualScrollController::~VirtualScrollController() = default;

void VirtualScrollController::setCallbacks(LayoutCallbacks callbacks)
{
    m_callbacks = std::move(callbacks);
}

void VirtualScrollController::setSections(
    QVector<std::shared_ptr<ReadingSection>> sections)
{
    m_sections = std::move(sections);
    m_mounted.clear();
    m_hasLastWindow = false;
}

void VirtualScrollController::updateMounted(qreal viewportTop,
                                            qreal viewportHeight)
{
    m_lastViewportTop = viewportTop;
    m_lastViewportHeight = viewportHeight;
    m_hasLastWindow = true;

    // Window formula from the Phase 6 plan: viewport ± 1× viewport height.
    // Degenerate-viewport guard: a zero-height viewport still gets a small
    // window so tests that never resize still see something mounted.
    qreal vh = viewportHeight;
    if (vh <= 0) vh = 400.0;
    const qreal windowTop = viewportTop - vh;
    const qreal windowBottom = viewportTop + 2.0 * vh;

    QSet<int> desired;
    for (int i = 0; i < m_sections.size(); ++i) {
        const auto &sec = m_sections.at(i);
        if (!sec || sec->hidden()) continue;
        const qreal top = sec->yPos();
        const qreal h = sec->actualHeight() > 0.0
                            ? sec->actualHeight()
                            : sec->estimatedHeight();
        const qreal bottom = top + qMax<qreal>(h, 1.0);
        // Intersects window → desired.
        if (bottom >= windowTop && top <= windowBottom)
            desired.insert(i);
    }

    // Unmount (currently-mounted - desired).
    QSet<int> toUnmount;
    for (int idx : m_mounted) {
        if (!desired.contains(idx))
            toUnmount.insert(idx);
    }
    for (int idx : toUnmount) {
        if (idx < 0 || idx >= m_sections.size()) {
            m_mounted.remove(idx);
            continue;
        }
        const auto &sec = m_sections.at(idx);
        QGraphicsItem *item = sec ? sec->graphicsItem() : nullptr;
        if (m_callbacks.releaseOne && item)
            m_callbacks.releaseOne(idx, item);
        m_mounted.remove(idx);
    }

    // Mount (desired - currently-mounted).
    bool anyChanged = !toUnmount.isEmpty();
    for (int idx : desired) {
        if (m_mounted.contains(idx)) continue;
        if (!m_callbacks.layoutOne) continue;
        QGraphicsItem *item = m_callbacks.layoutOne(idx);
        if (item) {
            m_mounted.insert(idx);
            anyChanged = true;
        }
    }

    if (anyChanged) Q_EMIT mountedChanged();
}

void VirtualScrollController::remountSection(int sectionIdx)
{
    if (sectionIdx < 0 || sectionIdx >= m_sections.size()) return;
    const auto &sec = m_sections.at(sectionIdx);
    QGraphicsItem *item = sec ? sec->graphicsItem() : nullptr;
    if (m_mounted.contains(sectionIdx) && m_callbacks.releaseOne && item)
        m_callbacks.releaseOne(sectionIdx, item);
    m_mounted.remove(sectionIdx);

    if (m_hasLastWindow)
        updateMounted(m_lastViewportTop, m_lastViewportHeight);
}

} // namespace Corbomite::ReadingView
