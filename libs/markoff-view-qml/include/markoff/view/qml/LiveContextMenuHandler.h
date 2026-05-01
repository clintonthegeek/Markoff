// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <memory>
#include <qqmlintegration.h>

#include <markoff/view/qml/LiveSelectionView.h>
#include <markoff/view/qml/LiveClipboardController.h>

class QMenu;

namespace Markoff::View::Qml {

/// KDAB-pattern Widget-window bridge for the right-click context menu.
/// Owns a QMenu that shows as a top-level OS window when popup() is invoked.
/// v0 actions: Copy, Select All. The menu is created with no parent so it
/// shows as a real native menu independent of the QQuickWindow.
class LiveContextMenuHandler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LiveSelectionView *selectionModel
               READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(QStringList blockTexts
               READ blockTexts WRITE setBlockTexts NOTIFY blockTextsChanged)
    Q_PROPERTY(int blockCount
               READ blockCount WRITE setBlockCount NOTIFY blockCountChanged)
    Q_PROPERTY(LiveClipboardController *clipboardController
               READ clipboardController WRITE setClipboardController
               NOTIFY clipboardControllerChanged)

public:
    explicit LiveContextMenuHandler(QObject *parent = nullptr);
    ~LiveContextMenuHandler() override;

    LiveSelectionView *selectionModel() const { return m_selection; }
    void setSelectionModel(LiveSelectionView *m);

    QStringList blockTexts() const { return m_blockTexts; }
    void setBlockTexts(const QStringList &t);

    int blockCount() const { return m_blockCount; }
    void setBlockCount(int n);

    LiveClipboardController *clipboardController() const { return m_clipboard; }
    void setClipboardController(LiveClipboardController *c);

    /// Pop the menu at the given GLOBAL screen coordinates.
    Q_INVOKABLE void popup(QPoint globalPos);

Q_SIGNALS:
    void selectionModelChanged();
    void blockTextsChanged();
    void blockCountChanged();
    void clipboardControllerChanged();

private:
    LiveSelectionView      *m_selection = nullptr;
    QStringList             m_blockTexts;
    int                     m_blockCount = 0;
    LiveClipboardController *m_clipboard = nullptr;
    std::unique_ptr<QMenu>  m_menu;
};

}  // namespace Markoff::View::Qml
