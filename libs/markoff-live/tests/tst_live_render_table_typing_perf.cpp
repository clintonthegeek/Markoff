// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-keystroke cost attribution for typing inside a TableDelegate cell.
//
// Loads the canonical `tables_basic.md` fixture (two tables surrounded by
// paragraphs), focuses a cell, types N characters, then dumps the
// `Markoff::Perf::Probe` accumulators. Asserts nothing about absolute
// timings (hardware-dependent); the test exists to surface per-stage
// counts and cumulative microseconds so we can reason about which layer
// dominates a keystroke and verify improvements move the right number.
//
// Probes instrumented (see PerfProbe.h):
//   parser.TreeSitterParser::parse              full block+inline parse
//   parser.block_grammar                        ts_parser_parse_string (block)
//   parser.inline_grammar_one_range             ts_parser_parse_string (per inline range)
//   parser.buildSpanMap                         CST → SourceSpan walk
//   parser.inlineSpansFor(QByteArray)           free-fn entry point
//   core.InlineParseCache::spansFor             per-block cache call
//   core.InlineParseCache.hit / .miss           cache effectiveness
//   live.LiveListModelBinding::onD2Changed      full model rebuild cascade
//   live.buildRecords.inlineSpansFor            per-row inline span fetch
//   live.TableEditBinding::applyCellEdit        the typing entry point
//   live.TableEditBinding::inlineSpansForCell   per-cell span filter+reproject
//   live.InlineHighlighter::setInlineSpans+rehighlight  span-driven rehighlight
//   live.InlineHighlighter::setInlineSpans.noop unchanged-spans short-circuit
//   live.InlineHighlighter::highlightBlock      per-line format pass
//   qml.TableDelegate.parseTable                JS tokenizer (Date.now ms res)
//   qml.TableDelegate.cellText.eval             cell text binding re-eval count

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/parser/PerfProbe.h>

#include <QFile>
#include <QQuickItem>
#include <QString>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

namespace {

QQuickItem *findTableDelegateAtRow(QmlIntegrationFixture &fx, int row)
{
    QQuickItem *d = fx.delegateAt(row);
    if (!d) return nullptr;
    return QString::fromUtf8(d->metaObject()->className())
               .contains("TableDelegate") ? d : nullptr;
}

QQuickItem *cellAt(QQuickItem *table, int r, int c)
{
    if (!table) return nullptr;
    QQuickItem *repeater = nullptr;
    for (QQuickItem *k : table->findChildren<QQuickItem *>()) {
        if (QString::fromUtf8(k->metaObject()->className())
                .contains("Repeater")) {
            repeater = k;
            break;
        }
    }
    if (!repeater) return nullptr;
    const int cols = table->property("parsedTable").toMap()
                         .value("headers").toList().size();
    if (cols < 1) return nullptr;
    QQuickItem *cell = nullptr;
    QMetaObject::invokeMethod(repeater, "itemAt",
                              Q_RETURN_ARG(QQuickItem *, cell),
                              Q_ARG(int, r * cols + c));
    return cell;
}

QQuickItem *cellEditAt(QQuickItem *table, int r, int c)
{
    QQuickItem *cell = cellAt(table, r, c);
    return cell ? cell->property("edit").value<QQuickItem *>() : nullptr;
}

}  // namespace

class TestTableTypingPerf : public QObject {
    Q_OBJECT
private slots:
    void per_keystroke_attribution_small_table_plain_cell();
    void per_keystroke_attribution_large_table_bold_cell();
    void per_keystroke_attribution_large_table_plain_cell();
};

namespace {

void runTypingBenchmark(const char *label,
                        int tableRow, int r, int c,
                        int charsToType)
{
    QFile f(QString::fromLatin1(MARKOFF_LIVE_TESTS_DIR)
                + QStringLiteral("/fixtures/tables_basic.md"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray md = f.readAll();
    f.close();

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/5);
    QVERIFY(fx.waitForDelegateAt(tableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, tableRow);
    QVERIFY(table);
    QTRY_VERIFY(cellAt(table, r, c) != nullptr);

    QQuickItem *cellEdit = cellEditAt(table, r, c);
    QVERIFY(cellEdit);

    cellEdit->setProperty("cursorPosition", 1);
    cellEdit->forceActiveFocus();
    QTRY_VERIFY(cellEdit->hasActiveFocus());

    // Warm-up: type once to trigger any one-shot work (lazy parsers,
    // first-rehighlight setup, JIT QML compile of bindings).
    fx.harness().typeChar(QLatin1Char('w'));
    QTest::qWait(50);

    // Reset probes AFTER warm-up so the dump reflects steady-state cost
    // for the remaining (charsToType - 1) characters.
    Markoff::Perf::Probe::instance().reset();
    const int steadyCount = charsToType - 1;
    for (int i = 0; i < steadyCount; ++i) {
        fx.harness().typeChar(QLatin1Char('x'));
        // Yield so flushPendingD2Changed and the binding cascade settle
        // between keystrokes. Without this, multiple keystrokes coalesce
        // into one cascade and the per-keystroke attribution is wrong.
        QTest::qWait(10);
    }
    // Final settle.
    QTest::qWait(100);

    qDebug().noquote() << QString::fromLatin1(
        "\n--- %1: %2 steady-state keystrokes (after 1 warm-up) ---")
        .arg(QString::fromLatin1(label)).arg(steadyCount);
    Markoff::Perf::Probe::instance().dump(label);
}

}  // namespace

void TestTableTypingPerf::per_keystroke_attribution_small_table_plain_cell()
{
    // Small 3-col × 3-body-row table at fixture row 1. Cell (1, 0) is
    // "cell 1   " — plain text, no inline syntax. The parser fast-path
    // should skip this cell's range entirely.
    runTypingBenchmark("small_table_plain_cell(1,0)",
                       /*tableRow=*/1, /*r=*/1, /*c=*/0, /*charsToType=*/20);
}

void TestTableTypingPerf::per_keystroke_attribution_large_table_bold_cell()
{
    // Large 4-col × 5-body-row table at fixture row 3. Cell (2, 0) is
    // "**bold cell**" — has inline syntax, so the parser fast-path does
    // NOT skip it. This is the worst case for per-keystroke parser cost
    // because each keystroke also re-runs the inline parser on that cell.
    runTypingBenchmark("large_table_bold_cell(2,0)",
                       /*tableRow=*/3, /*r=*/2, /*c=*/0, /*charsToType=*/20);
}

void TestTableTypingPerf::per_keystroke_attribution_large_table_plain_cell()
{
    // Large table, plain cell — typing into a cell with no inline syntax
    // in a 4×6 table (24 cells, 9 trigger-bearing cells). Lets us
    // isolate the per-cell rebind/highlighter cost from the
    // per-trigger-cell parse cost.
    runTypingBenchmark("large_table_plain_cell(1,1)",
                       /*tableRow=*/3, /*r=*/1, /*c=*/1, /*charsToType=*/20);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableTypingPerf)
#include "tst_live_render_table_typing_perf.moc"
