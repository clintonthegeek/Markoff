// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/SiblingMapOpHeader.h>

class TstD5SiblingMapEnvelope : public QObject {
    Q_OBJECT
private slots:
    void encodeDecode_simpleEntry() {
        Markoff::SiblingMapOpHeader h;
        h.key              = QByteArray("key-bytes");
        h.value            = QByteArray("value-bytes");
        h.lamportCounter   = 42;
        h.lamportReplicaId = 7;
        h.isTombstone      = false;

        QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        QVERIFY(!bytes.isEmpty());

        Markoff::SiblingMapOpHeader out;
        const bool ok = Markoff::SiblingMapOpHeader::decode(bytes, &out);
        QVERIFY(ok);
        QCOMPARE(out.key, h.key);
        QCOMPARE(out.value, h.value);
        QCOMPARE(out.lamportCounter, h.lamportCounter);
        QCOMPARE(out.lamportReplicaId, h.lamportReplicaId);
        QCOMPARE(out.isTombstone, h.isTombstone);
    }
    void encodeDecode_tombstone() {
        Markoff::SiblingMapOpHeader h;
        h.key              = QByteArray("k");
        h.value            = QByteArray();
        h.lamportCounter   = 1;
        h.lamportReplicaId = 1;
        h.isTombstone      = true;

        const QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        Markoff::SiblingMapOpHeader out;
        QVERIFY(Markoff::SiblingMapOpHeader::decode(bytes, &out));
        QCOMPARE(out.isTombstone, true);
        QVERIFY(out.value.isEmpty());
    }
    void decode_emptyBytesFails() {
        Markoff::SiblingMapOpHeader out;
        QVERIFY(!Markoff::SiblingMapOpHeader::decode(QByteArray(), &out));
    }
    void decode_truncatedFails() {
        Markoff::SiblingMapOpHeader h;
        h.key              = QByteArray("k");
        h.value            = QByteArray("v");
        h.lamportCounter   = 1;
        h.lamportReplicaId = 1;
        const QByteArray bytes = Markoff::SiblingMapOpHeader::encode(h);
        const QByteArray truncated = bytes.left(bytes.size() / 2);
        Markoff::SiblingMapOpHeader out;
        QVERIFY(!Markoff::SiblingMapOpHeader::decode(truncated, &out));
    }
};
QTEST_MAIN(TstD5SiblingMapEnvelope)
#include "tst_d5_sibling_map_envelope.moc"
