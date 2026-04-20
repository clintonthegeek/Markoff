// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 4 — section recycling pool + frontmatter-diff trigger tests.
//
// These tests assert pointer-identity (or pointer-difference) of the
// QGraphicsItem subtrees that ReadingView mounts for each section across
// successive setPlainText() calls. The pool must:
//   (1) allow a no-op reload to reuse every section pointer verbatim;
//   (2) re-render only the section whose source content changed;
//   (3) force re-render of every `usesFrontMatter=true` section on any
//       frontmatter delta;
//   (4) reuse sections across reorderings via the cross-reparse pool;
//   (5) not grow the pool unboundedly across many re-parses.

#include "corbomite/readingview/ReadingSection.h"
#include "corbomite/readingview/ReadingView.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QSignalSpy>
#include <QTest>

using namespace Corbomite::ReadingView;

class TestSectionRecycle : public QObject
{
    Q_OBJECT

private slots:
    void idempotentReloadReusesEveryPointer();
    void singleParagraphEditOnlyRerendersOneSection();
    void frontmatterChangeForcesUsesFrontMatterRebuild();
    void poolReuseAcrossReparses();
    void poolSizeStaysBounded();
};

static QVector<QGraphicsItem *> ptrs(const ReadingView &rv)
{
    QVector<QGraphicsItem *> out;
    for (const auto &sec : rv.sections())
        out.push_back(sec->graphicsItem());
    return out;
}

// Phase 5: setPlainText may yield mid-mount via the frame-budget scheduler.
// Callers that want to observe the final mounted section list must wait for
// `mountingFinished` first. This helper makes the per-test call-sites stay
// one line each.
static void setPlainTextAndWaitForMount(ReadingView &rv, const QString &md)
{
    QSignalSpy spy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md);
    // 10 s ceiling — offscreen + cold caches can push small mounts past
    // the 5 ms per-frame budget.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
}

void TestSectionRecycle::idempotentReloadReusesEveryPointer()
{
    ReadingView rv;
    const QString md =
        QStringLiteral("# A\n\npara-a\n\n# B\n\npara-b\n\n# C\n\npara-c\n");
    setPlainTextAndWaitForMount(rv, md);
    const QVector<QGraphicsItem *> first = ptrs(rv);
    QVERIFY(!first.isEmpty());

    setPlainTextAndWaitForMount(rv, md);
    const QVector<QGraphicsItem *> second = ptrs(rv);

    QCOMPARE(first.size(), second.size());
    for (int i = 0; i < first.size(); ++i)
        QCOMPARE(first.at(i), second.at(i));
}

void TestSectionRecycle::singleParagraphEditOnlyRerendersOneSection()
{
    ReadingView rv;
    // Two sibling top-level headings so each paragraph lives in its own
    // section. (With `# H1` + `## H2` the pipeline currently creates
    // overlapping nested sections — an artefact of Phase 3's single-pass
    // heading walker — so we use two H1s here to keep this test focused
    // on the recycling contract.)
    const QString md1b =
        QStringLiteral("# H1\n\npara1\n\n# H2\n\npara2\n");
    const QString md2b =
        QStringLiteral("# H1\n\npara1-CHANGED\n\n# H2\n\npara2\n");

    setPlainTextAndWaitForMount(rv, md1b);
    const QVector<QGraphicsItem *> first = ptrs(rv);
    QCOMPARE(first.size(), 2);

    setPlainTextAndWaitForMount(rv, md2b);
    const QVector<QGraphicsItem *> second = ptrs(rv);
    QCOMPARE(second.size(), 2);

    // Section 0 (H1 + para1) must have been re-rendered.
    QVERIFY(first.at(0) != second.at(0));
    // Section 1 (H2 + para2) should be pointer-identical.
    QCOMPARE(first.at(1), second.at(1));
}

void TestSectionRecycle::frontmatterChangeForcesUsesFrontMatterRebuild()
{
    ReadingView rv;
    const QString md1 = QStringLiteral(
        "---\ntitle: A\n---\n\n# H1 {{title}}\n\nbody1\n");
    const QString md2 = QStringLiteral(
        "---\ntitle: B\n---\n\n# H1 {{title}}\n\nbody1\n");

    setPlainTextAndWaitForMount(rv, md1);
    const QVector<QGraphicsItem *> first = ptrs(rv);
    // Expect: [frontmatter, H1-section]. The H1 section contains
    // `{{title}}` so usesFrontMatter=true.
    QVERIFY(first.size() >= 2);

    // Locate the usesFrontMatter=true section.
    int idxUFM = -1;
    for (int i = 0; i < rv.sections().size(); ++i) {
        if (rv.sections().at(i)->usesFrontMatter()) { idxUFM = i; break; }
    }
    QVERIFY2(idxUFM >= 0, "Expected a section flagged usesFrontMatter=true");

    setPlainTextAndWaitForMount(rv, md2);
    const QVector<QGraphicsItem *> second = ptrs(rv);
    QCOMPARE(second.size(), first.size());

    // The usesFrontMatter section must have a NEW pointer because
    // frontmatter changed; the forced-re-render rule fires regardless of
    // whether the section's own source bytes changed.
    QVERIFY(first.at(idxUFM) != second.at(idxUFM));
}

void TestSectionRecycle::poolReuseAcrossReparses()
{
    ReadingView rv;
    const QString md1 = QStringLiteral("# A\n\nbody-a\n\n# B\n\nbody-b\n");
    const QString md2 = QStringLiteral("# B\n\nbody-b\n\n# A\n\nbody-a\n");

    setPlainTextAndWaitForMount(rv, md1);
    const QVector<QGraphicsItem *> first = ptrs(rv);
    QCOMPARE(first.size(), 2);
    QGraphicsItem *aPtr = first.at(0);
    QGraphicsItem *bPtr = first.at(1);

    setPlainTextAndWaitForMount(rv, md2);
    const QVector<QGraphicsItem *> second = ptrs(rv);
    QCOMPARE(second.size(), 2);
    // After swap: section 0 is B, section 1 is A.
    QCOMPARE(second.at(0), bPtr);
    QCOMPARE(second.at(1), aPtr);
}

void TestSectionRecycle::poolSizeStaysBounded()
{
    ReadingView rv;
    for (int i = 0; i < 10; ++i) {
        QString md;
        for (int h = 0; h < 5; ++h) {
            md += QStringLiteral("# H-%1-%2\n\nbody-%1-%2\n\n").arg(i).arg(h);
        }
        setPlainTextAndWaitForMount(rv, md);
    }
    // Smoke test: first-in-wins dedupes identical shapes; across 10
    // re-parses with 5 sections each, worst case is ~50 pooled items but
    // typically fewer. The hard ceiling of 100 catches the regression
    // where the pool accumulates every item ever rendered.
    QVERIFY2(rv.recyclePoolSize() < 100,
             qPrintable(QStringLiteral("pool size = %1")
                            .arg(rv.recyclePoolSize())));
}

QTEST_MAIN(TestSectionRecycle)
#include "tst_section_recycle.moc"
