// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveContextMenuHandler.h>

#include <markoff/live/LiveActionController.h>
#include <markoff/core/MarkoffDocument.h>

#include <QAction>
#include <QMenu>
#include <QPoint>

namespace Markoff::Live {

LiveContextMenuHandler::LiveContextMenuHandler(QObject *parent)
    : QObject(parent)
    , m_menu(std::make_unique<QMenu>())
{
    m_undoBlockAction = new QAction(tr("Undo in this block"), this);
    m_undoBlockAction->setEnabled(false);
    QObject::connect(m_undoBlockAction, &QAction::triggered, this, [this]() {
        if (m_document) m_document->undoForBlock(m_currentAnchor);
    });
}

LiveContextMenuHandler::~LiveContextMenuHandler() = default;

void LiveContextMenuHandler::setActionController(LiveActionController *ac)
{
    if (m_actionController == ac) return;
    m_actionController = ac;
    buildMenu();
}

void LiveContextMenuHandler::setDocument(Markoff::MarkoffDocument *doc)
{
    m_document = doc;
}

void LiveContextMenuHandler::buildMenu()
{
    m_menu->clear();
    if (!m_actionController) return;

    m_menu->addAction(m_actionController->cutAction());
    m_menu->addAction(m_actionController->copyAction());
    m_menu->addAction(m_actionController->pasteAction());
    m_menu->addAction(m_actionController->selectAllAction());
    m_menu->addSeparator();
    m_menu->addAction(m_actionController->undoAction());
    m_menu->addAction(m_actionController->redoAction());
    m_menu->addSeparator();
    m_menu->addAction(m_undoBlockAction);
}

void LiveContextMenuHandler::popup(qreal globalX, qreal globalY,
                                   const QVariant &blockAnchor)
{
    if (blockAnchor.isValid() && blockAnchor.canConvert<Markoff::BlockAnchor>())
        m_currentAnchor = blockAnchor.value<Markoff::BlockAnchor>();
    else
        m_currentAnchor = Markoff::BlockAnchor{};

    const bool canUndo = m_document && m_document->canUndoForBlock(m_currentAnchor);
    m_undoBlockAction->setEnabled(canUndo);

    m_menu->popup(QPoint(static_cast<int>(globalX), static_cast<int>(globalY)));
}

}  // namespace Markoff::Live
