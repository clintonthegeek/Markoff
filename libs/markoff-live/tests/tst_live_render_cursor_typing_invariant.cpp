// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QQuickItem>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

// Per-keystroke invariant: after every TextEdit cursorPositionChanged
// emission, LiveCursorState.focusedQtPos == focused TextEdit's
// cursorPosition. Spec §5.3.
//
// Test document layout (1 row):
//   Row 0: paragraph "" (empty — ready for typing)
class TestCursorTypingInvariant : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void cursor_mirrors_textedit_through_ascii_typing();
    void cursor_mirrors_textedit_through_cjk_typing();
    void cursor_mirrors_textedit_through_emoji_typing();
    void cursor_mirrors_textedit_through_arrow_within_block();
    void cursor_mirrors_textedit_through_kind_transition();

private:
    void assertMirrorMatches(const QString &scenario);
    std::unique_ptr<QmlIntegrationFixture> m_fixture;
};

void TestCursorTypingInvariant::init() {
    // One empty paragraph; cursor lands in it at qtPos=0.
    // Empty document creates one empty text block via the parser.
    m_fixture = std::make_unique<QmlIntegrationFixture>(QByteArray(" "), 1);
    QVERIFY(m_fixture->waitForDelegateAt(0, 2000));
    m_fixture->placeCursorAtPos(0, 0);
}

void TestCursorTypingInvariant::cleanup() {
    m_fixture.reset();
}

// Asserts the per-keystroke invariant. Reads both the canonical mirror
// (m_cursor.focusedQtPos) and the production-truth (delegate's
// TextEdit cursorPosition) and compares.
void TestCursorTypingInvariant::assertMirrorMatches(const QString &scenario) {
    const int focusedRow      = m_fixture->cursorStateCurrentRow();
    QVERIFY2(focusedRow >= 0,
             qPrintable(QString("no focused row after %1").arg(scenario)));
    const int delegateQtPos = m_fixture->delegateCursorPos(focusedRow);
    const int mirrorQtPos   = m_fixture->cursorStateCurrentQtPos();
    QCOMPARE(mirrorQtPos, delegateQtPos);
}

// Slot bodies — added in Tasks 6–10.
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_ascii_typing() {
    QSKIP("Implemented in Task 6");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_cjk_typing() {
    QSKIP("Implemented in Task 7");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_emoji_typing() {
    QSKIP("Implemented in Task 8");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_arrow_within_block() {
    QSKIP("Implemented in Task 9");
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_kind_transition() {
    QSKIP("Implemented in Task 10");
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCursorTypingInvariant)
#include "tst_live_render_cursor_typing_invariant.moc"
