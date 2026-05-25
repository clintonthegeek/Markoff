// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/core/BlockAnchor.h>

#include <QQuickItem>
#include <QSignalSpy>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class MockDelegate : public QQuickItem {
    Q_OBJECT
public:
    Q_INVOKABLE void takeFocus(int qtPos) {
        m_calls.append(qtPos);
    }
    QList<int> takeFocusCalls() const { return m_calls; }
    void clearCalls() { m_calls.clear(); }
private:
    QList<int> m_calls;
};

class TestLiveCursorStateChokepoint : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void pending_supersession();                   // §7.1
    void pending_survives_delegate_destruction();  // §7.2
    void bad_blockid_drops_silently();             // §7.3
    void pending_times_out_after_500ms();          // §7.4
    void delegate_arrives_without_pending();       // §7.5
    void stale_registration_holds_pending();       // §5.1.1

    // 2026-05-22 cursor-authority decision (docs/specs/...).
    void syncFromTextEdit_rejects_crossblock();    // §6.1
    void syncFromTextEdit_accepts_sameblock();     // §6.1 — companion
    void anchor_clears_on_crossblock_request();    // §6.2

private:
    std::unique_ptr<LiveBlockModel>  m_model;
    std::unique_ptr<LiveCursorState> m_state;
};

void TestLiveCursorStateChokepoint::init() {
    m_model = std::make_unique<LiveBlockModel>();
    m_state = std::make_unique<LiveCursorState>(nullptr, nullptr, nullptr);
    m_state->attachModel(m_model.get());
}

void TestLiveCursorStateChokepoint::cleanup() {
    m_state.reset();
    m_model.reset();
}

void TestLiveCursorStateChokepoint::pending_supersession() {
    // §7.1 — latest establishFocus wins.
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    const auto b = Markoff::BlockAnchor::fromRaw(2);
    m_model->insertTestRow(a, "paragraph", "text a");
    m_model->insertTestRow(b, "paragraph", "text b");

    m_state->establishFocus(a, 5);
    m_state->establishFocus(b, 9);

    MockDelegate dB;
    m_state->delegateAvailable(b, "paragraph", &dB);
    QCOMPARE(dB.takeFocusCalls(), QList<int>{9});

    MockDelegate dA;
    m_state->delegateAvailable(a, "paragraph", &dA);
    QCOMPARE(dA.takeFocusCalls(), QList<int>{});  // a was superseded
}

void TestLiveCursorStateChokepoint::pending_survives_delegate_destruction() {
    // §7.2 — pending request survives a delegate being destroyed.
    const auto a = Markoff::BlockAnchor::fromRaw(1);

    auto *dOld = new MockDelegate;
    // Register while model has no row → kindFor returns "" → the
    // empty-currentKind guard in tryResolvePending suppresses dispatch even
    // though m_delegates contains the anchor. (Pre-tier-3 this also relied on
    // the literal-kind mismatch; tier-3 narrowed staleness to delegateClass,
    // so the empty-kind guard is the explicit "no basis to dispatch" rail.)
    m_state->delegateAvailable(a, "heading", dOld);
    m_state->establishFocus(a, 3);

    m_state->delegateGoingAway(a);
    delete dOld;

    // Now add the row to the model and register with matching kind.
    m_model->insertTestRow(a, "paragraph", "was heading");
    auto *dNew = new MockDelegate;
    m_state->delegateAvailable(a, "paragraph", dNew);
    QCOMPARE(dNew->takeFocusCalls(), QList<int>{3});
    delete dNew;
}

void TestLiveCursorStateChokepoint::bad_blockid_drops_silently() {
    // §7.3 — establishFocus for a BlockAnchor not in the model and
    // not registered is dropped silently. No assert, no signal.
    const auto unknown = Markoff::BlockAnchor::fromRaw(9999);
    m_state->establishFocus(unknown, 0);
    QVERIFY(!m_state->hasPendingFocus());
}

void TestLiveCursorStateChokepoint::pending_times_out_after_500ms() {
    // §7.4 — pending request expires after kPendingFocusTimeoutMs.
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    m_model->insertTestRow(a, "paragraph", "text");

    m_state->establishFocus(a, 5);
    QVERIFY(m_state->hasPendingFocus());

    QTest::qWait(600);
    // A delegateAvailable call triggers tryResolvePending → expiry check.
    m_state->delegateAvailable(Markoff::BlockAnchor::fromRaw(42), "paragraph", nullptr);
    QVERIFY(!m_state->hasPendingFocus());
}

void TestLiveCursorStateChokepoint::delegate_arrives_without_pending() {
    // §7.5 — delegateAvailable with no pending request is a no-op for
    // focus dispatch, but registers the delegate in m_delegates.
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    MockDelegate d;
    m_state->delegateAvailable(a, "paragraph", &d);
    QCOMPARE(d.takeFocusCalls(), QList<int>{});
    QVERIFY(m_state->isDelegateRegistered(a));
}

