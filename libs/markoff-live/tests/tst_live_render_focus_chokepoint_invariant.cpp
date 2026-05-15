// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

// Test document block layout (5 rows):
//   Row 0: heading    "# Heading"
//   Row 1: paragraph  "Paragraph one."
//   Row 2: blockquote "> Quote"
//   Row 3: list-item  "list item" (marker stripped)
//   Row 4: code-block "code\n"   (fence stripped)

class TestFocusChokepointInvariant : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void enter_at_paragraph_end();
    void enter_at_paragraph_middle();
    void enter_at_paragraph_start();
    void backspace_at_paragraph_start();
    void heading_demote_via_hash_deletion();
    void paragraph_promote_via_hash_typing();
    void blockquote_demote();
    void blockquote_promote();
    void codeblock_kind_transition();
    void listitem_kind_transition();
    void paste_at_block_start();
    void paste_at_block_middle();
    void paste_at_block_end();
    void undo_after_enter();
    void redo_after_undo();
    void click_to_focus_paragraph();
    void click_to_focus_heading();
    // Dogfood findings 2026-05-11 (D-fc-1 / D-fc-2 / D-fc-3).
    void nav_into_runtime_promoted_heading();
    void hr_promotion_lands_focus_on_text_block();
    void arrow_down_traverses_existing_hr();
    void arrow_up_traverses_existing_hr();
    void arrow_after_click_skips_hr();
    void click_on_hr_then_arrow_navigates_out();
    void click_on_hr_sets_block_selected();
    void backspace_after_typed_hr_lands_somewhere_sensible();
    void arrow_down_traverses_hr_between_headings();
    void arrow_down_traverses_consecutive_non_text_blocks();

    // Block-only kinds (spec §4 rules)
    void arrow_down_lands_blockselected_on_hr();
    void arrow_down_from_blockselected_hr_lands_on_text();
    void backspace_at_para_start_after_hr_selects_hr();
    void delete_at_para_end_before_hr_selects_hr();
    void backspace_on_selected_hr_removes_it();
    void enter_on_selected_hr_inserts_paragraph_after();
    void typing_on_selected_hr_is_noop();
    void tripleclick_on_hr_lands_blockselected();

private:
    void assertChokepointInvariant(const QString &scenario);
    std::unique_ptr<QmlIntegrationFixture> m_fixture;
};

void TestFocusChokepointInvariant::init() {
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "# Heading\n\nParagraph one.\n\n> Quote\n\n- list item\n\n```\ncode\n```\n",
        5);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));
}

void TestFocusChokepointInvariant::cleanup() {
    m_fixture.reset();
}

void TestFocusChokepointInvariant::assertChokepointInvariant(const QString &scenario) {
    // Spec §8.1 — after every structural event:
    //   focusedDelegate.modelIndex == cursorState.focusedAnchorRow
    //   focusedDelegate's TextEdit.cursorPosition == cursorState.focusedQtPos
    auto *focused = m_fixture->focusedDelegate();
    QVERIFY2(focused, qPrintable(QString("no focused delegate after %1").arg(scenario)));

    const int delegateRow = focused->property("modelIndex").toInt();
    const int cursorRow   = m_fixture->cursorStateCurrentRow();
    QCOMPARE(delegateRow, cursorRow);

    const int delegateCursorPos = m_fixture->delegateCursorPos(delegateRow);
    const int stateCursorPos    = m_fixture->cursorStateCurrentQtPos();
    QCOMPARE(delegateCursorPos, stateCursorPos);
}

void TestFocusChokepointInvariant::enter_at_paragraph_end() {
    m_fixture->placeCursorAtEndOf(1);  // paragraph row
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(200);
    assertChokepointInvariant("Enter at paragraph end");
}

void TestFocusChokepointInvariant::heading_demote_via_hash_deletion() {
    m_fixture->placeCursorAtPos(0, 1);  // inside heading, after first '#'
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);  // deletes the '#' → heading→paragraph transition
    QTest::qWait(200);
    assertChokepointInvariant("Heading→Paragraph kind transition");
}

