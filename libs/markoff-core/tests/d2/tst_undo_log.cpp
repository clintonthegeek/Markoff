// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/UndoLog.h>
#include <markoff/core/BlockId.h>

class TstUndoLog : public QObject {
    Q_OBJECT
private slots:
    // Task 2.1: Transaction RAII
    void singleTransaction_producesOneEntry();
    void emptyTransaction_producesNoEntry();
    void nestedTransaction_joinsOuter();
    // Task 2.2: undo/redo dispatch
    void undo_dispatchesTargetsInReverseOpOrder();
    void redo_replaysFwd();
    // Task 2.3: per-block undo
    void undoForBlock_picksMostRecentEntryThatTouchesThisBlock();
    // Task 2.4: coalescing
    void coalescing_extendsPreviousEntry();
    void coalescing_breaksOnFocusChange();
    void coalescing_breaksOnIdleThreshold();
    void coalescing_breaksOnStructuralOp();
    // Task 2.5: compact
    void compact_dropsEntriesAllOfWhoseOpsAreCollapsed();
};

// ---------- Task 2.1 ----------

void TstUndoLog::singleTransaction_producesOneEntry() {
    Markoff::UndoLog log;
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 42);
    }
    QCOMPARE(log.entryCount(), 1u);
}

void TstUndoLog::emptyTransaction_producesNoEntry() {
    Markoff::UndoLog log;
    { Markoff::UndoLog::Transaction t(log); }
    QCOMPARE(log.entryCount(), 0u);
}

void TstUndoLog::nestedTransaction_joinsOuter() {
    Markoff::UndoLog log;
    {
        Markoff::UndoLog::Transaction outer(log);
        outer.registerOp(Markoff::CrdtTarget::idList(), 1);
        {
            Markoff::UndoLog::Transaction inner(log);
            inner.registerOp(Markoff::CrdtTarget::buffer(Markoff::BlockId::fromRaw(7)), 2);
        }
    }
    QCOMPARE(log.entryCount(), 1u);
    QCOMPARE(log.lastEntry().targets.size(), 2u);
}

// ---------- Task 2.2 ----------

void TstUndoLog::undo_dispatchesTargetsInReverseOpOrder() {
    Markoff::UndoLog log;
    std::vector<Markoff::OpId> dispatched;
    log.setDispatcher([&](const Markoff::CrdtTarget &, Markoff::OpId opId, bool) {
        dispatched.push_back(opId);
    });
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 100);
        t.registerOp(Markoff::CrdtTarget::kindTagMap(), 101);
        t.registerOp(Markoff::CrdtTarget::buffer(Markoff::BlockId::fromRaw(1)), 102);
    }
    log.undo();
    QCOMPARE(dispatched.size(), 3u);
    QCOMPARE(dispatched[0], 102u);
    QCOMPARE(dispatched[1], 101u);
    QCOMPARE(dispatched[2], 100u);
}

void TstUndoLog::redo_replaysFwd() {
    Markoff::UndoLog log;
    std::vector<Markoff::OpId> dispatched;
    log.setDispatcher([&](const Markoff::CrdtTarget &, Markoff::OpId opId, bool) {
        dispatched.push_back(opId);
    });
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 10);
        t.registerOp(Markoff::CrdtTarget::buffer(Markoff::BlockId::fromRaw(1)), 11);
    }
    log.undo();
    dispatched.clear();
    log.redo();
    QCOMPARE(dispatched.size(), 2u);
    QCOMPARE(dispatched[0], 10u);  // forward order
    QCOMPARE(dispatched[1], 11u);
}

// ---------- Task 2.3 ----------

void TstUndoLog::undoForBlock_picksMostRecentEntryThatTouchesThisBlock() {
    Markoff::UndoLog log;
    std::vector<Markoff::OpId> dispatched;
    log.setDispatcher([&](const Markoff::CrdtTarget &, Markoff::OpId opId, bool) {
        dispatched.push_back(opId);
    });

    auto blkA = Markoff::BlockId::fromRaw(1);
    auto blkB = Markoff::BlockId::fromRaw(2);
    auto blkC = Markoff::BlockId::fromRaw(3);

    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blkA), 10); }
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blkB), 20); }
    {
        Markoff::UndoLog::Transaction t(log);
        t.registerOp(Markoff::CrdtTarget::idList(), 30);
        t.registerOp(Markoff::CrdtTarget::kindTagMap(), 31);
        t.registerOp(Markoff::CrdtTarget::buffer(blkC), 32);
    }

    log.undoForBlock(blkB);
    QCOMPARE(dispatched, std::vector<Markoff::OpId>{20});

    dispatched.clear();
    log.undoForBlock(blkC);
    QCOMPARE(dispatched, (std::vector<Markoff::OpId>{32, 31, 30}));
}

// ---------- Task 2.4 ----------

void TstUndoLog::coalescing_extendsPreviousEntry() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    Markoff::CoalesceContext ctx{blk, true, 0};
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 1);
    });
    ctx.timestampMs = 100;
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 2);
    });
    QCOMPARE(log.entryCount(), 1u);
    QCOMPARE(log.lastEntry().targets.size(), 2u);
}

void TstUndoLog::coalescing_breaksOnFocusChange() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    Markoff::CoalesceContext ctx{blk, true, 0, 0};
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 1);
    });
    ctx.focusGeneration = 1;  // focus changed
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 2);
    });
    QCOMPARE(log.entryCount(), 2u);
}

void TstUndoLog::coalescing_breaksOnIdleThreshold() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    Markoff::CoalesceContext ctx{blk, true, 0};
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 1);
    });
    ctx.timestampMs = 1500;  // >1000ms
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 2);
    });
    QCOMPARE(log.entryCount(), 2u);
}

void TstUndoLog::coalescing_breaksOnStructuralOp() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    Markoff::CoalesceContext ctx{blk, true, 0};
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::buffer(blk), 1);
    });
    // structural op = isPrintable false
    ctx.isPrintable = false;
    log.maybeCoalesceOrTransaction(ctx, [&](Markoff::UndoLog::Transaction &t) {
        t.registerOp(Markoff::CrdtTarget::idList(), 2);
    });
    QCOMPARE(log.entryCount(), 2u);
}

// ---------- Task 2.5 ----------

void TstUndoLog::compact_dropsEntriesAllOfWhoseOpsAreCollapsed() {
    Markoff::UndoLog log;
    auto blk = Markoff::BlockId::fromRaw(1);
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blk), 10); }
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blk), 20); }
    { Markoff::UndoLog::Transaction t(log); t.registerOp(Markoff::CrdtTarget::buffer(blk), 30); }
    QCOMPARE(log.entryCount(), 3u);

    // Collapse ops 10 and 20 but not 30
    log.compact([](const Markoff::CrdtTarget &, Markoff::OpId opId) { return opId <= 20; });
    QCOMPARE(log.entryCount(), 1u);
    QCOMPARE(log.lastEntry().targets[0].second, 30u);
}

QTEST_GUILESS_MAIN(TstUndoLog)
#include "tst_undo_log.moc"