void TestLiveCursorStateChokepoint::stale_registration_holds_pending() {
    // §5.1.1 — delegate registered with a kind whose `delegateClass` no
    // longer matches the model's current kind for the same anchor. Tier-3
    // narrowed staleness to *cross-class* mismatches: the within-class
    // transitions (paragraph ↔ heading ↔ blockquote ↔ list-item) reuse
    // the same UnifiedInlineTextDelegate, so dispatch on a "stale" entry
    // in that class is correct. Use code-block (its own delegateClass)
    // to exercise the genuine stale-cross-class path.
    const auto a = Markoff::BlockAnchor::fromRaw(1);

    // Register stale delegate with cross-class kind ("code-block") before
    // the row exists.
    MockDelegate dStale;
    m_state->delegateAvailable(a, "code-block", &dStale);

    // Now model knows the block as "paragraph" (different delegateClass).
    m_model->insertTestRow(a, "paragraph", "p text");

    m_state->establishFocus(a, 5);
    // Stale delegate must NOT receive takeFocus — cross-class transition
    // means a new delegate is incoming.
    QCOMPARE(dStale.takeFocusCalls(), QList<int>{});

    // After the stale delegate goes away and a matching one arrives → dispatch.
    m_state->delegateGoingAway(a);
    MockDelegate dFresh;
    m_state->delegateAvailable(a, "paragraph", &dFresh);
    QCOMPARE(dFresh.takeFocusCalls(), QList<int>{5});
}

// ---------------------------------------------------------------------------
// 2026-05-22 cursor-authority decision (docs/specs/2026-05-22-cursor-authority-decision.md)
// ---------------------------------------------------------------------------

void TestLiveCursorStateChokepoint::syncFromTextEdit_rejects_crossblock()
{
    // §6.1 — cross-block syncFromTextEdit calls are echoes (binding
    // refresh, ListView rebind, setPlainText cursor reset) and must
    // not move m_cursor. Cross-block moves go through request() /
    // begin() / establishFocus() — paths the chokepoint initiates.
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    const auto b = Markoff::BlockAnchor::fromRaw(2);
    m_model->insertTestRow(a, "paragraph", "text a");
    m_model->insertTestRow(b, "paragraph", "text b");

    // Focus on block A at qtPos 3.
    MockDelegate dA;
    m_state->delegateAvailable(a, "paragraph", &dA);
    m_state->establishFocus(a, 3);
    QCOMPARE(m_state->focusedAnchorRow(), 0);
    QCOMPARE(m_state->focusedQtPos(), 3);

    // A non-focused delegate (block B) reports a cursor change to
    // qtPos 7. This is what a ListView rebind would do — pushTextToDocument's
    // setPlainText sets cursorPosition to end-of-text, fires
    // cursorPositionChanged, the delegate's handler calls syncFromTextEdit.
    m_state->syncFromTextEdit(b, 7);

    // m_cursor must not have moved.
    QCOMPARE(m_state->focusedAnchorRow(), 0);   // still block A
    QCOMPARE(m_state->focusedQtPos(), 3);       // unchanged
}

void TestLiveCursorStateChokepoint::syncFromTextEdit_accepts_sameblock()
{
    // §6.1 companion — same-block syncFromTextEdit DOES update qtPos.
    // This is the legitimate path (user keyboard arrow within the
    // focused TextEdit).
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    m_model->insertTestRow(a, "paragraph", "text a");

    MockDelegate dA;
    m_state->delegateAvailable(a, "paragraph", &dA);
    m_state->establishFocus(a, 3);

    // Same-block report: user moved cursor within block A from 3 to 5.
    m_state->syncFromTextEdit(a, 5);

    QCOMPARE(m_state->focusedAnchorRow(), 0);
    QCOMPARE(m_state->focusedQtPos(), 5);
}

void TestLiveCursorStateChokepoint::anchor_clears_on_crossblock_request()
{
    // §6.2 — request() that crosses blocks clears m_selectionAnchor
    // (unless invoked via extend, which sets m_selectionExtended). This
    // closes the phantom-anchor failure mode: any non-extend cursor move
    // across blocks must abandon any prior anchor placed by an
    // unrelated click.
    //
    // The unit-test harness has no document, so begin() (which uses
    // blockAnchorAt → iterateBlocks) can't be used. Set the anchor + cursor
    // directly via the public APIs.
    const auto a = Markoff::BlockAnchor::fromRaw(1);
    const auto b = Markoff::BlockAnchor::fromRaw(2);
    m_model->insertTestRow(a, "paragraph", "text a");
    m_model->insertTestRow(b, "paragraph", "text b");

    // Simulate a click at block A, qtPos 3 — cursor + anchor co-located.
    MockDelegate dA;
    m_state->delegateAvailable(a, "paragraph", &dA);
    m_state->establishFocus(a, 3);
    m_state->setSelectionAnchor({a, 3});
    QVERIFY(m_state->selectionAnchor().has_value());
    QCOMPARE(m_state->selectionAnchor()->block, a);

    // Programmatic cross-block cursor move (e.g. requestTextCaretAtRow
    // from a cross-block down-arrow). This is NOT an extend — the user
    // never indicated a selection extension. The anchor must clear.
    MockDelegate dB;
    m_state->delegateAvailable(b, "paragraph", &dB);
    m_state->establishFocus(b, 2);

    QVERIFY2(!m_state->selectionAnchor().has_value(),
             "cross-block establishFocus must clear stale selection anchor");
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestLiveCursorStateChokepoint)
#include "tst_live_cursor_state_chokepoint.moc"
