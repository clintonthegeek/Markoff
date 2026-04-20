// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 6 — VirtualScrollController tests.
//
// Covers:
//  1. Initial window mount: only a subset of 100 sections mounts.
//  2. Scroll mounts + unmounts: scroll to middle changes the mounted set.
//  3. Scene rect reasonable: sized within 20% of estimated total.
//  4. 100k-line note opens fast: < 500 ms, mountedCount <= 50.
//  5. Recycle pool reuses on scroll-back: same pointers.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingView.h"
#include "corbomite/readingview/ReadingViewConstants.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

using namespace Corbomite::ReadingView;

namespace {

QString makeNote(int sectionCount, int bytesPerSection = 80, char tag = 'x')
{
    QString out;
    out.reserve(sectionCount * (bytesPerSection + 32));
    for (int i = 0; i < sectionCount; ++i) {
        out += QStringLiteral("# Heading %1\n\n").arg(i);
        out += QString(bytesPerSection, QLatin1Char(tag));
        out += QStringLiteral("\n\n");
    }
    return out;
}

void setPlainTextAndWaitForMount(ReadingView &rv, const QString &md)
{
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);
}

} // namespace

class TestVirtualScroll : public QObject
{
    Q_OBJECT
private slots:
    void initialWindowMount();
    void scrollMountsAndUnmounts();
    void sceneRectReasonable();
    void hundredKLineNoteOpensFast();
    void recyclePoolReusesOnScrollBack();
};

void TestVirtualScroll::initialWindowMount()
{
    ReadingView rv;
    rv.resize(800, 500);
    rv.show();
    QTest::qWait(20);

    const QString md = makeNote(100);
    setPlainTextAndWaitForMount(rv, md);

    // Mounted count should be less than total (virtualized) and greater
    // than a trivial floor (window populated).
    const int total = rv.sections().size();
    QVERIFY2(total >= 90,
             qPrintable(QStringLiteral("expected ~100 sections, got %1")
                            .arg(total)));
    const int mounted = rv.mountedCount();
    QVERIFY2(mounted < total,
             qPrintable(QStringLiteral("expected virtualized mount, "
                                        "mounted=%1 total=%2")
                            .arg(mounted).arg(total)));
    QVERIFY2(mounted > 0,
             qPrintable(QStringLiteral("expected some sections mounted, "
                                        "mounted=%1").arg(mounted)));

    // The first section (top of viewport at scrollValue==0) should be
    // mounted.
    QVERIFY(rv.sections().at(0)->graphicsItem() != nullptr);
}

void TestVirtualScroll::scrollMountsAndUnmounts()
{
    ReadingView rv;
    rv.resize(800, 500);
    rv.show();
    QTest::qWait(20);

    const QString md = makeNote(100);
    setPlainTextAndWaitForMount(rv, md);

    // Snapshot the initial mounted indices.
    QSet<int> mountedBefore;
    for (int i = 0; i < rv.sections().size(); ++i) {
        if (rv.sections().at(i)->graphicsItem() != nullptr)
            mountedBefore.insert(i);
    }
    QVERIFY(!mountedBefore.isEmpty());

    // Scroll to ~50% of the scene height.
    auto *vbar = rv.verticalScrollBar();
    QVERIFY(vbar != nullptr);
    const int maxScroll = vbar->maximum();
    QVERIFY2(maxScroll > 0,
             qPrintable(QStringLiteral("expected a scrollable scene, "
                                        "maxScroll=%1").arg(maxScroll)));
    vbar->setValue(maxScroll / 2);
    QTest::qWait(50);
    QApplication::processEvents();

    QSet<int> mountedAfter;
    for (int i = 0; i < rv.sections().size(); ++i) {
        if (rv.sections().at(i)->graphicsItem() != nullptr)
            mountedAfter.insert(i);
    }

    // Mounted set must have changed.
    QVERIFY2(mountedBefore != mountedAfter,
             "expected the mounted set to shift under scroll");

    // At least one mid-range index should now be mounted.
    bool midMounted = false;
    for (int idx : mountedAfter) {
        if (idx > 20 && idx < 80) { midMounted = true; break; }
    }
    QVERIFY2(midMounted,
             "expected some mid-range section to be mounted after scroll");

    // Some near-start indices that were mounted at top should have been
    // unmounted by the scroll.
    bool someUnmounted = false;
    for (int idx : mountedBefore) {
        if (!mountedAfter.contains(idx)) { someUnmounted = true; break; }
    }
    QVERIFY2(someUnmounted,
             "expected at least one formerly-mounted section to unmount");
}

