// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/Scenario.h>

class TstBenchScenario : public QObject {
    Q_OBJECT
private slots:
    void scenario_meta_table();
    void type_end_appends_one_byte_per_iter();
    void type_start_inserts_at_offset_zero();
    void block_boundary_inserts_blank_line();
    void paste_4kb_makes_4096_byte_insert();
    void replace_1kb_deletes_and_inserts();
    void cold_parse_emits_no_steps();
};

using namespace Markoff;
using namespace Markoff::Bench;

void TstBenchScenario::scenario_meta_table() {
    const ScenarioMeta m = scenarioMeta(ScenarioKind::TypeEnd);
    QCOMPARE(QString(m.name), QString("type_end"));
    QCOMPARE(m.warmupIters, 20);
    QCOMPARE(m.measuredIters, 180);
}

void TstBenchScenario::type_end_appends_one_byte_per_iter() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::TypeEnd, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, quint32(doc.size()));
    QCOMPARE(e.oldEnd,   quint32(doc.size()));
    QCOMPARE(e.newText.size(), 1);
}

void TstBenchScenario::type_start_inserts_at_offset_zero() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::TypeStart, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, 0u);
    QCOMPARE(e.oldEnd,   0u);
    QCOMPARE(e.newText.size(), 1);
}

void TstBenchScenario::block_boundary_inserts_blank_line() {
    QByteArray doc("Para A.\n\nPara B.\n");
    const MarkoffEdit e = nextStep(ScenarioKind::BlockBoundary, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, e.oldEnd);
    QVERIFY(e.newText == QByteArray("\n") || e.newText == QByteArray("\n\n"));
}

void TstBenchScenario::paste_4kb_makes_4096_byte_insert() {
    QByteArray doc("hello\n");
    const MarkoffEdit e = nextStep(ScenarioKind::Paste4Kb, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldStart, e.oldEnd);
    QCOMPARE(e.newText.size(), 4096);
}

void TstBenchScenario::replace_1kb_deletes_and_inserts() {
    QByteArray doc(2048, 'x');
    doc.append('\n');
    const MarkoffEdit e = nextStep(ScenarioKind::Replace1Kb, doc, 0, /*seed*/ 1);
    QCOMPARE(e.oldEnd - e.oldStart, 1024u);
    QCOMPARE(e.newText.size(), 1024);
}

void TstBenchScenario::cold_parse_emits_no_steps() {
    const ScenarioMeta m = scenarioMeta(ScenarioKind::ColdParse);
    QCOMPARE(m.measuredIters, 1);
    QCOMPARE(m.warmupIters, 0);
}

QTEST_GUILESS_MAIN(TstBenchScenario)
#include "tst_bench_scenario.moc"
