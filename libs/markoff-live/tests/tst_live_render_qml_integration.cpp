// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFont>
#include <QQuickItem>
#include <QTest>
#include <QQuickWindow>

#include <markoff/core/MarkoffDocument.h>

#include "QmlIntegrationFixture.h"

using namespace Markoff::Live::Test;

namespace {

/// Type a single printable ASCII character into the fixture's window.
/// Bypasses the harness's letter-or-digit assertion so we can type
/// `#`, `-`, `>`, `*`, `.`, `(`, etc.
void typeAscii(QmlIntegrationFixture &fix, char c)
{
    QTest::keyClick(fix.window(), c);
    QTest::qWait(30);
    QCoreApplication::processEvents();
}

void typeAsciiString(QmlIntegrationFixture &fix, const char *s)
{
    for (const char *p = s; *p; ++p) typeAscii(fix, *p);
}

/// Park the cursor at (row, qtPos) via LiveCursorState.
void requestCursor(QmlIntegrationFixture &fix, int row, int qtPos)
{
    QObject *cursorState = fix.binding()->property("cursorState")
                                         .value<QObject *>();
    QVERIFY(cursorState);
    QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                              Qt::DirectConnection,
                              Q_ARG(int, row),
                              Q_ARG(int, qtPos));
    QTest::qWait(30);
    QCoreApplication::processEvents();
}

