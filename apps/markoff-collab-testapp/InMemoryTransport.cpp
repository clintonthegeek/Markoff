// SPDX-License-Identifier: GPL-3.0-or-later
#include "InMemoryTransport.h"
#include <utility>

InMemoryTransport::InMemoryTransport(QString replicaName, QObject *parent)
    : QObject(parent), m_name(std::move(replicaName)) {}

void InMemoryTransport::push(QString streamName, QByteArray blob)
{
    for (auto *peer : m_peers)
        if (peer) peer->deliverFromPeer(streamName, blob, 0);
}

void InMemoryTransport::setOnInbound(OnInboundFn fn) { m_onInbound = std::move(fn); }

bool InMemoryTransport::pushAction(QString streamName, QByteArray actionId,
                                   QByteArray mabBytes)
{
    if (actionId.isEmpty() || mabBytes.isEmpty()) return false;
    const auto pending = m_pendingActions.constFind(actionId);
    if (pending != m_pendingActions.cend()
        && (pending->mabBytes != mabBytes || pending->streamName != streamName))
        return false;
    m_pendingActions.insert(actionId, PendingAction{streamName, mabBytes});
    for (auto *peer : m_peers) {
        if (peer)
            peer->deliverActionFromPeer(this, streamName, actionId, mabBytes, 0);
    }
    return true;
}

void InMemoryTransport::setOnAction(OnActionFn fn) { m_onAction = std::move(fn); }

void InMemoryTransport::setOnReceipt(OnReceiptFn fn) { m_onReceipt = std::move(fn); }

qsizetype InMemoryTransport::pendingActionCount() const noexcept
{
    return m_pendingActions.size();
}

void InMemoryTransport::connectPeer(InMemoryTransport *peer)
{
    if (!peer || m_peers.contains(peer)) return;
    m_peers.append(peer);
    // Delivery can synchronously return a receipt, which removes an entry from
    // m_pendingActions. Iterate a snapshot so that callback cannot invalidate
    // the active hash iterator.
    const auto pendingActions = m_pendingActions;
    for (auto it = pendingActions.cbegin(); it != pendingActions.cend(); ++it)
        peer->deliverActionFromPeer(this, it->streamName, it.key(),
                                    it->mabBytes, 0);
}

void InMemoryTransport::disconnectPeer(InMemoryTransport *peer)
{
    m_peers.removeAll(peer);
}

void InMemoryTransport::deliverFromPeer(QString stream, QByteArray blob, quint16 producer)
{
    if (m_onInbound) m_onInbound(std::move(stream), std::move(blob), producer);
}

void InMemoryTransport::deliverActionFromPeer(InMemoryTransport *source,
                                               QString stream,
                                               QByteArray actionId,
                                               QByteArray mabBytes,
                                               quint16 producer)
{
    if (!source || actionId.isEmpty() || mabBytes.isEmpty()) return;
    const auto prior = m_actionOrigins.constFind(actionId);
    if (prior != m_actionOrigins.cend() && prior.value() != source)
        return;
    m_actionOrigins.insert(actionId, source);
    if (!m_onAction) return;
    const auto receipts = m_onAction(std::move(stream), std::move(mabBytes), producer);
    for (const auto& receipt : receipts) {
        auto origin = m_actionOrigins.find(receipt.actionId);
        if (origin == m_actionOrigins.end()) continue;
        auto *recipient = origin.value().data();
        m_actionOrigins.erase(origin);
        if (recipient)
            recipient->deliverReceiptFromPeer(this, receipt);
    }
}

void InMemoryTransport::deliverReceiptFromPeer(InMemoryTransport *source,
                                                ActionReceipt receipt)
{
    Q_UNUSED(source)
    if (receipt.actionId.isEmpty() || receipt.acceptedRevision.isEmpty()
        || m_pendingActions.remove(receipt.actionId) == 0)
        return;
    if (m_onReceipt) m_onReceipt(std::move(receipt));
}
