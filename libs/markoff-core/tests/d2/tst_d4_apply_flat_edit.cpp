// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

using namespace Markoff;

class TstD4ApplyFlatEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void intraBlockInsert_appendsBytesToTargetBlock();
    void intraBlockDelete_dropsRangeFromTargetBlock();
    void insertNewlineAtBlockEnd_splitsIntoTwoBlocks();
    void insertSingleNewlineMidBlock_splitsBlock();
    void deleteSpanningTwoBlocks_mergesIntoFirst();
    void undo_restoresPreEditState_acrossSplitAndMerge();
    void roundTrip_arbitraryEditMatchesFlatStringReplace();
};

namespace {
QByteArray fullText(MarkoffDocument &doc)
{
    QByteArray out;
    for (BlockId id : doc.iterateBlocks()) {
        out += doc.blockText(id);
    }
    return out;
}
}  // namespace

// ── Task 1.1: intra-block insert ─────────────────────────────────────────────

void TstD4ApplyFlatEdit::intraBlockInsert_appendsBytesToTargetBlock()
{
    // "hello world" → one block "hello world" (11 bytes)
    // Insert "!" at offset 5 → "hello! world"
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello world"));

    doc.applyFlatEdit(/*oldStart=*/5, /*oldEnd=*/5,
                      /*newText=*/QByteArray("!"),
                      /*origin=*/Origin::UserEdit);

    QCOMPARE(fullText(doc), QByteArray("hello! world"));
}

// ── Task 1.4: intra-block delete ─────────────────────────────────────────────

void TstD4ApplyFlatEdit::intraBlockDelete_dropsRangeFromTargetBlock()
{
    // "hello world" → block0="hello world" (11 bytes)
    // Delete [5,11) = " world" → "hello"
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello world"));

    doc.applyFlatEdit(5, 11, QByteArray(), Origin::UserEdit);
    QCOMPARE(fullText(doc), QByteArray("hello"));
}

// ── Task 1.5: insert newline at block end → split ────────────────────────────

void TstD4ApplyFlatEdit::insertNewlineAtBlockEnd_splitsIntoTwoBlocks()
{
    // "hello" → block0="hello" (5 bytes)
    // Insert "\n\n" at offset 5 (end of block) → block0="hello", block1=""
    // B1: block buffers are content-only; the '\n' delimiter is NOT stored in
    // block0. The split produces a content-empty block1.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello"));
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));

    doc.applyFlatEdit(5, 5, QByteArray("\n\n"), Origin::UserEdit);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("hello"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray(""));
}

// ── Task 1.6: insert newline mid-block → split ───────────────────────────────

void TstD4ApplyFlatEdit::insertSingleNewlineMidBlock_splitsBlock()
{
    // "foobar" → block0="foobar" (6 bytes)
    // Insert "\n\n" at offset 3 → block0="foo", block1="bar"
    // B1: block buffers are content-only; the delimiter '\n' is NOT carried
    // in block0. block0 retains only the content before the split point.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foobar"));

    doc.applyFlatEdit(3, 3, QByteArray("\n\n"), Origin::UserEdit);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("bar"));
}

// ── Task 1.7: cross-block delete → merge ─────────────────────────────────────

void TstD4ApplyFlatEdit::deleteSpanningTwoBlocks_mergesIntoFirst()
{
    // "foo\n\nbar" → block0="foo" (3 bytes), block1="bar" (3 bytes)
    // B1 flat view: "foobar" (6 bytes, no separator in buffers)
    // To merge block1 into block0, delete [b0sz, b0sz+b1sz) = [3, 6)
    // which removes all of block1's content → block1 deleted, result: "foo"
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"));
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));

    const uint32_t b0sz = static_cast<uint32_t>(doc.blockText(blocks[0]).size());
    const uint32_t b1sz = static_cast<uint32_t>(doc.blockText(blocks[1]).size());
    // b0sz=3 ("foo"), b1sz=3 ("bar"). Delete [b0sz, b0sz+b1sz) = [3, 6)
    doc.applyFlatEdit(b0sz, b0sz + b1sz, QByteArray(), Origin::UserEdit);

    blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(1));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo"));
}

// ── Task 1.8: undo round-trip ─────────────────────────────────────────────────