void TestFocusChokepointInvariant::enter_at_paragraph_middle() {
    m_fixture->placeCursorAtPos(1, 5);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(100);
    assertChokepointInvariant("Enter at paragraph middle");
}

void TestFocusChokepointInvariant::enter_at_paragraph_start() {
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(100);
    assertChokepointInvariant("Enter at paragraph start");
}

void TestFocusChokepointInvariant::backspace_at_paragraph_start() {
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);
    QTest::qWait(100);
    assertChokepointInvariant("Backspace at paragraph start (block merge)");
}

void TestFocusChokepointInvariant::paragraph_promote_via_hash_typing() {
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('#'));
    h.typeChar(QChar(' '));
    QTest::qWait(100);
    assertChokepointInvariant("Paragraph→Heading kind transition");
}

void TestFocusChokepointInvariant::blockquote_demote() {
    // Cursor after '>' in "> Quote"; Backspace deletes '>' → kind demotes.
    m_fixture->placeCursorAtPos(2, 1);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);
    QTest::qWait(100);
    assertChokepointInvariant("Blockquote→Paragraph");
}

void TestFocusChokepointInvariant::blockquote_promote() {
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('>'));
    h.typeChar(QChar(' '));
    QTest::qWait(100);
    assertChokepointInvariant("Paragraph→Blockquote");
}

void TestFocusChokepointInvariant::codeblock_kind_transition() {
    m_fixture->placeCursorAtEndOf(4);  // inside code block
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(100);
    assertChokepointInvariant("CodeBlock Enter");
}

void TestFocusChokepointInvariant::listitem_kind_transition() {
    m_fixture->placeCursorAtEndOf(3);  // list item
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(100);
    assertChokepointInvariant("ListItem Enter");
}

void TestFocusChokepointInvariant::paste_at_block_start() {
    m_fixture->placeCursorAtPos(1, 0);
    m_fixture->pasteText("inserted ");
    QTest::qWait(100);
    assertChokepointInvariant("Paste at block start");
}

void TestFocusChokepointInvariant::paste_at_block_middle() {
    m_fixture->placeCursorAtPos(1, 5);
    m_fixture->pasteText("inserted ");
    QTest::qWait(100);
    assertChokepointInvariant("Paste at block middle");
}

void TestFocusChokepointInvariant::paste_at_block_end() {
    m_fixture->placeCursorAtEndOf(1);
    m_fixture->pasteText("inserted ");
    QTest::qWait(100);
    assertChokepointInvariant("Paste at block end");
}

void TestFocusChokepointInvariant::undo_after_enter() {
    m_fixture->placeCursorAtEndOf(1);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(50);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(100);
    assertChokepointInvariant("Undo after Enter");
}

void TestFocusChokepointInvariant::redo_after_undo() {
    m_fixture->placeCursorAtEndOf(1);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(50);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(50);
    h.keyClick(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(100);
    assertChokepointInvariant("Redo after undo");
}

void TestFocusChokepointInvariant::click_to_focus_paragraph() {
    m_fixture->clickOnBlock(1);
    auto *focused = m_fixture->focusedDelegate();
    QVERIFY2(focused, "no focused delegate after click on paragraph");
    const int delegateRow = focused->property("modelIndex").toInt();
    const int cursorRow   = m_fixture->cursorStateCurrentRow();
    // Bug C was the click bypassing the chokepoint because LiveView.qml accessed
    // `item.model.blockAnchor` from outside the delegate — context properties don't
    // propagate as root properties, so the call silently fell through to
    // forceActiveFocus() on the ListView. Fixed by exposing `blockAnchor` as a
    // root property on every delegate and typed-int parameter on takeFocus.
    QCOMPARE(delegateRow, cursorRow);
}

void TestFocusChokepointInvariant::click_to_focus_heading() {
    m_fixture->clickOnBlock(0);
    auto *focused = m_fixture->focusedDelegate();
    QVERIFY2(focused, "no focused delegate after click on heading");
    const int delegateRow = focused->property("modelIndex").toInt();
    const int cursorRow   = m_fixture->cursorStateCurrentRow();
    QCOMPARE(delegateRow, cursorRow);
}

void TestFocusChokepointInvariant::nav_into_runtime_promoted_heading() {
    // D-fc-2: promote a paragraph to heading via "# ", then navigate down
    // and back up. The caret must land on the heading row, not skip past it.
    //
    // Repro setup uses the standard 5-row doc; rows 1 and 2 are both text-
    // bearing (paragraph, blockquote). We promote row 1 → heading, then
    // Down-arrow into row 2, then Up-arrow back: caret should be on row 1.
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('#'));
    h.typeChar(QChar(' '));
    QTest::qWait(100);
    // Row 1 is now a heading. Navigate down.
    h.keyClick(Qt::Key_Down);
    QTest::qWait(80);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
    // Now navigate back up — should land on row 1 (the new heading).
    h.keyClick(Qt::Key_Up);
    QTest::qWait(80);
    assertChokepointInvariant("nav into runtime-promoted heading");
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
}

