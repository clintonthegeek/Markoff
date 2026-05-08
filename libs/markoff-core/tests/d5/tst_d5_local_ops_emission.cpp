// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff;

class TstD5LocalOpsEmission : public QObject {
    Q_OBJECT
private slots:
    void singleBufferEdit_emitsOneBundle() {
        MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n"));

        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());
        const BlockId b0 = blockIds.front();

        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(b0, /*offset*/5, /*remove*/0,
                                   QByteArrayLiteral("!"), t);
            QCOMPARE(spy.count(), 0);   // not yet committed
        }  // t destructs here, commit fires

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        const auto ops = args.at(0).value<QList<MarkoffOp>>();
        const auto meta = args.at(1).value<MarkoffBundleMeta>();

        QCOMPARE(meta.producerReplicaId, quint16(42));
        QCOMPARE(meta.opCountInBundle, quint16(ops.size()));
        QVERIFY(meta.bundleId > 0);
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].target, CrdtTarget::Buffer);
        QCOMPARE(ops[0].blockId, b0.raw());
        QVERIFY(!ops[0].payload.isEmpty());
        QCOMPARE(ops[0].producerReplicaId, quint16(42));
    }

    void compoundCmd_emitsOneBundleWithMultipleOps() {
        // Cmd::enterAtEnd touches IdList + Buffer in one transaction.
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("First\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(!blockIds.empty());

        Cmd::enterAtEnd(doc, blockIds.front());

        QCOMPARE(spy.count(), 1);
        const auto ops = spy.takeFirst().at(0).value<QList<MarkoffOp>>();
        // enterAtEnd inserts a new block in IdList + may touch Buffer.
        QVERIFY(ops.size() >= 1);
        bool sawIdList = false, sawBuffer = false;
        for (const auto &op : ops) {
            if (op.target == CrdtTarget::IdList)  sawIdList = true;
            if (op.target == CrdtTarget::Buffer)  sawBuffer = true;
        }
        QVERIFY(sawIdList);  // at least an IdList op (block insert)
        Q_UNUSED(sawBuffer);
    }

    void singleUserMode_noEmission() {
        // Single-user mode doc does not emit localOpsProduced.
        MarkoffDocument doc;   // no-arg ctor → isCollabConfigured() = false
        doc.loadFromMarkdown(QByteArrayLiteral("X\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::localOpsProduced);
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto blocks = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(blocks.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        QCOMPARE(spy.count(), 0);
    }
};
QTEST_MAIN(TstD5LocalOpsEmission)
#include "tst_d5_local_ops_emission.moc"
