// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include "FoldingModel.h"

using namespace Markoff;

class TstFoldingReconcile : public QObject {
    Q_OBJECT
private slots:
    void reconcile_empty_clearsStaleFolds();
    void reconcile_renameHeading_dropsFold();
    void reconcile_promoteH2ToH1_dropsFold();
    void reconcile_insertUnrelatedHeading_preservesFold();
    void reconcile_editBodyTextOnly_preservesFold();
    void reconcile_newDuplicateSibling_existingFoldPreserved();
    void reconcile_stableHeadings_noSignal();
    void reconcile_populatesHeadingsCache();
    void unfoldAncestors_noFoldedAncestors_returnsEmpty();
    void unfoldAncestors_twoFoldedAncestors_unfoldsBoth();
};

static HeadingInfo h(int lvl, QString text) { return {lvl, std::move(text), 0}; }

void TstFoldingReconcile::reconcile_empty_clearsStaleFolds() {
    FoldingModel m;
    m.fold({"Old"});
    m.reconcile({});
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldingReconcile::reconcile_renameHeading_dropsFold() {
    FoldingModel m;
    m.reconcile({ h(1, "Intro"), h(2, "Goals") });
    m.fold({"Intro","Goals"});
    m.reconcile({ h(1, "Intro"), h(2, "Objectives") }); // renamed
    QVERIFY(!m.isFolded({"Intro","Goals"}));
    QVERIFY(!m.isFolded({"Intro","Objectives"}));
}

void TstFoldingReconcile::reconcile_promoteH2ToH1_dropsFold() {
    FoldingModel m;
    m.reconcile({ h(1, "Intro"), h(2, "Goals") });
    m.fold({"Intro","Goals"});
    m.reconcile({ h(1, "Intro"), h(1, "Goals") }); // promoted -> path changes
    QVERIFY(!m.isFolded({"Intro","Goals"}));
}

void TstFoldingReconcile::reconcile_insertUnrelatedHeading_preservesFold() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(1, "B") });
    m.fold({"A"});
    m.reconcile({ h(1, "A"), h(1, "C"), h(1, "B") });
    QVERIFY(m.isFolded({"A"}));
}

void TstFoldingReconcile::reconcile_editBodyTextOnly_preservesFold() {
    FoldingModel m;
    const QList<HeadingInfo> hs = { h(1, "A"), h(2, "B") };
    m.reconcile(hs); m.fold({"A","B"});
    m.reconcile(hs); // identical reparse
    QVERIFY(m.isFolded({"A","B"}));
}

void TstFoldingReconcile::reconcile_newDuplicateSibling_existingFoldPreserved() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "Goals") });
    m.fold({"A","Goals"});
    m.reconcile({ h(1, "A"), h(2, "Goals"), h(2, "Goals") });
    QVERIFY(m.isFolded({"A","Goals"}));    // first sibling unchanged
    QVERIFY(!m.isFolded({"A","Goals#2"})); // new one not folded
}

void TstFoldingReconcile::reconcile_stableHeadings_noSignal() {
    FoldingModel m;
    const QList<HeadingInfo> hs = { h(1, "A"), h(2, "B") };
    m.reconcile(hs); m.fold({"A","B"});
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.reconcile(hs);
    QCOMPARE(spy.count(), 0);
}

void TstFoldingReconcile::reconcile_populatesHeadingsCache() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B") });
    QCOMPARE(m.headings().size(), 2);
    QCOMPARE(m.headings()[1].path, (QStringList{"A","B"}));
}

void TstFoldingReconcile::unfoldAncestors_noFoldedAncestors_returnsEmpty() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B") });
    auto r = m.unfoldAncestors({"A","B"});
    QVERIFY(r.isEmpty());
}

void TstFoldingReconcile::unfoldAncestors_twoFoldedAncestors_unfoldsBoth() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B"), h(3, "C") });
    m.fold({"A"}); m.fold({"A","B"});
    auto r = m.unfoldAncestors({"A","B","C"});
    QCOMPARE(r.size(), 2);
    QVERIFY(r.contains({"A"}));
    QVERIFY(r.contains({"A","B"}));
    QVERIFY(!m.isFolded({"A"}));
    QVERIFY(!m.isFolded({"A","B"}));
}

QTEST_MAIN(TstFoldingReconcile)
#include "tst_folding_reconcile.moc"
