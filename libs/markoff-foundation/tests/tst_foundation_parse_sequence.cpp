// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParseSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        MarkoffDocument d{1};
        QCOMPARE(d.parseSequence(), quint64{0});
    }

    void parse_sequence_increments_after_first_parse() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);
        MarkoffEdit e; e.oldStart = 0; e.oldEnd = 0; e.newText = "hello";
        d.applyLocalEdit({e});
        QVERIFY(spy.wait(2000));
        QVERIFY(d.parseSequence() > 0);
    }

    void parse_sequence_strictly_monotonic_across_three_parses() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        // First edit triggers first parse
        MarkoffEdit e1; e1.oldStart = 0; e1.oldEnd = 0; e1.newText = "a";
        d.applyLocalEdit({e1});
        QVERIFY(spy.wait(2000));
        const quint64 seq1 = d.parseSequence();
        QVERIFY(seq1 >= 1);
        spy.clear();

        // Second edit (after first completes) triggers second parse
        MarkoffEdit e2; e2.oldStart = 1; e2.oldEnd = 1; e2.newText = "b";
        d.applyLocalEdit({e2});
        QVERIFY(spy.wait(2000));
        const quint64 seq2 = d.parseSequence();
        QVERIFY(seq2 > seq1);
        spy.clear();

        // Third edit (after second completes) triggers third parse
        MarkoffEdit e3; e3.oldStart = 2; e3.oldEnd = 2; e3.newText = "c";
        d.applyLocalEdit({e3});
        QVERIFY(spy.wait(2000));
        const quint64 seq3 = d.parseSequence();
        QVERIFY(seq3 > seq2);
    }
};

QTEST_MAIN(TstFoundationParseSequence)
#include "tst_foundation_parse_sequence.moc"
