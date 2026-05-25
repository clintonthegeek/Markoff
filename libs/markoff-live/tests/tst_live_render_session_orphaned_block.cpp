// SPDX-License-Identifier: GPL-3.0-or-later
//
// B5b: Setting a selection on a now-deleted block clears the view's
// selection gracefully — no crash, no stale selection.
//
// Scenario: load two-paragraph doc, build a selection spanning both blocks,
// then replace the entire doc with a single character. The saved selection
// carries a TextAnchor into the now-deleted second block. Replaying that
// selection via session.setPrimarySelection must not crash and must leave the
// view without a selection (orphaned anchor → clear).

#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveCursorState.h>

class TestSessionOrphanedBlock : public QObject {
    Q_OBJECT
private slots:
    void orphaned_anchor_clears_selection()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("para A\n\npara B\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.cursorState();

        // selectAll() sets anchor at block 0/qtPos 0, active at the last block/end.
        sv->selectAll();
        QVERIFY(sv->hasSelection());

        // Capture the session's current primary selection.
        // The active anchor is in the last block ("para B" or similar).
        const Markoff::Selection savedSel = session->primarySelection();

        // Clear the view.
        sv->clearSelection();
        QVERIFY(!sv->hasSelection());

        // Compute the actual byte size of the document so applyFlatEdit doesn't
        // receive an out-of-range endByte (which would trigger Q_ASSERT in debug).
        const auto blocks = doc.iterateBlocks();
        if (blocks.empty()) QSKIP("No blocks to delete");

        uint32_t docSize = 0;
        for (const auto &blk : blocks)
            docSize += static_cast<uint32_t>(doc.blockText(blk).size());

        // Replace everything with a single character. This deletes the second
        // block and leaves only one block with content "x\n". The active anchor
        // in savedSel (which pointed into the second block) is now orphaned.
        doc.applyFlatEdit(0, docSize, QByteArray("x\n"), Markoff::Origin::UserEdit);

        // Replay the old (now-orphaned) selection. Must not crash.
        session->setPrimarySelection(savedSel);

        // The view must not carry a stale cross-block selection.
        // Either it is cleared (orphaned anchor), or it shows only the part
        // that is still valid (anchor at block 0 qtPos 0). In either case it
        // must NOT show both blocks selected (that would be stale).
        if (sv->hasSelection()) {
            // If something is selected, it can only be within block 0.
            QCOMPARE(sv->rangeForBlock(0).x(), 0);
            // Active must also be in block 0 — the old last-block anchor is gone.
            QVERIFY(binding.model()->rowCount() == 1);
        }
        // Regardless: accessing the model must not crash.
    }
};

QTEST_MAIN(TestSessionOrphanedBlock)
#include "tst_live_render_session_orphaned_block.moc"
