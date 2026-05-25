// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
//
// Unit tests for Markoff::CausalLwwMap — including local_clear(), the
// non-emitting single-replica reset primitive used by
// MarkoffDocument::wipeD2State() to drop all entries from a sibling map
// when the canonical content is replaced from outside the CRDT.
//
// Spec: docs/specs/2026-05-25-d2-reset-clear-design.md §3.2.
#include <QTest>
#include <markoff/core/CausalLwwMap.h>

class TstCausalLwwMap : public QObject {
    Q_OBJECT
private slots:
    void emptyMap_getReturnsNullopt();
    void set_then_getReturnsValue();
    void setOverwrites_higherStampWins();
    void remove_clearsEntry();
    void setWithNextStamp_returnsMonotonicOpId();
    void setOnChange_firesOnEachSet();
    void undo_revertsLastWrite();
    void redo_replaysAfterUndo();
    void undo_acrossMultipleKeys();
    void compact_dropsEntriesBelowWatermark();
    void applyRemote_acceptsForeignWrite();
    void applyRemote_doesNotEnterLocalUndoStack();
    // local_clear() — non-emitting single-replica reset (Phase C)
    void localClear_emptiesTheMap();
    void localClear_preservesClockMonotonicity();
    void localClear_doesNotFireChangeCallback();
    void localClear_clearsRedoStack();
};

void TstCausalLwwMap::emptyMap_getReturnsNullopt() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    QCOMPARE(map.get(42).has_value(), false);
}

void TstCausalLwwMap::set_then_getReturnsValue() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("hello"), Markoff::CausalStamp{1, 1});
    QCOMPARE(map.get(42).value(), QStringLiteral("hello"));
}

void TstCausalLwwMap::setOverwrites_higherStampWins() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("first"), Markoff::CausalStamp{1, 1});
    map.set(42, QStringLiteral("second"), Markoff::CausalStamp{1, 2});
    QCOMPARE(map.get(42).value(), QStringLiteral("second"));
    map.set(42, QStringLiteral("stale"), Markoff::CausalStamp{1, 1});
    QCOMPARE(map.get(42).value(), QStringLiteral("second"));
}

void TstCausalLwwMap::remove_clearsEntry() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(42, QStringLiteral("x"), Markoff::CausalStamp{1, 1});
    map.remove(42, Markoff::CausalStamp{1, 2});
    QCOMPARE(map.get(42).has_value(), false);
}

void TstCausalLwwMap::setWithNextStamp_returnsMonotonicOpId() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    auto op1 = map.setWithNextStamp(7, QStringLiteral("a"));
    auto op2 = map.setWithNextStamp(7, QStringLiteral("b"));
    QVERIFY(op2 > op1);
}

void TstCausalLwwMap::setOnChange_firesOnEachSet() {
    Markoff::CausalLwwMap<int, QString> map(1);
    int callCount = 0;
    int lastKey = -1;
    std::optional<QString> lastOld;
    std::optional<QString> lastNew;
    map.setOnChange([&](int k, std::optional<QString> oldV, std::optional<QString> newV) {
        ++callCount;
        lastKey = k;
        lastOld = oldV;
        lastNew = newV;
    });

    map.set(7, QStringLiteral("a"), {1, 1});
    QCOMPARE(callCount, 1);
    QCOMPARE(lastKey, 7);
    QCOMPARE(lastOld.has_value(), false);
    QCOMPARE(lastNew.value(), QStringLiteral("a"));

    map.set(7, QStringLiteral("b"), {1, 2});
    QCOMPARE(callCount, 2);
    QCOMPARE(lastOld.value(), QStringLiteral("a"));
    QCOMPARE(lastNew.value(), QStringLiteral("b"));

    // Stale write: no callback fire.
    map.set(7, QStringLiteral("c"), {1, 1});
    QCOMPARE(callCount, 2);
}

void TstCausalLwwMap::undo_revertsLastWrite() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(1, QStringLiteral("b"), {1, 2});
    map.undo();
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
}

