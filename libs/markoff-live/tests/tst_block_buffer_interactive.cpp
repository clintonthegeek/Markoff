// SPDX-License-Identifier: GPL-3.0-or-later
//
// B1 buffer convention — interactive contract tests.
//
// These tests drive input through the realistic-input harness (production
// QML window, QTest key events) and assert that block buffers satisfy the
// B1 invariant end-to-end:
//
//   "A block buffer holds content. A '\n' in the buffer, when present,
//    is user-authored content (e.g. a soft break), never a structural
//    delimiter."
//
// Spec:  docs/specs/2026-05-18-b1-buffer-convention-design.md §5.2
// Plan:  docs/plans/2026-05-18-b1-buffer-convention.md Task 2.2

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

// ---------------------------------------------------------------------------
// Local helper: get block text for row `row` via document().iterateBlocks().
// Centralises the bounds-check so tests stay readable.
// ---------------------------------------------------------------------------
static QByteArray bufferAt(QmlIntegrationFixture &fx, int row)
{
    const auto ids = fx.document()->iterateBlocks();
    if (row < 0 || row >= static_cast<int>(ids.size()))
        return QByteArray("(row out of range)");
    return fx.bufferText(ids[row]);
}

// ---------------------------------------------------------------------------
// Helper: poll until bufferAt(row) == expected, or timeout.
// ---------------------------------------------------------------------------
static bool waitForBufferAt(QmlIntegrationFixture &fx, int row,
                             const QByteArray &expected, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (bufferAt(fx, row) == expected)
            return true;
        QTest::qWait(25);
        QCoreApplication::processEvents();
    }
    return bufferAt(fx, row) == expected;
}

