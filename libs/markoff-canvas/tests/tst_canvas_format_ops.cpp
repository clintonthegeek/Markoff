// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.3 — format verbs over core FormatOps's per-block overloads, and
// CanvasActionController's enabled-state wiring.

#include <QTest>

#include <markoff/canvas/CanvasActionController.h>
#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::CanvasActionController;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;

class TstCanvasFormatOps : public QObject {
    Q_OBJECT

private slots:
    void toggleBold_no_selection_inserts_pair_and_parks_caret();
    void toggleBold_selection_wraps_and_selects_result();
    void toggleBold_multi_block_selection_wraps_each_block();
    void insertLink_selection_wraps_and_selects_url();
    void setHeadingLevel_sets_and_clears();
    void setHeadingLevel_noop_leaves_selection_intact();
    void read_only_blocks_format_ops();

    void actionController_enabled_states_track_readonly_and_document();
    void actionController_gating_is_the_falsification_target();
};

void TstCanvasFormatOps::toggleBold_no_selection_inserts_pair_and_parks_caret()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello");
    View view;
    view.setDocument(&doc);

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 5);
    view.toggleBold();

    QCOMPARE(doc.blockText(block), QByteArray("hello****"));
    QVERIFY(!view.hasSelection());
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 7);  // parked between the ** pair
}

void TstCanvasFormatOps::toggleBold_selection_wraps_and_selects_result()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello world");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 6);   // caret before "world"
    // Build a selection [6, 11) via setCaretPosition (anchor) + a second
    // caret move is not exposed publicly; drive it through the same
    // anchor field real drag-selection uses, via a Shift+End-style path:
    // simplest is to select via mouse-free anchor using setCaretPosition
    // to place the anchor, then extend with a keyboard Shift+Right chain.
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);

    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorByteOffset(), 6);
    QCOMPARE(view.caretByteOffset(), 11);

    view.toggleBold();

    QCOMPARE(doc.blockText(block), QByteArray("hello **world**"));
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorByteOffset(), 8);
    QCOMPARE(view.caretByteOffset(), 13);

    // Toggling again unwraps.
    view.toggleBold();
    QCOMPARE(doc.blockText(block), QByteArray("hello world"));
}

void TstCanvasFormatOps::toggleBold_multi_block_selection_wraps_each_block()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("first block\n\nsecond block");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));

    const QRectF r0 = view.blockRect(blocks[0]);
    const QRectF r1 = view.blockRect(blocks[1]);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(int(r0.x()) + 2, int(r0.y()) + 8));
    QTest::mouseMove(view.viewport(),
                      QPoint(int(r1.x()) + int(r1.width()) - 2, int(r1.y()) + 8));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                         QPoint(int(r1.x()) + int(r1.width()) - 2, int(r1.y()) + 8));
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks[0]);

    view.toggleBold();

    // Each covered block wrapped independently — never a cross-block byte
    // sum (C4): both blocks carry the delimiter, no bytes leaked across
    // the boundary.
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("**first block**"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("**second block**"));
}

void TstCanvasFormatOps::insertLink_selection_wraps_and_selects_url()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("see docs here");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 4);   // before "docs"
    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(view.selectionAnchorByteOffset(), 4);
    QCOMPARE(view.caretByteOffset(), 8);

    view.insertLink();

    QCOMPARE(doc.blockText(block), QByteArray("see [docs](url) here"));
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorByteOffset(), 11);  // "url" selected for replace
    QCOMPARE(view.caretByteOffset(), 14);
}

void TstCanvasFormatOps::setHeadingLevel_sets_and_clears()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("first\n\nsecond");
    View view;
    view.setDocument(&doc);

    const BlockId second = doc.iterateBlocks()[1];
    view.setCaretPosition(second, 2);

    view.setHeadingLevel(2);
    QCOMPARE(doc.blockText(second), QByteArray("## second"));
    QVERIFY(!view.hasSelection());
    QCOMPARE(view.caretByteOffset(), 5);

    view.setHeadingLevel(0);
    QCOMPARE(doc.blockText(second), QByteArray("second"));
}

void TstCanvasFormatOps::setHeadingLevel_noop_leaves_selection_intact()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("## Hello");
    View view;
    view.setDocument(&doc);

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 4);

    view.setHeadingLevel(2);   // already level 2: no-op

    QCOMPARE(doc.blockText(block), QByteArray("## Hello"));
    QCOMPARE(view.caretByteOffset(), 4);  // caret untouched by the no-op
}

void TstCanvasFormatOps::read_only_blocks_format_ops()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello");
    View view;
    view.setDocument(&doc);
    view.setReadOnly(true);

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 5);
    view.toggleBold();
    view.insertLink();
    view.setHeadingLevel(1);

    QCOMPARE(doc.blockText(block), QByteArray("hello"));  // untouched
}

void TstCanvasFormatOps::actionController_enabled_states_track_readonly_and_document()
{
    EditorWidget editor;
    CanvasActionController *ac = editor.actionController();
    QVERIFY(ac);

    // No document yet: disabled.
    QVERIFY(!ac->boldAction()->isEnabled());
    QVERIFY(!ac->heading1Action()->isEnabled());

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello");
    editor.setDocument(&doc);

    QVERIFY(ac->boldAction()->isEnabled());
    QVERIFY(ac->italicAction()->isEnabled());
    QVERIFY(ac->strikeAction()->isEnabled());
    QVERIFY(ac->inlineCodeAction()->isEnabled());
    QVERIFY(ac->linkAction()->isEnabled());
    for (int lvl = 0; lvl <= 6; ++lvl) {
        QAction *a = nullptr;
        switch (lvl) {
        case 0: a = ac->heading0Action(); break;
        case 1: a = ac->heading1Action(); break;
        case 2: a = ac->heading2Action(); break;
        case 3: a = ac->heading3Action(); break;
        case 4: a = ac->heading4Action(); break;
        case 5: a = ac->heading5Action(); break;
        case 6: a = ac->heading6Action(); break;
        }
        QVERIFY(a->isEnabled());
    }

    editor.setDocument(nullptr);
    QVERIFY(!ac->boldAction()->isEnabled());
}

void TstCanvasFormatOps::actionController_gating_is_the_falsification_target()
{
    // Named falsification target (plan P4.3): "invert the read-only
    // enabled-state; gating test fails." This test is the one that must
    // fail under that inversion — see the throwaway commit recorded in
    // the plan's findings log.
    EditorWidget editor;
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("hello");
    editor.setDocument(&doc);

    CanvasActionController *ac = editor.actionController();
    QVERIFY(ac->boldAction()->isEnabled());
    QVERIFY(ac->linkAction()->isEnabled());
    QVERIFY(ac->heading2Action()->isEnabled());

    editor.setReadOnly(true);

    QVERIFY(!ac->boldAction()->isEnabled());
    QVERIFY(!ac->italicAction()->isEnabled());
    QVERIFY(!ac->strikeAction()->isEnabled());
    QVERIFY(!ac->inlineCodeAction()->isEnabled());
    QVERIFY(!ac->linkAction()->isEnabled());
    QVERIFY(!ac->heading2Action()->isEnabled());

    editor.setReadOnly(false);
    QVERIFY(ac->boldAction()->isEnabled());
}

QTEST_MAIN(TstCanvasFormatOps)
#include "tst_canvas_format_ops.moc"
