// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>
#include <QPoint>
#include <qqmlintegration.h>

namespace Markoff::Live {

class LiveCursorState;

/// Cross-block selection for the live render view — stateless facade.
///
/// All selection state is owned by LiveCursorState. This class forwards
/// every call through m_cursorState and re-emits selectionChanged so that
/// QML bindings that depend on anchorBlock / activeBlock / hasSelection
/// keep working unchanged.
///
/// Write path from QML: begin(block, qtPos) / extend(block, qtPos) / clear().
/// Read path for rendering: rangeForBlock(n) → QPoint(start, end) or (-1,-1).
/// The `end` component may be INT32_MAX ("to end of block") — consumers must
/// clamp via Math.min(r.y, textEdit.length) before calling TextEdit.select.
class MARKOFF_LIVE_EXPORT LiveSelectionView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveSelectionView is provided by LiveListModelBinding")

    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit LiveSelectionView(QObject *parent = nullptr);

    /// Wire the canonical store. Must be called before any write operations.
    void setCursorState(LiveCursorState *cs) { m_cursorState = cs; }

    bool hasSelection() const;

    Q_INVOKABLE void begin(int blockIndex, int qtPos);
    Q_INVOKABLE void extend(int blockIndex, int qtPos);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deleteSelection();

    /// Returns QPoint(start, end) for the block, or QPoint(-1,-1) if untouched.
    /// end may be INT32_MAX — consumers must clamp to textEdit.length.
    Q_INVOKABLE QPoint rangeForBlock(int blockIndex) const;

    /// Copy the current selection to the system clipboard. Reads block
    /// texts directly from the bound LiveBlockModel via LiveCursorState.
    Q_INVOKABLE void copyToClipboard() const;

    // Accessors used by LiveClipboardController to compute paste byte offsets,
    // by LiveNavigationController to detect cross-block extension start, and
    // by QML delegates' selection-sync path.
    Q_INVOKABLE int anchorBlock() const;
    Q_INVOKABLE int anchorQtPos() const;
    Q_INVOKABLE int activeBlock() const;
    Q_INVOKABLE int activeQtPos() const;

Q_SIGNALS:
    void selectionChanged();

private:
    LiveCursorState *m_cursorState = nullptr;
};

}  // namespace Markoff::Live
