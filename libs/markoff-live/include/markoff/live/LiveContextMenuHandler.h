// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/BlockAnchor.h>

#include <QObject>
#include <QVariant>
#include <qqmlintegration.h>

#include <memory>

class QMenu;
class QAction;

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Live {

class LiveActionController;

/// Native QMenu-backed right-click context menu for the live render view.
/// Implements the Widget-bridge pattern from
/// docs/specs/2026-04-29-live-render-design.md §Widget-window bridge:
/// the menu is a QWidget (QMenu) populated from LiveActionController's
/// QActions, popped up at global screen coordinates. Replaces the earlier
/// QtQuick `Menu` (LiveContextMenu.qml) so the right-click affordance works
/// on every platform regardless of QtQuickControls style and so the C++-
/// owned QActions stay the single source of action state.
class MARKOFF_LIVE_EXPORT LiveContextMenuHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveContextMenuHandler is provided by LiveListModelBinding")

public:
    explicit LiveContextMenuHandler(QObject *parent = nullptr);
    ~LiveContextMenuHandler() override;

    void setActionController(LiveActionController *ac);
    void setDocument(Markoff::MarkoffDocument *doc);

    /// Pop up at the given global screen coordinates. `blockAnchor` (a
    /// `Markoff::BlockAnchor` wrapped in a QVariant) scopes the per-block
    /// "Undo in this block" item; pass an invalid QVariant for non-block
    /// contexts and the item will be disabled.
    Q_INVOKABLE void popup(qreal globalX, qreal globalY,
                           const QVariant &blockAnchor);

private:
    void buildMenu();

    std::unique_ptr<QMenu>     m_menu;
    QAction                  *m_undoBlockAction = nullptr;
    LiveActionController     *m_actionController = nullptr;
    Markoff::MarkoffDocument *m_document = nullptr;
    Markoff::BlockAnchor      m_currentAnchor;
};

}  // namespace Markoff::Live
