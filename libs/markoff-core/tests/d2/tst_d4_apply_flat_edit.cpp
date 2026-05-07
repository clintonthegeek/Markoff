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
    // Insert "\n\n" at offset 5 (end of block) → block0="hello\n", block1=""
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("hello"));
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));

    doc.applyFlatEdit(5, 5, QByteArray("\n\n"), Origin::UserEdit);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("hello\n"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray(""));
}

// ── Task 1.6: insert newline mid-block → split ───────────────────────────────

void TstD4ApplyFlatEdit::insertSingleNewlineMidBlock_splitsBlock()
{
    // "foobar" → block0="foobar" (6 bytes)
    // Insert "\n\n" at offset 3 → block0="foo\n", block1="bar"
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foobar"));

    doc.applyFlatEdit(3, 3, QByteArray("\n\n"), Origin::UserEdit);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo\n"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("bar"));
}

// ── Task 1.7: cross-block delete → merge ─────────────────────────────────────

void TstD4ApplyFlatEdit::deleteSpanningTwoBlocks_mergesIntoFirst()
{
    // "foo\n\nbar" → block0="foo\n" (4 bytes), block1="bar" (3 bytes)
    // Flat view: "foo\nbar" (7 bytes)
    // Delete [3, 7) = "\nbar" → result "foo" (one block)
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"));
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));

    const uint32_t b0sz = static_cast<uint32_t>(doc.blockText(blocks[0]).size());
    const uint32_t b1sz = static_cast<uint32_t>(doc.blockText(blocks[1]).size());
    // b0sz=4 ("foo\n"), b1sz=3 ("bar"). Delete [b0sz-1, b0sz+b1sz) = [3, 7)
    doc.applyFlatEdit(b0sz - 1, b0sz + b1sz, QByteArray(), Origin::UserEdit);

    blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(1));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("foo"));
}

// ── Task 1.8: undo round-trip ─────────────────────────────────────────────────

void TstD4ApplyFlatEdit::undo_restoresPreEditState_acrossSplitAndMerge()
{
    // "foo\n\nbar" → block0="foo\n" (4 bytes), block1="bar" (3 bytes)
    // Flat view: "foo\nbar" (7 bytes)
    MarkoffDocument doc(1);
    doc.loadFromMarkdown(QByteArray("foo\n\nbar"));
    const QByteArray before = fullText(doc);

    const auto blocksInit = doc.iterateBlocks();
    const uint32_t b0sz = static_cast<uint32_t>(doc.blockText(blocksInit[0]).size());
    // b0sz=4. Insert "\n\nzzz" at position 3 (before the trailing \n of block0)
    // flat[3]='\n' → inserting "\n\nzzz" at 3 splits: block0="foo\n", newblock="zzz\n", block1="bar"
    doc.applyFlatEdit(b0sz - 1, b0sz - 1, QByteArray("\n\nzzz"), Origin::UserEdit);
    // Now delete from [b0sz-1, b0sz+5) to merge back: removes "\nzzz\n" (the inserted block)
    // After first edit, b0sz-1=3, and "zzz\n" is 4 bytes in new block.
    // New flat view: "foo\nzzz\nbar". Delete [3, 8) removes "\nzzz\n".
    doc.applyFlatEdit(b0sz - 1, b0sz + 4, QByteArray(), Origin::UserEdit);

    doc.undoD2();
    doc.undoD2();

    QCOMPARE(fullText(doc), before);
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(2));
}

// ── Task 1.9: round-trip parity ──────────────────────────────────────────────

void TstD4ApplyFlatEdit::roundTrip_arbitraryEditMatchesFlatStringReplace()
{
    // Note on flat view: fullText() = concatenation of block buffers.
    // For a single block "abc", flat view = "abc" (3 bytes).
    // For "abc\n\ndef" load: block0="abc\n" (4 bytes), block1="def" (3 bytes).
    //   flat view = "abc\ndef" (7 bytes, NOT "abc\n\ndef").
    // Inserting "\n\n" at position 1 in "abc": → block0="a\n", block1="bc".
    //   flat view = "a\nbc" (4 bytes, not "a\n\nbc").
    // Replacing flat[3..5) in "abc\ndef" (= "\nd") with "Q": → "abc" + "Q" + "ef" = "abcQef".

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
        // Delete entire flat content across 2 blocks: flat="abc\ndef" (7 bytes), [0,7)→""
        {"delete-all",     "abc\n\ndef",  0, 7, "",           ""},
        // Insert \n\n at mid-single-block: flat "abc" [1,1)→"\n\n"
        //   result: block0="a\n", block1="bc", fullText="a\nbc"
        {"split-mid",      "abc",        1, 1, "\n\n",       "a\nbc"},
        // Cross-block replace: "abc\n\ndef" → flat="abc\ndef" (7 bytes).
        //   flat[3..5) = "\nd". Replacing with "Q" → cross-block edit:
        //   removes tail of block0 ("\n") and first byte of block1 ("d"),
        //   inserts "Q" → block0="abcQ", block1="ef". fullText="abcQef".
        {"cross-replace",  "abc\n\ndef",  3, 5, "Q",          "abcQef"},
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
