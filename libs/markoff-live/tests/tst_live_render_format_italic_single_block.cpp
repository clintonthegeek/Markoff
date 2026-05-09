// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatItalicSingleBlock : public QObject {
    Q_OBJECT
private slots:
    void wraps_selection_with_underscores() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(0, 5);  // "hello"
        fc.toggleItalic();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("_hello_"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.contains(" world"));
    }

    void wraps_trailing_word_with_underscores() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        binding.selectionView()->begin(0, 6);
        binding.selectionView()->extend(0, 11);  // "world"
        fc.toggleItalic();

        const QByteArray blockUtf8 = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(blockUtf8.contains("_world_"),
                 ("blockText was: " + blockUtf8).constData());
        QVERIFY(blockUtf8.contains("hello "));
    }
};

QTEST_MAIN(TestFormatItalicSingleBlock)
#include "tst_live_render_format_italic_single_block.moc"