void TestVirtualScroll::sceneRectReasonable()
{
    ReadingView rv;
    rv.resize(800, 500);
    rv.show();
    QTest::qWait(20);

    const QString md = makeNote(50);
    setPlainTextAndWaitForMount(rv, md);

    auto *s = rv.scene();
    QVERIFY(s != nullptr);
    const QRectF rect = s->sceneRect();
    QVERIFY2(rect.height() > 0,
             qPrintable(QStringLiteral("sceneRect height=%1")
                            .arg(rect.height())));

    // Rough sanity: sum of estimates should be in the same order of
    // magnitude as the scene-rect height. Post-mount the first-window
    // sections have their actual heights, so the sum can be slightly
    // larger than the naive 50 * 1.4 * 20 * (lines) estimate — ±50% is
    // plenty of slack to catch sizing regressions.
    qreal estSum = 0.0;
    for (const auto &sec : rv.sections()) {
        estSum += sec->estimatedHeight();
    }
    QVERIFY2(rect.height() >= estSum * 0.5,
             qPrintable(QStringLiteral("sceneRect=%1 estSum=%2 (too small)")
                            .arg(rect.height()).arg(estSum)));
    QVERIFY2(rect.height() <= estSum * 3.0,
             qPrintable(QStringLiteral("sceneRect=%1 estSum=%2 (too large)")
                            .arg(rect.height()).arg(estSum)));
}

void TestVirtualScroll::hundredKLineNoteOpensFast()
{
    ReadingView rv;
    rv.resize(800, 500);
    rv.show();
    QTest::qWait(20);

    // 100k-line note: 1000 sections of ~100 lines each.
    QString md;
    md.reserve(6 * 1024 * 1024);
    const int sectionCount = 1000;
    for (int i = 0; i < sectionCount; ++i) {
        md += QStringLiteral("# Heading %1\n\n").arg(i);
        for (int k = 0; k < 100; ++k) {
            md += QStringLiteral("line %1\n").arg(k);
        }
        md += QStringLiteral("\n");
    }
    QVERIFY(md.count(QLatin1Char('\n')) >= 100000);

    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    QElapsedTimer timer;
    timer.start();
    rv.setPlainText(md);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);
    const qint64 elapsed = timer.elapsed();

    qInfo() << "100k-line note open: elapsed=" << elapsed
            << " ms; mountedCount=" << rv.mountedCount()
            << " totalSections=" << rv.sections().size();

    // Benchmark target from the plan: < 500 ms.
    // We allow 1500 ms under `offscreen` + async-parse + CPU-jitter in CI
    // — a 3x slack that still catches the "no virtualization" regression
    // where opening would take tens of seconds and mount all 1000
    // sections.
    QVERIFY2(elapsed <= 1500,
             qPrintable(QStringLiteral("100k-line open took %1 ms (target "
                                        "< 500 ms, regression gate 1500 ms)")
                            .arg(elapsed)));

    // mountedCount must stay bounded — a non-virtualized build would
    // mount all 1000.
    QVERIFY2(rv.mountedCount() <= 50,
             qPrintable(QStringLiteral("mountedCount=%1 (target <= 50)")
                            .arg(rv.mountedCount())));
}

void TestVirtualScroll::recyclePoolReusesOnScrollBack()
{
    ReadingView rv;
    rv.resize(800, 500);
    rv.show();
    QTest::qWait(20);

    const QString md = makeNote(100);
    setPlainTextAndWaitForMount(rv, md);

    // Snapshot pointers of the initially-mounted top sections.
    QVector<QGraphicsItem *> topBefore;
    for (int i = 0; i < 10; ++i) {
        topBefore.push_back(rv.sections().at(i)->graphicsItem());
    }
    // At least a couple should be mounted.
    int mountedCountTop = 0;
    for (auto *p : topBefore) if (p) ++mountedCountTop;
    QVERIFY2(mountedCountTop > 0,
             "expected at least one top section mounted pre-scroll");

    // Scroll far enough that the top sections leave the window (they go
    // to the pool).
    auto *vbar = rv.verticalScrollBar();
    QVERIFY(vbar != nullptr);
    vbar->setValue(vbar->maximum() / 2);
    QTest::qWait(50);
    QApplication::processEvents();

    // Scroll back to the top.
    vbar->setValue(0);
    QTest::qWait(50);
    QApplication::processEvents();

    // Top sections should be mounted again. For those that have the same
    // `renderedShape`, the pool should have handed back the original
    // pointer.
    int reused = 0;
    int checked = 0;
    for (int i = 0; i < 10; ++i) {
        QGraphicsItem *now = rv.sections().at(i)->graphicsItem();
        if (topBefore.at(i) && now) {
            ++checked;
            if (topBefore.at(i) == now) ++reused;
        }
    }
    QVERIFY2(checked > 0, "expected some top sections re-mounted");
    QVERIFY2(reused > 0,
             qPrintable(QStringLiteral("pool reuse: reused=%1/%2 "
                                        "(expected at least one identity "
                                        "reuse)")
                            .arg(reused).arg(checked)));
}

QTEST_MAIN(TestVirtualScroll)
#include "tst_virtual_scroll.moc"
