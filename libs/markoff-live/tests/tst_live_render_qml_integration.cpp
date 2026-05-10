// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QQuickWindow>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

class TestLiveRenderQmlIntegration : public QObject {
    Q_OBJECT

private Q_SLOTS:

    /// Smoke: loads empty doc against production Main.qml, window exposes,
    /// model has zero rows (per tst_live_render_empty_doc_focus: empty markdown
    /// produces zero blocks; the host is responsible for handling the 0-row case).
    void loads_production_main_against_empty_doc() {
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/0);
        QVERIFY(fix.window() != nullptr);
        QVERIFY(fix.window()->isExposed() || fix.window()->isVisible());
        QVERIFY(fix.model() != nullptr);
        QCOMPARE(fix.model()->rowCount(), 0);
    }
};

QTEST_MAIN(TestLiveRenderQmlIntegration)
#include "tst_live_render_qml_integration.moc"
