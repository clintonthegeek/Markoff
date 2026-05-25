// SPDX-License-Identifier: GPL-3.0-or-later
//
// C6: Round-trip test — cut from one position, paste at another, verify
// that the source content was removed and reappears at the destination.
#include <QTest>
#include <QApplication>
#include <QClipboard>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveClipboardController.h>

class TestClipboardRoundTrip : public QObject {
    Q_OBJECT
private slots:
    void cut_paste_preserves_content() {
        // Cut "world" from "hello world", paste at beginning.
        // Result: "worldhello " (text is re-inserted at the start).
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        // Cut "world" (qtPos 6..11).
        sv->begin(0, 6);
        sv->extend(0, 11);
        cc.cut();

        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("world"));
        QVERIFY(!sv->hasSelection());

        // After cut: block is "hello " (6 chars; B1: content-only, no trailing '\n').
        {
            const auto ids = doc.iterateBlocks();
            QVERIFY(!ids.empty());
            QCOMPARE(doc.blockText(ids[0]), QByteArray("hello "));
        }

        // Paste at position 0 (start of the block).
        sv->begin(0, 0);
        sv->extend(0, 0);
        cc.paste();

        // "world" should now be in the document.
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QVERIFY2(doc.blockText(ids[0]).contains("world"),
                 "Expected 'world' in block text after cut→paste");
    }

    void copy_does_not_delete_source() {
        // Copy followed by paste should leave source intact.
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("abc\n");
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *sv = binding.cursorState();
        Markoff::Live::LiveClipboardController cc;
        cc.setDocument(&doc);
        cc.setSelectionView(sv);
        cc.setModel(binding.model());

        // Copy "ab".
        sv->begin(0, 0);
        sv->extend(0, 2);
        cc.copy();

        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("ab"));

        // Source unchanged (B1: content-only, no trailing '\n').
        {
            const auto ids = doc.iterateBlocks();
            QVERIFY(!ids.empty());
            QCOMPARE(doc.blockText(ids[0]), QByteArray("abc"));
        }

        // Paste at end of "abc" (qtPos=3).
        sv->begin(0, 3);
        sv->extend(0, 3);
        cc.paste();

        // "abc" still present; "ab" inserted at end → "abcab".
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        QCOMPARE(doc.blockText(ids[0]), QByteArray("abcab"));
    }
};

QTEST_MAIN(TestClipboardRoundTrip)
#include "tst_live_render_clipboard_round_trip.moc"
