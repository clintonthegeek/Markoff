// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatLinkSingleBlock : public QObject {
    Q_OBJECT
private slots:
    void nonempty_selection_wraps_with_link_syntax() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("see here\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        binding.selectionView()->begin(0, 4);
        binding.selectionView()->extend(0, 8);  // "here"
        fc.insertLink();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("[here](url)"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.contains("see "));
    }

    void empty_selection_inserts_link_placeholder() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("X\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        // Caret at position 1 (after "X"), no selection.
        binding.selectionView()->begin(0, 1);
        binding.selectionView()->extend(0, 1);
        fc.insertLink();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("[](url)"),
                 ("blockText was: " + blockUtf8).constData());
    }
};

QTEST_MAIN(TestFormatLinkSingleBlock)
#include "tst_live_render_format_link_single_block.moc"
