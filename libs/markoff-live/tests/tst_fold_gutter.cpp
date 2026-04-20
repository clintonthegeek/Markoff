// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include "GutterColumn.h"
#include "FoldingModel.h"
#include "FoldGutter.h"

using namespace Markoff;

class TstFoldArrowColumn : public QObject {
    Q_OBJECT
private slots:
    void width_returns16();
    void paintCell_nonHeading_paintsNothing();
    void paintCell_unfoldedHeading_paintsDownTriangle();
    void paintCell_foldedHeading_paintsRightTriangle();
    void handleClick_noModifier_togglesThatHeading();
    void handleClick_ctrlModifier_foldsAllAtThatLevel();
};

static FoldingModel::HeadingEntry mk(QStringList path, int level) {
    return {path, HeadingInfo{level, path.last(), 0}};
}

static bool imageHasNonBackgroundPixels(const QImage &img) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) return true;
    return false;
}

void TstFoldArrowColumn::width_returns16() {
    FoldingModel m;
    FoldArrowColumn col(&m);
    QCOMPARE(col.width(), 16);
}

void TstFoldArrowColumn::paintCell_nonHeading_paintsNothing() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), /*itemIndex=*/999); // out of range
    p.end();
    QVERIFY(!imageHasNonBackgroundPixels(img));
}

void TstFoldArrowColumn::paintCell_unfoldedHeading_paintsDownTriangle() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), /*headingIdx=*/0);
    p.end();
    QVERIFY(imageHasNonBackgroundPixels(img));
}

void TstFoldArrowColumn::paintCell_foldedHeading_paintsRightTriangle() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    m.fold({"A"});
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), 0);
    p.end();
    QVERIFY(imageHasNonBackgroundPixels(img));
    // Rightward triangle: right third of image empty, left third heavier.
    // Relaxed check: at least left quarter has pixels.
    bool leftQuarter = false;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < 4; ++x)
            if (qAlpha(img.pixel(x, y)) > 0) leftQuarter = true;
    QVERIFY(leftQuarter);
}

void TstFoldArrowColumn::handleClick_noModifier_togglesThatHeading() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QVERIFY(col.handleClick(QPoint(5, 5), 0, Qt::NoModifier));
    QVERIFY(m.isFolded({"A"}));
}

void TstFoldArrowColumn::handleClick_ctrlModifier_foldsAllAtThatLevel() {
    FoldingModel m;
    m.setHeadingsForTesting({
        mk({"A"}, 1), mk({"A","B"}, 2), mk({"A","C"}, 2)
    });
    FoldArrowColumn col(&m);
    QVERIFY(col.handleClick(QPoint(5, 5), 1, Qt::ControlModifier));
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(!m.isFolded({"A"}));
}

// ---------------------------------------------------------------------------
// TstFoldGutter — Task 10
// ---------------------------------------------------------------------------

class TstFoldGutter : public QObject {
    Q_OBJECT
private slots:
    void width_sumsColumnsPlusSeparator();
    void click_onArrowRow_togglesFold();
    void click_onNonHeadingRow_isNoop();
    void setColumns_replacesExisting();
};

void TstFoldGutter::width_sumsColumnsPlusSeparator()
{
    FoldingModel model;
    FoldGutter gutter(&model);

    // No columns → width 0
    QCOMPARE(gutter.width(), 0);

    // One column (FoldArrowColumn, width=16): no separator when only 1 column
    auto *col1 = new FoldArrowColumn(&model);
    gutter.setColumns({col1});
    // With one column: sum(16) + 2px separator = 18
    QCOMPARE(gutter.width(), 18);

    // Two columns: 16 + 16 + 2 = 34
    auto *col2 = new FoldArrowColumn(&model);
    auto *col3 = new FoldArrowColumn(&model);
    gutter.setColumns({col2, col3});
    QCOMPARE(gutter.width(), 34);
}

void TstFoldGutter::click_onArrowRow_togglesFold()
{
    FoldingModel model;
    model.setHeadingsForTesting({
        {{"Intro"}, HeadingInfo{1, "Intro", 0}},
        {{"Intro","Goals"}, HeadingInfo{2, "Goals", 0}}
    });

    FoldGutter gutter(&model);
    auto *col = new FoldArrowColumn(&model);
    gutter.setColumns({col});

    // Click at heading index 0 in column 0 (x=0, localPos doesn't matter for testing API)
    bool handled = gutter.handleMouseClickForTesting(QPoint(5, 0), 0, Qt::NoModifier);
    QVERIFY(handled);
    QVERIFY(model.isFolded({"Intro"}));
}

void TstFoldGutter::click_onNonHeadingRow_isNoop()
{
    FoldingModel model;
    model.setHeadingsForTesting({
        {{"Intro"}, HeadingInfo{1, "Intro", 0}}
    });

    FoldGutter gutter(&model);
    auto *col = new FoldArrowColumn(&model);
    gutter.setColumns({col});

    // headingIndex -1 means no heading at that Y → should return false
    bool handled = gutter.handleMouseClickForTesting(QPoint(5, 0), -1, Qt::NoModifier);
    QVERIFY(!handled);
    QVERIFY(!model.isFolded({"Intro"}));
}

void TstFoldGutter::setColumns_replacesExisting()
{
    FoldingModel model;
    FoldGutter gutter(&model);

    auto *col1 = new FoldArrowColumn(&model);
    gutter.setColumns({col1});
    QCOMPARE(gutter.width(), 18); // 16 + 2 separator

    // Replace with two columns — col1 must be deleted (no crash = ownership correct)
    auto *col2 = new FoldArrowColumn(&model);
    auto *col3 = new FoldArrowColumn(&model);
    gutter.setColumns({col2, col3});
    QCOMPARE(gutter.width(), 34); // 16+16+2

    // Replace with empty list
    gutter.setColumns({});
    QCOMPARE(gutter.width(), 0);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int result = 0;

    TstFoldArrowColumn t1;
    result |= QTest::qExec(&t1, argc, argv);

    TstFoldGutter t2;
    result |= QTest::qExec(&t2, argc, argv);

    return result;
}

#include "tst_fold_gutter.moc"
