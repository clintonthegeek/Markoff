// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>

class TstD5ReplicaId : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructor_isSingleUser() {
        Markoff::MarkoffDocument doc;
        QVERIFY(!doc.isCollabConfigured());
        QCOMPARE(doc.replicaId(), quint16(0x0001));
    }
    void explicitConstructor_isCollab() {
        Markoff::MarkoffDocument doc(quint16(42));
        QVERIFY(doc.isCollabConfigured());
        QCOMPARE(doc.replicaId(), quint16(42));
    }
    void replicaId_isImmutable_NoSetter() {
        Markoff::MarkoffDocument doc(quint16(7));
        QCOMPARE(doc.replicaId(), quint16(7));
    }
};
QTEST_MAIN(TstD5ReplicaId)
#include "tst_d5_replica_id.moc"
