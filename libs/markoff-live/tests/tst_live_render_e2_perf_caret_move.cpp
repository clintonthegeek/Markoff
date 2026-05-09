// SPDX-License-Identifier: GPL-3.0-or-later
//
// E2 H1: Caret-move benchmark.
//
// Measures the cost of requestTextCaretAtRow() cycling through 100 rows
// of a 100-paragraph document. 100 iterations; asserts p99 < 5 ms.
// Logs p50 + p99 to the console for tracking across hardware.

#include <QElapsedTimer>
#include <QTest>
#include <algorithm>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

class TstLiveRenderE2PerfCaretMove : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void caret_move_under_five_ms_p99()
    {
        // Build a 100-paragraph markdown document with bold/italic spans.
        QStringList paras;
        paras.reserve(100);
        for (int i = 0; i < 100; ++i)
            paras << QString("Paragraph %1 with **bold word** and *italic text* here").arg(i);
        const QByteArray markdown = paras.join("\n\n").toUtf8();

        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(markdown);

        // Allow async model updates to propagate.
        QTest::qWait(500);

        QCOMPARE(binding.model()->rowCount(), 100);

        auto *cs = binding.cursorState();
        QVERIFY(cs);

        // Warm-up: one call so Qt can finish any one-time initialisation.
        cs->requestTextCaretAtRow(0, 0);

        QList<qint64> timings;
        timings.reserve(100);

        for (int i = 0; i < 100; ++i) {
            QElapsedTimer t;
            t.start();
            cs->requestTextCaretAtRow(i, 0);
            timings.append(t.nsecsElapsed());
        }

        std::sort(timings.begin(), timings.end());
        const double p50ms = static_cast<double>(timings[49]) / 1.0e6;
        const double p99ms = static_cast<double>(timings[98]) / 1.0e6;

        qDebug() << "Caret-move timing  p50:" << p50ms << "ms  p99:" << p99ms << "ms";

        QVERIFY2(p99ms < 5.0,
                 qPrintable(QString("p99 %1 ms exceeded 5 ms gate").arg(p99ms)));
    }
};

QTEST_MAIN(TstLiveRenderE2PerfCaretMove)
#include "tst_live_render_e2_perf_caret_move.moc"
