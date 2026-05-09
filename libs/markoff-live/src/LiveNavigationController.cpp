// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveNavigationController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/BlockKindDescriptor.h>
#include <markoff/live/BlockRecord.h>

#include <QRectF>
#include <QVariant>

#include <algorithm>

namespace Markoff::Live {

LiveNavigationController::LiveNavigationController(
    const BlockKindRegistry *registry, LiveBlockModel *model,
    LiveCursorState *cursorState, LiveSelectionView *selectionView,
    QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_selectionView(selectionView)
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
    // Ctrl+Shift+Left/Right: word-extend selection across blocks.
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (key == Qt::Key_Left) {
            if (qtPos > 0) return NotHandled;  // native word-select within block
            m_cursorState->clearDesiredVisualX();
            const int targetRow = previousNavigableRow(blockIndex);
            if (targetRow < 0) return Handled;
            if (!m_model) return Handled;
            const int targetLen = m_model->recordAt(targetRow).text.length();
            if (m_selectionView) m_selectionView->extend(targetRow, targetLen);
            m_cursorState->requestTextCaretAtRow(targetRow, targetLen);
            return Handled;
        }
        if (key == Qt::Key_Right) {
            if (qtPos < blockText.length()) return NotHandled;  // native word-select within block
            m_cursorState->clearDesiredVisualX();
            const int targetRow = nextNavigableRow(blockIndex);
            if (targetRow < 0) return Handled;
            if (m_selectionView) m_selectionView->extend(targetRow, 0);
            m_cursorState->requestTextCaretAtRow(targetRow, 0);
            return Handled;
        }
    }

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

    // ------------------------------------------------------------------------
    // Option B (single source of truth): TextEdit's `selectByMouse` is false
    // and we capture every plain or Shift-modified arrow / Home / End so
    // LiveSelectionView is always authoritative. Within-block motion and
    // selection are also driven through here — the delegate's applySelection()
    // re-renders after each begin/extend, placing TextEdit's caret at the
    // active end via moveCursorSelection (direction-preserving).
    //
    // Shift+arrow extends; plain arrow collapses to caret at target.
    // ------------------------------------------------------------------------
    const bool shift = (modifiers & Qt::ShiftModifier);
    const bool extraNonShiftMods =
        (modifiers & ~static_cast<int>(Qt::ShiftModifier)) != 0;

    if (extraNonShiftMods)
        return NotHandled;  // Ctrl/Alt combos handled above or upstream.

    auto applyMotion = [&](int targetRow, int targetPos,
                           LiveCursorState::VisualLineHint hint
                               = LiveCursorState::VisualLineHint::None) -> int {
        if (!m_selectionView) return Handled;
        if (shift) {
            // Anchor at start position if no selection exists yet (D6).
            if (m_selectionView->anchorBlock() < 0)
                m_selectionView->begin(blockIndex, qtPos);
            m_selectionView->extend(targetRow, targetPos);
        } else {
            // Plain motion: collapse selection to caret at target.
            m_selectionView->begin(targetRow, targetPos);
        }
        // Cross-block always needs a focus + scroll request through the
        // cursorState pipeline. Within-block: applySelection() handles the
        // caret via moveCursorSelection — no cursorState request needed.
        if (targetRow != blockIndex) {
            if (hint == LiveCursorState::VisualLineHint::None)
                m_cursorState->requestTextCaretAtRow(targetRow, targetPos);
            else
                m_cursorState->requestTextCaretAtRowVisualX(targetRow, hint);
        }
        return Handled;
    };

    auto positionAtVisual = [&](double x, double y) -> int {
        if (!editItem) return -1;
        // QML TextEdit's positionAt is `Q_INVOKABLE int positionAt(qreal, qreal)`;
        // Q_RETURN_ARG(int, ...) is the correct match. Q_RETURN_ARG(QVariant, ...)
        // silently fails for non-QVariant return types and yields 0.
        int target = -1;
        QMetaObject::invokeMethod(editItem, "positionAt",
            Qt::DirectConnection, Q_RETURN_ARG(int, target),
            Q_ARG(double, x), Q_ARG(double, y));
        return target;
    };

