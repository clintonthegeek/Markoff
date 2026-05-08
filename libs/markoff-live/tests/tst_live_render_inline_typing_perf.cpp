// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Performance benchmark for the inline highlighter typing path.
//
// Measures rehighlight() cost on a realistic short block (bold+italic
// paragraph). 100 iterations; asserts p99 < 33 ms (30 FPS budget).
// Logs p50 + p99 to the console for tracking across hardware.

#include <QElapsedTimer>
#include <QTest>
#include <QTextDocument>
#include <algorithm>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff::Live;

class TstLiveRenderInlineTypingPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void typing_under_thirty_three_ms_p99()
    {
        // Realistic paragraph text — mirrors the n==1 rows in typing-corpus-1k.md.
        const QString text =
            QStringLiteral("Paragraph 1 with **bold word** and *italic word* mixed.");

        // Build a representative span list matching the text above.
        // **bold word** starts at offset 16 (len 11 including markers),
        // *italic word* starts at offset 32 (len 13 including markers).
        Markoff::SourceSpan bold{};
        bold.charOffset = 16;
        bold.charLength = 11;
        bold.bold = true;

        Markoff::SourceSpan italic{};
        italic.charOffset = 32;
        italic.charLength = 13;
        italic.italic = true;

        const QList<Markoff::SourceSpan> spans = {bold, italic};

        QTextDocument doc;
        doc.setPlainText(text);

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        h.setInlineSpans(spans);

        // Warm-up pass — allow Qt to finish any one-time initialisation.
        h.rehighlight();

        QList<qint64> timings;
        timings.reserve(100);

        for (int i = 0; i < 100; ++i) {
            QElapsedTimer t;
            t.start();
            h.rehighlight();
            timings.append(t.nsecsElapsed());
        }

        std::sort(timings.begin(), timings.end());
        const double p50ms = static_cast<double>(timings[49]) / 1.0e6;
        const double p99ms = static_cast<double>(timings[98]) / 1.0e6;

        qDebug() << "Per-keystroke rehighlight timing  p50:" << p50ms
                 << "ms  p99:" << p99ms << "ms";

        QVERIFY2(p99ms < 33.0,
                 qPrintable(QString("p99 %1 ms exceeded 33 ms gate").arg(p99ms)));
    }
};

QTEST_MAIN(TstLiveRenderInlineTypingPerf)
#include "tst_live_render_inline_typing_perf.moc"
