// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 6 — heading-fold tests.
//
// Covers:
//  1. Fold collapses subsequent sections (hidden + unmounted).
//  2. Fold persists via foldedHeadings() / setFoldedHeadings().
//  3. Fold survives setPlainText reload.
//  4. Fold nested headings (level-based scope).
//  5. Ephemeral round-trip.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingView.h"

#include <QGraphicsItem>
#include <QSignalSpy>
#include <QTest>

using namespace Corbomite::ReadingView;

namespace {

void setPlainTextAndWaitForMount(ReadingView &rv, const QString &md)
{
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);
}

int findSectionByHeadingText(const ReadingView &rv, const QString &heading)
{
    for (int i = 0; i < rv.sections().size(); ++i) {
        // Source-range text is not exposed directly, so we compare by
        // the heading-level stamp plus sourceLine — but simpler: iterate
        // in source order and return the nth heading.
        Q_UNUSED(heading);
    }
    return -1;
}

// Helper: find nth (0-indexed) section at heading level N.
int findSectionAtLevel(const ReadingView &rv, int level, int nth = 0)
{
    int count = 0;
    for (int i = 0; i < rv.sections().size(); ++i) {
        const auto &sec = rv.sections().at(i);
        if (sec->headingLevel() == level) {
            if (count == nth) return i;
            ++count;
        }
    }
    return -1;
}

} // namespace

class TestHeadingFold : public QObject
{
    Q_OBJECT
private slots:
    void foldCollapsesSubsequentSections();
    void foldPersistsViaFoldedHeadings();
    void foldSurvivesSetPlainTextReload();
    void foldNestedHeadings();
    void ephemeralRoundTrip();
};

void TestHeadingFold::foldCollapsesSubsequentSections()
{
    ReadingView rv;
    rv.resize(800, 600);
    rv.show();

    const QString md = QStringLiteral(
        "# A\n\nbody-a\n\n"
        "# B\n\nbody-b\n\n"
        "## B.1\n\nbody-b1\n\n"
        "# C\n\nbody-c\n");
    setPlainTextAndWaitForMount(rv, md);

    // Identify indices.
    const int idxA = findSectionAtLevel(rv, 1, 0);
    const int idxB = findSectionAtLevel(rv, 1, 1);
    const int idxB1 = findSectionAtLevel(rv, 2, 0);
    const int idxC = findSectionAtLevel(rv, 1, 2);
    QVERIFY(idxA >= 0 && idxB >= 0 && idxB1 >= 0 && idxC >= 0);

    // Initially none are hidden.
    QVERIFY(!rv.sections().at(idxA)->hidden());
    QVERIFY(!rv.sections().at(idxB)->hidden());
    QVERIFY(!rv.sections().at(idxB1)->hidden());
    QVERIFY(!rv.sections().at(idxC)->hidden());

    // Fold B.
    rv.toggleFold(idxB);
    QVERIFY(rv.sections().at(idxB)->headingCollapsed());

    // B.1 (deeper under B) should be hidden.
    QVERIFY(rv.sections().at(idxB1)->hidden());
    // A and C should not be hidden (C is next same-level heading).
    QVERIFY(!rv.sections().at(idxA)->hidden());
    QVERIFY(!rv.sections().at(idxB)->hidden());
    QVERIFY(!rv.sections().at(idxC)->hidden());

    // Hidden section's graphics item should be null (unmounted).
    QCOMPARE(rv.sections().at(idxB1)->graphicsItem(), static_cast<QGraphicsItem *>(nullptr));
}

void TestHeadingFold::foldPersistsViaFoldedHeadings()
{
    ReadingView rv;
    const QString md = QStringLiteral(
        "# A\n\nbody-a\n\n"
        "# B\n\nbody-b\n\n"
        "# C\n\nbody-c\n");
    setPlainTextAndWaitForMount(rv, md);

    const int idxB = findSectionAtLevel(rv, 1, 1);
    QVERIFY(idxB >= 0);
    const int srcLineB = rv.sections().at(idxB)->sourceLine();

    rv.toggleFold(idxB);
    QVector<int> folded = rv.foldedHeadings();
    QCOMPARE(folded.size(), 1);
    QCOMPARE(folded.at(0), srcLineB);

    // Clearing fold state unfolds.
    rv.setFoldedHeadings(QVector<int>{});
    QVERIFY(!rv.sections().at(idxB)->headingCollapsed());
    QVERIFY(rv.foldedHeadings().isEmpty());
}

