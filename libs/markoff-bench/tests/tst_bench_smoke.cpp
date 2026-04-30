// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/Scenario.h>
#include <markoff-bench/ScenarioRunner.h>

#include <QJsonDocument>

class TstBenchSmoke : public QObject {
    Q_OBJECT
private slots:
    void direct_parse_mid_prose_type_end_runs();
};

using namespace Markoff::Bench;

void TstBenchSmoke::direct_parse_mid_prose_type_end_runs() {
    const QByteArray corpus = generate(CorpusProfile::MidProse, 0xBEEF);
    QVERIFY(!corpus.isEmpty());

    RunResult r = runDirectParse(corpus, ScenarioKind::TypeEnd, /*seed*/ 0xBEEF);
    r.profileName = profileName(CorpusProfile::MidProse);

    // Sanity: warmup excluded, measured count matches the scenario meta.
    QCOMPARE(r.warmupIters, 20);
    QCOMPARE(r.iterations, 180);
    QCOMPARE(static_cast<int>(r.totalNs.count), 180);
    QVERIFY2(r.totalNs.p50 > 0, "p50 must be positive");
    QVERIFY2(r.totalNs.p99 >= r.totalNs.p50, "p99 must be ≥ p50");

    // Dump JSON to stderr for human inspection.
    const QJsonDocument doc(toJson(r));
    qInfo().noquote() << doc.toJson(QJsonDocument::Indented);
}

QTEST_GUILESS_MAIN(TstBenchSmoke)
#include "tst_bench_smoke.moc"