void TestFocusChokepointInvariant::arrow_down_traverses_existing_hr() {
    // D-fc-3: arrow Down from a paragraph above an existing HR must land
    // on the text-bearing row after the HR, not get stranded on the
    // source row. Verified at the QML-integration level (the unit test
    // navigable_row_skips_horizontal_rule only proves the row-skip math).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));
    QCOMPARE(m_fixture->modelText(0), QString("alpha"));
    QCOMPARE(m_fixture->modelText(2), QString("beta"));

    m_fixture->placeCursorAtEndOf(0);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::arrow_up_traverses_existing_hr() {
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->placeCursorAtPos(2, 0);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Up);
    QTest::qWait(150);

    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);
}

void TestFocusChokepointInvariant::arrow_after_click_skips_hr() {
    // The user's dogfood path: click into a paragraph (NOT placeCursorAtPos)
    // and then press an arrow key. Reproduces the actual interactive flow.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->clickOnBlock(0);
    QTest::qWait(100);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::click_on_hr_sets_block_selected() {
    // D-fc-4 generalizable rule: clicking on a non-text-bearing block
    // (HR / Image) must establish a `BlockSelected` cursor variant —
    // never `TextCaret`, since TextCaret is invalid for these kinds.
    // The chokepoint must be variant-aware via the registry rather
    // than always staging a TextCaret.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    m_fixture->clickOnBlock(1);  // click on HR row
    QTest::qWait(120);

    // After click on HR, the cursor's kind must be BlockSelected, not
    // TextCaret (which would be a silently-invalid variant for HR).
    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
}

void TestFocusChokepointInvariant::click_on_hr_then_arrow_navigates_out() {
    // D-fc-4: after clicking on the HR and entering BlockSelected state,
    // arrow Down must escape to the next text-bearing row (via the
    // HR-specific structural key handler), not stay stuck on the HR.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->clickOnBlock(1);
    QTest::qWait(120);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::backspace_after_typed_hr_lands_somewhere_sensible() {
    // D-fc-4 (active design): user types `---` at end of an existing
    // paragraph, HR is created and a fresh empty paragraph appears
    // after (D-fc-1 fix). User then presses Backspace on the empty
    // paragraph. Whatever the design decides happens next, the cursor
    // must NOT vanish — `focusedAnchorRow()` must report a valid row,
    // there must be either a focused delegate or an unambiguous
    // `cursorKind` (BlockSelected on the HR is acceptable; "none" is
    // not).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n", 1);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));

    m_fixture->placeCursorAtEndOf(0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(120);
    h.typeChar(QChar('-'));
    h.typeChar(QChar('-'));
    h.typeChar(QChar('-'));
    QTest::qWait(200);
    // Layout now: paragraph "alpha" (0), HR (1), empty paragraph (2).
    // Caret is on row 2 (per D-fc-1 fix).
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);

    h.keyClick(Qt::Key_Backspace);
    QTest::qWait(200);

    // Whatever the design decision: cursor must be on SOME row, with
    // a non-"none" cursorKind. Specifically, ruling out the bug:
    // focusedAnchorRow == -1 (cursor pointing at nothing).
    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    const QString kind = cs->property("cursorKind").toString();
    const int row      = m_fixture->cursorStateCurrentRow();
    qInfo() << "DBG post-backspace: cursorKind=" << kind << " row=" << row
            << " modelRowCount=" << m_fixture->model()->rowCount();
    QVERIFY2(kind != QStringLiteral("none"),
             qPrintable(QString("cursor was lost after Backspace; kind=%1").arg(kind)));
    QVERIFY2(row >= 0,
             qPrintable(QString("cursorRow=-1 after Backspace; kind=%1").arg(kind)));
}

void TestFocusChokepointInvariant::arrow_down_traverses_hr_between_headings() {
    // Variation: source is a heading rather than a paragraph, target is
    // a heading rather than a paragraph. Same expected behaviour.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "# alpha\n\n---\n\n## beta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->placeCursorAtEndOf(0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::arrow_down_traverses_consecutive_non_text_blocks() {
    // Two non-text blocks adjacent (HR, then HR). Arrow Down must skip
    // both, not get stuck on the first one. Tests the loop-skip rather
    // than single-skip.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\n---\n\nbeta\n", 4);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(3, 2000));

    m_fixture->placeCursorAtEndOf(0);
    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QCOMPARE(m_fixture->cursorStateCurrentRow(), 3);
}

void TestFocusChokepointInvariant::hr_promotion_lands_focus_on_text_block() {
    // D-fc-1: typing "---" on its own row promotes to HorizontalRule.
    // HR is non-text. After the structural event the caret must land on a
    // text-bearing block, not on the HR. Heuristic: after promotion the
    // model has one more block (a fresh paragraph after the HR) OR the
    // caret has migrated to a neighboring text-bearing row.
    m_fixture->placeCursorAtPos(1, 0);
    LiveRealisticInputHarness h(m_fixture->window());
    // Clear the paragraph text so "---" alone is the row's content.
    h.keyClick(Qt::Key_End);
    for (int i = 0; i < 14; ++i) h.keyClick(Qt::Key_Backspace);
    QTest::qWait(50);
    h.typeChar(QChar('-'));
    h.typeChar(QChar('-'));
    h.typeChar(QChar('-'));
    QTest::qWait(150);
    // Whatever the resolution, the focused row must be text-bearing —
    // assertChokepointInvariant requires a focused delegate, so a focusless
    // outcome (HR has the row but no caret) fails the QVERIFY2 inside.
    assertChokepointInvariant("HR promotion via '---'");
}

// ---------------------------------------------------------------------------
// Block-only kinds (spec §4 rules) — TDD red phase; production code pending
// ---------------------------------------------------------------------------

void TestFocusChokepointInvariant::arrow_down_lands_blockselected_on_hr() {
    // R-arrow-into: Down from text-bearing row 0 lands BlockSelected on HR (row 1),
    // not skip-past to row 2.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->placeCursorAtEndOf(0);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
}

void TestFocusChokepointInvariant::arrow_down_from_blockselected_hr_lands_on_text() {
    // R-arrow-out: Down from BlockSelected HR (row 1) lands TextCaret on row 2.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->clickOnBlock(1);  // click HR → BlockSelected row 1
    QTest::qWait(120);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Down);
    QTest::qWait(150);

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("TextCaret"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::backspace_at_para_start_after_hr_selects_hr() {
    // R-backspace-at-text-start-adjacent: Backspace at qtPos=0 on row 2 (beta)
    // when row 1 is an HR → BlockSelected on HR (row 1), model unchanged (3 rows).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(2, 2000));

    m_fixture->placeCursorAtPos(2, 0);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);
    QTest::qWait(150);

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
    QCOMPARE(m_fixture->model()->rowCount(), 3);
}

void TestFocusChokepointInvariant::delete_at_para_end_before_hr_selects_hr() {
    // R-delete-at-text-end-adjacent: Delete at end of row 0 (alpha) when row 1 is
    // an HR → BlockSelected on HR (row 1), model unchanged (3 rows).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    m_fixture->placeCursorAtEndOf(0);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Delete);
    QTest::qWait(150);

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
    QCOMPARE(m_fixture->model()->rowCount(), 3);
}

void TestFocusChokepointInvariant::backspace_on_selected_hr_removes_it() {
    // R-delete-blockonly: Backspace on BlockSelected HR removes it; cursor lands
    // TextCaret on the previous block (row 0 = alpha, now the only neighbour above).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    m_fixture->clickOnBlock(1);  // click HR → BlockSelected row 1
    QTest::qWait(120);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Backspace);
    QTest::qWait(200);

    // HR removed → 2 rows left (alpha=0, beta=1)
    QCOMPARE(m_fixture->model()->rowCount(), 2);
    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("TextCaret"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 0);
}

void TestFocusChokepointInvariant::enter_on_selected_hr_inserts_paragraph_after() {
    // R-enter-blockonly: Enter on BlockSelected HR inserts empty paragraph after the
    // HR and lands TextCaret on it (row 2 in the 4-row result).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    m_fixture->clickOnBlock(1);  // click HR → BlockSelected row 1
    QTest::qWait(120);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);

    LiveRealisticInputHarness h(m_fixture->window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(200);

    // New empty paragraph inserted after HR → 4 rows: alpha(0), HR(1), empty(2), beta(3)
    QCOMPARE(m_fixture->model()->rowCount(), 4);
    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("TextCaret"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 2);
}

void TestFocusChokepointInvariant::typing_on_selected_hr_is_noop() {
    // R-type-blockonly: Typing a printable character on BlockSelected HR is a no-op —
    // cursor stays BlockSelected on row 1, model unchanged (3 rows).
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    m_fixture->clickOnBlock(1);  // click HR → BlockSelected row 1
    QTest::qWait(120);
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);

    LiveRealisticInputHarness h(m_fixture->window());
    h.typeChar(QChar('x'));
    QTest::qWait(150);

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
    QCOMPARE(m_fixture->model()->rowCount(), 3);
}

