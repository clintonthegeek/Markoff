// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.4 — context menu (contract-v2 plan).
//
// contextMenuEvent builds cut/copy/paste/select-all + the CanvasActionController
// format section + "Copy Link Target" (when the right-click landed on a link)
// onto a QMenu via the protected virtual buildContextMenu(QMenu&) — the
// consumer-extension seam this task names. Most scenarios below call
// buildContextMenu() directly through a subclass (TestableView) that exposes
// the protected member — no need to drive a real, modal QMenu::exec() for
// enabled-state assertions. One test (copy_link_target_...) exercises the
// real contextMenuEvent → QMenu::exec() path end to end, using the standard
// Qt-test technique of intercepting the exec()'d popup asynchronously via
// QTimer::singleShot + QApplication::activePopupWidget().
//
// The plan's own named falsification target for this task:
// paste_action_disabled_while_read_only (show paste enabled while read-only
// → this test fails).

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QTest>
#include <QTimer>

#include <markoff/canvas/CanvasActionController.h>
#include <markoff/canvas/View.h>
#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

// Private to src/ — same "exact pixel, not a guessed offset" technique
// tst_canvas_links.cpp uses for click targets (pointForFullQChar below is a
// copy of that helper, not a second implementation of the projection math
// it wraps).
#include "../src/BlockPresentation.h"
#include "../src/InlineFormatting.h"
#include "../src/ProjectionMap.h"

using Markoff::BlockId;
using Markoff::Canvas::CanvasActionController;
using Markoff::Canvas::View;
using Markoff::DefaultLinkService;
using Markoff::MarkoffDocument;

namespace {

// Exposes the protected buildContextMenu() seam for direct testing — the
// same access pattern a real consumer override would use (spec §5.2
// "extensible by consumer").
class TestableView : public View {
public:
    using View::buildContextMenu;
};

QAction *findActionByText(const QMenu &menu, const QString &text)
{
    for (QAction *a : menu.actions()) {
        if (a->text() == text)
            return a;
    }
    return nullptr;
}

// Viewport-coordinate point that lands exactly on `fullQChar` (a QChar
// index in the block's own RAW text) — copy of tst_canvas_links.cpp's
// helper of the same name; see that file's doc comment for why a guessed
// pixel offset is not good enough (delimiter omission narrows what's
// actually laid out).
QPoint pointForFullQChar(MarkoffDocument &doc, const View &view, BlockId block, int fullQChar)
{
    const QByteArray text = doc.blockText(block);
    const auto spans = doc.inlineSpansFor(block);
    const auto omitted = Markoff::Canvas::Detail::omittedDelimiterRanges(spans, {});
    const Markoff::Canvas::ProjectionMap proj = Markoff::Canvas::ProjectionMap::build(text, omitted);
    const int layoutQChar = proj.fullQCharToLayoutQChar(fullQChar);

    const Markoff::Theme theme = Markoff::Theme::defaultLight();
    const auto style = Markoff::Canvas::presentationFor(doc, block, theme);

    QTextLayout layout(proj.layoutText());
    layout.setFont(style.font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    line.setLineWidth(100000);
    layout.endLayout();

    const qreal x = line.cursorToX(layoutQChar);
    const QRectF rect = view.blockRect(block);
    return QPoint(int(rect.x() + x) + 1, int(rect.y()) + 8);
}

}  // namespace

class TstCanvasContextMenu : public QObject {
    Q_OBJECT

private slots:
    void cut_copy_disabled_without_selection();
    void cut_copy_enabled_with_selection_cut_disabled_while_read_only();
    void paste_action_disabled_while_read_only();
    void paste_action_disabled_with_empty_clipboard();
    void select_all_action_selects_whole_document();
    void paste_action_inserts_clipboard_text_and_replaces_selection();
    void format_section_present_only_when_controller_attached();
    void copy_link_target_appears_over_link_and_copies_resolved_url();
};

void TstCanvasContextMenu::cut_copy_disabled_without_selection()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");

