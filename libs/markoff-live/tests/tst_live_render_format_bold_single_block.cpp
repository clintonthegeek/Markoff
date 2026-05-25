// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatBoldSingleBlock : public QObject {
    Q_OBJECT
private slots:
    void wraps_selection_with_double_asterisks() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        // Select "world" (qtPos 6–11 in "hello world").
        binding.cursorState()->begin(0, 6);
        binding.cursorState()->extend(0, 11);
        fc.toggleBold();

        // blockText includes trailing '\n'; content should be "hello **world**\n".
        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("**world**"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.contains("hello "));
    }

    void wraps_full_block_selection() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("foo\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.cursorState());
        fc.setModel(binding.model());

        binding.cursorState()->begin(0, 0);
        binding.cursorState()->extend(0, 3);  // "foo"
        fc.toggleBold();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("**foo**"),
                 ("blockText was: " + blockUtf8).constData());
    }
};

QTEST_MAIN(TestFormatBoldSingleBlock)
#include "tst_live_render_format_bold_single_block.moc"
