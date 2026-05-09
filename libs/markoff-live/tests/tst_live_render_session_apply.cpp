// SPDX-License-Identifier: GPL-3.0-or-later
//
// B3: LiveSelectionView subscribes to Session::primarySelectionChanged.
// When an external caller sets the session's primary selection, the view
// must reflect the new anchor+active positions.

#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveSelectionView.h>

class TestSessionApply : public QObject {
    Q_OBJECT
private slots:
    // External setPrimarySelection propagates into the view's selection state.
    void external_set_propagates_to_view()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.selectionView();
        QVERIFY(!sv->hasSelection());

        QSignalSpy spy(sv, &Markoff::Live::LiveSelectionView::selectionChanged);

        // Build a selection from byte 0 (anchor) to byte 5 (active, before " world").
        // "hello" is 5 UTF-8 bytes.
        Markoff::Selection sel;
        sel.kind   = Markoff::Selection::Kind::Primary;
        sel.anchor = doc.textAnchorAt(quint32(0), /*rightBias=*/false);
        sel.active = doc.textAnchorAt(quint32(5), /*rightBias=*/true);
        session->setPrimarySelection(sel);

        // The view should now show a selection spanning qtPos 0..5 on block 0.
        QCOMPARE(spy.count(), 1);
        QVERIFY(sv->hasSelection());
        const QPoint range = sv->rangeForBlock(0);
        QCOMPARE(range.x(), 0);
        QCOMPARE(range.y(), 5);
    }

    // Reentrancy guard: view calling setPrimarySelection during begin() must
    // NOT echo back and cause a double-selectionChanged emission.
    void no_echo_from_sync_to_session()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.selectionView();
        QSignalSpy spy(sv, &Markoff::Live::LiveSelectionView::selectionChanged);

        // begin() calls syncToSession() which calls setPrimarySelection()
        // which emits primarySelectionChanged. The guard must stop the echo.
        sv->begin(0, 2);
        sv->extend(0, 4);

        // Exactly 2 emissions from the two explicit calls; 0 echoes.
        QCOMPARE(spy.count(), 2);
    }

    // Detaching a session (setSession(nullptr)) stops propagation.
    void detach_stops_propagation()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");

        Markoff::Session *session = doc.createSession();
        binding.setSession(session);

        auto *sv = binding.selectionView();

        // Detach.
        binding.setSession(nullptr);

        QSignalSpy spy(sv, &Markoff::Live::LiveSelectionView::selectionChanged);

        Markoff::Selection sel;
        sel.kind   = Markoff::Selection::Kind::Primary;
        sel.anchor = doc.textAnchorAt(quint32(0), false);
        sel.active = doc.textAnchorAt(quint32(3), true);
        session->setPrimarySelection(sel);

        // No emission because we detached.
        QCOMPARE(spy.count(), 0);
        QVERIFY(!sv->hasSelection());
    }
};

QTEST_MAIN(TestSessionApply)
#include "tst_live_render_session_apply.moc"
