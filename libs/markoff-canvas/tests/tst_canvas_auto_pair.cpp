// SPDX-License-Identifier: GPL-3.0-or-later
//
// P7.2c (F1 #4) — auto-pairing / wrap-selection. CodeMirror reference:
// autocomplete/src/closebrackets.ts (insertBracket/deleteBracketPair/
// closeBracketsKeymap). Exactly the 5 pairs the plan names: (/), [/],
// "/", `/`, and the Markdown-specific bold wrap **/**.
//
// Falsification grouping (session protocol, logged in the plan's P7.2c
// findings entry): wrap-selection and auto-pair+type-through share one
// falsification pair (both live entirely inside tryAutoPairOrWrap() /
// performAutoPair() / wrapSelectionInPair(), and a single planted break —
// disabling the whole intercept — kills both at once, so a second break
// would just be exercising the same code path twice). Backspace-deletes-
// both gets its OWN falsification pair since it depends on the separate,
// trickier m_autoPairedClose state-tracking mechanism (tryDeleteFreshPair)
// rather than the insertion path.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::View;

class TstCanvasAutoPair : public QObject {
    Q_OBJECT

private slots:
    void auto_pair_bracket_quote_backtick();
    void type_through_over_auto_inserted_closer();
    void wrap_selection_in_pair();
    void bold_two_star_completion_and_type_through();
    void backspace_deletes_fresh_pair_only();
    void manually_typed_pair_does_not_type_through_or_delete_as_unit();
};

void TstCanvasAutoPair::auto_pair_bracket_quote_backtick()
{
    // Seeded with a single inert "z" prefix — loadFromMarkdown("") /
    // ("\n") produce a zero-block document (tst_d2_load's own precedent),
    // so every test in this file needs at least one real character to get
    // a block to place the caret in. Caret starts right after "z"; every
    // assertion below carries that prefix.
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    view.setCaretPosition(blocks[0], 1);

    QTest::keyClick(&view, Qt::Key_ParenLeft);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z()"));
    QCOMPARE(view.caretByteOffset(), 2);

    QTest::keyClick(&view, Qt::Key_BracketLeft);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z([])"));
    QCOMPARE(view.caretByteOffset(), 3);

    QTest::keyClick(&view, Qt::Key_QuoteDbl);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z([\"\"])"));
    QCOMPARE(view.caretByteOffset(), 4);

    QTest::keyClick(&view, Qt::Key_QuoteLeft);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z([\"``\"])"));
    QCOMPARE(view.caretByteOffset(), 5);
}

void TstCanvasAutoPair::type_through_over_auto_inserted_closer()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 1);

    QTest::keyClick(&view, Qt::Key_ParenLeft);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z()"));
    QCOMPARE(view.caretByteOffset(), 2);

    // Typing the matching closer types through: caret moves past it, no
    // second ')' inserted.
    QTest::keyClick(&view, Qt::Key_ParenRight);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z()"));
    QCOMPARE(view.caretByteOffset(), 3);

    // Symmetric marker (quote) types through the same way.
    view.setCaretPosition(blocks[0], 1);
    QTest::keyClick(&view, Qt::Key_QuoteDbl);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z\"\"()"));
    QCOMPARE(view.caretByteOffset(), 2);
    QTest::keyClick(&view, Qt::Key_QuoteDbl);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z\"\"()"));
    QCOMPARE(view.caretByteOffset(), 3);
}

void TstCanvasAutoPair::wrap_selection_in_pair()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello world\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();

    // Select "hello" (bytes 0..5).
    view.setCaretPosition(blocks[0], 0);
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks[0]);
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), blocks[0]);
    QCOMPARE(view.caretByteOffset(), 5);

    QTest::keyClick(&view, Qt::Key_QuoteDbl);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("\"hello\" world"));
    // CodeMirror behavior: the wrapped text stays selected.
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorByteOffset(), 1);
    QCOMPARE(view.caretByteOffset(), 6);
}

void TstCanvasAutoPair::bold_two_star_completion_and_type_through()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 1);

    // A single '*' is plain text (italics stays untouched).
    QTest::keyClick(&view, Qt::Key_Asterisk);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z*"));
    QCOMPARE(view.caretByteOffset(), 2);

    // A second, consecutive '*' completes the bold marker.
    QTest::keyClick(&view, Qt::Key_Asterisk);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z****"));
    QCOMPARE(view.caretByteOffset(), 3);

    // Typing '*' again types through the whole 2-byte close unit.
    QTest::keyClick(&view, Qt::Key_Asterisk);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z****"));
    QCOMPARE(view.caretByteOffset(), 5);
}

void TstCanvasAutoPair::backspace_deletes_fresh_pair_only()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 1);

    QTest::keyClick(&view, Qt::Key_ParenLeft);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z()"));
    QCOMPARE(view.caretByteOffset(), 2);

    // Immediately-following Backspace deletes BOTH characters as one op.
    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z"));
    QCOMPARE(view.caretByteOffset(), 1);

    // Same for the bold pair (2-byte open/close).
    QTest::keyClick(&view, Qt::Key_Asterisk);
    QTest::keyClick(&view, Qt::Key_Asterisk);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z****"));
    QCOMPARE(view.caretByteOffset(), 3);
    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("z"));
    QCOMPARE(view.caretByteOffset(), 1);
}

void TstCanvasAutoPair::manually_typed_pair_does_not_type_through_or_delete_as_unit()
{
    // "()" typed as two separate literal keystrokes with no auto-pair
    // machinery involved (constructed directly in the buffer, exactly the
    // way a paste or a remote edit would land it) must NOT type-through or
    // Backspace-delete-as-a-unit: only a JUST auto-inserted pair gets that
    // treatment (point 3's own wording).
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("()\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();

    // Caret between the pre-existing '(' and ')' — never auto-inserted by
    // this view, so m_autoPairedClose was never armed for it.
    view.setCaretPosition(blocks[0], 1);

    // Typing ')' here does NOT type through — it inserts a second ')'.
    QTest::keyClick(&view, Qt::Key_ParenRight);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("())"));
    QCOMPARE(view.caretByteOffset(), 2);

    // Fresh doc/view: check Backspace deletes only the '(' before the
    // caret, not both characters, for a manually-constructed (not
    // auto-inserted) pair.
    Markoff::MarkoffDocument doc2;
    doc2.loadFromMarkdown("()\n");
    View view2;
    view2.resize(400, 300);
    view2.setDocument(&doc2);
    view2.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view2));
    const auto blocks2 = doc2.iterateBlocks();
    view2.setCaretPosition(blocks2[0], 1);
    QTest::keyClick(&view2, Qt::Key_Backspace);
    QCOMPARE(doc2.blockText(blocks2[0]), QByteArray(")"));
    QCOMPARE(view2.caretByteOffset(), 0);
}

QTEST_MAIN(TstCanvasAutoPair)
#include "tst_canvas_auto_pair.moc"
