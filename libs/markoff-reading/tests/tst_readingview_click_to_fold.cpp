// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Phase C5 regression guard for fold-arrow click-to-fold dispatch.
//
// The click-dispatch path lives in ReadingView::eventFilter
// (MouseButtonPress branch): sectionIndexAt(pos) looks up items under
// the cursor whose data() carries kFoldArrowSectionIdxProperty, and
// toggleFold() fires. This behaviour was already landed during earlier
// ReadingView virtualization work; this test freezes the observable
// contract so future refactors don't silently regress it.

#include "markoff/reading/ReadingSection.h"
#include "markoff/reading/ReadingView.h"
#include "markoff/reading/ReadingViewConstants.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QSignalSpy>
#include <QTest>

using namespace Markoff::Reading;

class TstReadingViewClickToFold : public QObject
{
    Q_OBJECT

private slots:
    void clickOnFoldArrowTogglesFold();
};

void TstReadingViewClickToFold::clickOnFoldArrowTogglesFold()
{
    ReadingView rv;
    rv.resize(800, 600);
    rv.show();
    QVERIFY(QTest::qWaitForWindowExposed(&rv));

    const QString md = QStringLiteral(
        "# Heading A\n\nbody-a\n\n"
        "# Heading B\n\nbody-b\n");

    QSignalSpy mountSpy(&rv, &ReadingView::mountingFinished);
    rv.setPlainText(md);
    QTRY_VERIFY_WITH_TIMEOUT(mountSpy.count() >= 1, 30000);

    // Find a fold-arrow graphics item by its stamped property key.
    auto *scene = rv.scene();
    QVERIFY(scene);
    QGraphicsItem *arrowItem = nullptr;
    int arrowSectionIdx = -1;
    for (auto *item : scene->items()) {
        const QVariant v = item->data(kFoldArrowSectionIdxProperty);
        if (v.isValid() && v.canConvert<int>()) {
            arrowItem = item;
            arrowSectionIdx = v.toInt();
            break;
        }
    }
    QVERIFY2(arrowItem, "no fold-arrow item found in scene");
    QVERIFY(arrowSectionIdx >= 0);
    QVERIFY(!rv.sections().at(arrowSectionIdx)->headingCollapsed());

    // Translate the arrow's scene position into the graphics-view
    // viewport coordinate space and synthesize a left click there.
    auto *gv = rv.graphicsView();
    QVERIFY(gv);
    const QPointF sceneCentre = arrowItem->sceneBoundingRect().center();
    const QPoint viewportPos = gv->mapFromScene(sceneCentre);

    QSignalSpy foldSpy(&rv, &ReadingView::foldedHeadingsChanged);
    QVERIFY(foldSpy.isValid());

    QTest::mouseClick(gv->viewport(), Qt::LeftButton,
                      Qt::NoModifier, viewportPos);

    QTRY_COMPARE(foldSpy.count(), 1);
    QVERIFY(rv.sections().at(arrowSectionIdx)->headingCollapsed());
    QCOMPARE(rv.foldedHeadingLines().size(), 1);
}

QTEST_MAIN(TstReadingViewClickToFold)
#include "tst_readingview_click_to_fold.moc"
