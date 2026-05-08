// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QMetaType>
#include <markoff/core/MarkoffOp.h>

class TstD5OpTypes : public QObject {
    Q_OBJECT
private slots:
    void crdtTarget_hasExpectedValues() {
        using T = Markoff::CrdtTarget;
        QCOMPARE(static_cast<quint8>(T::IdList),         quint8(0));
        QCOMPARE(static_cast<quint8>(T::Buffer),         quint8(1));
        QCOMPARE(static_cast<quint8>(T::KindTagMap),     quint8(2));
        QCOMPARE(static_cast<quint8>(T::BlockAttrsMap),  quint8(3));
        QCOMPARE(static_cast<quint8>(T::FrontmatterMap), quint8(4));
        QCOMPARE(static_cast<quint8>(T::LinkRefMap),     quint8(5));
        QCOMPARE(static_cast<quint8>(T::FootnoteDefMap), quint8(6));
    }
    void markoffOp_isDefaultConstructible() {
        Markoff::MarkoffOp op;
        QCOMPARE(op.target, Markoff::CrdtTarget::IdList);
        QCOMPARE(op.blockId, quint64(0));
        QVERIFY(op.payload.isEmpty());
        QCOMPARE(op.producerReplicaId, quint16(0));
    }
    void markoffOp_metatypeRegistered() {
        const int id = qMetaTypeId<Markoff::MarkoffOp>();
        QVERIFY(id > 0);
    }
    void markoffOpList_metatypeRegistered() {
        const int id = qMetaTypeId<QList<Markoff::MarkoffOp>>();
        QVERIFY(id > 0);
    }
};
QTEST_MAIN(TstD5OpTypes)
#include "tst_d5_op_types.moc"
