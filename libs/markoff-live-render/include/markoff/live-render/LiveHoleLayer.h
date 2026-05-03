// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/BlockHole.h>
#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QHash>
#include <QObject>
#include <qqmlintegration.h>

class QTimer;

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::LiveRender {

class LiveBlockModel;
class UndoCoalescer;

class MARKOFF_LIVE_RENDER_EXPORT LiveHoleLayer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveHoleLayer is provided by LiveListModelBinding")

public:
    explicit LiveHoleLayer(Markoff::MarkoffDocument *doc,
                           LiveBlockModel    *blockModel,
                           UndoCoalescer     *undoCoalescer,
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

Q_SIGNALS:
    void holeInserted(quint64 holeId);
    void holeBufferChanged(quint64 holeId);
    void holeAbandoned(quint64 holeId);
    void idleCommitDue(quint64 holeId);
    void holeReified(quint64 holeId, Markoff::TextAnchor newRowAnchor);

private:
    struct HoleEntry {
        HoleKind kind = HoleKind::Paragraph;
        Markoff::TextAnchor reifyAnchor;
        QString bufferText;
        bool composing = false;
        QTimer *idleTimer = nullptr;
        // Task 15 adds: undo stack
    };

    static constexpr int kIdleCommitMs = 250;

    void restartIdleTimer(quint64 holeId);
    void stopIdleTimer(quint64 holeId);

    QHash<quint64, HoleEntry> m_holes;
    quint64 m_nextHoleId = 1;

    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel *m_blockModel;
    UndoCoalescer *m_undoCoalescer;
};

}  // namespace Markoff::LiveRender