    auto cursorRect = [&]() -> QRectF {
        const QVariant rectV = editItem ? editItem->property("cursorRectangle")
                                        : QVariant();
        return rectV.canConvert<QRectF>() ? rectV.toRectF() : QRectF{};
    };

    if (key == Qt::Key_Left) {
        m_cursorState->clearDesiredVisualX();
        if (qtPos > 0)
            return applyMotion(blockIndex, qtPos - 1);
        const int prev = previousNavigableRow(blockIndex);
        if (prev < 0) return Handled;
        const int prevLen = m_model->recordAt(prev).text.length();
        return applyMotion(prev, prevLen);
    }

    if (key == Qt::Key_Right) {
        m_cursorState->clearDesiredVisualX();
        if (qtPos < blockText.length())
            return applyMotion(blockIndex, qtPos + 1);
        const int next = nextNavigableRow(blockIndex);
        if (next < 0) return Handled;
        return applyMotion(next, 0);
    }

    if (key == Qt::Key_Home) {
        const QRectF cr = cursorRect();
        if (cr.isEmpty()) return NotHandled;
        m_cursorState->clearDesiredVisualX();
        const int target = positionAtVisual(0.0, cr.y());
        return applyMotion(blockIndex, target < 0 ? 0 : target);
    }

    if (key == Qt::Key_End) {
        const QRectF cr = cursorRect();
        if (cr.isEmpty()) return NotHandled;
        m_cursorState->clearDesiredVisualX();
        const qreal w = editItem->property("width").toReal();
        const int target = positionAtVisual(w - 1.0, cr.y());
        return applyMotion(blockIndex, target < 0 ? blockText.length() : target);
    }

    if (key == Qt::Key_Up) {
        const QRectF cr = cursorRect();
        if (cr.isEmpty()) return NotHandled;
        qreal desiredX = m_cursorState->desiredVisualX();
        if (desiredX < 0) {
            desiredX = cr.x();
            m_cursorState->setDesiredVisualX(desiredX);
        }
        // Try within-block visual-line up.
        const qreal targetY = cr.y() - cr.height() * 0.5;
        if (targetY >= 0) {
            const int target = positionAtVisual(desiredX, targetY);
            if (target >= 0 && target != qtPos)
                return applyMotion(blockIndex, target);
        }
        // Already at top visual line → cross-block.
        const int prev = previousNavigableRow(blockIndex);
        if (prev < 0) return Handled;
        return applyMotion(prev, 0,
            LiveCursorState::VisualLineHint::LastLine);
    }

    if (key == Qt::Key_Down) {
        const QRectF cr = cursorRect();
        if (cr.isEmpty()) return NotHandled;
        qreal desiredX = m_cursorState->desiredVisualX();
        if (desiredX < 0) {
            desiredX = cr.x();
            m_cursorState->setDesiredVisualX(desiredX);
        }
        bool ok = false;
        const qreal contentH = editItem->property("contentHeight").toReal(&ok);
        const qreal targetY = cr.bottom() + cr.height() * 0.5;
        if (ok && contentH > 0 && targetY < contentH) {
            const int target = positionAtVisual(desiredX, targetY);
            if (target >= 0 && target != qtPos)
                return applyMotion(blockIndex, target);
        }
        const int next = nextNavigableRow(blockIndex);
        if (next < 0) return Handled;
        return applyMotion(next, 0,
            LiveCursorState::VisualLineHint::FirstLine);
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
    for (int r = currentRow - 1; r >= 0; --r)
        if (isTextBearing(r)) return r;
    return -1;
}

int LiveNavigationController::nextNavigableRow(int currentRow) const {
    if (!m_model) return -1;
    for (int r = currentRow + 1; r < m_model->rowCount(); ++r)
        if (isTextBearing(r)) return r;
    return -1;
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
