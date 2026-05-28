// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

class TstD2InteractiveNewline : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_block_end_creates_transient_empty_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));  // [Hello, World]
        // No-sep end of "Hello" == byte 5.
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 3);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));   // transient empty
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral("World"));
        QVERIFY(newBlk == blocks[1]);  // caret target = the new empty block
    }

    void enter_mid_block_splits_with_tail_in_new_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("HelloWorld"));  // one block
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("World"));
        QVERIFY(newBlk == blocks[1]);
    }

    void enter_at_document_end_creates_empty_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QVERIFY(newBlk == blocks[1]);
    }

    void enter_at_block_start_pushes_content_down() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(0, Markoff::Origin::UserEdit);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral(""));     // blank line above
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("Hello"));
        QVERIFY(newBlk == blocks[1]);  // caret stays with the content
    }

    void enter_at_start_of_existing_empty_line_pushes_existing_empty_down() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello"));
        // First Enter at end of "Hello" -> [Hello, ""].
        const Markoff::BlockId firstEmpty =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 2);
        QVERIFY(firstEmpty == blocks[1]);
        // Second Enter at the SAME no-sep byte 5 should NOT insert the new
        // block between Hello and firstEmpty — it should land AFTER firstEmpty
        // (vim-faithful: cursor moves down a line).
        const Markoff::BlockId secondEmpty =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 3);
        QVERIFY(blocks[0] == doc.iterateBlocks()[0]);  // Hello
        QVERIFY(blocks[1] == firstEmpty);              // pre-existing empty stays at index 1
        QVERIFY(blocks[2] == secondEmpty);             // new sibling appended after
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
    }

    void enter_with_run_of_empties_inserts_at_last_position() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        // Manufacture [Hello, "", "", World] by two Enters at no-sep 5.
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        auto blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 4);
        QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[2]), QByteArrayLiteral(""));
        QCOMPARE(doc.blockText(blocks[3]), QByteArrayLiteral("World"));
        // One more Enter at the SAME boundary: skip-empties rule attributes
        // to the LAST empty in the run -> new sibling inserted between
        // blocks[2] (the last empty) and blocks[3] (World).
        const Markoff::BlockId newBlk =
            doc.applyInteractiveNewline(5, Markoff::Origin::UserEdit);
        blocks = doc.iterateBlocks();
        QCOMPARE(int(blocks.size()), 5);
        QVERIFY(newBlk == blocks[3]);  // landed between last empty and World
        QCOMPARE(doc.blockText(blocks[4]), QByteArrayLiteral("World"));
    }
};

QTEST_APPLESS_MAIN(TstD2InteractiveNewline)
#include "tst_d2_interactive_newline.moc"
