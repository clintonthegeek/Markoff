// SPDX-License-Identifier: GPL-3.0-or-later
//
// The retired operation-envelope consumer used numeric block IDs. Keep an
// explicit root-default regression that proves those IDs cannot be written to
// the canonical structural-identity MKOF/2 wire format.
#include <QTest>

#include <markoff/core/MarkoffSerializer.h>

class TstD5LegacyIdentityRejection final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void numericBufferIdentityIsRejectedByMkof2()
    {
        Markoff::MarkoffOp operation;
        operation.target = Markoff::CrdtTarget::Buffer;
        operation.blockId = Markoff::BlockId::fromRaw(7);
        operation.payload = QByteArrayLiteral("legacy-operation");
        operation.producerReplicaId = 1;

        Markoff::MarkoffBundleMeta metadata;
        metadata.producerReplicaId = 1;
        metadata.bundleId = 1;
        metadata.opCountInBundle = 1;
        metadata.actionId = Markoff::ActionId::Bold;
        metadata.producerLamport = 1;

        QVERIFY(Markoff::MarkoffSerializer::encode({operation}, metadata).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstD5LegacyIdentityRejection)
#include "tst_d5_legacy_identity_rejection.moc"
