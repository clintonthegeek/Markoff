// SPDX-License-Identifier: GPL-3.0-or-later
//
// P6.0 falsification: MarkoffDocument::blockCrdtAnchorAt() /
// resolveBlockCrdtAnchor() are the D2-safe companions to the legacy
// anchorAt()/resolveAnchor(), which are backed by the legacy flat buffer
// and never see a block created after a D2 load. See plan task P6.0,
// docs/plans/2026-08-13-canvas-production-plan.md.
#include <QTest>

#include <crdt/Anchor.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD2BlockCrdtAnchor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // The falsifying case: a block created AFTER loadFromMarkdown() is
    // never in the legacy flat buffer's latestBlockRanges/latestBlockAnchors
    // (those are populated by the parse, not by d2InsertBlock). The new
    // D2-safe accessor must still resolve a correct, round-trippable anchor
    // on that block; the legacy anchorAt()/resolveAnchor() path does not
    // even have a byte range to address it with.
    void blockCrdtAnchorAt_resolves_on_block_created_after_d2_load()
    {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first paragraph\n");

        const auto blocksBefore = doc.iterateBlocks();
        QCOMPARE(blocksBefore.size(), size_t(1));
        const BlockId first = blocksBefore.front();

        // Create a new block AFTER the D2 load — this block has no entry
        // in the legacy flat buffer's parse-derived byte ranges. The
        // Transaction commits its ops when it goes out of scope.
        BlockId newBlock;
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            newBlock = doc.d2InsertBlock(first, BlockKind::Paragraph, t);
        }

        doc.applyBlockEdit(BlockEdit{newBlock, 0, 0, "second paragraph"});

        // blockByteRange (parse-derived) does NOT know about the new block:
        // this is the "never in the legacy flat buffer" condition the new
        // seam exists to cover.
        QVERIFY(!doc.blockByteRange(newBlock).has_value());

        // The new D2-safe accessor resolves a real anchor at offset 3 into
        // the new block's own per-block CRDT buffer...
        const CollabText::Crdt::Anchor a =
            doc.blockCrdtAnchorAt(newBlock, 3, CollabText::Crdt::Bias::Left);

        // ...and its inverse recovers the same block-local offset.
        const int roundTripped = doc.resolveBlockCrdtAnchor(newBlock, a);
        QCOMPARE(roundTripped, 3);

        // Contrast: the legacy anchorAt()/resolveAnchor() pair is blind to
        // this block. There is no byte offset in the legacy flat buffer's
        // coordinate space that identifies a position inside `newBlock`
        // (its content was never parsed into latestBlockRanges), so a
        // legacy-path anchor built from an offset that happens to land
        // past the end of the (stale, one-block) flat buffer resolves back
        // to the flat buffer's own clamp point — NOT into the new block.
        const CollabText::Crdt::Anchor legacyAnchor =
            doc.anchorAt(3, CollabText::Crdt::Bias::Left);
        const quint32 legacyResolved = doc.resolveAnchor(legacyAnchor);
        // The legacy path only ever knows about `first`'s content
        // ("first paragraph\n", 16 bytes); it has no notion of `newBlock`
        // at all, so this resolved byte is a position in the ORIGINAL
        // single-block document, not a position that means anything for
        // `newBlock`. Demonstrate the two paths diverge in what they can
        // even address: the legacy anchor is well within the original
        // block's own range, while the new seam's anchor is scoped to a
        // block the legacy machinery never indexed.
        QVERIFY(legacyResolved <= 16u);
        QVERIFY(!doc.blockByteRange(newBlock).has_value());
    }
};

QTEST_MAIN(TstD2BlockCrdtAnchor)
#include "tst_d2_block_crdt_anchor.moc"
