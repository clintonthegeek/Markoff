// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>

class TstD5LocalOpsSignal : public QObject {
    Q_OBJECT
private slots:
    void signalExists_canBeSpiedOn() {
        Markoff::MarkoffDocument doc(quint16(42));
        QSignalSpy spy(&doc, SIGNAL(localOpsProduced(QList<Markoff::MarkoffOp>,
                                                      Markoff::MarkoffBundleMeta)));
        QVERIFY(spy.isValid());
    }
};
QTEST_MAIN(TstD5LocalOpsSignal)
#include "tst_d5_local_ops_signal.moc"
