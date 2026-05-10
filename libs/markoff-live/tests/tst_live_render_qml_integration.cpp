// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QQuickWindow>

#include <markoff/core/MarkoffDocument.h>

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

    /// Three-layer convention smoke: after load, all three layers agree on
    /// the empty-paragraph text. No edits driven; this guards the accessors.
    void three_layer_accessors_agree_after_load() {
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/0);

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 0u);

        // With 0 blocks there is nothing to assert on the three layers;
        // switch to a one-block doc to exercise the accessors.
        QmlIntegrationFixture fix2(/*markdown=*/"hello", /*expectedRowCount=*/1);

        const auto blockIds2 = fix2.document()->iterateBlocks();
        QCOMPARE(blockIds2.size(), 1u);

        QCOMPARE(fix2.bufferText(blockIds2[0]), QByteArray("hello"));
        QCOMPARE(fix2.modelText(0),             QString("hello"));
        // delegateText and delegateCursorPos need the delegate to be
        // realised — wait for it via the helper added in Task 5.
        // For now just verify buffer and model agree.
    }

    /// Wait helpers smoke: loading a two-block doc, both delegates
    /// become realised within timeout; focusedDelegate is non-null.
    void wait_helpers_resolve_two_block_doc() {
        QmlIntegrationFixture fix(/*markdown=*/"A\n\nB",
                                  /*expectedRowCount=*/2);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));
        // At least one delegate has focus (the production ListView focus
        // policy auto-focuses the first row on load).
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
    }
};

QTEST_MAIN(TestLiveRenderQmlIntegration)
#include "tst_live_render_qml_integration.moc"
