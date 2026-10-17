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

    void actionReceiptsRouteToTheirOrigin() {
        InMemoryTransport a("A"), b("B"), c("C");
        a.connectPeer(&b);
        b.connectPeer(&a);
        c.connectPeer(&b);
        b.connectPeer(&c);

        int actionCalls = 0;
        b.setOnAction([&](QString, QByteArray, quint16) {
            ++actionCalls;
            if (actionCalls < 3) return std::vector<InMemoryTransport::ActionReceipt>{};
            return std::vector<InMemoryTransport::ActionReceipt>{
                {QByteArrayLiteral("causal-wait"), QByteArrayLiteral("rev-1")},
                {QByteArrayLiteral("dependency"), QByteArrayLiteral("rev-2")}};
        });

        QList<InMemoryTransport::ActionReceipt> aReceipts;
        QList<InMemoryTransport::ActionReceipt> cReceipts;
        a.setOnReceipt([&](InMemoryTransport::ActionReceipt receipt) {
            aReceipts.push_back(std::move(receipt));
        });
        c.setOnReceipt([&](InMemoryTransport::ActionReceipt receipt) {
            cReceipts.push_back(std::move(receipt));
        });

        const QByteArray waitingMab = QByteArrayLiteral("MAB-wait");
        QVERIFY(a.pushAction(QStringLiteral("actions"),
                             QByteArrayLiteral("causal-wait"), waitingMab));
        QCOMPARE(a.pendingActionCount(), 1);
        QCOMPARE(aReceipts.size(), 0);
        QVERIFY(a.pushAction(QStringLiteral("actions"),
                             QByteArrayLiteral("causal-wait"), waitingMab));
        QCOMPARE(a.pendingActionCount(), 1);
        QCOMPARE(aReceipts.size(), 0);
        QVERIFY(!a.pushAction(QStringLiteral("actions"),
                              QByteArrayLiteral("causal-wait"),
                              QByteArrayLiteral("changed-bytes")));

        QVERIFY(c.pushAction(QStringLiteral("actions"),
                             QByteArrayLiteral("dependency"),
                             QByteArrayLiteral("MAB-dependency")));
        QCOMPARE(aReceipts.size(), 1);
        QCOMPARE(aReceipts.front().actionId, QByteArrayLiteral("causal-wait"));
        QCOMPARE(aReceipts.front().acceptedRevision, QByteArrayLiteral("rev-1"));
        QCOMPARE(cReceipts.size(), 1);
        QCOMPARE(cReceipts.front().actionId, QByteArrayLiteral("dependency"));
        QCOMPARE(cReceipts.front().acceptedRevision, QByteArrayLiteral("rev-2"));
        QCOMPARE(a.pendingActionCount(), 0);
        QCOMPARE(c.pendingActionCount(), 0);
    }

    void reconnectReplaysExactPendingActionOnItsOriginalStream() {
        InMemoryTransport a("A"), b("B");
        QList<InMemoryTransport::ActionReceipt> receipts;
        QString receivedStream;
        QByteArray receivedMab;
        b.setOnAction([&](QString stream, QByteArray mab, quint16) {
            receivedStream = std::move(stream);
            receivedMab = std::move(mab);
            return std::vector<InMemoryTransport::ActionReceipt>{
                {QByteArrayLiteral("action-1"), QByteArrayLiteral("revision-1")}};
        });
        a.setOnReceipt([&](InMemoryTransport::ActionReceipt receipt) {
            receipts.push_back(std::move(receipt));
        });

        QVERIFY(a.pushAction(QStringLiteral("markoff-actions"),
            QByteArrayLiteral("action-1"), QByteArrayLiteral("MAB/3 exact bytes")));
        QCOMPARE(a.pendingActionCount(), qsizetype(1));
        QCOMPARE(receivedStream, QString());

        a.connectPeer(&b);

        QCOMPARE(receivedStream, QStringLiteral("markoff-actions"));
        QCOMPARE(receivedMab, QByteArrayLiteral("MAB/3 exact bytes"));
        QCOMPARE(receipts.size(), 1);
        QCOMPARE(receipts.front().actionId, QByteArrayLiteral("action-1"));
        QCOMPARE(a.pendingActionCount(), qsizetype(0));
    }

};
QTEST_MAIN(TstD5InMemoryTransport)
#include "tst_d5_in_memory_transport.moc"
