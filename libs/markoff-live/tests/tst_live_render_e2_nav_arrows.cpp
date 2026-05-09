// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/CrdtProxies.h>

using namespace Markoff::Live;

// Helper: load markdown into a D2 document and wait for the model rows to
// populate via the structureChanged/mapChanged → onD2Changed pipeline.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    doc.loadFromMarkdown(content);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    QSignalSpy spy(doc.idListProxy(), &Markoff::IdListProxy::structureChanged);
    if (!spy.wait(timeoutMs))
        return binding.model()->rowCount() == expectedRows;
    return binding.model()->rowCount() == expectedRows;
}

class TestE2NavArrows : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- D1: controller is exposed on the binding ----

    void controller_is_exposed_on_binding() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        QVERIFY(binding.navigationController() != nullptr);
    }

    void controller_returns_not_handled_for_arbitrary_key() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        // tryHandle returns NotHandled (0) for all keys in the Phase D stub.
        QCOMPARE(nav->tryHandle(Qt::Key_Up, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
        QCOMPARE(nav->tryHandle(Qt::Key_Down, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
        QCOMPARE(nav->tryHandle(Qt::Key_Left, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
        QCOMPARE(nav->tryHandle(Qt::Key_Right, 0, 0, 0, nullptr, QString()),
                 static_cast<int>(LiveNavigationController::NotHandled));
    }

    // ---- D2: previousNavigableRow / nextNavigableRow ----

    void prev_navigable_row_at_zero_returns_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Load 4 paragraphs (3 blank lines → 4 blocks).
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(0), -1);
    }

    void prev_navigable_row_walks_back() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(3), 2);
        QCOMPARE(nav->previousNavigableRow(2), 1);
        QCOMPARE(nav->previousNavigableRow(1), 0);
    }

    void next_navigable_row_at_last_returns_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        const int N = binding.model()->rowCount();
        QCOMPARE(N, 4);
        QCOMPARE(nav->nextNavigableRow(N - 1), -1);
    }

    void next_navigable_row_walks_forward() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "A\n\nB\n\nC\n\nD", 4));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->nextNavigableRow(0), 1);
        QCOMPARE(nav->nextNavigableRow(1), 2);
        QCOMPARE(nav->nextNavigableRow(2), 3);
    }

    void single_block_doc_both_directions_return_minus_one() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "only one block", 1));

        auto *nav = binding.navigationController();
        QVERIFY(nav != nullptr);
        QCOMPARE(nav->previousNavigableRow(0), -1);
        QCOMPARE(nav->nextNavigableRow(0), -1);
    }
};

QTEST_MAIN(TestE2NavArrows)
#include "tst_live_render_e2_nav_arrows.moc"
