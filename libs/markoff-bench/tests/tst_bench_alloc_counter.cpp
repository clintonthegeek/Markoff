// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/AllocCounter.h>

#include <string>
#include <vector>

class TstBenchAllocCounter : public QObject {
    Q_OBJECT
private slots:
    void disabled_by_default();
    void counts_when_enabled();
    void scope_guard_resets();
};

using namespace Markoff::Bench;

void TstBenchAllocCounter::disabled_by_default()
{
    AllocSnapshot before = currentAllocSnapshot();
    auto *p = new int(7);
    delete p;
    AllocSnapshot after = currentAllocSnapshot();
    // Disabled → counters unchanged regardless of allocations.
    QCOMPARE(before.bytes, after.bytes);
    QCOMPARE(before.count, after.count);
}

void TstBenchAllocCounter::counts_when_enabled()
{
    {
        AllocCounterScope scope;     // enables + zeros TLS counters
        std::vector<int> v;
        v.reserve(1024);             // ≥ one heap allocation
        AllocSnapshot snap = currentAllocSnapshot();
        QVERIFY2(snap.count >= 1, "expected at least one allocation");
        QVERIFY2(snap.bytes >= sizeof(int) * 1024, "expected ≥4096 bytes counted");
    }
    // Out of scope: disabled again.
    AllocSnapshot snap = currentAllocSnapshot();
    QCOMPARE(snap.count, 0u);
    QCOMPARE(snap.bytes, 0ull);
}

void TstBenchAllocCounter::scope_guard_resets()
{
    {
        AllocCounterScope scope;
        auto *p = new int[64];
        delete[] p;
        AllocSnapshot s1 = currentAllocSnapshot();
        QVERIFY(s1.count >= 1);
    }
    {
        AllocCounterScope scope;     // fresh scope → counters back to zero
        AllocSnapshot s2 = currentAllocSnapshot();
        QCOMPARE(s2.count, 0u);
        QCOMPARE(s2.bytes, 0ull);
    }
}

QTEST_GUILESS_MAIN(TstBenchAllocCounter)
#include "tst_bench_alloc_counter.moc"
