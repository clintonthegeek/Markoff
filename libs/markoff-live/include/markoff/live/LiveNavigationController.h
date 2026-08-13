// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>
#include <Qt>
#include <qqmlintegration.h>

class QQuickItem;

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;
class BlockKindRegistry;

/// Cross-block keyboard navigation. Sibling to LiveStructuralKeyHandler.
/// Handles cursor motion across blocks. Spec §2.2 / §3.2.
class MARKOFF_LIVE_EXPORT LiveNavigationController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveNavigationController is provided by LiveListModelBinding")

public:
    enum HandleResult { NotHandled = 0, Handled = 1 };
    Q_ENUM(HandleResult)

    explicit LiveNavigationController(const BlockKindRegistry *registry,
                                      LiveBlockModel          *model,
                                      LiveCursorState         *cursorState,
                                      QObject                 *parent = nullptr);

    Q_INVOKABLE int tryHandle(int key, int modifiers,
                              int blockIndex, int qtPos,
                              QObject *editItem,
                              const QString &blockText);

    Q_INVOKABLE void setListView(QObject *listView);

    int previousNavigableRow(int currentRow) const;
    int nextNavigableRow(int currentRow) const;

    /// Nearest text-bearing row to `fromRow` (which may be out of range or
    /// itself non-text-bearing) — checks `fromRow` first, then alternates
    /// outward (fromRow-1, fromRow+1, fromRow-2, ...). Returns -1 if no
    /// text-bearing row exists at all. Used to re-anchor the caret when the
    /// previously-focused block disappears out from under it (e.g. undo
    /// removing the block a structural edit had created — queue #10).
    int nearestTextBearingRow(int fromRow) const;

private:
    QObject *m_listView = nullptr;
    bool isAtVisualTopLine(QObject *editItem) const;
    bool isAtVisualBottomLine(QObject *editItem) const;
    bool isTextBearing(int row) const;
    int  findFirstTextBearingRow() const;
    int  findLastTextBearingRow() const;

    const BlockKindRegistry *m_registry;
    LiveBlockModel          *m_model;
    LiveCursorState         *m_cursorState;
};

}  // namespace Markoff::Live