void TestHeadingFold::foldSurvivesSetPlainTextReload()
{
    ReadingView rv;
    const QString md = QStringLiteral(
        "# A\n\nbody-a\n\n"
        "# B\n\nbody-b\n\n"
        "# C\n\nbody-c\n");
    setPlainTextAndWaitForMount(rv, md);

    const int idxB = findSectionAtLevel(rv, 1, 1);
    QVERIFY(idxB >= 0);
    const int srcLineB = rv.sections().at(idxB)->sourceLine();
    rv.toggleFold(idxB);
    QVERIFY(rv.foldedHeadings().contains(srcLineB));

    // Idempotent reload — B should still be folded.
    setPlainTextAndWaitForMount(rv, md);
    const int idxB2 = findSectionAtLevel(rv, 1, 1);
    QVERIFY(idxB2 >= 0);
    QVERIFY2(rv.sections().at(idxB2)->headingCollapsed(),
             "B should still be folded after idempotent reload");
    QVERIFY(rv.foldedHeadings().contains(srcLineB));
}

void TestHeadingFold::foldNestedHeadings()
{
    ReadingView rv;
    const QString md = QStringLiteral(
        "# A\n\n"
        "## A.1\n\n"
        "### A.1.1\n\n"
        "## A.2\n");
    setPlainTextAndWaitForMount(rv, md);

    const int idxA = findSectionAtLevel(rv, 1, 0);
    const int idxA1 = findSectionAtLevel(rv, 2, 0);
    const int idxA11 = findSectionAtLevel(rv, 3, 0);
    const int idxA2 = findSectionAtLevel(rv, 2, 1);
    QVERIFY(idxA >= 0 && idxA1 >= 0 && idxA11 >= 0 && idxA2 >= 0);

    // Fold ##A.1 → A.1.1 hidden; A.2 visible; A visible.
    rv.toggleFold(idxA1);
    QVERIFY(rv.sections().at(idxA11)->hidden());
    QVERIFY(!rv.sections().at(idxA2)->hidden());
    QVERIFY(!rv.sections().at(idxA)->hidden());

    // Unfold ##A.1, then fold #A → all sub-sections hidden.
    rv.toggleFold(idxA1);
    QVERIFY(!rv.sections().at(idxA1)->headingCollapsed());

    rv.toggleFold(idxA);
    QVERIFY(rv.sections().at(idxA)->headingCollapsed());
    QVERIFY(rv.sections().at(idxA1)->hidden());
    QVERIFY(rv.sections().at(idxA11)->hidden());
    QVERIFY(rv.sections().at(idxA2)->hidden());
}

void TestHeadingFold::ephemeralRoundTrip()
{
    ReadingView rv;
    const QString md = QStringLiteral(
        "# A\n\nbody-a\n\n"
        "# B\n\nbody-b\n\n"
        "# C\n\nbody-c\n");
    setPlainTextAndWaitForMount(rv, md);

    const int idxB = findSectionAtLevel(rv, 1, 1);
    QVERIFY(idxB >= 0);
    const int srcLineB = rv.sections().at(idxB)->sourceLine();

    rv.toggleFold(idxB);
    const QVector<int> saved = rv.foldedHeadings();
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0), srcLineB);

    // Simulate a session reload: fresh ReadingView, same markdown, then
    // restore folded heading state.
    ReadingView rv2;
    setPlainTextAndWaitForMount(rv2, md);
    rv2.setFoldedHeadings(saved);

    const int idxB2 = findSectionAtLevel(rv2, 1, 1);
    QVERIFY(idxB2 >= 0);
    QVERIFY2(rv2.sections().at(idxB2)->headingCollapsed(),
             "After setFoldedHeadings, B should be folded.");
    QCOMPARE(rv2.foldedHeadings(), saved);
}

QTEST_MAIN(TestHeadingFold)
#include "tst_heading_fold.moc"
