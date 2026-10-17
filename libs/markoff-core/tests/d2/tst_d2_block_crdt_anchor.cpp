// SPDX-License-Identifier: GPL-3.0-or-later
//
// Block CRDT anchors remain scoped to a block created after initial load.
// See plan task P6.0,
// docs/plans/2026-08-13-canvas-production-plan.md.
#include <QTest>

#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD2BlockCrdtAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // A block created after initial load still has its own addressable CRDT.
    void textAnchor_resolves_on_block_created_after_d2_load()
    {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first paragraph\n");

        const auto blocksBefore = doc.iterateBlocks();
        QCOMPARE(blocksBefore.size(), size_t(1));
        const BlockId first = blocksBefore.front();

        // Create a new block after initial load. The transaction commits
        // its ops when it goes out of scope.
        BlockId newBlock;
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            newBlock = doc.d2InsertBlock(first, BlockKind::Paragraph, t);
        }

        doc.applyBlockEdit(BlockEdit{newBlock, 0, 0, "second paragraph"});

        const TextAnchor anchor = doc.textAnchorAt(newBlock, 3, false);
        const int roundTripped = doc.offsetInBlock(newBlock, anchor);
        QCOMPARE(roundTripped, 3);
    }
};

QTEST_MAIN(TstD2BlockCrdtAnchor)
#include "tst_d2_block_crdt_anchor.moc"