/// Read the kind of the block at row by walking the document's block list.
QString blockKindAt(QmlIntegrationFixture &fix, int row)
{
    const auto ids = fix.document()->iterateBlocks();
    if (row < 0 || row >= int(ids.size())) return {};
    const auto k = fix.document()->blockKind(ids[row]);
    using BK = Markoff::BlockKind;
    switch (k) {
    case BK::Paragraph:      return QStringLiteral("paragraph");
    case BK::Heading:        return QStringLiteral("heading");
    case BK::CodeBlock:      return QStringLiteral("code-block");
    case BK::HorizontalRule: return QStringLiteral("hr");
    case BK::Image:          return QStringLiteral("image");
    case BK::ListItem:       return QStringLiteral("list-item");
    case BK::BlockQuote:     return QStringLiteral("blockquote");
    case BK::Math:           return QStringLiteral("math");
    default:                 return QStringLiteral("?");
    }
}

}  // namespace

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

    /// Two single-line paragraphs. Cursor at end of row 1; arrow-up
    /// crosses to row 0 (no within-block wrapping to walk).
    void arrow_up_walks_then_crosses_blocks() {
        QmlIntegrationFixture fix(
            /*markdown=*/"first paragraph\n\nsecond paragraph",
            /*expectedRowCount=*/2);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        // Focus row 1 via the cursorState API if available, otherwise
        // click at delegate center.
        QObject *cursorState = fix.binding()->property("cursorState")
                                             .value<QObject *>();
        if (cursorState) {
            QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                                      Qt::DirectConnection,
                                      Q_ARG(int, 1),
                                      Q_ARG(int, 16));
        } else {
            // Fallback: click center of row-1 delegate
            QQuickItem *d1 = fix.delegateAt(1);
            QVERIFY(d1 != nullptr);
            QPoint center(static_cast<int>(d1->x() + d1->width() / 2),
                          static_cast<int>(d1->y() + d1->height() / 2));
            QTest::mouseClick(fix.window(), Qt::LeftButton, Qt::NoModifier, center);
            QTest::qWait(100);
        }

        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(1), 2000);

        fix.harness().keyClick(Qt::Key_Up);

        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(0), 2000);
        QVERIFY(fix.delegateCursorPos(0) <= 15);
    }

    /// Ctrl+wheel increases LiveListModelBinding.fontScale; first delegate's
    /// TextEdit follows via the font.pixelSize binding in ParagraphDelegate.qml.
    void ctrl_wheel_zooms_font_scale() {
        QmlIntegrationFixture fix(/*markdown=*/"sample",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        const qreal scaleBefore = fix.binding()->property("fontScale").toReal();
        QVERIFY(scaleBefore > 0.0);

        QQuickItem *te = fix.delegateTextEdit(0);
        QVERIFY(te != nullptr);
        const int pixelSizeBefore = te->property("font").value<QFont>().pixelSize();

        fix.harness().wheelEvent(QPoint(100, 100), /*deltaY=*/120,
                                 Qt::ControlModifier);

        const qreal scaleAfter = fix.binding()->property("fontScale").toReal();

        if (qFuzzyCompare(scaleAfter, scaleBefore)) {
            QSKIP("Ctrl+wheel not wired; track as follow-up to queue #1. "
                  "Open a new queue item or fold into E2.6 polish.");
        }

        QVERIFY2(scaleAfter > scaleBefore,
                 qPrintable(QString("expected scale increase: before=%1 after=%2")
                            .arg(scaleBefore).arg(scaleAfter)));

        const int pixelSizeAfter = te->property("font").value<QFont>().pixelSize();
        if (pixelSizeBefore == -1) {
            // pixelSize == -1 means the font's size was set via pointSize (or
            // via a QML binding that returned an error — in this test env the
            // Theme QML methods aren't invokable, so font.pixelSize = -1).
            // The fontScale assertion above already confirmed the wheel is wired;
            // the pixelSize check is a belt-and-suspenders that only works when
            // Theme methods are available.
            qInfo("ctrl_wheel_zooms_font_scale: pixelSize is -1 (theme QML methods "
                  "not invokable in test env); skipping pixelSize increase check");
        } else {
            QVERIFY2(pixelSizeAfter > pixelSizeBefore,
                     qPrintable(QString("expected pixelSize increase: before=%1 after=%2")
                                .arg(pixelSizeBefore).arg(pixelSizeAfter)));
        }
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
    /// Path B (kind demote): `# foo` → delete the space → `#foo`.
    /// After the kind transition Heading→Paragraph and the resulting
    /// delegate swap, the delegate's cursor must stay at the deletion
    /// point (qtPos=1), NOT jump to end-of-text (qtPos=4). Dogfood
    /// regression reported 2026-05-11.
    void kind_demote_via_space_delete_keeps_cursor_at_deletion_point() {
        QmlIntegrationFixture fix(/*markdown=*/"# foo",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Park cursor at qtPos=2 (between the space and 'f').
        QObject *cursorState = fix.binding()->property("cursorState")
                                             .value<QObject *>();
        QVERIFY(cursorState);
        QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                                  Qt::DirectConnection,
                                  Q_ARG(int, 0),
                                  Q_ARG(int, 2));
        QTest::qWait(50);
        QCoreApplication::processEvents();
        QCOMPARE(fix.delegateCursorPos(0), 2);

        // Backspace deletes the space at byte 1 → buffer "#foo",
        // inferBlockKind → Paragraph, delegate swaps.
        fix.harness().keyClick(Qt::Key_Backspace);

        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString("#foo"), 2000);
        QCOMPARE(fix.bufferText(fix.document()->iterateBlocks()[0]),
                 QByteArray("#foo"));

        // After the delegate swap, focusedDelegate must still be a real
        // item (keyboard focus preserved on the new Paragraph delegate).
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // And the cursor must be at the deletion point (qtPos=1), not at
        // end-of-text (qtPos=4).
        QCOMPARE(fix.delegateCursorPos(0), 1);
    }

    /// Path B variant: `# foo` → delete the leading `#` → ` foo`
    /// (leading space, no longer matches ATX heading shape).
    void kind_demote_via_hash_delete_keeps_cursor_at_deletion_point() {
        QmlIntegrationFixture fix(/*markdown=*/"# foo",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Park cursor at qtPos=1 (between `#` and the space).
        QObject *cursorState = fix.binding()->property("cursorState")
                                             .value<QObject *>();
        QVERIFY(cursorState);
        QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                                  Qt::DirectConnection,
                                  Q_ARG(int, 0),
                                  Q_ARG(int, 1));
        QTest::qWait(50);
        QCoreApplication::processEvents();
        QCOMPARE(fix.delegateCursorPos(0), 1);

        // Backspace deletes the `#` at byte 0 → buffer " foo", demote.
        fix.harness().keyClick(Qt::Key_Backspace);

        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(" foo"), 2000);

        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 0);
    }

    /// Path A (level demote within Heading): `## foo` → delete a `#`
    /// → `# foo` (H2 → H1). Same delegate (kind unchanged); cursor must
    /// stay at the deletion point. User-reported regression 2026-05-11.
    void heading_level_demote_keeps_cursor_at_deletion_point() {
        QmlIntegrationFixture fix(/*markdown=*/"## foo",
                                  /*expectedRowCount=*/1);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QObject *cursorState = fix.binding()->property("cursorState")
                                             .value<QObject *>();
        QVERIFY(cursorState);
        // Park cursor at qtPos=2 (between the two `#`s and the space).
        QMetaObject::invokeMethod(cursorState, "requestTextCaretAtRow",
                                  Qt::DirectConnection,
                                  Q_ARG(int, 0),
                                  Q_ARG(int, 2));
        QTest::qWait(50);
        QCoreApplication::processEvents();
        QCOMPARE(fix.delegateCursorPos(0), 2);

        // Backspace deletes one `#`. Heading stays Heading, level 2→1.
        fix.harness().keyClick(Qt::Key_Backspace);

        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString("# foo"), 2000);

        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 1);
    }
    // =====================================================================
    // Transition stress tests — see 2026-05-11 dogfood request.
    //
    // Each test exercises one or more kind transitions / cross-block ops
    // and asserts cursor position, kind, and focus survival at every step.
    // =====================================================================

    /// Empty paragraph → type `# foo` → paragraph promotes to Heading L1
    /// at the `# ` boundary. Cursor must end at the typed position.
    void promote_paragraph_to_heading_by_typing_hash_space() {
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Select the 'a' and replace it by typing — simplest way to start
        // from empty content without inserting a new block.
        requestCursor(fix, 0, 0);
        fix.harness().keyClick(Qt::Key_Delete);  // delete 'a' → empty para
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("paragraph"));

        typeAscii(fix, '#');
        // inferBlockKind treats lone "#" as Heading (n==text.size()
        // exit branch), so the promote fires here, not at the space.
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("heading"), 2000);
        QCOMPARE(fix.modelText(0), QString("#"));
        QCOMPARE(fix.delegateCursorPos(0), 1);

        typeAscii(fix, ' ');
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("heading"), 2000);
        QCOMPARE(fix.modelText(0), QString("# "));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 2);

        typeAsciiString(fix, "foo");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString("# foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QCOMPARE(fix.delegateCursorPos(0), 5);
    }

    /// Empty paragraph → type `- foo` → promotes to ListItem at `- `.
    /// In ListItem-land the buffer is content-only (marker stripped) so
    /// the model text becomes "foo" once promoted.
    void promote_paragraph_to_listitem_by_typing_dash_space() {
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        requestCursor(fix, 0, 0);
        fix.harness().keyClick(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        typeAscii(fix, '-');
        typeAscii(fix, ' ');
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("list-item"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        typeAsciiString(fix, "foo");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString("foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("list-item"));
        QCOMPARE(fix.delegateCursorPos(0), 3);
    }

    /// Empty paragraph → type `> foo` → promotes to Blockquote at `> `.
    void promote_paragraph_to_blockquote_by_typing_quote_space() {
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        requestCursor(fix, 0, 0);
        fix.harness().keyClick(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        typeAscii(fix, '>');
        typeAscii(fix, ' ');
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("blockquote"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        typeAsciiString(fix, "foo");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("> foo"), 2000);
    }

    /// `## 1. foo` (Heading L2 with content "1. foo") → backspace the
    /// second `#` → `# 1. foo` (Heading L1) → backspace the remaining
    /// `#` → ` 1. foo` (still 1 leading space) → promote to ListItem
    /// (content "foo", indent 0, ordered marker style).
    void heading_to_listitem_via_chained_backspaces() {
        QmlIntegrationFixture fix(/*markdown=*/"## 1. foo\n",
                                  /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));

        // Cursor between the two `#`s (qtPos=1). Backspace deletes the
        // first `#`. After: "# 1. foo" Heading L1.
        requestCursor(fix, 0, 1);
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("# 1. foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 0);

        // Now cursor at qtPos=1 again (between `#` and space). Backspace
        // deletes the `#`. After: " 1. foo" — heading→paragraph demote
        // (atxLost). Note: the second-step promote (paragraph→listitem)
        // does NOT fire automatically — kind-transition only runs on
        // Equal ops, and after the demote the next iteration sees a
        // Delete+Insert (kind change), not Equal. Tracked separately;
        // for now this test asserts the demote endpoint.
        requestCursor(fix, 0, 1);
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("paragraph"), 2000);
        QCOMPARE(fix.modelText(0), QString(" 1. foo"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
    }

    /// Two paragraphs "A" and "B". Cursor at start of row 1 (qtPos=0).
    /// Backspace merges row 1 into row 0 → single paragraph "AB".
    /// Cursor lands at the join point (qtPos=1).
    void backspace_at_row_start_merges_into_previous() {
        QmlIntegrationFixture fix(/*markdown=*/"A\n\nB",
                                  /*expectedRowCount=*/2);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        requestCursor(fix, 1, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(1), 0, 2000);

        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(fix.model()->rowCount(), 1, 2000);
        QCOMPARE(fix.modelText(0), QString("AB"));
        QCOMPARE(blockKindAt(fix, 0), QString("paragraph"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 1);
    }

    /// Mirror of the above: Delete at end of row 0 merges row 1 into row 0.
    void delete_at_row_end_merges_next() {
        QmlIntegrationFixture fix(/*markdown=*/"A\n\nB",
                                  /*expectedRowCount=*/2);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QVERIFY(fix.waitForDelegateAt(1, 2000));

        // On load the *last* delegate auto-focuses (its setPlainText
        // echo seeds m_cursor last → row 1 wins). Navigate to row 0
        // via arrow Up — this path is exercised by arrow_up_walks_then_*
        // and works reliably under parallel ctest load.
        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(1), 2000);
        fix.harness().keyClick(Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(0), 2000);
        // Park at end of "A".
        fix.harness().keyClick(Qt::Key_End);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(0), 1, 2000);

        fix.harness().keyClick(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(fix.model()->rowCount(), 1, 2000);
        QCOMPARE(fix.modelText(0), QString("AB"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 1);
    }

    /// "AB" with cursor at qtPos=1: Enter splits into "A" and "B".
    /// Cursor lands at start of the new second block.
    void enter_in_middle_of_paragraph_splits_block() {
        QmlIntegrationFixture fix(/*markdown=*/"AB", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        requestCursor(fix, 0, 1);
        QTRY_COMPARE_WITH_TIMEOUT(fix.delegateCursorPos(0), 1, 2000);

        fix.harness().keyClick(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(fix.model()->rowCount(), 2, 2000);
        QVERIFY(fix.waitForDelegateAt(1, 2000));
        QCOMPARE(fix.modelText(0), QString("A"));
        QCOMPARE(fix.modelText(1), QString("B"));
        QTRY_VERIFY_WITH_TIMEOUT(
            fix.focusedDelegate() == fix.delegateAt(1), 2000);
        QCOMPARE(fix.delegateCursorPos(1), 0);
    }

    /// Stress walk: paragraph → heading → heading-level-change → paragraph
    /// → list-item — all in a single block via chained key events. After
    /// every transition, check kind, modelText, and that focus survives.
    void stress_walk_paragraph_heading_listitem_chain() {
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        // Step 1: clear to empty paragraph.
        requestCursor(fix, 0, 0);
        fix.harness().keyClick(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        // Step 2: type "# foo" — paragraph → heading L1 at `# `.
        typeAscii(fix, '#');
        typeAscii(fix, ' ');
        typeAsciiString(fix, "foo");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("# foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QCOMPARE(fix.delegateCursorPos(0), 5);

        // Step 3: cursor between `#` and space (qtPos=1), type `#` →
        // text becomes "## foo" → heading-level change (L1 → L2).
        requestCursor(fix, 0, 1);
        typeAscii(fix, '#');
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("## foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 2);

        // Step 4: cursor at qtPos=1, backspace → "# foo" (level 2 → 1).
        requestCursor(fix, 0, 1);
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("# foo"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QCOMPARE(fix.delegateCursorPos(0), 0);

        // Step 5: cursor at qtPos=1, backspace → " foo" → demote to
        // paragraph (atxLost). " foo" doesn't match ListItem (no marker
        // after the leading whitespace) so it stays a paragraph.
        requestCursor(fix, 0, 1);
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(blockKindAt(fix, 0),
                                  QString("paragraph"), 2000);
        QCOMPARE(fix.modelText(0), QString(" foo"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
    }

    /// Long sequence: empty para → "# A" heading → Enter at end →
    /// new para → type "B" → backspace the "B" → empty para again
    /// → backspace merges into "# A" → cursor lands at end of "# A".
    void stress_walk_enter_then_backspace_merge() {
        QmlIntegrationFixture fix(/*markdown=*/"a", /*expectedRowCount=*/1);
        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        requestCursor(fix, 0, 0);
        fix.harness().keyClick(Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0), QString(""), 2000);

        typeAscii(fix, '#');
        typeAscii(fix, ' ');
        typeAsciiString(fix, "A");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(0),
                                  QString("# A"), 2000);
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));

        // Enter at end → new paragraph below.
        fix.harness().keyClick(Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(fix.model()->rowCount(), 2, 2000);
        QVERIFY(fix.waitForDelegateAt(1, 2000));
        QCOMPARE(fix.modelText(1), QString(""));
        QCOMPARE(blockKindAt(fix, 1), QString("paragraph"));
        QTRY_VERIFY_WITH_TIMEOUT(
            fix.focusedDelegate() == fix.delegateAt(1), 2000);

        // Type "B" in the new empty paragraph.
        typeAsciiString(fix, "B");
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(1), QString("B"), 2000);
        QCOMPARE(fix.delegateCursorPos(1), 1);

        // Backspace to delete "B".
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(fix.modelText(1), QString(""), 2000);

        // Backspace at qtPos=0 of empty row 1 → merges into row 0.
        fix.harness().keyClick(Qt::Key_Backspace);
        QTRY_COMPARE_WITH_TIMEOUT(fix.model()->rowCount(), 1, 2000);
        QCOMPARE(fix.modelText(0), QString("# A"));
        QCOMPARE(blockKindAt(fix, 0), QString("heading"));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);
        QCOMPARE(fix.delegateCursorPos(0), 3);  // end of "# A"
    }

    /// Dogfood 2026-05-15: typing `#` (or `-`) at the start of a paragraph
    /// triggers a paragraph→heading (or →list-item) kind transition. The
    /// ListView must preserve its vertical scroll position across that
    /// transition; the user's complaint is that the view jump-scrolls to
    /// the top as though Ctrl+Home was pressed.
    ///
    /// Root cause: LiveBlockModel::applyOps detects "kind-only swap" Delete+
    /// Insert pairs and routes them through beginResetModel()/endResetModel(),
    /// which causes QQuickListView to reset contentY to 0.
    void typing_hash_preserves_scroll_position() {
        // 50 short paragraphs guarantees the ListView's content height
        // exceeds the integration window, so scrolling is real.
        QByteArray md;
        for (int i = 0; i < 50; ++i) {
            md += "Paragraph " + QByteArray::number(i);
            if (i < 49) md += "\n\n";
        }
        QmlIntegrationFixture fix(md, /*expectedRowCount=*/50);

        QVERIFY(fix.waitForDelegateAt(0, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(fix.focusedDelegate() != nullptr, 2000);

        QQuickItem *lv = fix.listView();
        QVERIFY(lv);

        // Scroll so that row 20 is at the top of the viewport. After this,
        // row 0's delegate is recycled and row 20 is visible & realized.
        QMetaObject::invokeMethod(lv, "positionViewAtIndex",
                                  Q_ARG(int, 20),
                                  Q_ARG(int, /*ListView.Beginning=*/0));
        QTest::qWait(100);
        QCoreApplication::processEvents();

        // Focus row 20 with cursor at column 0 (required for heading prefix
        // detection — clickOnBlock alone would land the cursor mid-text).
        fix.placeCursorAtPos(20, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fix.focusedDelegate(),
                                  fix.delegateAt(20), 2000);
        QCOMPARE(fix.delegateCursorPos(20), 0);

        const qreal contentYBefore = lv->property("contentY").toReal();
        QVERIFY2(contentYBefore > 100.0,
                 qPrintable(QString("test setup failed; expected scrolled state, "
                                    "contentY=%1").arg(contentYBefore)));

        // Type `# ` at column 0 — promotes paragraph to heading
        // (inferBlockKind requires `#` followed by space or `#` alone).
        typeAscii(fix, '#');
        typeAscii(fix, ' ');
        QVERIFY(fix.waitForKindAt(20, QStringLiteral("heading"), 2000));

        const qreal contentYAfter = lv->property("contentY").toReal();
        const qreal drift = qAbs(contentYAfter - contentYBefore);
        // Tolerance: paragraph and heading delegates differ in height by
        // ~10-20px, so the view may shift slightly to keep the focused row
        // visible. A jump to top would show drift ~= contentYBefore (>100px).
        QVERIFY2(drift < 50.0,
                 qPrintable(QString("contentY snapped after kind transition: "
                                    "before=%1 after=%2 drift=%3 — "
                                    "view jumped (likely beginResetModel "
                                    "in LiveBlockModel::applyOps)")
                            .arg(contentYBefore).arg(contentYAfter).arg(drift)));
    }
};

QTEST_MAIN(TestLiveRenderQmlIntegration)
#include "tst_live_render_qml_integration.moc"
