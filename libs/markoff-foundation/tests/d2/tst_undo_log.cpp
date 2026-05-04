// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/BlockId.h>

class TstUndoLog : public QObject {
    Q_OBJECT
private slots:
    // Task 2.1: Transaction RAII
    void singleTransaction_producesOneEntry();
    void emptyTransaction_producesNoEntry();
    void nestedTransaction_joinsOuter();
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

QTEST_GUILESS_MAIN(TstUndoLog)
#include "tst_undo_log.moc"
