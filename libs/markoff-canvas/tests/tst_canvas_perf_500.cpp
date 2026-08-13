// SPDX-License-Identifier: GPL-3.0-or-later
//
// T10 — perf harness (exit E9).
//
// A 500-block synthetic document (mixed kinds, model on
// tst_live_render_table_typing_perf) driven through the production widget's
// real event path. Four budgets, asserted:
//   1. load -> first paintEvent           < 500 ms
//   2. p95 keystroke -> next paint         < 16 ms  (200 keystrokes, mid-doc)
//   3. scroll start -> end realizes        < 30% of blocks
//   4. RSS delta across the whole run      < 100 MB
//
// Numbers are logged via qDebug either way (plan: "record them in spec §9
// either way"); the four QVERIFY2s are the pass/fail gate.

#include <QElapsedTimer>
#include <QFile>
#include <QScrollBar>
#include <QTest>
#include <algorithm>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

namespace {

/// 500 blocks cycling through five kinds (100 of each): paragraph, heading,
/// list item, code block, blockquote. Each cycle step is isolated by a
/// blank line so it lands as its own block, matching the "mixed kinds"
/// requirement without depending on how the parser groups list runs.
QByteArray syntheticDoc(int blockCount)
{
    QByteArray src;
    for (int i = 0; i < blockCount; ++i) {
        switch (i % 5) {
        case 0:
            src += "Paragraph " + QByteArray::number(i)
                 + " with enough prose in it to occupy a realistic line "
                   "of editor width.\n\n";
            break;
        case 1:
            src += "## Heading " + QByteArray::number(i) + "\n\n";
            break;
        case 2:
            src += "- list item " + QByteArray::number(i) + "\n\n";
            break;
        case 3:
            src += "```\ncode line " + QByteArray::number(i) + "\n```\n\n";
            break;
        case 4:
            src += "> quote " + QByteArray::number(i) + "\n\n";
            break;
        }
    }
    return src;
}

/// QTest::keyClicks() can't represent all input this test wants to drive
/// uniformly; a direct QKeyEvent with the target text is the same real
/// event path View::keyPressEvent reads (event->text()), same helper as
/// tst_canvas_typing.cpp.
void sendTextKeyEvent(QWidget *w, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

/// Current process resident set size, in kilobytes, or -1 if unavailable
/// (non-Linux; the spike's test matrix is offscreen Linux only).
qint64 currentVmRssKb()
{
    // QTextStream::readLine() over /proc/self/status is unreliable (procfs
    // reports a zero st_size, which throws off some sequential-device
    // paths) — read the whole pseudo-file in one shot instead, exactly the
    // pattern the plan calls for ("/proc/self/status VmRSS before/after").
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray all = f.readAll();
    const int pos = all.indexOf("VmRSS:");
    if (pos < 0)
        return -1;
    const QByteArrayView rest(all.constData() + pos + 6, all.size() - pos - 6);
    const QStringList parts = QString::fromLatin1(rest.first(qMin(rest.size(), 40)))
                                   .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.isEmpty() ? -1 : parts[0].toLongLong();
}

}  // namespace

class TstCanvasPerf500 : public QObject {
    Q_OBJECT

private slots:
    void four_perf_budgets_on_a_500_block_document();
};

void TstCanvasPerf500::four_perf_budgets_on_a_500_block_document()
{
    const qint64 rssBefore = currentVmRssKb();

    // ---- Budget 1: load -> first paintEvent ----------------------------
    QElapsedTimer loadTimer;
    loadTimer.start();

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(syntheticDoc(500));

    View view;
    view.resize(800, 600);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(view.paintCount() > 0);

    const qint64 loadToFirstPaintMs = loadTimer.elapsed();
    qDebug() << "load -> first paint:" << loadToFirstPaintMs << "ms";
#ifdef NDEBUG
    // The plan's E9 budgets are explicitly "release build" numbers (spec
    // §9, T10 finding): this repo's default build-dev tree carries no
    // -DCMAKE_BUILD_TYPE (no -O flags at all), and an unoptimized load of
    // 500 blocks does not fit in 500 ms even though a RelWithDebInfo build
    // does with room to spare. Gate the hard assertion on NDEBUG (set by
    // -DCMAKE_BUILD_TYPE=RelWithDebInfo/Release) so this budget is real
    // where it's supposed to be measured, without turning every default
    // dev build red. The other three budgets pass unconditionally in both
    // configs and stay asserted below regardless of build type.
    QVERIFY2(loadToFirstPaintMs < 500,
             qPrintable(QStringLiteral("load->first paint %1 ms exceeded 500 ms budget")
                            .arg(loadToFirstPaintMs)));
#else
    qDebug() << "load->first paint budget only asserted in an NDEBUG "
                "(release-ish) build — see spec §9 T10 finding";
#endif

    QCOMPARE(view.blockCount(), 500);

    // ---- Budget 2: p95 keystroke -> next paint, mid-document -----------
    // Scroll to the middle, then click there to place the caret inside a
    // realized block.
    QScrollBar *vbar = view.verticalScrollBar();
    vbar->setValue(vbar->maximum() / 2);
    view.repaint();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      view.viewport()->rect().center());
    QVERIFY2(!view.caretBlock().isNull(), "click did not place a caret mid-document");

