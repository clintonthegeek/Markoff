// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class InMemoryTransport : public QObject {
    Q_OBJECT
public:
    explicit InMemoryTransport(QString replicaName, QObject *parent = nullptr);

    using OnInboundFn = std::function<void(QString, QByteArray, quint16)>;
    using OnAckFn     = std::function<void(quint64)>;

    void push(QString streamName, QByteArray blob);
    void setOnInbound(OnInboundFn fn);
    void onAckUpdate(OnAckFn fn);

    void connectPeer(InMemoryTransport *peer);
    void disconnectPeer(InMemoryTransport *peer);

    void publishLocalWatermark(quint64 W);
    void observePeerWatermark(QString peerName, quint64 W);
    quint64 lowestPeerAckedLamport() const;

    QString replicaName() const { return m_name; }

private:
    void deliverFromPeer(QString stream, QByteArray blob, quint16 producer);

    QString m_name;
    QList<InMemoryTransport *> m_peers;
    OnInboundFn m_onInbound;
    OnAckFn     m_onAck;
    quint64     m_localWatermark = 0;
    QHash<QString, quint64> m_peerWatermarks;
};
