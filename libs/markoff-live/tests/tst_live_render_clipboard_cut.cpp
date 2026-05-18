// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QClipboard>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestClipboardCut : public QObject {
    Q_OBJECT
private slots:
    void cut_copies_then_deletes() {
        // Cut "hello " (first 6 chars) from "hello world"; block should become "world".
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.selectionView();
        sv->begin(0, 0);
        sv->extend(0, 6);  // "hello "

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());
        cc.cut();

        // After cut: block text should be "world"
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        // B1 convention: blockText is content-only, no trailing '\n'.
        QCOMPARE(doc.blockText(ids[0]), QByteArray("world"));

        // Clipboard plain text should be what was cut.
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hello "));

        // Selection should be cleared after cut.
        QVERIFY(!sv->hasSelection());
    }

    void cut_records_in_recent_cuts() {
        // Cut "beta" from "alpha\n\nbeta"; takeRecentCut(cutSeq) should return non-empty.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("alpha\n\nbeta\n");
        QCOMPARE(binding.model()->rowCount(), 2);

        auto *sv = binding.selectionView();
        sv->begin(1, 0);
        sv->extend(1, 4);  // "beta"

        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        const quint64 preSeq = doc.d2EditSequence();
        cc.cut();

        // The cut should have been recorded at cutSeq == preSeq + 1 (the
        // sequence number used by the deletion).  takeRecentCut consumes the
        // entry, so a second call returns empty.
        const auto cached = doc.takeRecentCut(preSeq + 1);
        QVERIFY(!cached.empty());
    }
};

QTEST_MAIN(TestClipboardCut)
#include "tst_live_render_clipboard_cut.moc"
