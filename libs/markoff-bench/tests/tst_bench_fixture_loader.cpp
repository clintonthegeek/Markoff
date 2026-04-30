// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/FixtureLoader.h>

class TstBenchFixtureLoader : public QObject {
    Q_OBJECT
private slots:
    void loads_foundation_design();
    void loads_typing_perf_plan();
    void unknown_name_returns_empty();
};

using namespace Markoff::Bench;

void TstBenchFixtureLoader::loads_foundation_design() {
    const QByteArray bytes = loadFixture("foundation-design");
    QVERIFY2(!bytes.isEmpty(), "fixture should not be empty");
    QVERIFY(bytes.contains("foundation"));
}

void TstBenchFixtureLoader::loads_typing_perf_plan() {
    const QByteArray bytes = loadFixture("typing-perf-plan");
    QVERIFY(!bytes.isEmpty());
    QVERIFY(bytes.contains("perf"));
}

void TstBenchFixtureLoader::unknown_name_returns_empty() {
    const QByteArray bytes = loadFixture("does-not-exist");
    QVERIFY(bytes.isEmpty());
}

QTEST_GUILESS_MAIN(TstBenchFixtureLoader)
#include "tst_bench_fixture_loader.moc"
