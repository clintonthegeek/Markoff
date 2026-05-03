// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff-foundation/BlockAnchor.h>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <qqmlintegration.h>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {
class LiveCursorState;
class LiveHoleLayer;
}

namespace Markoff::LiveRender {

/// View-side undo coalescing policy (spec §6.1 L5). Tracks the most-recent
/// edit's classification and focus context; on each subsequent edit, decides
/// whether to call `MarkoffDocument::coalesceLastUndo()` to merge it into
/// the previous undo entry.
///
/// Policy: consecutive printables in the same `blockAnchor` within
/// `kIdleThresholdMs` (1000 ms) coalesce into one undo entry. Any of the
/// following BREAK the chain — the next printable cannot coalesce:
///   - `recordStructural()`     (Enter, Backspace-edge, Delete-edge, Shift-Enter)
///   - `recordOther()`          (paste, multi-char delete, IME commit)
///   - `notifyFocusChanged()`   (cursor moved to a different block)
///   - `notifyMovement()`       (arrow-key without edit; reserved for R6+)
///   - `notifyIdleExpired()`    (1000 ms passed since the last record)
///
/// `recordPrintable(anchor)` returns `true` if it called `coalesceLastUndo()`,
/// `false` if not. Test affordance.
///
/// Owned by `LiveListModelBinding`. Consumers: `LiveEditBinding`,
/// `LiveStructuralKeyHandler`, the QML focus-change connection (in
/// `LiveView.qml`) calls `notifyFocusChanged` when `cursorState.cursorChanged`
/// arrives with a different `blockAnchor` than the prior cursor.
///
/// Task 15: undo() and redo() are the Ctrl-Z / Ctrl-Shift-Z entry points.
/// They route to LiveHoleLayer when the cursor is on a hole row, and to
/// MarkoffDocument otherwise.
class MARKOFF_LIVE_RENDER_EXPORT UndoCoalescer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("UndoCoalescer is provided by LiveListModelBinding")

public:
    static constexpr int kIdleThresholdMs = 1000;

    explicit UndoCoalescer(Markoff::MarkoffDocument *document,
                           LiveCursorState          *cursorState = nullptr,
                           LiveHoleLayer            *holeLayer   = nullptr,
                           QObject                  *parent      = nullptr);

    bool recordPrintable(const Markoff::BlockAnchor &anchor);
    void recordStructural();
    void recordOther();

    void notifyFocusChanged();
    void notifyMovement();
    void notifyIdleExpired();

    /// Ctrl-Z entry point. If the focused row is a hole, routes to
    /// LiveHoleLayer::undoBlockHole; on empty-buffer-empty-stack,
    /// abandons the hole. Otherwise routes to MarkoffDocument::undo().
    Q_INVOKABLE void undo();

    /// Ctrl-Shift-Z entry point. Symmetric to undo().
    Q_INVOKABLE void redo();

private:
    void clearLast();

    QPointer<Markoff::MarkoffDocument> m_document;
    QPointer<LiveCursorState>          m_cursorState;
    QPointer<LiveHoleLayer>            m_holeLayer;

    // Last-record state. m_haveLast == false means the chain is broken;
    // the next recordPrintable cannot coalesce.
    bool                  m_haveLast = false;
    bool                  m_lastWasPrintable = false;
    Markoff::BlockAnchor  m_lastAnchor;
    QElapsedTimer         m_lastTimer;
};

}  // namespace Markoff::LiveRender
