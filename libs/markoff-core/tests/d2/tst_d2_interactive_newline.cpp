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
};

QTEST_APPLESS_MAIN(TstD2InteractiveNewline)
#include "tst_d2_interactive_newline.moc"
