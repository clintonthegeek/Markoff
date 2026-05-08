// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <limits>

using namespace Markoff;

class TstD5WatermarkGate : public QObject {
    Q_OBJECT
private slots:
    void saveInCollabMode_emitsWantsAcks() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        QSignalSpy spy(&doc, &MarkoffDocument::wantsAcksAtWatermark);

        // Make an edit so localOpsProduced fires and maxProducedLamport > 0.
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            QVERIFY(!b.empty());
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        doc.simulateSaveSucceeded();

        QCOMPARE(spy.count(), 1);
        const quint64 watermark = spy.takeFirst().at(0).toULongLong();
        QVERIFY(watermark > 0);
    }

    void notifyAcks_belowWatermark_doesNotCompact() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        doc.simulateSaveSucceeded();

        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        doc.notifyAcksAtWatermark(0);  // 0 below W; should not compact
        QCOMPARE(compactSpy.count(), 0);
    }

    void notifyAcks_atOrAboveWatermark_compacts() {
        MarkoffDocument doc(quint16(7));
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        QSignalSpy wantsSpy(&doc, &MarkoffDocument::wantsAcksAtWatermark);
        doc.simulateSaveSucceeded();
        QCOMPARE(wantsSpy.count(), 1);
        const quint64 W = wantsSpy.takeFirst().at(0).toULongLong();

        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        doc.notifyAcksAtWatermark(W);
        QCOMPARE(compactSpy.count(), 1);
    }

    void singleUserMode_uintMaxAck_compactsOnSave() {
        MarkoffDocument doc;  // single-user
        doc.loadFromMarkdown(QByteArrayLiteral("x\n"));
        doc.notifyAcksAtWatermark(std::numeric_limits<quint64>::max());

        QSignalSpy compactSpy(&doc, &MarkoffDocument::watermarkCompacted);
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            const auto b = doc.iterateBlocks();
            doc.d2ApplyBufferEdit(b.front(), 1, 0, QByteArrayLiteral("!"), t);
        }
        doc.simulateSaveSucceeded();
        // In single-user mode with ackedWatermark=UINT64_MAX, the gate is
        // open (any W <= UINT64_MAX), so compact fires even though localOpsProduced
        // was suppressed (single-user mode). W may be 0 if no ops were emitted
        // in collab mode, but the gate is open regardless.
        QCOMPARE(compactSpy.count(), 1);
    }
};
QTEST_MAIN(TstD5WatermarkGate)
#include "tst_d5_watermark_gate.moc"
