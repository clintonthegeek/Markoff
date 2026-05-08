// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/Origin.h>

class TestTargetedBlockSignals : public QObject {
    Q_OBJECT
private slots:
    void edit_emits_blocksChanged_with_touched_id()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# Hello\n\nWorld\n"));
        QCoreApplication::processEvents(); // flush deferred signals

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(blockIds.size() >= 2);
        const Markoff::BlockId target = blockIds[1];

        QSignalSpy changed(&doc, &Markoff::MarkoffDocument::blocksChanged);
        Markoff::BlockEdit edit;
        edit.blockId = target;
        edit.withinBlockByteOffset = 5;
        edit.removedBytes = 0;
        edit.insertedUtf8 = QByteArrayLiteral("!");
        doc.applyBlockEdit(edit);

        QVERIFY(changed.count() >= 1);
        const auto ids = changed.first().at(0).value<QList<Markoff::BlockId>>();
        QVERIFY(ids.contains(target));
    }

    void structural_insert_emits_blockInserted()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# A\n"));
        QCoreApplication::processEvents();

        const auto blocks = doc.iterateBlocks();
        QVERIFY(blocks.size() >= 1);
        const uint32_t totalLen = static_cast<uint32_t>(doc.blockText(blocks[0]).size());

        QSignalSpy inserted(&doc, &Markoff::MarkoffDocument::blockInserted);
        // Replace the entire content with a two-block document. The newText
        // contains "\n\n" so applyFlatEdit will split it into two blocks.
        doc.applyFlatEdit(0, totalLen, QByteArrayLiteral("# A\n\nB\n"), Markoff::Origin::UserEdit);
        QVERIFY(inserted.count() >= 1);
    }

    void structural_remove_emits_blockRemoved()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# A\n\nB\n"));
        QCoreApplication::processEvents();

        const auto blocksBefore = doc.iterateBlocks();
        QVERIFY(blocksBefore.size() >= 2);

        // Compute the total byte length of both blocks to delete the second block
        const uint32_t block0Len = static_cast<uint32_t>(doc.blockText(blocksBefore[0]).size());
        const uint32_t block1Len = static_cast<uint32_t>(doc.blockText(blocksBefore[1]).size());
        const uint32_t totalLen  = block0Len + block1Len;

        QSignalSpy removed(&doc, &Markoff::MarkoffDocument::blockRemoved);
        // Delete the entire second block by selecting its range and replacing with nothing
        doc.applyFlatEdit(block0Len, totalLen, QByteArrayLiteral(""), Markoff::Origin::UserEdit);
        QVERIFY(removed.count() >= 1);
    }

    void blockInserted_carries_valid_row()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# A\n"));
        QCoreApplication::processEvents();

        const auto blocks = doc.iterateBlocks();
        const uint32_t totalLen = static_cast<uint32_t>(doc.blockText(blocks[0]).size());

        QSignalSpy inserted(&doc, &Markoff::MarkoffDocument::blockInserted);
        // Replace entire content with two-block version to force a block insertion.
        doc.applyFlatEdit(0, totalLen, QByteArrayLiteral("# A\n\nB\n"), Markoff::Origin::UserEdit);

        QVERIFY(inserted.count() >= 1);
        const int row = inserted.first().at(1).toInt();
        QVERIFY(row >= 0);
        QVERIFY(row < static_cast<int>(doc.iterateBlocks().size()));
    }

    void blockRemoved_carries_former_row()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("# A\n\nB\n"));
        QCoreApplication::processEvents();

        const auto blocksBefore = doc.iterateBlocks();
        QVERIFY(blocksBefore.size() >= 2);
        const uint32_t block0Len = static_cast<uint32_t>(doc.blockText(blocksBefore[0]).size());
        const uint32_t block1Len = static_cast<uint32_t>(doc.blockText(blocksBefore[1]).size());
        const uint32_t totalLen  = block0Len + block1Len;
        const int expectedRow    = 1; // second block

        QSignalSpy removed(&doc, &Markoff::MarkoffDocument::blockRemoved);
        doc.applyFlatEdit(block0Len, totalLen, QByteArrayLiteral(""), Markoff::Origin::UserEdit);

        QVERIFY(removed.count() >= 1);
        const int row = removed.first().at(1).toInt();
        QCOMPARE(row, expectedRow);
    }
};

QTEST_GUILESS_MAIN(TestTargetedBlockSignals)
#include "tst_v10_targeted_block_signals.moc"
