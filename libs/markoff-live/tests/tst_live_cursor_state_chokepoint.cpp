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
    // Register with kind "heading" while model has no row → kindFor returns "" →
    // stale check suppresses dispatch even if establishFocus is called after.
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
    // §5.1.1 — delegate registered with kind "heading"; model now
    // reports "paragraph"; establishFocus should NOT dispatch.
    const auto a = Markoff::BlockAnchor::fromRaw(1);

    // Register stale delegate (kind "heading") before the row exists.
    MockDelegate dStale;
    m_state->delegateAvailable(a, "heading", &dStale);

    // Now model knows the block as "paragraph".
    m_model->insertTestRow(a, "paragraph", "p text");

    m_state->establishFocus(a, 5);
    // Stale delegate must NOT receive takeFocus.
    QCOMPARE(dStale.takeFocusCalls(), QList<int>{});

    // After the stale delegate goes away and a matching one arrives → dispatch.
    m_state->delegateGoingAway(a);
    MockDelegate dFresh;
    m_state->delegateAvailable(a, "paragraph", &dFresh);
    QCOMPARE(dFresh.takeFocusCalls(), QList<int>{5});
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestLiveCursorStateChokepoint)
#include "tst_live_cursor_state_chokepoint.moc"