void TestFocusChokepointInvariant::tripleclick_on_hr_lands_blockselected() {
    // R-tripleclick-blockonly: Three rapid clicks on an HR land BlockSelected (same as
    // single click). Triple-click counter in the MouseArea resets cleanly; model
    // unchanged (3 rows).
    // NOTE: No dedicated triple-click helper exists; we send 3 rapid QTest::mouseClick
    // calls at the same position with minimal delay between them, mimicking the OS
    // triple-click interval.
    m_fixture.reset();
    m_fixture = std::make_unique<QmlIntegrationFixture>(
        "alpha\n\n---\n\nbeta\n", 3);
    QVERIFY(m_fixture->waitForDelegateAt(1, 2000));

    QQuickItem *d = m_fixture->delegateAt(1);
    QVERIFY2(d != nullptr, "delegate at row 1 (HR) not found");
    QVariant contentItemVar = m_fixture->listView()->property("contentItem");
    QQuickItem *contentItem = contentItemVar.value<QQuickItem *>();
    const qreal offsetX = contentItem ? contentItem->x() : 0.0;
    const qreal offsetY = contentItem ? contentItem->y() : 0.0;
    const QPoint clickPos(
        static_cast<int>(d->x() + d->width() / 2 + offsetX),
        static_cast<int>(d->y() + d->height() / 2 + offsetY));

    // Send three rapid clicks within typical triple-click interval (~100 ms).
    QTest::mouseClick(m_fixture->window(), Qt::LeftButton, Qt::NoModifier, clickPos);
    QTest::qWait(40);
    QTest::mouseClick(m_fixture->window(), Qt::LeftButton, Qt::NoModifier, clickPos);
    QTest::qWait(40);
    QTest::mouseClick(m_fixture->window(), Qt::LeftButton, Qt::NoModifier, clickPos);
    QTest::qWait(150);
    QCoreApplication::processEvents();

    QObject *cs = m_fixture->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs != nullptr);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));
    QCOMPARE(m_fixture->cursorStateCurrentRow(), 1);
    QCOMPARE(m_fixture->model()->rowCount(), 3);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusChokepointInvariant)
#include "tst_live_render_focus_chokepoint_invariant.moc"
