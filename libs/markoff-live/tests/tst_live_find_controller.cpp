// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFindController.h>
#include <markoff/live/LiveBlockModel.h>

class TstLiveFindController : public QObject {
    Q_OBJECT
private slots:
    void needle_search_finds_matches_across_blocks() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "First paragraph has the word find.\n\n"
            "Second paragraph also has find inside.\n\n"
            "Third paragraph: nothing here.\n"
        ));
        QTRY_COMPARE(binding.model()->rowCount(), 3);

        auto *fc = binding.findController();
        QVERIFY(fc != nullptr);
        fc->activate();
        fc->setNeedle(QStringLiteral("find"));

        QCOMPARE(fc->matchCount(), 2);
        QCOMPARE(fc->currentMatchIndex(), 0);
    }

    void find_next_wraps_at_end() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "alpha alpha alpha\n"
        ));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("alpha"));
        QCOMPARE(fc->matchCount(), 3);
        QCOMPARE(fc->currentMatchIndex(), 0);

        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 1);
        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 2);
        fc->findNext();
        QCOMPARE(fc->currentMatchIndex(), 0);  // wraps
    }

    void find_previous_wraps_at_start() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "beta beta beta\n"
        ));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("beta"));
        QCOMPARE(fc->matchCount(), 3);
        QCOMPARE(fc->currentMatchIndex(), 0);

        fc->findPrevious();
        QCOMPARE(fc->currentMatchIndex(), 2);  // wraps to last
        fc->findPrevious();
        QCOMPARE(fc->currentMatchIndex(), 1);
    }

    void deactivate_clears_matches() {
        Markoff::MarkoffDocument doc(1);
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown(QByteArrayLiteral("gamma gamma\n"));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *fc = binding.findController();
        fc->activate();
        fc->setNeedle(QStringLiteral("gamma"));
        QVERIFY(fc->matchCount() > 0);
        QVERIFY(fc->isActive());

        fc->deactivate();

        QCOMPARE(fc->matchCount(), 0);
        QCOMPARE(fc->currentMatchIndex(), -1);
        QVERIFY(!fc->isActive());
    }
};

QTEST_MAIN(TstLiveFindController)
#include "tst_live_find_controller.moc"
