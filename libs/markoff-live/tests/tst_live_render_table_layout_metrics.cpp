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

using Markoff::Live::TableEditBinding;

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
};

QTEST_MAIN(TstLiveRenderTableLayoutMetrics)
#include "tst_live_render_table_layout_metrics.moc"
