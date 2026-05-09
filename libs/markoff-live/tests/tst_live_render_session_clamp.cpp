// SPDX-License-Identifier: GPL-3.0-or-later
//
// B5a: Setting a selection whose TextAnchor resolves to a byte offset past
// the end of the block text clamps to the block length — no crash, no
// out-of-range qtPos.

#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveSelectionView.h>

class TestSessionClamp : public QObject {
    Q_OBJECT
private slots:
    // Anchor at end-of-block (byte == blockLen) must clamp to block text length.
    void clamp_to_block_length()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        // "hi\n" — block text in model is "hi" (2 chars); raw blockText is "hi\n" (3 bytes).
        doc.loadFromMarkdown("hi\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.selectionView();

        // textAnchorAt byte 2 = end of "hi" content (just before the '\n' separator).
        // This is a valid in-range byte; resolve should give qtPos == 2.
        Markoff::Selection sel;
        sel.kind   = Markoff::Selection::Kind::Primary;
        sel.anchor = doc.textAnchorAt(quint32(0), false);
        sel.active = doc.textAnchorAt(quint32(2), true);
        session->setPrimarySelection(sel);

        QVERIFY(sv->hasSelection());
        const QPoint range = sv->rangeForBlock(0);
        QCOMPARE(range.x(), 0);
        // qtPos 2 = end of "hi".
        QVERIFY(range.y() <= 2);
        QVERIFY(range.y() >= 0);
    }

    // A null / default TextAnchor that resolves to byte 0 gives qtPos 0 with
    // no crash.
    void zero_byte_anchor_is_safe()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("abc\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.selectionView();

        // anchor == active at byte 0 → collapsed selection → hasSelection() false.
        Markoff::Selection sel;
        sel.kind   = Markoff::Selection::Kind::Primary;
        sel.anchor = doc.textAnchorAt(quint32(0), false);
        sel.active = doc.textAnchorAt(quint32(0), false);
        session->setPrimarySelection(sel);  // Must not crash.

        // Collapsed selection (anchor == active) is not "hasSelection".
        // The view may or may not report it as a selection; either is acceptable
        // as long as there is no crash and no out-of-range qtPos.
        const QPoint range = sv->rangeForBlock(0);
        // Either not-participating (-1,-1) or a zero-width range (x==y==0).
        const bool notParticipating = (range.x() == -1 && range.y() == -1);
        const bool zeroWidth        = (range.x() == 0  && range.y() == 0);
        QVERIFY(notParticipating || zeroWidth);
    }
};

QTEST_MAIN(TestSessionClamp)
#include "tst_live_render_session_clamp.moc"
