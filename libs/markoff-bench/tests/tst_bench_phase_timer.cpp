// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/PhaseTimer.h>

#include <thread>
#include <chrono>

class TstBenchPhaseTimer : public QObject {
    Q_OBJECT
private slots:
    void records_into_correct_slot();
    void accumulates_across_calls();
    void zero_elapsed_when_unused();
};

using namespace Markoff::Bench;

void TstBenchPhaseTimer::records_into_correct_slot()
{
    PhaseTable t{};
    {
        PhaseTimer guard(t, Phase::Diff);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    QVERIFY(t[static_cast<int>(Phase::Diff)] >= 1'000'000ull);          // ≥1ms
    QCOMPARE(t[static_cast<int>(Phase::Extract)], 0ull);
    QCOMPARE(t[static_cast<int>(Phase::ParseBlock)], 0ull);
}

void TstBenchPhaseTimer::accumulates_across_calls()
{
    PhaseTable t{};
    for (int i = 0; i < 3; ++i) {
        PhaseTimer guard(t, Phase::Queries);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QVERIFY(t[static_cast<int>(Phase::Queries)] >= 2'500'000ull);       // ≥2.5ms total
}

void TstBenchPhaseTimer::zero_elapsed_when_unused()
{
    PhaseTable t{};
    QCOMPARE(t[static_cast<int>(Phase::Snapshot)], 0ull);
}

QTEST_GUILESS_MAIN(TstBenchPhaseTimer)
#include "tst_bench_phase_timer.moc"
