// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/Selection.h>

#include <QObject>
#include <QByteArray>
#include <QString>

namespace Markoff { class MarkoffDocument; }
class InMemoryTransport;

Q_DECLARE_METATYPE(Markoff::Selection)

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
    /// Send one caret. The transport drops it if no peer is connected.
    /// This does not publish a document action or a receipt.
    [[nodiscard]] bool publishPresence(const Markoff::Selection& selection);

signals:
    void remoteActionReceiptReceived(QByteArray actionId, QByteArray acceptedRevision);
    void remotePresenceReceived(Markoff::Selection selection);

private:
    Markoff::MarkoffDocument *m_doc;
    InMemoryTransport        *m_transport;
};
