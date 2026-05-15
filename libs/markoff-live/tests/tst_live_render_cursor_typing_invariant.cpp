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
    LiveRealisticInputHarness h(m_fixture->window());

    const QString text = QStringLiteral("Hello12");
    for (int i = 0; i < text.size(); ++i) {
        h.typeChar(text.at(i));
        assertMirrorMatches(
            QString("ASCII typing after char #%1 (%2)").arg(i + 1).arg(text.at(i)));
    }
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_cjk_typing() {
    LiveRealisticInputHarness h(m_fixture->window());

    const QString text = QString::fromUtf8("これはテスト");
    for (int i = 0; i < text.size(); ++i) {
        h.typeUnicode(text.at(i));
        assertMirrorMatches(
            QString("CJK typing after char #%1").arg(i + 1));
    }
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_emoji_typing() {
    LiveRealisticInputHarness h(m_fixture->window());

    const QString text = QString::fromUtf8("🎉🚀✨");
    m_fixture->pasteText(text);
    h.idle(100);  // Wait for paste to settle
    assertMirrorMatches("emoji typing");

    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), text.size());
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_arrow_within_block() {
    LiveRealisticInputHarness h(m_fixture->window());

    h.typeString(QStringLiteral("abcdef"));
    assertMirrorMatches("after typing abcdef");

    for (int i = 0; i < 3; ++i) {
        h.keyClick(Qt::Key_Left);
        assertMirrorMatches(QString("Left #%1 within block").arg(i + 1));
    }
    QCOMPARE(m_fixture->cursorStateCurrentQtPos(), 3);
}
void TestCursorTypingInvariant::cursor_mirrors_textedit_through_kind_transition() {
    LiveRealisticInputHarness h(m_fixture->window());

    h.typeChar(QLatin1Char('#'));
    assertMirrorMatches("after typing '#'");

    h.keyClick(Qt::Key_Space);
    QVERIFY(m_fixture->waitForKindAt(0, QStringLiteral("heading"), 2000));

    assertMirrorMatches("after kind transition Paragraph → Heading");
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestCursorTypingInvariant)
#include "tst_live_render_cursor_typing_invariant.moc"
