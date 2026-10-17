// SPDX-License-Identifier: GPL-3.0-or-later
#include "CollabConsumer.h"
#include "InMemoryTransport.h"
#include <markoff/core/MarkoffDocument.h>

CollabConsumer::CollabConsumer(Markoff::MarkoffDocument *doc,
                                InMemoryTransport *transport,
                                QObject *parent,
                                DeliveryMode mode)
    : QObject(parent), m_doc(doc), m_transport(transport)
{
    if (mode == DeliveryMode::actionBundles) {
        QObject::connect(m_doc,
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted, this,
            [this](QByteArray actionId, QByteArray mabBytes) {
                if (!sendActionEnvelope(std::move(actionId), std::move(mabBytes)))
                    qWarning("CollabConsumer: committed action could not be queued");
            });
        m_transport->setOnAction([this](QString, QByteArray mabBytes,
                                         quint16) {
            const auto routed = m_doc->routeRemoteAction(mabBytes);
            std::vector<InMemoryTransport::ActionReceipt> receipts;
            receipts.reserve(routed.committed.size());
            for (const auto& receipt : routed.committed)
                receipts.push_back({receipt.actionId,
                                    receipt.acceptedRevision.token});
            return receipts;
        });
        m_transport->setOnReceipt([this](InMemoryTransport::ActionReceipt receipt) {
            auto stored = m_doc->acknowledgeOutboundActionReceipt(
                receipt.actionId, receipt.acceptedRevision);
            if (!stored.ok()) {
                qWarning("CollabConsumer: transport receipt could not be persisted");
                return;
            }
            emit remoteActionReceiptReceived(std::move(receipt.actionId),
                                             std::move(receipt.acceptedRevision));
        });
        const auto pending = m_doc->pendingOutboundActions();
        if (!pending.ok()) {
            qWarning("CollabConsumer: persisted outbound actions could not be read");
        } else {
            for (const auto& action : pending.value()) {
                if (!sendActionEnvelope(action.actionId, action.mabBytes))
                    qWarning("CollabConsumer: pending action could not be requeued");
            }
        }
        return;
    }
    qWarning("CollabConsumer: legacy operation delivery is unavailable in the candidate build");
}

bool CollabConsumer::sendActionEnvelope(QByteArray actionId, QByteArray mabBytes,
                                        QString streamName)
{
    return m_transport->pushAction(std::move(streamName), std::move(actionId),
                                  std::move(mabBytes));
}
