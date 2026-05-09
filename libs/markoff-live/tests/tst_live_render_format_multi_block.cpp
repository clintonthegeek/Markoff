// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatMultiBlock : public QObject {
    Q_OBJECT
private slots:
    void bold_across_blocks_wraps_each_independently() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("first\n\nsecond\n\nthird\n");

        QCOMPARE(binding.model()->rowCount(), 3);

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        // Select all three blocks fully.
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(2, 5);  // "third" is 5 chars
        fc.toggleBold();

        const auto ids = doc.iterateBlocks();
        QVERIFY(static_cast<int>(ids.size()) >= 3);

        const QByteArray b0 = doc.blockText(ids[0]);
        const QByteArray b1 = doc.blockText(ids[1]);
        const QByteArray b2 = doc.blockText(ids[2]);

        QVERIFY2(b0.contains("**first**"),
                 ("block 0 was: " + b0).constData());
        QVERIFY2(b1.contains("**second**"),
                 ("block 1 was: " + b1).constData());
        QVERIFY2(b2.contains("**third**"),
                 ("block 2 was: " + b2).constData());
    }

    void italic_partial_first_and_last_block() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("abcde\n\nfghij\n\nklmno\n");

        QCOMPARE(binding.model()->rowCount(), 3);

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        // Select from qtPos 2 on block 0 to qtPos 3 on block 2.
        // block 0: "cde" (pos 2..5), block 1: full "fghij", block 2: "klm" (pos 0..3).
        binding.selectionView()->begin(0, 2);
        binding.selectionView()->extend(2, 3);
        fc.toggleItalic();

        const auto ids = doc.iterateBlocks();
        QVERIFY(static_cast<int>(ids.size()) >= 3);

        const QByteArray b0 = doc.blockText(ids[0]);
        const QByteArray b1 = doc.blockText(ids[1]);
        const QByteArray b2 = doc.blockText(ids[2]);

        QVERIFY2(b0.contains("_cde_"),
                 ("block 0 was: " + b0).constData());
        QVERIFY2(b0.contains("ab"),
                 ("block 0 was: " + b0).constData());
        QVERIFY2(b1.contains("_fghij_"),
                 ("block 1 was: " + b1).constData());
        QVERIFY2(b2.contains("_klm_"),
                 ("block 2 was: " + b2).constData());
        QVERIFY2(b2.contains("no"),
                 ("block 2 was: " + b2).constData());
    }
};

QTEST_MAIN(TestFormatMultiBlock)
#include "tst_live_render_format_multi_block.moc"
