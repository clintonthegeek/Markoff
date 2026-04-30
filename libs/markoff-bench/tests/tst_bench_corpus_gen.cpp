// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-bench/CorpusGen.h>

class TstBenchCorpusGen : public QObject {
    Q_OBJECT
private slots:
    void all_profiles_within_size_tolerance_data();
    void all_profiles_within_size_tolerance();

    void deterministic_for_same_seed();
    void differs_for_different_seed();

    void code_heavy_has_code_blocks();
    void table_heavy_has_tables();
    void footnote_heavy_has_footnotes();
};

using namespace Markoff::Bench;

void TstBenchCorpusGen::all_profiles_within_size_tolerance_data() {
    QTest::addColumn<int>("profile");
    QTest::addColumn<qsizetype>("targetMin");
    QTest::addColumn<qsizetype>("targetMax");
    QTest::newRow("tiny")               << int(CorpusProfile::Tiny)              <<       900LL <<       1200LL;
    QTest::newRow("mid_prose")          << int(CorpusProfile::MidProse)          <<     14000LL <<      18000LL;
    QTest::newRow("mid_mixed")          << int(CorpusProfile::MidMixed)          <<     14000LL <<      18000LL;
    QTest::newRow("big_prose")          << int(CorpusProfile::BigProse)          <<     90000LL <<     110000LL;
    QTest::newRow("big_code_heavy")     << int(CorpusProfile::BigCodeHeavy)      <<     90000LL <<     110000LL;
    QTest::newRow("big_table_heavy")    << int(CorpusProfile::BigTableHeavy)     <<     90000LL <<     110000LL;
    QTest::newRow("big_footnote_heavy") << int(CorpusProfile::BigFootnoteHeavy)  <<     90000LL <<     110000LL;
    QTest::newRow("huge")               << int(CorpusProfile::Huge)              <<    450000LL <<     550000LL;
    QTest::newRow("pathological")       << int(CorpusProfile::Pathological)      <<   1800000LL <<    2200000LL;
}

void TstBenchCorpusGen::all_profiles_within_size_tolerance() {
    QFETCH(int, profile);
    QFETCH(qsizetype, targetMin);
    QFETCH(qsizetype, targetMax);
    const QByteArray bytes = generate(static_cast<CorpusProfile>(profile), 0xBEEF);
    QVERIFY2(bytes.size() >= targetMin && bytes.size() <= targetMax,
             qPrintable(QString("size %1 not in [%2, %3]").arg(bytes.size()).arg(targetMin).arg(targetMax)));
}

void TstBenchCorpusGen::deterministic_for_same_seed() {
    auto a = generate(CorpusProfile::MidMixed, 42);
    auto b = generate(CorpusProfile::MidMixed, 42);
    QCOMPARE(a, b);
}

void TstBenchCorpusGen::differs_for_different_seed() {
    auto a = generate(CorpusProfile::MidMixed, 1);
    auto b = generate(CorpusProfile::MidMixed, 2);
    QVERIFY(a != b);
}

void TstBenchCorpusGen::code_heavy_has_code_blocks() {
    auto bytes = generate(CorpusProfile::BigCodeHeavy, 0xBEEF);
    QVERIFY2(bytes.contains("```"), "expected fenced code blocks");
    QVERIFY(bytes.count("```") >= 60);
}

void TstBenchCorpusGen::table_heavy_has_tables() {
    auto bytes = generate(CorpusProfile::BigTableHeavy, 0xBEEF);
    QVERIFY2(bytes.contains("\n| "), "expected pipe tables");
}

void TstBenchCorpusGen::footnote_heavy_has_footnotes() {
    auto bytes = generate(CorpusProfile::BigFootnoteHeavy, 0xBEEF);
    QVERIFY2(bytes.contains("[^"), "expected footnote refs");
}

QTEST_GUILESS_MAIN(TstBenchCorpusGen)
#include "tst_bench_corpus_gen.moc"
