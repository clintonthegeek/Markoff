// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveNavigationController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/BlockKindDescriptor.h>
#include <markoff/live/BlockRecord.h>

#include <QRectF>
#include <QVariant>

#include <algorithm>

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

void LiveNavigationController::setListView(QObject *listView) {
    m_listView = listView;
}

bool LiveNavigationController::isTextBearing(int row) const {
    if (!m_model || !m_registry) return false;
    if (row < 0 || row >= m_model->rowCount()) return false;
    const BlockRecord &rec = m_model->recordAt(row);
    const BlockKindDescriptor *desc = m_registry->find(rec.kind);
    if (!desc) return false;
    return desc->supportedCursorVariants.contains(QStringLiteral("TextCaret"));
}

int LiveNavigationController::findFirstTextBearingRow() const {
    if (!m_model) return -1;
    for (int r = 0; r < m_model->rowCount(); ++r)
        if (isTextBearing(r)) return r;
    return -1;
}

int LiveNavigationController::findLastTextBearingRow() const {
    if (!m_model) return -1;
    for (int r = m_model->rowCount() - 1; r >= 0; --r)
        if (isTextBearing(r)) return r;
    return -1;
}

int LiveNavigationController::tryHandle(int key, int modifiers,
                                        int blockIndex, int qtPos,
                                        QObject *editItem,
                                        const QString &blockText) {
    // Ctrl+Home / Ctrl+End: jump to document start/end
    if (modifiers == Qt::ControlModifier) {
        if (key == Qt::Key_Home) {
            const int firstRow = findFirstTextBearingRow();
            if (firstRow >= 0) {
                m_cursorState->clearDesiredVisualX();
                m_cursorState->requestTextCaretAtRow(firstRow, 0);
            }
            return Handled;
        }
        if (key == Qt::Key_End) {
            if (!m_model) return Handled;
            const int lastRow = findLastTextBearingRow();
            if (lastRow >= 0) {
                const int len = m_model->recordAt(lastRow).text.length();
                m_cursorState->clearDesiredVisualX();
                m_cursorState->requestTextCaretAtRow(lastRow, len);
            }
            return Handled;
        }

        // Ctrl+Left: word-boundary within block handled by TextEdit natively.
        // Cross-block (at pos 0): go to end of prev block.
        if (key == Qt::Key_Left) {
            if (qtPos > 0) return NotHandled;
            m_cursorState->clearDesiredVisualX();
            const int targetRow = previousNavigableRow(blockIndex);
            if (targetRow < 0) return Handled;
            if (!m_model) return Handled;
            const int targetLen = m_model->recordAt(targetRow).text.length();
            m_cursorState->requestTextCaretAtRow(targetRow, targetLen);
            return Handled;
        }

        // Ctrl+Right: word-boundary within block handled by TextEdit natively.
        // Cross-block (at end): go to start of next block.
        if (key == Qt::Key_Right) {
            if (qtPos < blockText.length()) return NotHandled;
            m_cursorState->clearDesiredVisualX();
            const int targetRow = nextNavigableRow(blockIndex);
            if (targetRow < 0) return Handled;
            m_cursorState->requestTextCaretAtRow(targetRow, 0);
            return Handled;
        }
    }

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
        if (!m_model) return Handled;
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

    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        if (!m_listView || !editItem) return NotHandled;
        const QVariant rectV = editItem->property("cursorRectangle");
        if (!rectV.canConvert<QRectF>()) return NotHandled;
        const QRectF cursorRect = rectV.toRectF();

        const qreal viewH = m_listView->property("height").toReal();
        if (viewH <= 0) return NotHandled;

        // editItem is the TextEdit; its parent is the delegate Item root.
        // The delegate Item's y is its position within the ListView content.
        QObject *delegateItem = editItem->parent();
        if (!delegateItem) return NotHandled;
        const qreal delegateY = delegateItem->property("y").toReal();
        const qreal cursorYInView = delegateY + cursorRect.y();
        const qreal cursorXInView = delegateItem->property("x").toReal() + cursorRect.x();

        const qreal targetY = (key == Qt::Key_PageDown)
            ? cursorYInView + viewH
            : cursorYInView - viewH;

        QVariant hitResult;
        QMetaObject::invokeMethod(m_listView, "hit",
            Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, hitResult),
            Q_ARG(double, cursorXInView),
            Q_ARG(double, targetY));

        if (!hitResult.canConvert<QVariantMap>()) return Handled;
        const QVariantMap hitMap = hitResult.toMap();
        const int hitRow = hitMap.value(QStringLiteral("blockIndex"), -1).toInt();
        const int hitQtPos = hitMap.value(QStringLiteral("qtPos"), 0).toInt();
        if (hitRow < 0) return Handled;

        m_cursorState->clearDesiredVisualX();
        if (isTextBearing(hitRow))
            m_cursorState->requestTextCaretAtRow(hitRow, std::max(0, hitQtPos));
        else {
            const int textRow = findFirstTextBearingRow();
            if (textRow >= 0)
                m_cursorState->requestTextCaretAtRow(textRow, 0);
        }
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
