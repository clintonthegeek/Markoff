// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "InMemoryTransport.h"

class TstD5InMemoryTransport : public QObject {
    Q_OBJECT
private slots:
    void pushReceive_singlePeer() {
        InMemoryTransport a("A"), b("B");
        a.connectPeer(&b);
        b.connectPeer(&a);

        QByteArray received;
        QString receivedStream;
        b.setOnInbound([&](QString s, QByteArray blob, quint16) {
            receivedStream = s; received = blob;
        });

        a.push("test", QByteArray("hello"));
        QCOMPARE(received, QByteArray("hello"));
        QCOMPARE(receivedStream, QString("test"));
    }

    void ackTracking_advancesAsBothPeersConfirm() {
        InMemoryTransport a("A"), b("B");
        a.connectPeer(&b);
        b.connectPeer(&a);

        // B publishes its watermark → a.observePeerWatermark("B", 100) fires,
        // populating a.m_peerWatermarks so lowestPeerAckedLamport() returns 100.
        b.publishLocalWatermark(100);
        QCOMPARE(a.lowestPeerAckedLamport(), quint64(100));
    }
};
QTEST_MAIN(TstD5InMemoryTransport)
#include "tst_d5_in_memory_transport.moc"
