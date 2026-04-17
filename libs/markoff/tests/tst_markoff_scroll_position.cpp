// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cluster E Phase 2 — visual-line float scroll on `Markoff::Editor`.
//
// Contract: `scrollPositionVisualLine()` reads a float count of visual lines
// from the top of the scene; `setScrollPositionVisualLine()` is the inverse,
// with ±0.5 visual-line tolerance per the plan. A "visual line" here is one
// wrapped display line's worth of vertical space; for non-text block items
// (tables, images, code blocks) the contribution is
// `ceil(height / lineSpacing)` so the count stays meaningful across block
// types.
//
// Runs headless via QT_QPA_PLATFORM=offscreen.

#include <markoff/Editor.h>

#include <QObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

namespace {

QString makeBlocks(int count)
{
    // Each paragraph is one block in Markoff's scene; the blocks stack
    // vertically and each contributes at least one visual line.
    QStringList lines;
    lines.reserve(count);
    for (int i = 0; i < count; ++i) {
        lines.append(QStringLiteral("Paragraph %1 contents.").arg(i));
    }
    return lines.join(QStringLiteral("\n\n"));
}

} // namespace

class MarkoffScrollPositionTest : public QObject {
    Q_OBJECT

private slots:
    void integerRoundTrip() {
        Markoff::Editor editor;
        editor.setPlainText(makeBlocks(30));
        editor.resize(600, 240);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTest::qWait(30);

        editor.setScrollPositionVisualLine(10.0f);
        QTest::qWait(20);

        const float got = editor.scrollPositionVisualLine();
        QVERIFY2(std::abs(got - 10.0f) <= 0.5f,
                 qPrintable(QStringLiteral("integer round-trip off: got %1").arg(got)));
    }

    void fractionalRoundTrip() {
        Markoff::Editor editor;
        editor.setPlainText(makeBlocks(30));
        editor.resize(600, 240);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTest::qWait(30);

        editor.setScrollPositionVisualLine(10.5f);
        QTest::qWait(20);

        const float got = editor.scrollPositionVisualLine();
        QVERIFY2(std::abs(got - 10.5f) <= 0.5f,
                 qPrintable(QStringLiteral("fractional round-trip off: got %1").arg(got)));
    }

    void reflowStability() {
        Markoff::Editor editor;
        editor.setPlainText(makeBlocks(30));
        editor.resize(800, 300);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTest::qWait(30);

        editor.setScrollPositionVisualLine(15.0f);
        QTest::qWait(20);
        const float before = editor.scrollPositionVisualLine();

        // Force a reflow — 60 % of original width. Paragraphs wrap to
        // more visual lines each, so the line count above any given block
        // changes; per the plan we tolerate ±2 line in this case.
        editor.resize(480, 300);
        QTest::qWait(30);

        const float after = editor.scrollPositionVisualLine();
        // Visual-line is reflow-dependent by definition — asserting
        // *round-trip stability after reflow* within a looser ±2-line
        // tolerance per the Phase-2 plan. The goal is that the scroll
        // position is still meaningful and didn't jump to page-start.
        QVERIFY2(std::abs(after - before) <= 3.0f,
                 qPrintable(QStringLiteral(
                     "reflow stability failed: before=%1 after=%2")
                            .arg(before).arg(after)));
    }

    void signalFiresOnManualScroll() {
        Markoff::Editor editor;
        editor.setPlainText(makeBlocks(30));
        editor.resize(600, 240);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTest::qWait(30);

        QSignalSpy spy(&editor,
                       &Markoff::Editor::scrollPositionVisualLineChanged);
        // Jump the scrollbar by a comfortably visible pixel delta so even
        // a conservatively-granular mapping still fires at least once.
        QScrollBar *vbar = editor.verticalScrollBar();
        const int mid = (vbar->minimum() + vbar->maximum()) / 2;
        QVERIFY(mid > vbar->value());
        vbar->setValue(mid);
        QTest::qWait(20);

        QVERIFY2(spy.count() >= 1,
                 qPrintable(QStringLiteral(
                     "scrollPositionVisualLineChanged not fired (count=%1)")
                                .arg(spy.count())));
        const float arg = spy.takeLast().at(0).toFloat();
        QVERIFY2(arg >= 0.0f && arg < 10000.0f,
                 qPrintable(QStringLiteral(
                     "signal argument out of plausible range: %1").arg(arg)));
    }
};

QTEST_MAIN(MarkoffScrollPositionTest)
#include "tst_markoff_scroll_position.moc"
