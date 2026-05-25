// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffSerializer.h>
#include <markoff/core/MarkoffOp.h>

class TstD5Serialization : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_emptyBundle() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 5;
        meta.bundleId          = 12345;
        meta.opCountInBundle   = 0;
        meta.actionId          = Markoff::ActionId::None;
        meta.producerLamport   = 99;

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QVERIFY(!blob.isEmpty());

        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), 0);
        QCOMPARE(outMeta.producerReplicaId, meta.producerReplicaId);
        QCOMPARE(outMeta.bundleId, meta.bundleId);
        QCOMPARE(outMeta.opCountInBundle, meta.opCountInBundle);
        QCOMPARE(static_cast<int>(outMeta.actionId), static_cast<int>(meta.actionId));
        QCOMPARE(outMeta.producerLamport, meta.producerLamport);
    }
    void roundtrip_singleBufferOp() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffOp op;
        op.target = Markoff::CrdtTarget::Buffer;
        op.blockId = 0xdead'beefULL;
        op.payload = QByteArray("opaque-buffer-bytes");
        op.producerReplicaId = 5;
        ops.append(op);

        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 5;
        meta.bundleId          = 1;
        meta.opCountInBundle   = 1;

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), 1);
        QCOMPARE(outOps[0].target, Markoff::CrdtTarget::Buffer);
        QCOMPARE(outOps[0].blockId, op.blockId);
        QCOMPARE(outOps[0].payload, op.payload);
        QCOMPARE(outOps[0].producerReplicaId, op.producerReplicaId);
    }
    void roundtrip_mixedOps() {
        QList<Markoff::MarkoffOp> ops;
        for (auto t : { Markoff::CrdtTarget::IdList,
                        Markoff::CrdtTarget::Buffer,
                        Markoff::CrdtTarget::KindTagMap,
                        Markoff::CrdtTarget::BlockAttrsMap,
                        Markoff::CrdtTarget::FrontmatterMap,
                        Markoff::CrdtTarget::LinkRefMap,
                        Markoff::CrdtTarget::FootnoteDefMap }) {
            Markoff::MarkoffOp op;
            op.target = t;
            op.blockId = (t == Markoff::CrdtTarget::Buffer) ? 42 : 0;
            op.payload = QByteArray("p-").append(QByteArray::number(static_cast<int>(t)));
            op.producerReplicaId = 9;
            ops.append(op);
        }
        Markoff::MarkoffBundleMeta meta;
        meta.producerReplicaId = 9;
        meta.bundleId = 100;
        meta.opCountInBundle = quint16(ops.size());

        QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(Markoff::MarkoffSerializer::decode(blob, &outOps, &outMeta));
        QCOMPARE(outOps.size(), ops.size());
        for (int i = 0; i < ops.size(); ++i) {
            QCOMPARE(outOps[i].target, ops[i].target);
            QCOMPARE(outOps[i].blockId, ops[i].blockId);
            QCOMPARE(outOps[i].payload, ops[i].payload);
            QCOMPARE(outOps[i].producerReplicaId, ops[i].producerReplicaId);
        }
    }
    void decode_truncatedFails() {
        QList<Markoff::MarkoffOp> ops;
        Markoff::MarkoffOp op;
        op.target = Markoff::CrdtTarget::Buffer;
        op.blockId = 1;
        op.payload = QByteArray("xx");
        ops.append(op);
        Markoff::MarkoffBundleMeta meta;
        meta.opCountInBundle = 1;
        const QByteArray blob = Markoff::MarkoffSerializer::encode(ops, meta);
        const QByteArray truncated = blob.left(blob.size() / 2);

        QList<Markoff::MarkoffOp> outOps;
        Markoff::MarkoffBundleMeta outMeta;
        QVERIFY(!Markoff::MarkoffSerializer::decode(truncated, &outOps, &outMeta));
    }
};
QTEST_MAIN(TstD5Serialization)
#include "tst_d5_serialization.moc"
