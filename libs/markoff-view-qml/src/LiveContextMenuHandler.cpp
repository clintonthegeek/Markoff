// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveContextMenuHandler.h>

#include <QAction>
#include <QMenu>

namespace Markoff::View::Qml {

LiveContextMenuHandler::LiveContextMenuHandler(QObject *parent)
    : QObject(parent)
    , m_menu(std::make_unique<QMenu>())  // no parent — top-level OS window
{
    QAction *copyAction = m_menu->addAction(tr("Copy"));
    QObject::connect(copyAction, &QAction::triggered, this, [this]() {
        if (m_selection) m_selection->copySelectionToClipboard(m_blockTexts);
    });

    QAction *selectAllAction = m_menu->addAction(tr("Select All"));
    QObject::connect(selectAllAction, &QAction::triggered, this, [this]() {
        if (!m_selection || m_blockCount <= 0) return;
        m_selection->begin(0, 0);
        const int lastIdx = m_blockCount - 1;
        const int lastLen = (lastIdx < m_blockTexts.size())
            ? m_blockTexts.at(lastIdx).size() : 0;
        m_selection->extend(lastIdx, lastLen);
    });

    QAction *cutAction = m_menu->addAction(tr("Cut"));
    QObject::connect(cutAction, &QAction::triggered, this, [this]() {
        if (m_clipboard) m_clipboard->cut();
    });

    QAction *pasteAction = m_menu->addAction(tr("Paste"));
    QObject::connect(pasteAction, &QAction::triggered, this, [this]() {
        if (m_clipboard) m_clipboard->paste();
    });
}

LiveContextMenuHandler::~LiveContextMenuHandler() = default;

void LiveContextMenuHandler::setSelectionModel(LiveSelectionView *m)
{
    if (m_selection == m) return;
    m_selection = m;
    Q_EMIT selectionModelChanged();
}

void LiveContextMenuHandler::setBlockTexts(const QStringList &t)
{
    if (m_blockTexts == t) return;
    m_blockTexts = t;
    Q_EMIT blockTextsChanged();
}

void LiveContextMenuHandler::setBlockCount(int n)
{
    if (m_blockCount == n) return;
    m_blockCount = n;
    Q_EMIT blockCountChanged();
}

void LiveContextMenuHandler::setClipboardController(LiveClipboardController *c)
{
    if (m_clipboard == c) return;
    m_clipboard = c;
    Q_EMIT clipboardControllerChanged();
}

void LiveContextMenuHandler::popup(QPoint globalPos)
{
    m_menu->popup(globalPos);
}

}  // namespace Markoff::View::Qml
