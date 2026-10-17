// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>

namespace Markoff { class MarkoffDocument; }
class InMemoryTransport;

class CollabConsumer : public QObject {
    Q_OBJECT
public:
    enum class DeliveryMode { legacyOperations, actionBundles };
    CollabConsumer(Markoff::MarkoffDocument *doc,
                   InMemoryTransport *transport,
                   QObject *parent = nullptr,
                   DeliveryMode mode = DeliveryMode::actionBundles);

    [[nodiscard]] bool sendActionEnvelope(QByteArray actionId, QByteArray mabBytes,
                                         QString streamName = QStringLiteral("markoff-actions"));

signals:
    void remoteActionReceiptReceived(QByteArray actionId, QByteArray acceptedRevision);

private:
    Markoff::MarkoffDocument *m_doc;
    InMemoryTransport        *m_transport;
};
