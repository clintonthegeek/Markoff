// SPDX-License-Identifier: GPL-3.0-or-later
#include "CollabConsumer.h"
#include "InMemoryTransport.h"
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffSerializer.h>
#include <markoff/core/MarkoffOp.h>

CollabConsumer::CollabConsumer(Markoff::MarkoffDocument *doc,
                                InMemoryTransport *transport,
                                QObject *parent)
    : QObject(parent), m_doc(doc), m_transport(transport)
{
    QObject::connect(m_doc, &Markoff::MarkoffDocument::localOpsProduced,
                     this, [this](QList<Markoff::MarkoffOp> ops,
                                   Markoff::MarkoffBundleMeta meta) {
        const QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        m_transport->push(QStringLiteral("markoff-bundles"), blob);
    });

    QObject::connect(m_doc, &Markoff::MarkoffDocument::wantsAcksAtWatermark,
                     this, [this](quint64 W) {
        const quint64 cur = m_transport->lowestPeerAckedLamport();
        if (cur >= W) m_doc->notifyAcksAtWatermark(cur);
    });

    m_transport->onAckUpdate([this](quint64 W) {
        m_doc->notifyAcksAtWatermark(W);
    });

    m_transport->setOnInbound([this](QString /*stream*/, QByteArray blob,
                                      quint16 /*producer*/) {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffBundleMeta meta;
        if (!Markoff::MarkoffSerializer::decode(blob, &ops, &meta)) {
            qWarning("CollabConsumer: bundle decode failed; dropping");
            return;
        }
        m_doc->applyRemoteOps(std::move(ops), std::move(meta));
    });
}
