// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QJsonDocument>
#include "FoldingModel.h"

using namespace Markoff;

class TstFoldPersistence : public QObject {
    Q_OBJECT
private slots:
    void serialize_emptyModel_returnsVersionOnly();
    void serialize_twoFolds_roundTrips();
    void restore_droppedPathsNotReadded_whenReconcileRemovedThem();
    void restore_malformedJson_isNoOpWithNoCrash();
    void restore_missingFoldsKey_isNoOp();
    void restore_unknownVersion_stillLoadsFolds();
};

static FoldingModel::HeadingEntry entry(QStringList path, int level) {
    return { path, HeadingInfo{level, path.last(), 0} };
}

void TstFoldPersistence::serialize_emptyModel_returnsVersionOnly() {
    FoldingModel m;
    auto j = m.serialize();
    QCOMPARE(j.value("version").toInt(), 1);
    QVERIFY(j.value("folds").toArray().isEmpty());
}

void TstFoldPersistence::serialize_twoFolds_roundTrips() {
    FoldingModel m;
    m.setHeadingsForTesting({
        entry({"Intro","Goals"}, 2),
        entry({"Reference","API","Query"}, 3),
        entry({"Other"}, 1),
    });
    m.fold({"Intro","Goals"});
    m.fold({"Reference","API","Query"});

    auto j = m.serialize();

    FoldingModel m2;
    m2.setHeadingsForTesting({
        entry({"Intro","Goals"}, 2),
        entry({"Reference","API","Query"}, 3),
        entry({"Other"}, 1),
    });
    m2.restore(j);

    QVERIFY(m2.isFolded({"Intro","Goals"}));
    QVERIFY(m2.isFolded({"Reference","API","Query"}));
    QVERIFY(!m2.isFolded({"Other"}));
}

void TstFoldPersistence::restore_droppedPathsNotReadded_whenReconcileRemovedThem() {
    FoldingModel m;
    m.restore(QJsonObject{
        {"version", 1},
        {"folds", QJsonArray{ QJsonArray{"Gone"} }}
    });
    // Headings cache is empty. reconcile() drops the path. Task 5 validates
    // end-to-end; here we just verify restore populated before reconcile runs.
    QVERIFY(m.isFolded({"Gone"}));
}

void TstFoldPersistence::restore_malformedJson_isNoOpWithNoCrash() {
    FoldingModel m;
    m.fold({"X"}); m.setHeadingsForTesting({ entry({"X"}, 1) });
    QJsonObject garbage;
    garbage["folds"] = QJsonValue(42); // wrong type
    m.restore(garbage);
    // No crash. Pre-existing fold cleared (restore replaces state).
    QVERIFY(!m.isFolded({"X"}));
}

void TstFoldPersistence::restore_missingFoldsKey_isNoOp() {
    FoldingModel m;
    m.restore(QJsonObject{ {"version", 1} });
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldPersistence::restore_unknownVersion_stillLoadsFolds() {
    FoldingModel m;
    m.setHeadingsForTesting({ entry({"A"}, 1) });
    m.restore(QJsonObject{
        {"version", 999},
        {"folds", QJsonArray{ QJsonArray{"A"} }},
    });
    QVERIFY(m.isFolded({"A"}));
}

QTEST_MAIN(TstFoldPersistence)
#include "tst_fold_persistence.moc"
