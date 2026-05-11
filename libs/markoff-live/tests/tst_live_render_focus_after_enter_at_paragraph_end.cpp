// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestFocusAfterEnterAtParagraphEnd : public QObject {
    Q_OBJECT
private slots:
    void user_can_type_immediately_after_pressing_enter();
};

void TestFocusAfterEnterAtParagraphEnd::user_can_type_immediately_after_pressing_enter() {
    QmlIntegrationFixture fx("A paragraph.\n", 1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtEndOf(0);

    LiveRealisticInputHarness h(fx.window());
    h.keyClick(Qt::Key_Return);
    QTest::qWait(100);
    h.typeChar(QChar('x'));
    QTest::qWait(50);

    // The 'x' should land in the new (second) block. If focus was lost,
    // the keystroke is silently dropped and the document is unchanged.
    // Note: Bug A (focus loss) only manifests when a CodeBlock is present
    // in the document (Bug C, Qt.callLater race). Captured in
    // tst_live_render_focus_chokepoint_invariant click_to_focus_* slots.
    QCOMPARE(fx.documentText(), QStringLiteral("A paragraph.\nx\n"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusAfterEnterAtParagraphEnd)
#include "tst_live_render_focus_after_enter_at_paragraph_end.moc"
