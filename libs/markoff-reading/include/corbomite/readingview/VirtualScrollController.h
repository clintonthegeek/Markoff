// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_VIRTUALSCROLLCONTROLLER_H
#define CORBOMITE_READINGVIEW_VIRTUALSCROLLCONTROLLER_H

#include <QObject>
#include <QSet>
#include <QVector>
#include <functional>
#include <memory>

class QGraphicsItem;

namespace Corbomite::ReadingView {

class ReadingSection;

/// Phase 6 — viewport-window mount/unmount driver.
///
/// The controller owns the "which sections are currently on the scene"
/// state. Given a viewport position + height, it computes the selection
/// window as:
///
///     windowTop    = viewportTop - viewportHeight
///     windowBottom = viewportTop + 2 * viewportHeight
///
/// Every section whose [yPos, yPos + estimatedOrActual) rect intersects
/// [windowTop, windowBottom) is "desired". The delta between the desired
/// set and the currently-mounted set drives the `layoutOne`/`releaseOne`
/// callbacks.
///
/// **Hidden sections are ignored.** A section whose `hidden()` flag is
/// true is never mounted, regardless of position. The fold engine in
/// `ReadingView` sets this flag before asking the controller to
/// re-evaluate; the controller treats such sections as absent.
///
/// The controller does NOT own the sections or the graphics items — both
/// are caller-owned (ReadingView + the recycle pool respectively). The
/// callbacks are the sole bridge.
class VirtualScrollController : public QObject
{
    Q_OBJECT
public:
    struct LayoutCallbacks {
        /// Lay out section at `sectionIdx`. Returns a QGraphicsItem that the
        /// controller will then mount to the scene. Implementation may reuse
        /// a pooled item or build fresh. The callback is responsible for
        /// setting `section.graphicsItem()`.
        std::function<QGraphicsItem *(int sectionIdx)> layoutOne;

        /// Release the section's QGraphicsItem — remove from scene, offer to
        /// the recycle pool. Implementation clears `section.graphicsItem()`.
        std::function<void(int sectionIdx, QGraphicsItem *item)> releaseOne;
    };

    explicit VirtualScrollController(QObject *parent = nullptr);
    ~VirtualScrollController() override;

    void setCallbacks(LayoutCallbacks callbacks);

    /// Replace the section list. Resets mounted state (caller is
    /// responsible for having released any previously-mounted items before
    /// this call — the controller just drops its bookkeeping).
    void setSections(QVector<std::shared_ptr<ReadingSection>> sections);

    /// Re-evaluate the mounted set against the window defined by
    /// `viewportTop` + `viewportHeight`. Mounts newly-desired sections,
    /// unmounts newly-undesired ones.
    void updateMounted(qreal viewportTop, qreal viewportHeight);

    /// Force `sectionIdx` to unmount (if mounted) and remount on the next
    /// `updateMounted` — used after an edit changes a section's shape.
    void remountSection(int sectionIdx);

    /// Accessors for tests.
    QSet<int> mountedIndices() const { return m_mounted; }
    int mountedCount() const { return m_mounted.size(); }

Q_SIGNALS:
    void mountedChanged();

private:
    QVector<std::shared_ptr<ReadingSection>> m_sections;
    QSet<int> m_mounted;
    LayoutCallbacks m_callbacks;

    // Cached last window so `remountSection` + fold transitions can
    // re-evaluate with the current viewport without needing another
    // callsite to remember it.
    qreal m_lastViewportTop = 0.0;
    qreal m_lastViewportHeight = 0.0;
    bool m_hasLastWindow = false;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_VIRTUALSCROLLCONTROLLER_H
