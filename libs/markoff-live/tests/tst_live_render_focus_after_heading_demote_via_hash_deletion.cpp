// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <QtTest/QtTest>

namespace Markoff::Live::Test {

class TestFocusAfterHeadingDemote : public QObject {
    Q_OBJECT
private slots:
    void user_can_type_immediately_after_demoting_heading();
};

void TestFocusAfterHeadingDemote::user_can_type_immediately_after_demoting_heading() {
    QmlIntegrationFixture fx("# Heading\n", 1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));
    fx.placeCursorAtPos(0, 1);  // between '#' and ' '

    LiveRealisticInputHarness h(fx.window());
    h.keyClick(Qt::Key_Backspace);  // deletes '#' → kind transitions to paragraph
    QTest::qWait(100);
    h.typeChar(QChar('x'));
    QTest::qWait(50);

    // Bug B (focus loss) only manifests when a CodeBlock is present in the
    // document (Bug C, Qt.callLater race). Captured in
    // tst_live_render_focus_chokepoint_invariant click_to_focus_* slots.
    QCOMPARE(fx.documentText(), QStringLiteral("x Heading\n"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestFocusAfterHeadingDemote)
#include "tst_live_render_focus_after_heading_demote_via_hash_deletion.moc"
