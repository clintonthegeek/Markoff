// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParseInputEditSeq : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void parse_carries_input_edit_sequence() {
        MarkoffDocument d{/*replicaId=*/1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        // Apply one local edit; capture the editSequence afterwards.
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "hello";
        d.applyLocalEdit({e});
        const quint64 seqAfterApply = d.editSequence();

        // Wait for the asynchronous parse to deliver parseUpdated.
        QVERIFY(spy.wait(2000));

        // The signal carries (parsed*, parseSequence, blockAnchors,
        // parseInputEditSequence). The 4th argument is the editSequence
        // captured when the parse input was scheduled — it should equal
        // the value editSequence held immediately after the apply that
        // triggered this parse.
        QVERIFY(spy.count() >= 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 4);
        const quint64 carried = args.at(3).toULongLong();
        QCOMPARE(carried, seqAfterApply);
    }

    void parse_input_seq_increases_across_edits() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        // First edit + parse.
        MarkoffEdit e1;
        e1.oldStart = 0; e1.oldEnd = 0; e1.newText = "a";
        d.applyLocalEdit({e1});
        const quint64 seq1 = d.editSequence();
        QVERIFY(spy.wait(2000));
        // Drain in case multiple parses fired (coalesce + reset on first
        // load); the LAST one is what reflects current state.
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried1 = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried1, seq1);

        // Second edit + parse.
        MarkoffEdit e2;
        e2.oldStart = 1; e2.oldEnd = 1; e2.newText = "b";
        d.applyLocalEdit({e2});
        const quint64 seq2 = d.editSequence();
        QVERIFY(seq2 > seq1);
        spy.clear();
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried2 = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried2, seq2);
    }

    void reset_content_carries_input_edit_sequence() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        d.resetContent("# heading\n\nbody\n", Origin::FirstOpen);
        const quint64 seqAfterReset = d.editSequence();
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried, seqAfterReset);
    }
};

QTEST_MAIN(TstFoundationParseInputEditSeq)
#include "tst_foundation_parse_input_edit_seq.moc"
