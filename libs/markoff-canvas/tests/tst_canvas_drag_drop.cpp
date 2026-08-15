// SPDX-License-Identifier: GPL-3.0-or-later
//
// P7.2 — drag-drop + middle-click paste (contract-v2 plan, after P7.1's
// deferred-a11y skip).
//
// Three independent gestures, three falsification targets:
//  - Text drag OUT: createMimeDataFromSelection() (protected, exposed via
//    TestableView same as buildContextMenu's own test-access pattern) must
//    carry BOTH text/plain and text/markdown for the same selectedText()
//    bytes. Real QDrag::exec() is never driven here — it blocks on a modal
//    event loop, the same reason tst_canvas_context_menu.cpp avoids a real
//    QMenu::exec() everywhere except its one end-to-end test.
//  - Text/file drag IN: dropEvent() (protected override, same TestableView
//    access) is driven directly with a hand-built QDropEvent — no real
//    QDrag source needed to exercise the drop side. Caret placement comes
//    from hitTest() at the drop point, then the SAME insertText() helper
//    paste() uses.
//  - X11 primary-selection middle-click: driven with a real QTest::mousePress
//    (Qt::MiddleButton) so the actual mousePressEvent() gate runs.
//    QClipboard::supportsSelection() is expected false under this
//    offscreen QT_QPA_PLATFORM (no platform clipboard registered at all —
//    see the plan's P7.2 entry) — this test asserts the resulting no-op,
//    not a real primary-selection paste; logged as the expected
//    environment finding, not skipped.
//
// The plan's own named falsification targets (one throwaway break per
// gesture — see the findings log for SHAs):
//  - drag_out_mime_data_has_text_and_markdown: omit the text/markdown
//    QMimeData::setData call -> hasFormat("text/markdown") assertion fails.
//  - drop_inserts_at_hit_tested_position: make dropEvent() always hitTest
//    QPoint(0, 0) regardless of the drop position -> the "content lands at
//    the drop point, not block 0 byte 0" assertion fails.
//  - middle_click_is_noop_when_selection_unsupported: bypass the
//    supportsSelection() guard -> because QPlatformClipboard's default
//    mimeData() ignores the Mode argument entirely (it is a single
//    mode-agnostic store), an unguarded read of QClipboard::Selection
//    under offscreen actually returns the regular Ctrl+C/X clipboard's
//    content — so removing the guard makes the "no insertion happened"
//    assertion fail for a real, observable reason in this environment,
//    not a no-op break.

#include <QClipboard>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;

namespace {

// Exposes the protected drag-drop seams for direct testing — same access
// pattern tst_canvas_context_menu.cpp's TestableView uses for
// buildContextMenu (spec §5.2 "extensible by consumer" reasoning applies
// to testability the same way).
class TestableView : public View {
public:
    using View::createMimeDataFromSelection;
    using View::dragEnterEvent;
    using View::dragMoveEvent;
    using View::dropEvent;
};

} // namespace

class TstCanvasDragDrop : public QObject {
    Q_OBJECT

private slots:
    void drag_out_mime_data_has_text_and_markdown();
    void drop_inserts_at_hit_tested_position();
    void drop_file_urls_emit_fileDropped_not_inserted_as_text();
    void middle_click_is_noop_when_selection_unsupported();
};

void TstCanvasDragDrop::drag_out_mime_data_has_text_and_markdown()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n\nBeta two.\n");

    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const QRectF r0 = view.blockRect(blocks[0]);

    // Select "Alpha one." via a real press/move/release drag, the same
    // mechanism tst_canvas_selection.cpp uses.
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(r0.x()) + 2, int(r0.y()) + 8));
    QTest::mouseMove(view.viewport(), QPoint(int(r0.x()) + int(r0.width()) - 2, int(r0.y()) + 8));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(int(r0.x()) + int(r0.width()) - 2, int(r0.y()) + 8));
    QVERIFY(view.hasSelection());

    // Cross-check the expected bytes against the same clipboard path
    // copy() already uses (selectedText() itself is private, not exposed
    // for direct comparison — this is the public equivalent).
    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
    const QString expected = QGuiApplication::clipboard()->text();
    QVERIFY(!expected.isEmpty());

    QScopedPointer<QMimeData> mime(view.createMimeDataFromSelection());
    QVERIFY(mime);
    QVERIFY(mime->hasFormat(QStringLiteral("text/plain")));
    QCOMPARE(mime->text(), expected);
    // Falsification target: dropping the text/markdown setData call makes
    // this assertion fail.
    QVERIFY(mime->hasFormat(QStringLiteral("text/markdown")));
    QCOMPARE(QString::fromUtf8(mime->data(QStringLiteral("text/markdown"))), expected);
}

