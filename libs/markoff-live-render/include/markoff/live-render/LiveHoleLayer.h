// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/BlockHole.h>
#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStack>
#include <qqmlintegration.h>

class QTimer;

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::LiveRender {

class LiveBlockModel;

class MARKOFF_LIVE_RENDER_EXPORT LiveHoleLayer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveHoleLayer is provided by LiveListModelBinding")

public:
    explicit LiveHoleLayer(Markoff::MarkoffDocument *doc,
                           LiveBlockModel    *blockModel,
                           QObject           *parent = nullptr);
    ~LiveHoleLayer() override;

    // Lifecycle.
    quint64 createBlockHole(HoleKind kind, Markoff::TextAnchor reifyAnchor);
    void    setBlockHoleBuffer(quint64 holeId, const QString &text);
    void    setHoleComposition(quint64 holeId, bool composing);
    void    abandonBlockHole(quint64 holeId);
    void    commitBlockHole(quint64 holeId);
    void    commitAllPendingHoles();

    // Lookups.
    int     holeCount() const noexcept;
    bool    exists(quint64 holeId) const noexcept;
    HoleKind kind(quint64 holeId) const;
    QString  bufferText(quint64 holeId) const;
    Markoff::TextAnchor reifyAnchor(quint64 holeId) const;
    QList<quint64> holesInOrder() const;

    // Per-hole undo/redo (Task 15).

    /// Pushes the current bufferText onto the per-hole undo stack and
    /// clears the redo stack. Called explicitly by tests / by the
    /// 1-second coalesce-break in setBlockHoleBuffer.
    void recordHoleUndoPoint(quint64 holeId);

    /// Pop one snapshot off the undo stack into bufferText (and push the
    /// pre-undo buffer onto the redo stack). Returns true if a state
    /// change occurred. Returns FALSE if both undoStack is empty AND
    /// bufferText is empty — the caller should treat that as a signal
    /// to drop the hole (matching v1 spec §3.3 Ctrl-Z-empty rule).
    bool undoBlockHole(quint64 holeId);

    /// Symmetric to undoBlockHole.
    bool redoBlockHole(quint64 holeId);

Q_SIGNALS:
    void holeInserted(quint64 holeId);
    void holeBufferChanged(quint64 holeId);
    void holeAbandoned(quint64 holeId);
    void idleCommitDue(quint64 holeId);
    /// Emitted at the very start of `commitBlockHole`, BEFORE the hole row
    /// is removed from the layer/proxy. Listeners can still resolve the
    /// hole's proxy row + buffer length (which they need to schedule
    /// post-commit cursor delivery into the new paragraph that is about to
    /// land at the hole's former proxy position).
    void aboutToCommit(quint64 holeId);
    void holeReified(quint64 holeId, Markoff::TextAnchor newRowAnchor);

private:
    struct HoleEntry {
        HoleKind kind = HoleKind::Paragraph;
        Markoff::TextAnchor reifyAnchor;
        QString bufferText;
        bool composing = false;
        QTimer *idleTimer = nullptr;
        QStack<QString> undoStack;
        QStack<QString> redoStack;
        qint64 lastEditMs = 0;
    };

    static constexpr int kIdleCommitMs = 250;

    void restartIdleTimer(quint64 holeId);
    void stopIdleTimer(quint64 holeId);

    QHash<quint64, HoleEntry> m_holes;
    quint64 m_nextHoleId = 1;

    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel *m_blockModel;
};

}  // namespace Markoff::LiveRender
