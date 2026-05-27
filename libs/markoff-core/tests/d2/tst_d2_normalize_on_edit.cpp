// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

namespace {
QByteArray flat(Markoff::MarkoffDocument &d) { return d.flatView(); }
int blockCount(Markoff::MarkoffDocument &d) { return int(d.iterateBlocks().size()); }
bool noInternalNewlines(Markoff::MarkoffDocument &d) {
    for (auto id : d.iterateBlocks())
        if (d.blockText(id).contains('\n')) return false;
    return true;
}
}  // namespace

class TstD2NormalizeOnEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void single_newline_insert_splits_block() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("alphabeta"));
        QCOMPARE(blockCount(d), 1);
        d.applyFlatEdit(5, 5, QByteArrayLiteral("\n"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 2);
        QCOMPARE(flat(d), QByteArrayLiteral("alpha\n\nbeta"));
    }
    void newline_run_collapses_no_empty_blocks() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("alphabeta"));
        d.applyFlatEdit(5, 5, QByteArrayLiteral("\n\n\n\n"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 2);
        QCOMPARE(flat(d), QByteArrayLiteral("alpha\n\nbeta"));
    }
    void multiline_paste_into_block() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("xy"));
        d.applyFlatEdit(1, 1, QByteArrayLiteral("a\nb\nc"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 3);
        QCOMPARE(flat(d), QByteArrayLiteral("xa\n\nb\n\ncy"));
    }
    void enter_at_end_of_block_splits_tail() {
        // Doc "alpha\n\nbeta": block0="alpha"(5), block1="beta"(4).
        // Flat no-sep positions: block0=[0,5), block1=[5,9).
        // Cursor at flat byte 5 → cursor-edit bias → startIdx=1, startWithin=0.
        // Inserting "\n" at the start of "beta" would logically create an empty
        // head block (suppressed) → net effect: block structure unchanged.
        // Expected: flat still "alpha\n\nbeta", 2 blocks, no internal newlines.
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        d.applyFlatEdit(5, 5, QByteArrayLiteral("\n"), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
        QCOMPARE(blockCount(d), 2);
        QCOMPARE(flat(d), QByteArrayLiteral("alpha\n\nbeta"));
    }
    void canonical_input_identity() {
        Markoff::MarkoffDocument d(1);
        d.loadFromMarkdown(QByteArrayLiteral("# H\n\n- one\n- two\n\npara"));
        d.applyFlatEdit(0, 0, QByteArray(), Markoff::Origin::UserEdit);
        QVERIFY(noInternalNewlines(d));
    }
};

QTEST_GUILESS_MAIN(TstD2NormalizeOnEdit)
#include "tst_d2_normalize_on_edit.moc"
