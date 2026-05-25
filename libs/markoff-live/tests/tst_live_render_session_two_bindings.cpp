// SPDX-License-Identifier: GPL-3.0-or-later
//
// B4: Two LiveListModelBindings sharing the same Session.
// When binding A sets a selection (→ syncToSession → setPrimarySelection),
// binding B sees the updated selection via primarySelectionChanged.

#include <QTest>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveCursorState.h>

class TestTwoBindings : public QObject {
    Q_OBJECT
private slots:
    // Binding A begin/extend → Session.setPrimarySelection →
    // binding B's selectionView reflects the same range.
    void binding_a_selection_propagates_to_binding_b()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding bindingA;
        Markoff::Live::LiveListModelBinding bindingB;
        bindingA.setDocument(&doc);
        bindingB.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Session *session = doc.createSession();
        bindingA.setSession(session);
        bindingB.setSession(session);

        auto *svA = bindingA.cursorState();
        auto *svB = bindingB.cursorState();

        QSignalSpy spyB(svB, &Markoff::Live::LiveCursorState::selectionChanged);

        // Binding A sets a selection on block 0, qtPos 0..5 ("hello").
        svA->begin(0, 0);
        svA->extend(0, 5);

        // Binding B must have received a selectionChanged emission.
        QVERIFY(spyB.count() >= 1);
        QVERIFY(svB->hasSelection());
        const QPoint rangeB = svB->rangeForBlock(0);
        QCOMPARE(rangeB.x(), 0);
        QCOMPARE(rangeB.y(), 5);
    }

    // Reentrancy: neither binding enters an infinite echo loop.
    // After one round-trip, the selection stabilises (no further emissions).
    void no_echo_loop()
    {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding bindingA;
        Markoff::Live::LiveListModelBinding bindingB;
        bindingA.setDocument(&doc);
        bindingB.setDocument(&doc);
        doc.loadFromMarkdown("hello\n");

        Markoff::Session *session = doc.createSession();
        bindingA.setSession(session);
        bindingB.setSession(session);

        auto *svA = bindingA.cursorState();
        auto *svB = bindingB.cursorState();

        QSignalSpy spyA(svA, &Markoff::Live::LiveCursorState::selectionChanged);
        QSignalSpy spyB(svB, &Markoff::Live::LiveCursorState::selectionChanged);

        // One begin on A (→ syncToSession → primarySelectionChanged → B updates once).
        svA->begin(0, 2);

        // A emitted once (the explicit call).
        QCOMPARE(spyA.count(), 1);
        // B emitted once (from the session signal), not in a loop.
        QCOMPARE(spyB.count(), 1);
    }
};

QTEST_MAIN(TestTwoBindings)
#include "tst_live_render_session_two_bindings.moc"
