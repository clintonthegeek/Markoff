// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/PercentileReducer.h>

#include <vector>
#include <numeric>

class TstBenchPercentile : public QObject {
    Q_OBJECT
private slots:
    void empty_input_yields_zeros();
    void single_value();
    void monotonic_sequence();
    void unsorted_does_not_mutate();
};

using namespace Markoff::Bench;

void TstBenchPercentile::empty_input_yields_zeros()
{
    std::vector<quint64> v;
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 0u);
    QCOMPARE(d.p50, 0ull);
    QCOMPARE(d.p95, 0ull);
    QCOMPARE(d.p99, 0ull);
    QCOMPARE(d.max, 0ull);
    QCOMPARE(d.min, 0ull);
}

void TstBenchPercentile::single_value()
{
    std::vector<quint64> v{42};
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 1u);
    QCOMPARE(d.p50, 42ull);
    QCOMPARE(d.p95, 42ull);
    QCOMPARE(d.p99, 42ull);
    QCOMPARE(d.max, 42ull);
    QCOMPARE(d.min, 42ull);
}

void TstBenchPercentile::monotonic_sequence()
{
    std::vector<quint64> v(100);
    std::iota(v.begin(), v.end(), 1);   // 1..100
    Distribution d = reducePercentiles(v);
    QCOMPARE(d.count, 100u);
    QCOMPARE(d.min, 1ull);
    QCOMPARE(d.max, 100ull);
    // Nearest-rank: p50 = ceil(0.50 * 100) = 50th element (1-indexed) = 50
    QCOMPARE(d.p50, 50ull);
    QCOMPARE(d.p95, 95ull);
    QCOMPARE(d.p99, 99ull);
}

void TstBenchPercentile::unsorted_does_not_mutate()
{
    std::vector<quint64> v{3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    std::vector<quint64> copy = v;
    Distribution d = reducePercentiles(v);
    QCOMPARE(v, copy);                        // input unchanged
    QCOMPARE(d.min, 1ull);
    QCOMPARE(d.max, 9ull);
}

QTEST_GUILESS_MAIN(TstBenchPercentile)
#include "tst_bench_percentile.moc"
