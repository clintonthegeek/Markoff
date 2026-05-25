// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QElapsedTimer>
#include <QApplication>
#include <QClipboard>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestE25PerfBulkPaste : public QObject {
    Q_OBJECT
private:
    QByteArray makeBigDoc(int blocks) {
        QByteArray b;
        for (int i = 0; i < blocks; ++i) {
            if (i) b.append("\n\n");
            b.append("paragraph ");
            b.append(QByteArray::number(i));
        }
        return b;
    }
private slots:
    void paste_1000_blocks_under_200ms() {
        const QByteArray big = makeBigDoc(1000);
        QApplication::clipboard()->setText(QString::fromUtf8(big));

        Markoff::MarkoffDocument doc(quint16(42));
        doc.loadFromMarkdown("");
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(binding.cursorState());
        cc.setModel(binding.model());
        binding.cursorState()->begin(0, 0);
        binding.cursorState()->extend(0, 0);

        QElapsedTimer t; t.start();
        cc.paste();
        const qint64 ms = t.elapsed();
        qDebug() << "paste of 1000 blocks took" << ms << "ms";
        QVERIFY2(ms < 200, qPrintable(QString("paste 1000 blocks took %1ms (budget 200)").arg(ms)));
    }
};

QTEST_MAIN(TestE25PerfBulkPaste)
#include "tst_live_render_e2_5_perf_bulk_paste.moc"
