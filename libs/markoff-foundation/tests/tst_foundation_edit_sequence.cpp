// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationEditSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        MarkoffDocument d{1};
        QCOMPARE(d.editSequence(), quint64{0});
    }

    void apply_local_edit_increments() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        QVERIFY(d.editSequence() > seq0);
    }

    void undo_increments() {
        MarkoffDocument d{1};
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        const auto seq1 = d.editSequence();
        d.undo();
        QVERIFY(d.editSequence() > seq1);
    }

    void redo_increments() {
        MarkoffDocument d{1};
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "x";
        d.applyLocalEdit({e});
        d.undo();
        const auto seq2 = d.editSequence();
        d.redo();
        QVERIFY(d.editSequence() > seq2);
    }

    void reset_content_increments() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        d.resetContent("hello", Origin::FirstOpen);
        QVERIFY(d.editSequence() > seq0);
    }

    void monotonic_under_burst() {
        MarkoffDocument d{1};
        quint64 prev = d.editSequence();
        for (int i = 0; i < 20; ++i) {
            MarkoffEdit e;
            e.oldStart = static_cast<quint32>(i);
            e.oldEnd   = static_cast<quint32>(i);
            e.newText  = "a";
            d.applyLocalEdit({e});
            const auto cur = d.editSequence();
            QVERIFY(cur > prev);
            prev = cur;
        }
    }
};

QTEST_MAIN(TstFoundationEditSequence)
#include "tst_foundation_edit_sequence.moc"