class TestBlockBufferInteractive : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Slot 1: soft_break_and_split_preserves_content_newline
    //
    // A user-typed soft break ('\n' via Shift+Enter) survives a block split and
    // a subsequent merge as content, not as a structural delimiter.
    //
    // Spec §5.2, Test 3 (interactive):
    //   Row 0 starts as "Heading". After:
    //   - Shift+Enter at pos 7 → buffer "Heading\n"      (1 block, \n is content)
    //   - type '='              → buffer "Heading\n="    (1 block)
    //   - Enter at pos 8        → blocks "Heading\n" / "="  (2 blocks, \n stays in block 0)
    //   - Backspace at start of row 1 → merged buffer "Heading\n="  (1 block, \n survived)
    // =========================================================================
    void soft_break_and_split_preserves_content_newline()
    {
        // Step 1: one-block document.
        QmlIntegrationFixture fix(/*markdown=*/"Heading", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Step 2: confirm initial buffer state — no trailing \n after B1.
        QCOMPARE(bufferAt(fix, 0), QByteArray("Heading"));

        // Step 3: place cursor at pos 7 (end of "Heading") and press Shift+Enter.
        fix.placeCursorAtPos(0, 7);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(0), 7, 2000);

        fix.harness().keyClick(Qt::Key_Return, Qt::ShiftModifier);

        // Step 4: wait for the buffer to absorb the soft break.
        QVERIFY2(waitForBufferAt(fix, 0, QByteArray("Heading\n")),
                 qPrintable(QString("after Shift+Enter: expected buffer 'Heading\\n', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 0)))));

        // Step 5: type '='
        fix.harness().typeChar(QLatin1Char('='));

        // Step 6: wait for buffer to absorb '='.
        QVERIFY2(waitForBufferAt(fix, 0, QByteArray("Heading\n=")),
                 qPrintable(QString("after typing '=': expected buffer 'Heading\\n=', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 0)))));

        // Step 7: place cursor at pos 8 (between \n and =), press Enter to split.
        fix.placeCursorAtPos(0, 8);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(0), 8, 2000);

        fix.harness().keyClick(Qt::Key_Return);

        // Step 8: wait for 2 blocks.
        QVERIFY2(fix.waitForRowCount(2, 3000),
                 qPrintable(QString("Enter split did not produce 2 blocks; row count = %1")
                            .arg(fix.model()->rowCount())));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        // Step 9: block 0 still holds "Heading\n" — the user's soft break is content.
        QCOMPARE(bufferAt(fix, 0), QByteArray("Heading\n"));

        // Step 10: block 1 holds "=" — no trailing \n.
        QCOMPARE(bufferAt(fix, 1), QByteArray("="));

        // Step 11: merge — place cursor at start of row 1 and press Backspace.
        fix.placeCursorAtPos(1, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(1), 0, 2000);

        fix.harness().keyClick(Qt::Key_Backspace);

        // Step 12: wait for the merge (back to 1 block).
        QVERIFY2(fix.waitForRowCount(1, 3000),
                 qPrintable(QString("Backspace merge did not collapse to 1 block; row count = %1")
                            .arg(fix.model()->rowCount())));

        // Step 13: merged buffer must contain the user's '\n' — it is content,
        // not a delimiter that the merge command should strip.
        QVERIFY2(waitForBufferAt(fix, 0, QByteArray("Heading\n=")),
                 qPrintable(QString("after merge: expected 'Heading\\n=', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 0)))));
    }

    // =========================================================================
    // Slot 2: paste_multi_block_does_not_synthesize_terminator
    //
    // Inserting "a\n\nb\n\nc" (three blocks separated by blank lines) via the
    // flat-text edit path (`applyFlatEdit`) produces three blocks, none of
    // which end with '\n'.
    //
    // This slot exercises `applyFlatEdit`, which is the production path for
    // all flat-text pastes (`LiveClipboardController::paste` calls it for the
    // plain-text fallback). The Ctrl+V harness route is not used here because
    // the QML app's paste Shortcut wiring is tracked separately (see
    // LiveView.qml comment: "currently no-ops in the standalone test app").
    //
    // The test verifies that `applyFlatEdit`'s block-split decomposer honours
    // the B1 invariant: content buffers, no trailing terminators.
    //
    // Spec §5.2 comment: "verify none end with \n"
    // =========================================================================
    void paste_multi_block_does_not_synthesize_terminator()
    {
        // Step 1: one-block document containing "a" (so the block exists in the
        // CRDT and applyFlatEdit has a byte range to replace).
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Confirm initial state.
        QCOMPARE(bufferAt(fix, 0), QByteArray("a"));

        // Steps 2–3: apply the multi-paragraph text over the entire first block
        // via applyFlatEdit (flat-text entry point, B1-conformant after B1 landed).
        // The document currently has one block "a" at byte offset 0, length 1.
        fix.document()->applyFlatEdit(
            /*startByte=*/0, /*endByte=*/1,
            QByteArray("a\n\nb\n\nc"),
            Markoff::Origin::UserEdit);
        QTest::qWait(50);
        QCoreApplication::processEvents();

        // Step 4: wait for three blocks.
        QVERIFY2(fix.waitForRowCount(3, 3000),
                 qPrintable(QString("applyFlatEdit did not produce 3 blocks; row count = %1")
                            .arg(fix.model()->rowCount())));

        // Step 5–6: each block holds plain content without trailing '\n'.
        QVERIFY2(waitForBufferAt(fix, 0, QByteArray("a")),
                 qPrintable(QString("block 0: expected 'a', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 0)))));
        QVERIFY2(waitForBufferAt(fix, 1, QByteArray("b")),
                 qPrintable(QString("block 1: expected 'b', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 1)))));
        QVERIFY2(waitForBufferAt(fix, 2, QByteArray("c")),
                 qPrintable(QString("block 2: expected 'c', got '%1'")
                            .arg(QString::fromUtf8(bufferAt(fix, 2)))));

        // Belt-and-suspenders: none of the buffers end with '\n'.
        for (int row = 0; row < 3; ++row) {
            const QByteArray buf = bufferAt(fix, row);
            QVERIFY2(!buf.endsWith('\n'),
                     qPrintable(QString("block %1 has trailing \\n after applyFlatEdit: '%2'")
                                .arg(row)
                                .arg(QString::fromUtf8(buf))));
        }
    }
};

QTEST_MAIN(TestBlockBufferInteractive)
#include "tst_block_buffer_interactive.moc"
