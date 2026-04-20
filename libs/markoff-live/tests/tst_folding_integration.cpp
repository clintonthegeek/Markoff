// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QApplication>
#include <markoff/Editor.h>

using namespace Markoff;

class TstFoldingIntegration : public QObject {
    Q_OBJECT
private slots:
    void editor_setPlainText_populatesHeadingPaths();
    void editor_foldAndUnfold_emitsSignal();
    void editor_serializeAndRestore_roundTrip();
    void editor_renameHeading_dropsStaleFold();
};

static QString kSample =
    "# Intro\n\nBody\n\n## Goals\n\nMore body\n\n"
    "## Non-goals\n\nText\n\n# Other\n\nEnd\n";

static void waitForReparse() {
    // Coordinator uses a debounce timer for reparse. Spin the event loop
    // a short while. Actual timer period is ~50ms; 500ms for slow machines.
    QTest::qWait(500);
}

void TstFoldingIntegration::editor_setPlainText_populatesHeadingPaths() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    const auto paths = e.headingPaths();
    QVERIFY(paths.contains((QStringList{"Intro"})));
    QVERIFY(paths.contains((QStringList{"Intro","Goals"})));
    QVERIFY(paths.contains((QStringList{"Other"})));
}

void TstFoldingIntegration::editor_foldAndUnfold_emitsSignal() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    QSignalSpy spy(&e, &Editor::foldStateChanged);
    e.fold({"Intro","Goals"});
    QCOMPARE(spy.count(), 1);
    QVERIFY(e.isFolded({"Intro","Goals"}));
    e.unfold({"Intro","Goals"});
    QCOMPARE(spy.count(), 2);
}

void TstFoldingIntegration::editor_serializeAndRestore_roundTrip() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro","Goals"});
    e.fold({"Other"});

    auto j = e.serializeFoldState();

    Editor e2;
    e2.setPlainText(kSample);
    waitForReparse();
    e2.restoreFoldState(j);

    QVERIFY(e2.isFolded({"Intro","Goals"}));
    QVERIFY(e2.isFolded({"Other"}));
}

void TstFoldingIntegration::editor_renameHeading_dropsStaleFold() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro","Goals"});

    QString renamed = kSample;
    renamed.replace("## Goals", "## Objectives");
    e.setPlainText(renamed);
    waitForReparse();

    QVERIFY(!e.isFolded({"Intro","Goals"}));
}

// ---------------------------------------------------------------------------
// Visibility tests — Task 7
// ---------------------------------------------------------------------------
// NOTE: MarkdownSplitter only splits on tables/images — plain heading+para
// text becomes a single MarkdownTextItem with multiple QTextBlocks. Folding
// therefore hides blocks within the item (via zero line-height), not whole
// items. We measure "visible height" of each item's document as the proxy.
// ---------------------------------------------------------------------------

#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "MarkdownTextItem.h"
#include <QGraphicsItem>

/// Sum of document heights for all MarkdownTextItem instances in the scene.
/// Non-text items (tables/images) contribute their full bounding rect height.
static qreal totalVisibleHeight(const QList<SelectableItem *> &items) {
    qreal h = 0;
    for (auto *it : items) {
        if (!it->asGraphicsItem()->isVisible()) continue;
        h += it->asGraphicsItem()->boundingRect().height();
    }
    return h;
}

class TstFoldingVisibility : public QObject {
    Q_OBJECT
private slots:
    void foldH1_hidesChildrenButKeepsHeading();
    void unfold_reshowsChildren();
    void nestedFold_independent();
    void editor_setGutterVisible_false_hidesGutter();
};

void TstFoldingVisibility::foldH1_hidesChildrenButKeepsHeading() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();

    auto *coord = e.coordinatorForTesting();
    const qreal total = totalVisibleHeight(coord->items());

    e.fold({"Intro"});
    QTest::qWait(50);

    const qreal afterFold = totalVisibleHeight(coord->items());
    QVERIFY2(afterFold < total, "folding should reduce total visible height");
}

void TstFoldingVisibility::unfold_reshowsChildren() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    auto *coord = e.coordinatorForTesting();
    const qreal total = totalVisibleHeight(coord->items());

    e.fold({"Intro"});
    QTest::qWait(50);
    e.unfold({"Intro"});
    QTest::qWait(50);

    QCOMPARE(totalVisibleHeight(coord->items()), total);
}

