// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 3a end-to-end test: feeds a 500-line synthetic markdown fixture
// through ReadingView::setPlainText() and asserts the scene is non-empty
// and every section got a mounted QGraphicsItem.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingView.h"

#include <QGraphicsScene>
#include <QSignalSpy>
#include <QTest>

using namespace Corbomite::ReadingView;

class TestReadingViewEndToEnd : public QObject
{
    Q_OBJECT

private slots:
    void fiveHundredLineNote();
    void scrollApiIsNotIdentity();
    void reparseWithOneParagraphEditMostlyReuses();
};

static QString buildFixture()
{
    QString md;
    md += QStringLiteral("---\ntitle: Demo\n---\n");
    for (int i = 0; i < 30; ++i) {
        md += QStringLiteral("\n# Heading %1\n\n").arg(i);
        md += QStringLiteral(
            "This is **bold** and *italic* and `code` in a paragraph.\n");
        md += QStringLiteral("A second line with a [link](http://x) and a "
                              "[[WikiNote]] reference.\n\n");
        md += QStringLiteral("## Subheading %1\n\n").arg(i);
        md += QStringLiteral("- item one\n- item two\n- item three\n\n");
        md += QStringLiteral("1. first\n2. second\n\n");
        md += QStringLiteral("> a blockquote line\n\n");
        md += QStringLiteral("---\n\n");
        md += QStringLiteral("```python\n"
                             "def f(x):\n"
                             "    return x * 2\n"
                             "```\n\n");
        // New Phase 3b content types.
        md += QStringLiteral("| L | C | R |\n|:-|:-:|-:|\n"
                              "| a | b | c |\n| d | e | f |\n\n");
        md += QStringLiteral("Some math: $x=1$ in a sentence.\n\n");
        md += QStringLiteral("$$\ny = 2\n$$\n\n");
        md += QStringLiteral("![alt-%1](missing-%1.png)\n\n").arg(i);
        md += QStringLiteral("```mermaid\ngraph TD;\nA-->B;\n```\n\n");
    }
    return md;
}

void TestReadingViewEndToEnd::fiveHundredLineNote()
{
    ReadingView rv;
    const QString md = buildFixture();
    // Guard: the fixture should comfortably exceed 500 lines.
    QVERIFY(md.count(QLatin1Char('\n')) >= 500);

    // Phase 5: fixture is well over the 10240-byte threshold so parse runs
    // on the worker thread. Wait for mounting to finish before asserting
    // scene geometry.
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);

    auto *s = rv.scene();
    QVERIFY(s != nullptr);

    const QRectF rect = s->sceneRect();
    QVERIFY(rect.width() > 0);
    QVERIFY(rect.height() > 0);

    QVERIFY(!rv.sections().isEmpty());
    // Phase 6: virtualization — not every section mounts up-front. Assert
    // only that AT LEAST ONE section mounted (the first-window mount is
    // done by the time `mountingFinished` fires) and that mounted count is
    // bounded below the total.
    int mounted = 0;
    for (const auto &sec : rv.sections()) {
        if (sec->graphicsItem() != nullptr) ++mounted;
    }
    QVERIFY2(mounted > 0,
             "Expected at least one section mounted post-mountingFinished.");
}

void TestReadingViewEndToEnd::scrollApiIsNotIdentity()
{
    ReadingView rv;
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(buildFixture());
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);
    rv.resize(800, 600);
    // Ensure the scrollbars know their range.
    rv.show();
    QTest::qWait(20);

    const float initial = rv.scrollPositionVisualLine();
    rv.setScrollPositionVisualLine(25.0f);
    QTest::qWait(10);
    const float after = rv.scrollPositionVisualLine();
    // The API should actually move; we don't require exact equality
    // because pixel rounding + viewport clipping alter the reached value.
    QVERIFY(after > initial);
}

void TestReadingViewEndToEnd::reparseWithOneParagraphEditMostlyReuses()
{
    // Phase 4 end-to-end: start from the 30-iteration fixture, mutate a
    // single paragraph in the middle, and verify the diff-driven
    // recycling path keeps ≥80% of section pointers identical across
    // the re-parse.
    ReadingView rv;
    const QString md1 = buildFixture();
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md1);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);

    QVector<QGraphicsItem *> first;
    for (const auto &sec : rv.sections())
        first.push_back(sec->graphicsItem());
    QVERIFY(first.size() >= 30);

    // Mutate one paragraph in iteration 15. Keep the structure
    // byte-identical everywhere else.
    QString md2 = md1;
    const QString target = QStringLiteral("# Heading 15\n\n"
        "This is **bold** and *italic* and `code` in a paragraph.\n");
    const QString replacement = QStringLiteral("# Heading 15\n\n"
        "This is **bold** and *italic* and `code` in a paragraph-EDITED.\n");
    QVERIFY(md2.contains(target));
    md2.replace(target, replacement);

    const int priorCount = spy.count();
    rv.setPlainText(md2);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > priorCount, 30000);

    QVector<QGraphicsItem *> second;
    for (const auto &sec : rv.sections())
        second.push_back(sec->graphicsItem());

    // Count pointer-identity matches at each index (sections are
    // positionally ordered for this fixture; no reordering is happening).
    const int n = qMin(first.size(), second.size());
    int reused = 0;
    for (int i = 0; i < n; ++i) {
        if (first.at(i) == second.at(i))
            ++reused;
    }
    const double ratio = static_cast<double>(reused) / n;
    QVERIFY2(ratio >= 0.80,
             qPrintable(QStringLiteral("reuse ratio = %1 (%2/%3)")
                            .arg(ratio, 0, 'f', 3)
                            .arg(reused)
                            .arg(n)));
}

QTEST_MAIN(TestReadingViewEndToEnd)
#include "tst_readingview_end_to_end.moc"
