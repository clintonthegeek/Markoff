// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QClipboard>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestPasteFlat : public QObject {
    Q_OBJECT
private slots:
    void paste_text_inserts_at_caret() {
        // Plain-text paste at end of "X" → "XY".
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("X\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.selectionView();
        // Caret at position 1 (end of "X").
        sv->begin(0, 1);
        sv->extend(0, 1);

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        QApplication::clipboard()->setText(QStringLiteral("Y"));
        cc.paste();

        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        // blockText includes trailing '\n'
        QCOMPARE(doc.blockText(ids[0]), QByteArray("XY\n"));
    }

    void paste_replaces_selection() {
        // Paste over "hello" in "hello world" → "bye world".
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.selectionView();
        sv->begin(0, 0);
        sv->extend(0, 5);  // "hello"

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        QApplication::clipboard()->setText(QStringLiteral("bye"));
        cc.paste();

        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QCOMPARE(doc.blockText(ids[0]), QByteArray("bye world\n"));
    }
};

QTEST_MAIN(TestPasteFlat)
#include "tst_live_render_clipboard_paste_flat.moc"