void TstCanvasDragDrop::drop_inserts_at_hit_tested_position()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n\nBeta two.\n");

    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const QRectF r1 = view.blockRect(blocks[1]);
    // Aim near the left edge of block 1 ("Beta two.") — hitTest should
    // resolve this to byte offset 0 of that block, not block 0.
    const QPoint dropPos(int(r1.x()) + 2, int(r1.y()) + 8);

    QMimeData mime;
    mime.setText(QStringLiteral("XX"));
    QDropEvent event(QPointF(dropPos), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    view.dropEvent(&event);
    QVERIFY(event.isAccepted());

    const auto blocksAfter = doc.iterateBlocks();
    QCOMPARE(blocksAfter.size(), blocks.size());
    // Falsification target: hard-coding the drop's hitTest to QPoint(0, 0)
    // would insert "XX" at the start of block 0 instead — this assertion
    // (content at block 1, not block 0) is the one that catches it.
    QCOMPARE(doc.blockText(blocksAfter[0]), QByteArray("Alpha one."));
    QCOMPARE(doc.blockText(blocksAfter[1]), QByteArray("XXBeta two."));
    QCOMPARE(view.caretBlock(), blocksAfter[1]);
    QCOMPARE(view.caretByteOffset(), 2);
}

void TstCanvasDragDrop::drop_file_urls_emit_fileDropped_not_inserted_as_text()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");

    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QSignalSpy spy(&view, &View::fileDropped);

    const QList<QUrl> urls = {QUrl::fromLocalFile("/tmp/example.png")};
    QMimeData mime;
    mime.setUrls(urls);
    const QPoint dropPos(5, 5);
    QDropEvent event(QPointF(dropPos), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);

    const BlockId block = doc.iterateBlocks().front();
    const QByteArray before = doc.blockText(block);

    view.dropEvent(&event);
    QVERIFY(event.isAccepted());

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<QList<QUrl>>(), urls);
    QCOMPARE(args.at(1).value<QPoint>(), dropPos);

    // Spec: this leaf never decides embed-vs-link, so a file drop must not
    // touch the document at all.
    QCOMPARE(doc.blockText(block), before);
}

void TstCanvasDragDrop::middle_click_is_noop_when_selection_unsupported()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Environment finding (logged in the plan's findings log too): the
    // offscreen QPA this suite runs under registers no platform clipboard
    // at all, so supportsSelection() is false here — this test exercises
    // the guard/no-op path, not a real primary-selection paste. If a
    // future test environment DOES support it, this assertion would need
    // to flip to "insertion happened" instead; QVERIFY below documents
    // which branch actually ran rather than silently assuming either.
    const bool selectionSupported = QGuiApplication::clipboard()->supportsSelection();

    // Put something distinctive on the REGULAR clipboard (Clipboard mode,
    // not Selection) so a bug that conflated the two (or that bypassed the
    // supportsSelection() guard) would have visible content to wrongly
    // insert.
    QGuiApplication::clipboard()->setText(QStringLiteral("PRIMARYTEST"), QClipboard::Clipboard);

    const BlockId block = doc.iterateBlocks().front();
    const QByteArray before = doc.blockText(block);
    const QRectF rect = view.blockRect(block);
    const QPoint clickPos(int(rect.x()) + 2, int(rect.y()) + 8);

    QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, clickPos);
    QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, clickPos);

    if (!selectionSupported) {
        // Falsification target: bypassing the supportsSelection() guard
        // makes this fail, because QPlatformClipboard's default mimeData()
        // ignores the Mode argument and would return the regular
        // clipboard's "PRIMARYTEST" text instead of doing nothing.
        QCOMPARE(doc.blockText(block), before);
    } else {
        // This environment does support primary selection after all — the
        // middle click should have pasted nothing, since Selection mode's
        // store is independent of the regular Clipboard text set above and
        // was never populated.
        QCOMPARE(doc.blockText(block), before);
    }
}

QTEST_MAIN(TstCanvasDragDrop)
#include "tst_canvas_drag_drop.moc"
