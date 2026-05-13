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
    // Dogfood findings 2026-05-11 (D-fc-1 / D-fc-2).
    void nav_into_runtime_promoted_heading();
    void hr_promotion_lands_focus_on_text_block();

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

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusChokepointInvariant)
#include "tst_live_render_focus_chokepoint_invariant.moc"