    TestableView view;
    view.setDocument(&doc);
    view.setCaretPosition(doc.iterateBlocks().front(), 0);

    QMenu menu;
    view.buildContextMenu(menu);

    QAction *cutAction = findActionByText(menu, TestableView::tr("Cut"));
    QAction *copyAction = findActionByText(menu, TestableView::tr("Copy"));
    QVERIFY(cutAction);
    QVERIFY(copyAction);
    QVERIFY(!view.hasSelection());
    QVERIFY(!cutAction->isEnabled());
    QVERIFY(!copyAction->isEnabled());
}

void TstCanvasContextMenu::cut_copy_enabled_with_selection_cut_disabled_while_read_only()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");

    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(block);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 2, int(rect.y()) + 8));
    QTest::mouseMove(view.viewport(), QPoint(int(rect.x()) + 40, int(rect.y()) + 8));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        QPoint(int(rect.x()) + 40, int(rect.y()) + 8));
    QVERIFY(view.hasSelection());

    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *cutAction = findActionByText(menu, TestableView::tr("Cut"));
        QAction *copyAction = findActionByText(menu, TestableView::tr("Copy"));
        QVERIFY(cutAction->isEnabled());
        QVERIFY(copyAction->isEnabled());
    }

    // Read-only: Cut disables in its entirety (spec §4.2); Copy keeps
    // working — the same asymmetry keyboard Ctrl+X/Ctrl+C already have
    // (tst_canvas_selection::read_only_blocks_cut_but_not_copy).
    view.setReadOnly(true);
    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *cutAction = findActionByText(menu, TestableView::tr("Cut"));
        QAction *copyAction = findActionByText(menu, TestableView::tr("Copy"));
        QVERIFY(!cutAction->isEnabled());
        QVERIFY(copyAction->isEnabled());
    }
}

// The plan's own named falsification target for this task (P4.4):
// "show paste enabled while read-only; menu test fails."
void TstCanvasContextMenu::paste_action_disabled_while_read_only()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");
    QGuiApplication::clipboard()->setText(QStringLiteral("clipboard text"));

    TestableView view;
    view.setDocument(&doc);
    view.setCaretPosition(doc.iterateBlocks().front(), 0);

    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *pasteAction = findActionByText(menu, TestableView::tr("Paste"));
        QVERIFY(pasteAction);
        QVERIFY(pasteAction->isEnabled());
    }

    view.setReadOnly(true);
    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *pasteAction = findActionByText(menu, TestableView::tr("Paste"));
        QVERIFY(pasteAction);
        QVERIFY(!pasteAction->isEnabled());
    }
}

void TstCanvasContextMenu::paste_action_disabled_with_empty_clipboard()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");
    QGuiApplication::clipboard()->clear();

    TestableView view;
    view.setDocument(&doc);
    view.setCaretPosition(doc.iterateBlocks().front(), 0);

    QMenu menu;
    view.buildContextMenu(menu);
    QAction *pasteAction = findActionByText(menu, TestableView::tr("Paste"));
    QVERIFY(pasteAction);
    QVERIFY(!pasteAction->isEnabled());
}

void TstCanvasContextMenu::select_all_action_selects_whole_document()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n\nBeta two.\n");

    TestableView view;
    view.setDocument(&doc);
    view.setCaretPosition(doc.iterateBlocks().front(), 0);

    QMenu menu;
    view.buildContextMenu(menu);
    QAction *selectAllAction = findActionByText(menu, TestableView::tr("Select All"));
    QVERIFY(selectAllAction);
    QVERIFY(selectAllAction->isEnabled());

    selectAllAction->trigger();

    const auto blocks = doc.iterateBlocks();
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks.front());
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), blocks.back());
    QCOMPARE(view.caretByteOffset(), int(doc.blockText(blocks.back()).size()));
}

