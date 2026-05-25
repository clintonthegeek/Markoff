// SPDX-License-Identifier: GPL-3.0-or-later
//
// E4 follow-up A1 — per-cell column-width metric unit tests. Drives
// TableEditBinding's public static helpers directly (no QML).
//
// Extended in A2 with `distributeColumnsAuto` slots and in A3 with
// `computeColumnWidths` slots.
//
// Spec: docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md §3.1
// Plan: docs/plans/2026-05-23-e4-cell-wrap-and-column-width.md A1

#include <markoff/live/TableEditBinding.h>

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QTest>
#include <QtMath>

using Markoff::Live::TableEditBinding;
using Metrics = TableEditBinding::ColumnMetrics;

class TstLiveRenderTableLayoutMetrics : public QObject
{
    Q_OBJECT

private:
    QFont m_font;
    qreal m_padding = 4.0;

private Q_SLOTS:
    void initTestCase()
    {
        // Use the default app font so the test is platform-stable —
        // the algorithm correctness is what's under test, not specific
        // pixel values.
        m_font = QGuiApplication::font();
    }

    // ---- A1: cellMinWidth / cellMaxWidth ----

    void cellMinWidth_emptyString_returnsKMinColumnWidth()
    {
        const qreal w = TableEditBinding::cellMinWidth(QString(), m_font, m_padding);
        QCOMPARE(w, TableEditBinding::kMinColumnWidth);
    }

    void cellMinWidth_singleWord_returnsWordAdvancePlusPadding_flooredAt60()
    {
        const QString word = QStringLiteral("Hello");
        QFontMetricsF fm(m_font);
        const qreal expected =
            qMax(fm.horizontalAdvance(word) + 2 * m_padding,
                 TableEditBinding::kMinColumnWidth);
        const qreal w = TableEditBinding::cellMinWidth(word, m_font, m_padding);
        QCOMPARE(w, expected);
    }

    void cellMinWidth_multipleWords_returnsLongestWordWidth()
    {
        const QString text = QStringLiteral("short verylongword x");
        QFontMetricsF fm(m_font);
        // The widest token is "verylongword"; per-token advance + padding,
        // then floored at kMinColumnWidth.
        const qreal expected =
            qMax(fm.horizontalAdvance(QStringLiteral("verylongword")) + 2 * m_padding,
                 TableEditBinding::kMinColumnWidth);
        const qreal w = TableEditBinding::cellMinWidth(text, m_font, m_padding);
        QCOMPARE(w, expected);
    }

    void cellMaxWidth_returnsFullStringWidth()
    {
        const QString text = QStringLiteral("the quick brown fox");
        QFontMetricsF fm(m_font);
        const qreal expected = fm.horizontalAdvance(text) + 2 * m_padding;
        const qreal w = TableEditBinding::cellMaxWidth(text, m_font, m_padding);
        QCOMPARE(w, expected);
    }

    void cellMaxWidth_emptyString_returnsJustPadding()
    {
        // Padding-only; max isn't floored at kMinColumnWidth — the column
        // aggregation floor handles that downstream.
        const qreal w = TableEditBinding::cellMaxWidth(QString(), m_font, m_padding);
        QCOMPARE(w, 2 * m_padding);
    }

    // ---- A2: distributeColumnsAuto (Penelope port) ----------------
    //
    // Three branches from spec §3.3:
    //   1. totalMax <= avail    — distribute surplus evenly above maxes.
    //   2. totalMin >= avail    — scale mins proportionally (aggressive wrap).
    //   3. proportional         — between min and max via W/D.
    // Plus a sums-to-avail sanity check across all three.

