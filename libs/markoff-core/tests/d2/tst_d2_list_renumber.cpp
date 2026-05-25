// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff;

class TstD2ListRenumber : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void no_op_on_already_sequential() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("1. one\n2. two\n3. three\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 3);

        {
            UndoLog::Transaction t(doc.d2UndoLog());
            Cmd::renumberRunStartingAt(doc, ids[1], t);
        }
        // Numbers stay 1, 2, 3 — no edits needed
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0]).value(AttrNames::MarkerNumber)), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1]).value(AttrNames::MarkerNumber)), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2]).value(AttrNames::MarkerNumber)), 3);
    }

    void renumber_after_mid_insert() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("1. one\n2. two\n3. three\n");
        const auto idsBefore = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(idsBefore.size()), 3);

        BlockId newId;
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            newId = Cmd::insertListItemAfter(doc, idsBefore[0], t);
            Cmd::renumberRunStartingAt(doc, newId, t);
        }

        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 4);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0]).value(AttrNames::MarkerNumber)), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1]).value(AttrNames::MarkerNumber)), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2]).value(AttrNames::MarkerNumber)), 3);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[3]).value(AttrNames::MarkerNumber)), 4);
    }

    void renumber_does_not_cross_indent_boundary() {
        // Document order: outer1(indent=0), inner-a(indent=1), inner-b(indent=1), outer2(indent=0)
        // The inner items have ordered markers; they form their own run at indent=1.
        // Renumbering from the inner start (ids[1]) should fix inner items only.
        // Outer items must remain unaffected.
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown(
            "1. one\n"
            "   1. sub a\n"
            "   2. sub b\n"
            "2. two\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 4);

        // Force inner item 2 (sub b) to a wrong number, then renumber from inner start.
        // The outer items must not be touched.
        {
            UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2SetBlockAttr(ids[2], AttrNames::MarkerNumber, 99, t);
            // Renumber only the inner run (starting from ids[1], indent=1)
            Cmd::renumberRunStartingAt(doc, ids[1], t);
        }

        // Inner run: sub a=1, sub b=2 (renumbered from 99 back to 2)
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1]).value(AttrNames::MarkerNumber)), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2]).value(AttrNames::MarkerNumber)), 2);
        // Outer items untouched: one=1, two=2
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0]).value(AttrNames::MarkerNumber)), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[3]).value(AttrNames::MarkerNumber)), 2);
    }

    void renumber_ignores_non_ordered_runs() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("- a\n- b\n- c\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 3);

        {
            UndoLog::Transaction t(doc.d2UndoLog());
            Cmd::renumberRunStartingAt(doc, ids[0], t);
        }
        // No-op: unordered list has no MarkerNumber attr at all
        for (const auto &id : ids)
            QVERIFY(!doc.blockAttrs(id).contains(AttrNames::MarkerNumber));
    }
};

QTEST_GUILESS_MAIN(TstD2ListRenumber)
#include "tst_d2_list_renumber.moc"