void TstD4ApplyFlatEdit::undo_restoresPreEditState_acrossSplitAndMerge()
{
    // "foo\n\nbar" → block0="foo" (3 bytes, B1 content-only), block1="bar" (3 bytes)
    // B1 flat view: "foobar" (6 bytes)
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"));
    const QByteArray before = fullText(doc);

    const auto blocksInit = doc.iterateBlocks();
    const uint32_t b0sz = static_cast<uint32_t>(doc.blockText(blocksInit[0]).size());
    // b0sz=3. Insert "\n\nzzz" at position b0sz-1=2 (within block0, before its last char).
    // Splits block0 at pos 2: block0="fo", newblock="zzzo" (tail "o" appended), block1="bar".
    doc.applyFlatEdit(b0sz - 1, b0sz - 1, QByteArray("\n\nzzz"), Origin::UserEdit);
    // Delete [b0sz-1, b0sz+4) = [2, 7) to collapse the inserted block back.
    // Removes tail of block0 (0 bytes at pos 2) + all of newblock (4 bytes "zzzo")
    // + first byte of bar; stitches remainder back. Two-step round-trip.
    doc.applyFlatEdit(b0sz - 1, b0sz + 4, QByteArray(), Origin::UserEdit);

    doc.undoD2();
    doc.undoD2();

    QCOMPARE(fullText(doc), before);
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
}

// ── Task 1.9: round-trip parity ──────────────────────────────────────────────

void TstD4ApplyFlatEdit::roundTrip_arbitraryEditMatchesFlatStringReplace()
{
    // Note on flat view (B1): fullText() = concatenation of block buffers.
    // Block buffers are content-only — the '\n\n' inter-block separator is NOT
    // stored in any buffer.
    // For a single block "abc", flat view = "abc" (3 bytes).
    // For "abc\n\ndef" load: block0="abc" (3 bytes), block1="def" (3 bytes).
    //   flat view = "abcdef" (6 bytes, NOT "abc\ndef" or "abc\n\ndef").
    // Inserting "\n\n" at position 1 in "abc": → block0="a", block1="bc".
    //   flat view = "abc" (3 bytes, not "a\nbc").
    // Replacing flat[3..5) in "abcdef" (= "de") with "Q" → cross-block edit:
    //   removes bytes 3-4 ("de") spanning boundary of block0/block1,
    //   inserts "Q" → block0="abcQ", block1="f". fullText="abcQf".

    struct Case {
        const char *label;
        QByteArray initial;
        uint32_t s, e;
        QByteArray ins;
        QByteArray expected;  // expected fullText() after edit
    };
    const Case cases[] = {
        // Replace entire block content
        {"replace-all",    "abc",        0, 3, "xyz",        "xyz"},
        // Delete entire flat content across 2 blocks: flat="abcdef" (6 bytes), [0,6)→""
        {"delete-all",     "abc\n\ndef",  0, 6, "",           ""},
        // Insert \n\n at mid-single-block: flat "abc" [1,1)→"\n\n"
        //   result: block0="a", block1="bc", fullText="abc"
        {"split-mid",      "abc",        1, 1, "\n\n",       "abc"},
        // Cross-block replace: "abc\n\ndef" → flat="abcdef" (6 bytes).
        //   flat[3..5) = "de". Replacing with "Q" → cross-block edit:
        //   removes tail of block0 (0 bytes at end) and first 2 bytes of block1 ("de"),
        //   inserts "Q" → block0="abcQ", block1="f". fullText="abcQf".
        {"cross-replace",  "abc\n\ndef",  3, 5, "Q",          "abcQf"},
    };
    for (const auto &c : cases) {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(c.initial);
        doc.applyFlatEdit(c.s, c.e, c.ins, Origin::UserEdit);
        const QByteArray got = fullText(doc);
        if (got != c.expected) {
            qDebug() << "FAIL case" << c.label
                     << "initial=" << c.initial
                     << "s=" << c.s << "e=" << c.e
                     << "ins=" << c.ins
                     << "expected=" << c.expected
                     << "got=" << got;
        }
        QCOMPARE(got, c.expected);
    }
}

QTEST_GUILESS_MAIN(TstD4ApplyFlatEdit)
#include "tst_d4_apply_flat_edit.moc"