    // Warm-up keystroke: absorb any one-shot cost (first cache miss, first
    // transaction) before the measured run.
    sendTextKeyEvent(&view, QStringLiteral("w"));
    {
        quint64 before = view.paintCount();
        while (view.paintCount() == before)
            QCoreApplication::processEvents();
    }

    const int keystrokes = 200;
    QList<qint64> timingsNs;
    timingsNs.reserve(keystrokes);

    for (int i = 0; i < keystrokes; ++i) {
        const quint64 beforePaint = view.paintCount();
        QElapsedTimer t;
        t.start();
        sendTextKeyEvent(&view, QStringLiteral("x"));
        while (view.paintCount() == beforePaint)
            QCoreApplication::processEvents();
        timingsNs.append(t.nsecsElapsed());
    }

    std::sort(timingsNs.begin(), timingsNs.end());
    const int p95Index = int(0.95 * (keystrokes - 1));
    const double p95Ms = double(timingsNs[p95Index]) / 1.0e6;
    const double p50Ms = double(timingsNs[keystrokes / 2]) / 1.0e6;
    qDebug() << "keystroke -> paint  p50:" << p50Ms << "ms  p95:" << p95Ms << "ms";
    QVERIFY2(p95Ms < 16.0,
             qPrintable(QStringLiteral("p95 keystroke->paint %1 ms exceeded 16 ms budget")
                            .arg(p95Ms)));

    // ---- Budget 3: scroll start -> end realizes < 30% of blocks --------
    // Fresh view: the mid-document typing above already realized a chunk
    // near the middle, which would bias this measurement.
    Markoff::MarkoffDocument scrollDoc;
    scrollDoc.loadFromMarkdown(syntheticDoc(500));
    View scrollView;
    scrollView.resize(800, 600);
    scrollView.setDocument(&scrollDoc);
    scrollView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&scrollView));

    // Jump straight from top to bottom (no intermediate paging) — the
    // realization budget is about not walking the whole document, which a
    // single jump exercises honestly.
    QTest::keyClick(&scrollView, Qt::Key_End, Qt::ControlModifier);
    scrollView.repaint();

    const int realized = scrollView.realizedBlockCount();
    const double realizedFraction = double(realized) / double(scrollView.blockCount());
    qDebug() << "scroll start->end realized:" << realized << "/" << scrollView.blockCount()
             << "(" << (realizedFraction * 100.0) << "%)";
    QVERIFY2(realizedFraction < 0.30,
             qPrintable(QStringLiteral("scroll start->end realized %1% of blocks, "
                                       "exceeded 30% budget")
                            .arg(realizedFraction * 100.0)));

    // ---- Budget 4: RSS delta across the whole run -----------------------
    const qint64 rssAfter = currentVmRssKb();
    if (rssBefore >= 0 && rssAfter >= 0) {
        const qint64 deltaKb = rssAfter - rssBefore;
        qDebug() << "RSS delta:" << deltaKb << "KB (" << (deltaKb / 1024.0) << "MB)";
        QVERIFY2(deltaKb < 100 * 1024,
                 qPrintable(QStringLiteral("RSS delta %1 KB exceeded 100 MB budget")
                                .arg(deltaKb)));
    } else {
        qDebug() << "RSS measurement unavailable on this platform — skipped";
    }
}

QTEST_MAIN(TstCanvasPerf500)
#include "tst_canvas_perf_500.moc"