    void auto_totalMaxLeqAvail_distributesSurplusEvenly()
    {
        QList<Metrics> m { {50, 100}, {50, 100}, {50, 100} };
        const qreal avail = 400.0;
        // totalMax = 300; surplus = 100; per-col +100/3.
        const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
        QCOMPARE(widths.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(widths[i], 100.0 + 100.0 / 3.0);
    }

    void auto_totalMinGeqAvail_scalesMinsProportionally()
    {
        QList<Metrics> m { {100, 200}, {100, 200}, {100, 200} };
        const qreal avail = 150.0;
        // totalMin = 300 > avail; each col gets 100 * 150/300 = 50.
        const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
        QCOMPARE(widths.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(widths[i], 50.0);
    }

    void auto_proportionalBranch_distributesBetweenMinAndMax()
    {
        QList<Metrics> m { {50, 200}, {50, 100} };
        const qreal avail = 225.0;
        // totalMin=100, totalMax=300, W=125, D=200.
        // col 0: 50 + 150 * 125/200 = 50 + 93.75 = 143.75.
        // col 1: 50 +  50 * 125/200 = 50 + 31.25 =  81.25.
        const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
        QCOMPARE(widths.size(), 2);
        QCOMPARE(widths[0], 143.75);
        QCOMPARE(widths[1],  81.25);
    }

    void auto_sumsToAvailWidth_acrossAllBranches()
    {
        // Branch 1.
        {
            QList<Metrics> m { {50, 100}, {50, 100}, {50, 100} };
            const qreal avail = 400.0;
            const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
            qreal sum = 0;
            for (auto w : widths) sum += w;
            QVERIFY(qFabs(sum - avail) < 1e-6);
        }
        // Branch 2.
        {
            QList<Metrics> m { {100, 200}, {100, 200}, {100, 200} };
            const qreal avail = 150.0;
            const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
            qreal sum = 0;
            for (auto w : widths) sum += w;
            QVERIFY(qFabs(sum - avail) < 1e-6);
        }
        // Branch 3.
        {
            QList<Metrics> m { {50, 200}, {50, 100} };
            const qreal avail = 225.0;
            const auto widths = TableEditBinding::distributeColumnsAuto(m, avail);
            qreal sum = 0;
            for (auto w : widths) sum += w;
            QVERIFY(qFabs(sum - avail) < 1e-6);
        }
    }

    void auto_emptyMetrics_returnsEmptyList()
    {
        const auto widths = TableEditBinding::distributeColumnsAuto({}, 400.0);
        QCOMPARE(widths.size(), 0);
    }

    // ---- A3: computeColumnWidths Q_INVOKABLE ----------------------
    //
    // End-to-end metric-aggregation + distribution via the public API.
    // Uses QGuiApplication's default font (no Theme dependency in this
    // test — the test exercises the algorithm shape, not theme wiring).
    // The theme-wiring path is exercised by the QML integration test
    // in C2.

    void computeColumnWidths_emptyHeaders_returnsEmptyList()
    {
        TableEditBinding teb;
        const auto out = teb.computeColumnWidths(QVariantList(),
                                                 QVariantList(), 400.0);
        QCOMPARE(out.size(), 0);
    }

    void computeColumnWidths_nonPositiveAvail_returnsEmptyList()
    {
        TableEditBinding teb;
        QVariantList headers { QStringLiteral("a"), QStringLiteral("b") };
        QVariantList body;
        QCOMPARE(teb.computeColumnWidths(headers, body,  0.0).size(), 0);
        QCOMPARE(teb.computeColumnWidths(headers, body, -1.0).size(), 0);
    }

    void computeColumnWidths_aggregatesMaxAcrossRows()
    {
        // Headers: short labels. Body has a long string in column 0,
        // short in column 1. Expect col 0 width to dominate col 1.
        TableEditBinding teb;
        QVariantList headers { QStringLiteral("a"), QStringLiteral("b") };
        QVariantList row0 {
            QStringLiteral("Lorem ipsum dolor sit amet consectetur"),
            QStringLiteral("x")
        };
        QVariantList body { QVariant::fromValue(row0) };
        const auto out = teb.computeColumnWidths(headers, body, 800.0);
        QCOMPARE(out.size(), 2);
        QVERIFY(out[0].toReal() > out[1].toReal());
    }

    void computeColumnWidths_sumsToAvailWidth()
    {
        TableEditBinding teb;
        QVariantList headers { QStringLiteral("Short"),
                               QStringLiteral("Description"),
                               QStringLiteral("Code") };
        QVariantList row0 { QStringLiteral("x"),
                            QStringLiteral("Lorem ipsum dolor sit amet"),
                            QStringLiteral("y") };
        QVariantList row1 { QStringLiteral("z"),
                            QStringLiteral("consectetur adipiscing elit"),
                            QStringLiteral("w") };
        QVariantList body {
            QVariant::fromValue(row0),
            QVariant::fromValue(row1)
        };
        // Sweep three widths spanning the three branches.
        for (qreal avail : { 200.0, 500.0, 2000.0 }) {
            const auto out = teb.computeColumnWidths(headers, body, avail);
            QCOMPARE(out.size(), 3);
            qreal sum = 0;
            for (const QVariant &v : out) sum += v.toReal();
            QVERIFY2(qFabs(sum - avail) < 1e-3,
                     qPrintable(QStringLiteral("avail=%1 sum=%2")
                                .arg(avail).arg(sum)));
        }
    }
};

QTEST_MAIN(TstLiveRenderTableLayoutMetrics)
#include "tst_live_render_table_layout_metrics.moc"
