// SPDX-License-Identifier: GPL-3.0-or-later
#include "InMemoryTransport.h"
#include <algorithm>
#include <limits>

InMemoryTransport::InMemoryTransport(QString replicaName, QObject *parent)
    : QObject(parent), m_name(std::move(replicaName)) {}

void InMemoryTransport::push(QString streamName, QByteArray blob)
{
    for (auto *peer : m_peers)
        if (peer) peer->deliverFromPeer(streamName, blob, 0);
}

void InMemoryTransport::setOnInbound(OnInboundFn fn) { m_onInbound = std::move(fn); }
void InMemoryTransport::onAckUpdate(OnAckFn fn)      { m_onAck = std::move(fn); }

void InMemoryTransport::connectPeer(InMemoryTransport *peer)
{
    if (peer && !m_peers.contains(peer)) m_peers.append(peer);
}

void InMemoryTransport::disconnectPeer(InMemoryTransport *peer)
{
    m_peers.removeAll(peer);
    m_peerWatermarks.remove(peer ? peer->replicaName() : QString());
}

void InMemoryTransport::publishLocalWatermark(quint64 W)
{
    m_localWatermark = W;
    for (auto *peer : m_peers)
        if (peer) peer->observePeerWatermark(m_name, W);
}

void InMemoryTransport::observePeerWatermark(QString peerName, quint64 W)
{
    m_peerWatermarks[peerName] = W;
    if (m_onAck) m_onAck(lowestPeerAckedLamport());
}

quint64 InMemoryTransport::lowestPeerAckedLamport() const
{
    if (m_peerWatermarks.isEmpty()) return 0;
    quint64 lo = std::numeric_limits<quint64>::max();
    for (auto v : m_peerWatermarks) lo = std::min(lo, v);
    return lo;
}

void InMemoryTransport::deliverFromPeer(QString stream, QByteArray blob, quint16 producer)
{
    if (m_onInbound) m_onInbound(std::move(stream), std::move(blob), producer);
}
