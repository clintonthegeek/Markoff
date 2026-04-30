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

    // Per-phase splits must populate on Tier 1 direct_parse. Each parse-side
    // phase has measurable cost on a 16 KB doc edit.
    auto phase = [&](Phase p) { return r.phases[static_cast<int>(p)].p50; };
    QVERIFY2(phase(Phase::Extract)     > 0, qPrintable(QString("phase_extract p50=%1").arg(phase(Phase::Extract))));
    QVERIFY2(phase(Phase::Diff)        > 0, qPrintable(QString("phase_diff p50=%1").arg(phase(Phase::Diff))));
    QVERIFY2(phase(Phase::ParseBlock)  > 0, qPrintable(QString("phase_parse_block p50=%1").arg(phase(Phase::ParseBlock))));
    QVERIFY2(phase(Phase::ParseInline) > 0, qPrintable(QString("phase_parse_inline p50=%1").arg(phase(Phase::ParseInline))));
    QVERIFY2(phase(Phase::Queries)     > 0, qPrintable(QString("phase_queries p50=%1").arg(phase(Phase::Queries))));
    QVERIFY2(phase(Phase::Snapshot)    > 0, qPrintable(QString("phase_snapshot p50=%1").arg(phase(Phase::Snapshot))));

    // Dump JSON to stderr for human inspection.
    const QJsonDocument doc(toJson(r));
    qInfo().noquote() << doc.toJson(QJsonDocument::Indented);
}

QTEST_GUILESS_MAIN(TstBenchSmoke)
#include "tst_bench_smoke.moc"