void TstFoldingVisibility::nestedFold_independent() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    auto *coord = e.coordinatorForTesting();

    e.fold({"Intro","Goals"});
    e.fold({"Intro"});
    QTest::qWait(50);
    const qreal bothFolded = totalVisibleHeight(coord->items());

    e.unfold({"Intro"});
    QTest::qWait(50);
    // Goals is still folded — blocks under Goals still hidden.
    const qreal onlyGoalsFolded = totalVisibleHeight(coord->items());
    QVERIFY(onlyGoalsFolded > bothFolded); // Intro body re-shown
    QVERIFY(e.isFolded({"Intro","Goals"})); // unchanged
}

void TstFoldingVisibility::editor_setGutterVisible_false_hidesGutter() {
    Editor e;
    QVERIFY(e.isGutterVisible());
    e.setGutterVisible(false);
    QVERIFY(!e.isGutterVisible());
    e.setGutterVisible(true);
    QVERIFY(e.isGutterVisible());
}

// ---------------------------------------------------------------------------
// Auto-unfold tests — Task 8
// ---------------------------------------------------------------------------
// Verify that scrollToHeading() and findText() auto-unfold folded ancestors
// and emit foldsAutoExpanded with the unfolded paths.
// ---------------------------------------------------------------------------

class TstFoldAutoExpand : public QObject {
    Q_OBJECT
private slots:
    void scrollToHeading_foldedAncestor_autoUnfolds();
    void findText_matchInFoldedRegion_autoUnfolds();
};

void TstFoldAutoExpand::scrollToHeading_foldedAncestor_autoUnfolds()
{
    Editor e;
    // Document: H1 "Intro" -> H2 "Goals" (child). Fold "Intro" so "Goals" is
    // hidden, then navigate to the "Goals" heading. The ancestor "Intro" should
    // be auto-unfolded and foldsAutoExpanded should fire with its path.
    e.setPlainText(kSample);
    waitForReparse();

    // Fold the parent heading so the child is hidden.
    e.fold({"Intro"});
    QVERIFY(e.isFolded({"Intro"}));

    QSignalSpy spy(&e, &Editor::foldsAutoExpanded);

    // Build a HeadingInfo for "Goals" to pass to scrollToHeading.
    // sourceOffset: count bytes up to "## Goals" in kSample.
    const QByteArray utf8 = kSample.toUtf8();
    int goalsOffset = kSample.indexOf(QLatin1String("## Goals"));
    QVERIFY(goalsOffset >= 0);

    HeadingInfo goalsHeading;
    goalsHeading.level = 2;
    goalsHeading.text = QStringLiteral("Goals");
    goalsHeading.sourceOffset = goalsOffset;

    e.scrollToHeading(goalsHeading);

    // The ancestor "Intro" must now be unfolded.
    QVERIFY(!e.isFolded({"Intro"}));

    // foldsAutoExpanded must have fired exactly once carrying the unfolded path.
    QCOMPARE(spy.count(), 1);
    auto arg = spy.at(0).at(0).value<QList<QStringList>>();
    QVERIFY(arg.contains(QStringList{"Intro"}));
}

void TstFoldAutoExpand::findText_matchInFoldedRegion_autoUnfolds()
{
    Editor e;
    // "More body" lives under "## Goals" which is under "# Intro".
    // Fold "Intro" so "More body" is hidden, then findText("More body").
    // The find should succeed, unfold Intro, and emit foldsAutoExpanded.
    e.setPlainText(kSample);
    waitForReparse();

    e.fold({"Intro"});
    QVERIFY(e.isFolded({"Intro"}));

    QSignalSpy spy(&e, &Editor::foldsAutoExpanded);

    const bool found = e.findText(QStringLiteral("More body"));
    QVERIFY(found);

    // The ancestor "Intro" must now be unfolded.
    QVERIFY(!e.isFolded({"Intro"}));

    // foldsAutoExpanded fired.
    QCOMPARE(spy.count(), 1);
    auto arg = spy.at(0).at(0).value<QList<QStringList>>();
    // At minimum the "Intro" path should have been unfolded.
    QVERIFY(!arg.isEmpty());
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    int status = 0;
    { TstFoldingIntegration t;  status |= QTest::qExec(&t, argc, argv); }
    { TstFoldingVisibility  t;  status |= QTest::qExec(&t, argc, argv); }
    { TstFoldAutoExpand     t;  status |= QTest::qExec(&t, argc, argv); }
    return status;
}
#include "tst_folding_integration.moc"
