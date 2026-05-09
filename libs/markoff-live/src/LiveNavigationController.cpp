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

int LiveNavigationController::tryHandle(int key, int modifiers,
                                        int blockIndex, int qtPos,
                                        QObject *editItem,
                                        const QString &blockText) {
    if (modifiers != Qt::NoModifier) return NotHandled;

    if (key == Qt::Key_Up) {
        if (!isAtVisualTopLine(editItem)) return NotHandled;
        qreal desiredX = m_cursorState->desiredVisualX();
        if (desiredX < 0) {
            const QVariant rectV = editItem ? editItem->property("cursorRectangle") : QVariant();
            desiredX = rectV.canConvert<QRectF>() ? rectV.toRectF().x() : 0.0;
            m_cursorState->setDesiredVisualX(desiredX);
        }
        const int targetRow = previousNavigableRow(blockIndex);
        if (targetRow < 0) return Handled;
        m_cursorState->requestTextCaretAtRowVisualX(
            targetRow, LiveCursorState::VisualLineHint::LastLine);
        return Handled;
    }

    if (key == Qt::Key_Down) {
        if (!isAtVisualBottomLine(editItem)) return NotHandled;
        qreal desiredX = m_cursorState->desiredVisualX();
        if (desiredX < 0) {
            const QVariant rectV = editItem ? editItem->property("cursorRectangle") : QVariant();
            desiredX = rectV.canConvert<QRectF>() ? rectV.toRectF().x() : 0.0;
            m_cursorState->setDesiredVisualX(desiredX);
        }
        const int targetRow = nextNavigableRow(blockIndex);
        if (targetRow < 0) return Handled;
        m_cursorState->requestTextCaretAtRowVisualX(
            targetRow, LiveCursorState::VisualLineHint::FirstLine);
        return Handled;
    }

    if (key == Qt::Key_Left) {
        if (qtPos > 0) return NotHandled;
        m_cursorState->clearDesiredVisualX();
        const int targetRow = previousNavigableRow(blockIndex);
        if (targetRow < 0) return Handled;
        const int targetLen = m_model->recordAt(targetRow).text.length();
        m_cursorState->requestTextCaretAtRow(targetRow, targetLen);
        return Handled;
    }

    if (key == Qt::Key_Right) {
        if (qtPos < blockText.length()) return NotHandled;
        m_cursorState->clearDesiredVisualX();
        const int targetRow = nextNavigableRow(blockIndex);
        if (targetRow < 0) return Handled;
        m_cursorState->requestTextCaretAtRow(targetRow, 0);
        return Handled;
    }

    return NotHandled;
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
