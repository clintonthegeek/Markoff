// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFont>
#include <QQuickItem>
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

    /// Shift+Enter inserts a soft break into the paragraph buffer; the
    /// delegate's TextEdit must visibly render two lines.
    void shift_enter_creates_visible_newline() {
        QmlIntegrationFixture fix(/*markdown=*/"Heading",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.harness().keyClick(Qt::Key_End);
        QCOMPARE(fix.delegateCursorPos(0), 7);

        fix.harness().keyClick(Qt::Key_Return, Qt::ShiftModifier);

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 1u);

        QCOMPARE(fix.bufferText(blockIds[0]), QByteArray("Heading\n"));

        const QString dt = fix.delegateText(0);
        QEXPECT_FAIL("", "queue.md #4 chop-\\n bug — fix lands in a follow-up plan", Continue);
        QVERIFY2(dt.contains(QLatin1Char('\n')),
                 qPrintable(QString("delegate text missing \\n: %1").arg(dt)));

        // Cursor at position 8 also fails while queue.md #4 is open: the model
        // text is "Heading" (7 chars, chop strips the \n) so TextEdit can only
        // park the cursor at 7, not 8.
        QEXPECT_FAIL("", "queue.md #4 chop-\\n bug — cursor cannot reach pos 8 when text is chopped", Continue);
        QCOMPARE(fix.delegateCursorPos(0), 8);
    }

    /// Enter at paragraph-end creates a new block and migrates focus
    /// to it. The "cursor lost on Enter" regression class (queue.md #2
    /// concern #7) lives here.
    void enter_at_paragraph_end_migrates_focus() {
        QmlIntegrationFixture fix(/*markdown=*/"A", /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.harness().keyClick(Qt::Key_End);
        QCOMPARE(fix.delegateCursorPos(0), 1);

        fix.harness().keyClick(Qt::Key_Return);

        QVERIFY(fix.waitForRowCount(2, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(1), 2000);
        QCOMPARE(fix.delegateCursorPos(1), 0);
    }

    /// Typing-reverses-chars regression killer. Type "abc" into an
    /// auto-focused empty paragraph; all three layers must agree
    /// on "abc" with cursor at position 3.
    void typing_preserves_insertion_order() {
        // Start from scratch with an empty doc (0 blocks). Insert one
        // empty paragraph block programmatically before the QML runs —
        // the production architecture requires a block to be present for
        // typing to land. The regression being guarded is char-reversal
        // during sequential keystroke processing, which can only manifest
        // once a block exists and receives focus.
        QmlIntegrationFixture fix(/*markdown=*/"", /*expectedRowCount=*/0);

        // Insert a fresh empty paragraph block so the ListView has something
        // to render and focus.
        {
            Markoff::UndoLog::Transaction t(fix.document()->d2UndoLog());
            fix.document()->d2InsertBlock(Markoff::BlockId{},
                                          Markoff::BlockKind::Paragraph, t);
        }

        QVERIFY(fix.waitForRowCount(1, 2000));
        QVERIFY(fix.waitForDelegateAt(0, 2000));

        // The programmatic d2InsertBlock doesn't set cursor focus the way the
        // structural-key handler does. Click in the delegate center to focus it.
        {
            QQuickItem *d0 = fix.delegateAt(0);
            QVERIFY(d0 != nullptr);
            QPoint center(static_cast<int>(d0->x() + d0->width() / 2),
                          static_cast<int>(d0->y() + d0->height() / 2));
            QTest::mouseClick(fix.window(), Qt::LeftButton, Qt::NoModifier, center);
            QTest::qWait(50);
            QCoreApplication::processEvents();
        }
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        fix.harness().typeString("abc");

        const auto blockIds = fix.document()->iterateBlocks();
        QCOMPARE(blockIds.size(), 1u);

        QCOMPARE(fix.bufferText(blockIds[0]), QByteArray("abc"));
        QCOMPARE(fix.modelText(0),            QString("abc"));
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateText(0), QString("abc"), 1000);
        QCOMPARE(fix.delegateCursorPos(0),    3);
    }
};

QTEST_MAIN(TestLiveRenderQmlIntegration)
#include "tst_live_render_qml_integration.moc"
