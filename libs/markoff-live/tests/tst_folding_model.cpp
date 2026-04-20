// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>

using namespace Markoff;

class TstFoldingModel : public QObject {
    Q_OBJECT
private slots:
    // --- Path computation ---
    void path_singleHeading_returnsOwnText();
    void path_nestedHeadings_includesAncestors();
    void path_skippedLevels_skipsMissingAncestors();
    void path_boldMarkdownInHeading_isStripped();
    void path_duplicateSiblings_getSuffix();
    void path_duplicateSiblings_firstHasNoSuffix();
};

static HeadingInfo h(int level, QString text, int off = 0) {
    return HeadingInfo{level, std::move(text), off};
}

void TstFoldingModel::path_singleHeading_returnsOwnText() {
    QList<HeadingInfo> headings = { h(1, "Intro") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths.size(), 1);
    QCOMPARE(paths[0], QStringList{ "Intro" });
}

void TstFoldingModel::path_nestedHeadings_includesAncestors() {
    QList<HeadingInfo> headings = {
        h(1, "Intro"),
        h(2, "Goals"),
        h(3, "Non-goals"),
    };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], (QStringList{ "Intro" }));
    QCOMPARE(paths[1], (QStringList{ "Intro", "Goals" }));
    QCOMPARE(paths[2], (QStringList{ "Intro", "Goals", "Non-goals" }));
}

void TstFoldingModel::path_skippedLevels_skipsMissingAncestors() {
    // # A \n ### C — no H2 between. Path is ["A", "C"] (skipped level).
    QList<HeadingInfo> headings = { h(1, "A"), h(3, "C") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[1], (QStringList{ "A", "C" }));
}

void TstFoldingModel::path_boldMarkdownInHeading_isStripped() {
    QList<HeadingInfo> headings = { h(2, "**Goals**") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], QStringList{ "Goals" });
}

void TstFoldingModel::path_duplicateSiblings_getSuffix() {
    QList<HeadingInfo> headings = {
        h(1, "A"),
        h(2, "Goals"),
        h(2, "Goals"),
        h(2, "Goals"),
    };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[1], (QStringList{ "A", "Goals" }));
    QCOMPARE(paths[2], (QStringList{ "A", "Goals#2" }));
    QCOMPARE(paths[3], (QStringList{ "A", "Goals#3" }));
}

void TstFoldingModel::path_duplicateSiblings_firstHasNoSuffix() {
    // Re-asserts the no-suffix-on-first rule in isolation.
    QList<HeadingInfo> headings = { h(1, "Same"), h(1, "Same") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], QStringList{ "Same" });
    QCOMPARE(paths[1], QStringList{ "Same#2" });
}

#include "FoldingModel.h"

class TstFoldingModelState : public QObject {
    Q_OBJECT
private slots:
    void initialState_isEmpty();
    void fold_addsPath();
    void unfold_removesPath();
    void toggle_flipsState();
    void fold_duplicateCall_doesNotDoubleEmit();
    void foldStateChanged_firesOnlyOnActualChange();
};

void TstFoldingModelState::initialState_isEmpty() {
    FoldingModel m;
    QVERIFY(m.foldedPaths().isEmpty());
    QVERIFY(!m.isFolded({ "Anything" }));
}

void TstFoldingModelState::fold_addsPath() {
    FoldingModel m;
    m.fold({ "Intro" });
    QVERIFY(m.isFolded({ "Intro" }));
    QCOMPARE(m.foldedPaths().size(), 1);
}

void TstFoldingModelState::unfold_removesPath() {
    FoldingModel m;
    m.fold({ "Intro" });
    m.unfold({ "Intro" });
    QVERIFY(!m.isFolded({ "Intro" }));
}

void TstFoldingModelState::toggle_flipsState() {
    FoldingModel m;
    m.toggle({ "X" }); QVERIFY(m.isFolded({ "X" }));
    m.toggle({ "X" }); QVERIFY(!m.isFolded({ "X" }));
}

void TstFoldingModelState::fold_duplicateCall_doesNotDoubleEmit() {
    FoldingModel m;
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.fold({ "X" });
    m.fold({ "X" }); // already folded; no-op.
    QCOMPARE(spy.count(), 1);
}

void TstFoldingModelState::foldStateChanged_firesOnlyOnActualChange() {
    FoldingModel m;
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.unfold({ "Nonexistent" }); // no-op
    QCOMPARE(spy.count(), 0);
}

class TstFoldingModelBulk : public QObject {
    Q_OBJECT
private slots:
    void foldAll_foldsEveryHeading();
    void unfoldAll_clears();
    void foldAllAtLevel_foldsOnlySpecifiedLevel();
    void foldLevel_foldsAtLevelAndDeeper();
    void unfoldLevel_unfoldsAtLevelAndDeeper();
private:
    void seedHeadings(FoldingModel &m);
};

void TstFoldingModelBulk::seedHeadings(FoldingModel &m) {
    m.setHeadingsForTesting({
        { {"A"},           {1, "A", 0} },
        { {"A","B"},       {2, "B", 10} },
        { {"A","C"},       {2, "C", 20} },
        { {"D"},           {1, "D", 30} },
        { {"D","","E"},    {3, "E", 40} },  // skipped H2
    });
}

void TstFoldingModelBulk::foldAll_foldsEveryHeading() {
    FoldingModel m; seedHeadings(m);
    m.foldAll();
    QCOMPARE(m.foldedPaths().size(), 5);
}

void TstFoldingModelBulk::unfoldAll_clears() {
    FoldingModel m; seedHeadings(m);
    m.foldAll(); m.unfoldAll();
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldingModelBulk::foldAllAtLevel_foldsOnlySpecifiedLevel() {
    FoldingModel m; seedHeadings(m);
    m.foldAllAtLevel(2);
    QCOMPARE(m.foldedPaths().size(), 2);
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(!m.isFolded({"A"}));
}

void TstFoldingModelBulk::foldLevel_foldsAtLevelAndDeeper() {
    FoldingModel m; seedHeadings(m);
    m.foldLevel(2);  // H2 and H3
    QCOMPARE(m.foldedPaths().size(), 3);
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(m.isFolded({"D","","E"}));
}

void TstFoldingModelBulk::unfoldLevel_unfoldsAtLevelAndDeeper() {
    FoldingModel m; seedHeadings(m);
    m.foldAll();
    m.unfoldLevel(2);
    QVERIFY(m.isFolded({"A"}));
    QVERIFY(m.isFolded({"D"}));
    QVERIFY(!m.isFolded({"A","B"}));
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TstFoldingModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TstFoldingModelState t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TstFoldingModelBulk t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}
#include "tst_folding_model.moc"