void TstCausalLwwMap::redo_replaysAfterUndo() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(1, QStringLiteral("b"), {1, 2});
    map.undo();
    map.redo();
    QCOMPARE(map.get(1).value(), QStringLiteral("b"));
}

void TstCausalLwwMap::undo_acrossMultipleKeys() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(2, QStringLiteral("b"), {1, 2});
    map.undo();  // undoes set(2, "b")
    QCOMPARE(map.get(2).has_value(), false);
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
}

void TstCausalLwwMap::compact_dropsEntriesBelowWatermark() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("a"), {1, 1});
    map.set(2, QStringLiteral("b"), {1, 5});
    map.set(3, QStringLiteral("c"), {1, 10});
    map.compact(Markoff::CausalStamp{1, 5});

    // State is preserved
    QCOMPARE(map.get(1).value(), QStringLiteral("a"));
    QCOMPARE(map.get(2).value(), QStringLiteral("b"));
    QCOMPARE(map.get(3).value(), QStringLiteral("c"));

    // Only stamp-10 op survives on the undo stack
    map.undo();
    QCOMPARE(map.get(3).has_value(), false);
    map.undo();
    QCOMPARE(map.get(2).value(), QStringLiteral("b")); // can't undo further
}

void TstCausalLwwMap::applyRemote_acceptsForeignWrite() {
    Markoff::CausalLwwMap<int, QString> map(/*replicaId=*/1);
    Markoff::CausalLwwMap<int, QString>::RemoteOp op{
        /*key=*/7, /*value=*/QStringLiteral("from-replica-2"),
        /*stamp=*/{2, 1}, /*tombstone=*/false
    };
    map.applyRemote(op);
    QCOMPARE(map.get(7).value(), QStringLiteral("from-replica-2"));
}

void TstCausalLwwMap::applyRemote_doesNotEnterLocalUndoStack() {
    Markoff::CausalLwwMap<int, QString> map(1);
    map.set(1, QStringLiteral("local"), {1, 1});
    Markoff::CausalLwwMap<int, QString>::RemoteOp op{2, QStringLiteral("remote"), {2, 1}, false};
    map.applyRemote(op);
    map.undo();  // undoes only set(1, "local")
    QCOMPARE(map.get(1).has_value(), false);
    QCOMPARE(map.get(2).value(), QStringLiteral("remote")); // remote write survives
}

// --- local_clear() tests (Phase C: falsify then implement) ---

void TstCausalLwwMap::localClear_emptiesTheMap()
{
    Markoff::CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.setWithNextStamp("b", "2");
    QVERIFY(map.get("a").has_value());
    QVERIFY(map.get("b").has_value());

    map.local_clear();

    QVERIFY(!map.get("a").has_value());
    QVERIFY(!map.get("b").has_value());

    int liveCount = 0;
    map.forEachValue([&](const QByteArray &, const QByteArray &){ ++liveCount; });
    QCOMPARE(liveCount, 0);
}

void TstCausalLwwMap::localClear_preservesClockMonotonicity()
{
    Markoff::CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    const Markoff::CausalStamp before = map.currentStamp();

    map.local_clear();

    map.setWithNextStamp("a", "2");
    const Markoff::CausalStamp after = map.currentStamp();

    QVERIFY(before < after);
}

void TstCausalLwwMap::localClear_doesNotFireChangeCallback()
{
    Markoff::CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.setWithNextStamp("b", "2");

    int calls = 0;
    map.setOnChange([&](const QByteArray &, std::optional<QByteArray>,
                        std::optional<QByteArray>){ ++calls; });

    map.local_clear();

    QCOMPARE(calls, 0);
}

void TstCausalLwwMap::localClear_clearsRedoStack()
{
    Markoff::CausalLwwMap<QByteArray, QByteArray> map(/*replicaId=*/1);
    map.setWithNextStamp("a", "1");
    map.undo();
    // After undo, redo stack has one entry; calling redo would restore "a".

    map.local_clear();
    map.redo();  // must be a no-op; no crash, no resurrected entry.

    QVERIFY(!map.get("a").has_value());
}

QTEST_GUILESS_MAIN(TstCausalLwwMap)
#include "tst_causal_lww_map.moc"
