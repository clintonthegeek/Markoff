// SPDX-License-Identifier: GPL-3.0-or-later
//
// P2.4 — perf re-baseline addendum (exit E9).
//
// tst_canvas_perf_500's mid-document keystroke run lands in a plain
// paragraph with no spans nearby, so the projection map for that block is
// trivial (no omitted delimiter runs) — it does not exercise the P2.1-P2.3
// reveal/omission machinery on the hot per-keystroke path. This test types
// with the caret INSIDE a revealed emphasis/strong span in a single
// formatted paragraph (delimiters shown, ProjectionMap non-trivial, full
// per-block rebuild on every content-changing keystroke per P2.1's
// restyleInline() note), the worst case the projection map was built to
// cover. One budget, asserted: p95 keystroke -> next paint < 16 ms.

#include <QElapsedTimer>
#include <QTest>
#include <algorithm>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

namespace {

/// QTest::keyClicks() can't represent all input this test wants to drive
/// uniformly; a direct QKeyEvent with the target text is the same real
/// event path View::keyPressEvent reads (event->text()), same helper as
/// tst_canvas_perf_500.cpp / tst_canvas_typing.cpp.
void sendTextKeyEvent(QWidget *w, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

}  // namespace

class TstCanvasPerfFormatted : public QObject {
    Q_OBJECT

private slots:
    void p95_keystroke_inside_a_revealed_formatted_span();
};

void TstCanvasPerfFormatted::p95_keystroke_inside_a_revealed_formatted_span()
{
    // One long paragraph, several bold/italic spans, padded to a realistic
    // editor line width — the caret lands inside the first bold span so its
    // "**" delimiters stay revealed (P2.1 §4.2 reveal window) for the whole
    // run, and the two later spans keep the block's ProjectionMap carrying
    // more than one kept-run boundary.
    // Byte layout of interest: the opening "**" starts at byte 27, so caret
    // byte 29 sits immediately after it — right before 'b' of "bold",
    // inside the span, delimiters revealed. The closing "**" starts at
    // byte 78.
    const QByteArray para =
        "Intro text before the span **bold content here that is fairly "
        "long for realism** and then some *italic content also fairly "
        "long here for realism* and more plain text padding out to a "
        "realistic paragraph width for perf testing purposes today.\n";

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(para);

    View view;
    view.resize(800, 600);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(view.paintCount() > 0);
    QCOMPARE(view.blockCount(), 1);

    const BlockId block = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretBlock(), block);

    // Place the caret at byte 29 — right after the opening "**", inside the
    // bold span. Direct placement (not an arrow-key walk): [cluster-k] P6
    // narrowed the delimiter reveal radius, which makes a single Right
    // hop over a still-hidden delimiter run skip straight past its far
    // edge to the first touched position — a real, intentional behavior
    // change to *how many keypresses* land where, orthogonal to what this
    // test is actually about (the perf budget once the caret is settled
    // inside a revealed span), so pin the position directly instead of
    // coupling this test to that hop-count arithmetic.
    view.setCaretPosition(block, 29);
    QCOMPARE(view.caretByteOffset(), 29);

    // Confirm the reveal precondition the test is named for: both "**"
    // delimiter runs of the span the caret sits in are shown.
    QVERIFY(!view.isDelimiterHiddenAt(block, 27));  // opening "**" at byte 27-28
    QVERIFY(!view.isDelimiterHiddenAt(block, 78));  // closing "**" at byte 78-79

    // Warm-up keystroke: absorb any one-shot cost before the measured run.
    sendTextKeyEvent(&view, QStringLiteral("w"));
    {
        quint64 before = view.paintCount();
        while (view.paintCount() == before)
            QCoreApplication::processEvents();
    }

    const int keystrokes = 200;
    QList<qint64> timingsNs;
    timingsNs.reserve(keystrokes);

    for (int i = 0; i < keystrokes; ++i) {
        const quint64 beforePaint = view.paintCount();
        QElapsedTimer t;
        t.start();
        sendTextKeyEvent(&view, QStringLiteral("x"));
        while (view.paintCount() == beforePaint)
            QCoreApplication::processEvents();
        timingsNs.append(t.nsecsElapsed());
    }

    // The caret never left the span (only insertions of plain 'x', no
    // navigation), so reveal state stayed "shown" throughout — the
    // per-keystroke cost measured here is ProjectionMap rebuild + restyle
    // on a non-trivial map, not a reveal-state transition itself.
    QVERIFY(!view.isDelimiterHiddenAt(block, 27));

    std::sort(timingsNs.begin(), timingsNs.end());
    const int p95Index = int(0.95 * (keystrokes - 1));
    const double p95Ms = double(timingsNs[p95Index]) / 1.0e6;
    const double p50Ms = double(timingsNs[keystrokes / 2]) / 1.0e6;
    qDebug() << "formatted-paragraph keystroke -> paint  p50:" << p50Ms
             << "ms  p95:" << p95Ms << "ms";
    QVERIFY2(p95Ms < 16.0,
             qPrintable(QStringLiteral("p95 keystroke->paint (formatted paragraph) %1 ms "
                                       "exceeded 16 ms budget")
                            .arg(p95Ms)));
}

QTEST_MAIN(TstCanvasPerfFormatted)
#include "tst_canvas_perf_formatted.moc"