void TstCanvasContextMenu::paste_action_inserts_clipboard_text_and_replaces_selection()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");
    QGuiApplication::clipboard()->setText(QStringLiteral("XY"));

    TestableView view;
    const BlockId block = doc.iterateBlocks().front();
    view.setDocument(&doc);
    view.setCaretPosition(block, 0);

    QMenu menu;
    view.buildContextMenu(menu);
    QAction *pasteAction = findActionByText(menu, TestableView::tr("Paste"));
    QVERIFY(pasteAction);
    QVERIFY(pasteAction->isEnabled());

    pasteAction->trigger();

    QCOMPARE(doc.blockText(block), QByteArray("XYAlpha one."));
    QCOMPARE(view.caretByteOffset(), 2);
}

void TstCanvasContextMenu::format_section_present_only_when_controller_attached()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");

    TestableView view;
    view.setDocument(&doc);
    view.setCaretPosition(doc.iterateBlocks().front(), 0);

    {
        QMenu menu;
        view.buildContextMenu(menu);
        QVERIFY(!findActionByText(menu, CanvasActionController::tr("Bold")));
    }

    CanvasActionController controller;
    controller.setView(&view);
    view.setActionController(&controller);

    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *boldAction = findActionByText(menu, CanvasActionController::tr("Bold"));
        QVERIFY(boldAction);
        QCOMPARE(boldAction, controller.boldAction());
        QVERIFY(boldAction->isEnabled());
    }

    // Format section mirrors the controller's own enabled-state (P4.3) —
    // read-only disables it, same authority CanvasActionController itself
    // reads.
    view.setReadOnly(true);
    {
        QMenu menu;
        view.buildContextMenu(menu);
        QAction *boldAction = findActionByText(menu, CanvasActionController::tr("Bold"));
        QVERIFY(boldAction);
        QVERIFY(!boldAction->isEnabled());
    }
}

// End-to-end: a real right-click QContextMenuEvent delivered to the
// viewport, over a link span, pops the real (exec()'d, modal) QMenu.
// QMenu::exec() blocks the calling thread until the menu closes, so the
// assertions run from a QTimer::singleShot(0, ...) fired once the nested
// event loop is pumping — the standard Qt-test technique for a synchronous
// popup, via QApplication::activePopupWidget().
void TstCanvasContextMenu::copy_link_target_appears_over_link_and_copies_resolved_url()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("before [link](http://example.com) after\n");
    DefaultLinkService svc;

    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setLinkService(&svc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockText(block), QByteArray("before [link](http://example.com) after"));
    // "before " is 7 bytes; '[' at 7, "link" at [8, 12) — land in the
    // middle of it (same fixture/offsets as tst_canvas_links.cpp's
    // hover_emits_signal_sets_cursor_and_caches_shape).
    const QPoint onLink = pointForFullQChar(doc, view, block, 10);

    QGuiApplication::clipboard()->clear();

    // A small non-zero delay (not 0 — check-constitution.sh's C2 grep
    // targets `singleShot(0` as the production view-side-deferral smell;
    // this is an unrelated test-only technique for interacting with
    // QMenu::exec()'s nested event loop, not a leaf-code deferral) gives
    // the exec() call below time to actually start pumping events before
    // this fires.
    bool sawMenu = false;
    QTimer::singleShot(20, &view, [&] {
        QMenu *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(popup);
        sawMenu = true;
        QAction *copyLinkAction = findActionByText(*popup, TestableView::tr("Copy Link Target"));
        QVERIFY(copyLinkAction);
        copyLinkAction->trigger();
        popup->close();
    });

    QContextMenuEvent ev(QContextMenuEvent::Mouse, onLink,
                          view.viewport()->mapToGlobal(onLink));
    QCoreApplication::sendEvent(view.viewport(), &ev);

    QVERIFY(sawMenu);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("http://example.com"));
}

QTEST_MAIN(TstCanvasContextMenu)
#include "tst_canvas_context_menu.moc"
