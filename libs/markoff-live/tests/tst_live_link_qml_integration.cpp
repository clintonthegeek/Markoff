// SPDX-License-Identifier: GPL-3.0-or-later
//
// D1: QML-integration test for Ctrl+click TapHandler wired in
// UnifiedInlineTextDelegate. Verifies that:
//   1. A Ctrl+click over a wikilink dispatches one activation to the
//      registered LinkService.
//   2. A plain click over the same wikilink does NOT dispatch.
//
// Per INVARIANTS.md #4 and #5 these tests exercise the production callsite
// (QML TapHandler → activateLinkAt Q_INVOKABLE) rather than calling
// activateLinkAt directly. The falsifiability proof (Step 6) confirms the
// test actually fails when the production path is broken.

#include "QmlIntegrationFixture.h"
#include "RecordingLinkService.h"

#include <markoff/live/LiveListModelBinding.h>

#include <QTest>
#include <QCoreApplication>

using namespace Markoff::Live::Test;

class TestLiveLinkQmlIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ctrl_click_on_wikilink_dispatches_activation();
    void plain_click_on_wikilink_does_not_activate();
};

// Helper: cast the fixture's QObject* binding to the concrete type.
static Markoff::Live::LiveListModelBinding *liveBinding(QmlIntegrationFixture &fx)
{
    return qobject_cast<Markoff::Live::LiveListModelBinding *>(fx.binding());
}

void TestLiveLinkQmlIntegration::ctrl_click_on_wikilink_dispatches_activation()
{
    // "See [[Page]] now." — one paragraph block (row count = 1).
    QmlIntegrationFixture fx("See [[Page]] now.", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    Markoff::Live::LiveListModelBinding *lb = liveBinding(fx);
    QVERIFY2(lb, "binding() is not a LiveListModelBinding");
    lb->setLinkService(&svc);

    const QPoint clickPt = fx.scenePointAtFirstWikilink();
    QVERIFY2(!clickPt.isNull(), "could not compute scene point for first wikilink");

    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::ControlModifier, clickPt);
    QTRY_COMPARE_WITH_TIMEOUT(svc.activations.size(), 1, 2000);
    QCOMPARE(svc.activations.first().page, QStringLiteral("Page"));
}

void TestLiveLinkQmlIntegration::plain_click_on_wikilink_does_not_activate()
{
    QmlIntegrationFixture fx("See [[Page]] now.", /*expectedRowCount=*/1);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    Markoff::Live::LiveListModelBinding *lb = liveBinding(fx);
    QVERIFY2(lb, "binding() is not a LiveListModelBinding");
    lb->setLinkService(&svc);

    const QPoint clickPt = fx.scenePointAtFirstWikilink();
    QVERIFY2(!clickPt.isNull(), "could not compute scene point for first wikilink");

    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::NoModifier, clickPt);
    QTest::qWait(100);
    QCoreApplication::processEvents();
    QCOMPARE(svc.activations.size(), 0);
}

QTEST_MAIN(TestLiveLinkQmlIntegration)
#include "tst_live_link_qml_integration.moc"
