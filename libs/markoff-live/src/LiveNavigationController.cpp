// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveNavigationController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKindRegistry.h>

#include <QRectF>

namespace Markoff::Live {

LiveNavigationController::LiveNavigationController(
    const BlockKindRegistry *registry, LiveBlockModel *model,
    LiveCursorState *cursorState, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_model(model)
    , m_cursorState(cursorState)
{
}

int LiveNavigationController::tryHandle(int /*key*/, int /*modifiers*/,
                                        int /*blockIndex*/, int /*qtPos*/,
                                        QObject * /*editItem*/,
                                        const QString & /*blockText*/) {
    return NotHandled;  // Phase D stub. Phase E fills in arrow handlers.
}

int LiveNavigationController::previousNavigableRow(int currentRow) const {
    return currentRow > 0 ? currentRow - 1 : -1;
}

int LiveNavigationController::nextNavigableRow(int currentRow) const {
    if (!m_model) return -1;
    return currentRow + 1 < m_model->rowCount() ? currentRow + 1 : -1;
}

bool LiveNavigationController::isAtVisualTopLine(QObject *editItem) const {
    if (!editItem) return false;
    const QVariant rectV = editItem->property("cursorRectangle");
    if (!rectV.canConvert<QRectF>()) return false;
    const QRectF cursorRect = rectV.toRectF();
    return cursorRect.y() < cursorRect.height() * 0.5;
}

bool LiveNavigationController::isAtVisualBottomLine(QObject *editItem) const {
    if (!editItem) return false;
    const QVariant rectV = editItem->property("cursorRectangle");
    if (!rectV.canConvert<QRectF>()) return false;
    const QRectF cursorRect = rectV.toRectF();
    bool ok = false;
    const qreal contentH = editItem->property("contentHeight").toReal(&ok);
    if (!ok || contentH <= 0) return false;
    return cursorRect.bottom() > contentH - cursorRect.height() * 0.5;
}

}  // namespace Markoff::Live
