// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff;

class TstFoundationEditSequence : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fresh_doc_starts_at_zero() {
        MarkoffDocument d{1};
        QCOMPARE(d.editSequence(), quint64{0});
    }

    void apply_flat_edit_increments() {
        MarkoffDocument d{1};
        const auto seq0 = d.d2EditSequence();
        d.applyFlatEdit(0, 0, "x", Origin::UserEdit);
        QVERIFY(d.d2EditSequence() > seq0);
    }

    void undo_increments() {
        MarkoffDocument d{1};
        // resetContent(UserRevertToSaved) pushes an undoable entry into the legacy buffer.
        d.resetContent("x", Origin::UserRevertToSaved);
        const auto seq1 = d.editSequence();
        d.undo();
        QVERIFY(d.editSequence() > seq1);
    }

    void redo_increments() {
        MarkoffDocument d{1};
        d.resetContent("x", Origin::UserRevertToSaved);
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
        quint64 prev = d.d2EditSequence();
        for (int i = 0; i < 20; ++i) {
            d.applyFlatEdit(static_cast<quint32>(i), static_cast<quint32>(i),
                            "a", Origin::UserEdit);
            const auto cur = d.d2EditSequence();
            QVERIFY(cur > prev);
            prev = cur;
        }
    }

    void undo_on_empty_stack_does_not_increment() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        const auto op = d.undo();
        QVERIFY(!op.has_value());
        QCOMPARE(d.editSequence(), seq0);
    }

    void redo_on_empty_stack_does_not_increment() {
        MarkoffDocument d{1};
        const auto seq0 = d.editSequence();
        const auto op = d.redo();
        QVERIFY(!op.has_value());
        QCOMPARE(d.editSequence(), seq0);
    }
};

QTEST_MAIN(TstFoundationEditSequence)
#include "tst_foundation_edit_sequence.moc"
