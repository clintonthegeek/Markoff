// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QAbstractListModel>
#include <QHash>
#include <QVector>
#include <qqmlintegration.h>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::LiveRender {

class LiveBlockModel;
class LiveHoleLayer;

class MARKOFF_LIVE_RENDER_EXPORT LiveProxyBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveProxyBlockModel is provided by LiveListModelBinding")

public:
    enum Roles {
        IsHoleRole       = Qt::UserRole + 1000,
        BufferTextRole   = Qt::UserRole + 1001,
        HoleIdRole       = Qt::UserRole + 1002,
    };

    explicit LiveProxyBlockModel(Markoff::MarkoffDocument *doc,
                                  LiveBlockModel          *inner,
                                  LiveHoleLayer           *layer,
                                  QObject                 *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int     innerRowForProxy(int proxyRow) const;
    Q_INVOKABLE int     proxyRowForInner(int innerRow) const;
    Q_INVOKABLE int     proxyRowForHole(quint64 holeId) const;
    Q_INVOKABLE bool    proxyRowIsHole(int proxyRow) const noexcept;
    Q_INVOKABLE quint64 holeAtProxyRow(int proxyRow) const noexcept;

private:
    quint32 innerStartByteForRow(int r) const;

private Q_SLOTS:
    void onInnerRowsInserted(const QModelIndex &, int first, int last);
    void onInnerRowsAboutToBeRemoved(const QModelIndex &, int first, int last);
    void onInnerDataChanged(const QModelIndex &tl, const QModelIndex &br,
                            const QVector<int> &roles);
    void onInnerModelReset();
    void onHoleInserted(quint64 holeId);
    void onHoleBufferChanged(quint64 holeId);
    void onHoleAbandoned(quint64 holeId);

private:
    struct ProxyRow {
        bool    isHole;
        int     innerRow;     // valid when !isHole
        quint64 holeId;       // valid when isHole
    };

    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel           *m_inner;
    LiveHoleLayer            *m_layer;
    QVector<ProxyRow>         m_rows;

    void rebuildMapping();
};

}  // namespace Markoff::LiveRender
