// SPDX-License-Identifier: GPL-3.0-or-later
//
// E2 H2: Caret-inline rehighlight benchmark.
//
// Verifies that setLocalCaretPosition() on a span-rich block (10 spans)
// stays under 33 ms p99 — the same 30 FPS gate as the E1 typing-perf
// benchmark. 100 iterations; logs p50 + p99 to the console.

#include <QElapsedTimer>
#include <QTest>
#include <QTextDocument>
#include <algorithm>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff::Live;

class TstLiveRenderE2PerfCaretInline : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void caret_move_rehighlight_under_thirty_three_ms_p99()
    {
        // A 200-char text string with ~10 inline spans (alternating bold/italic).
        const QString text = QStringLiteral(
            "Hello **world** this is *italic* and **more bold** text "
            "with ==highlight== and `code` and _underscore_ and ~~strike~~ "
            "and some more text to fill out the line with extra words here.");

        // Build ~10 non-overlapping spans at approximate positions in the text.
        auto makeSpan = [](int off, int len, bool bold, bool italic) {
            Markoff::SourceSpan s{};
            s.charOffset = off;
            s.charLength = len;
            s.bold   = bold;
            s.italic = italic;
            return s;
        };

        const QList<Markoff::SourceSpan> spans = {
            makeSpan(6,   9,  true,  false),   // **world**
            makeSpan(22,  8,  false, true),    // *italic*
            makeSpan(35,  11, true,  false),   // **more bold**
            makeSpan(52,  11, false, false),   // ==highlight==
            makeSpan(68,  6,  false, false),   // `code`
            makeSpan(79,  11, false, true),    // _underscore_
            makeSpan(95,  8,  false, false),   // ~~strike~~
            makeSpan(108, 5,  true,  false),   // filler
            makeSpan(118, 5,  false, true),    // filler
            makeSpan(128, 4,  true,  true),    // filler
        };

        QTextDocument doc;
        doc.setPlainText(text);

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        h.setInlineSpans(spans);

        // Warm-up: allow Qt to finish any one-time initialisation.
        h.setLocalCaretPosition(0);

        QList<qint64> timings;
        timings.reserve(100);

        const int textLen = text.length();
        for (int i = 0; i < 100; ++i) {
            QElapsedTimer t;
            t.start();
            h.setLocalCaretPosition(i % textLen);
            timings.append(t.nsecsElapsed());
        }

        std::sort(timings.begin(), timings.end());
        const double p50ms = static_cast<double>(timings[49]) / 1.0e6;
        const double p99ms = static_cast<double>(timings[98]) / 1.0e6;

        qDebug() << "Caret-move inline rehighlight  p50:" << p50ms
                 << "ms  p99:" << p99ms << "ms";

        QVERIFY2(p99ms < 33.0,
                 qPrintable(QString("p99 %1 ms exceeded 33 ms gate").arg(p99ms)));
    }
};

QTEST_MAIN(TstLiveRenderE2PerfCaretInline)
#include "tst_live_render_e2_perf_caret_inline.moc"
