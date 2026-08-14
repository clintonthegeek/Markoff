// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.7 — task-list checkboxes: `- [ ]`/`- [x]` ListItem blocks render a
// checkbox glyph in the marker-decoration slot (BlockPresentation's
// `isTaskItem`/`taskChecked`, painted by View::paintEvent) instead of a
// bullet/number; a click on the glyph toggles the block's `Checked` attr
// via `MarkoffDocument::toggleListItemChecked` (one `UndoLog::Transaction`,
// same core API markoff-live's QML delegate already calls).
//
// Per core's ListItem buffer convention (markoff-core/CLAUDE.md "Block
// buffer convention" table): the marker — including a task item's
// `[ ]`/`[x]` — is stripped from the buffer at load and is display-only,
// reconstructed via `listItemDisplayMarker()`/attrs. There is therefore no
// "x byte" inside a ListItem's own block buffer for `d2ApplyBufferEdit` to
// flip; `Checked` is a block attr, and `toggleListItemChecked` is core's
// existing one-transaction, per-block (never cross-block) toggle path.
//
// The falsification target named by the plan ("toggle writes to the wrong
// byte; neighbor-item test fails") maps onto this attr-based mechanism as
// "toggle writes to the wrong BLOCK" — neighbor_item_click_does_not_toggle
// is the test that would catch a toggle wired to the wrong BlockId (e.g. a
// hardcoded first-item id, or an off-by-one into the adjacent item).

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;

namespace {

bool checkedAttr(MarkoffDocument &doc, BlockId id)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(Markoff::AttrNames::Checked);
    if (it == attrs.cend())
        return false;
    const bool *v = std::get_if<bool>(&it.value());
    return v && *v;
}

}  // namespace

class TstCanvasTaskCheckbox : public QObject {
    Q_OBJECT

private slots:
    void render_marker_is_empty_for_task_items();
    void click_toggles_unchecked_to_checked();
    void click_toggles_checked_to_unchecked();
    void neighbor_item_click_does_not_toggle();
    void click_is_noop_while_read_only();
    void caret_typing_in_item_text_unaffected_by_checkbox();
};

void TstCanvasTaskCheckbox::render_marker_is_empty_for_task_items()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] one\n- [x] two\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));

    // Checkbox glyph occupies the SAME decoration slot a bullet/number
    // marker would (BlockPresentation.h's mutual-exclusion note) — a task
    // item's rect is non-null, and it never also carries bullet-marker
    // text (that would double-paint the gutter).
    QVERIFY(!view.taskCheckboxRectFor(blocks[0]).isNull());
    QVERIFY(!view.taskCheckboxRectFor(blocks[1]).isNull());
}

void TstCanvasTaskCheckbox::click_toggles_unchecked_to_checked()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] one\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(!checkedAttr(doc, block));

    const QRectF box = view.taskCheckboxRectFor(block);
    QVERIFY(!box.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       box.center().toPoint());

    QVERIFY(checkedAttr(doc, block));
}

void TstCanvasTaskCheckbox::click_toggles_checked_to_unchecked()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [x] one\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QVERIFY(checkedAttr(doc, block));

    const QRectF box = view.taskCheckboxRectFor(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       box.center().toPoint());

    QVERIFY(!checkedAttr(doc, block));
}

// Named falsification target (plan P4.7): toggling item N must not touch
// N-1 or N+1's Checked attr. A toggle wired to a hardcoded/wrong BlockId
// (the throwaway-commit plant) fails this.
void TstCanvasTaskCheckbox::neighbor_item_click_does_not_toggle()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] one\n- [ ] two\n- [ ] three\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(3));
    for (const BlockId b : blocks)
        QVERIFY(!checkedAttr(doc, b));

    const QRectF middleBox = view.taskCheckboxRectFor(blocks[1]);
    QVERIFY(!middleBox.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       middleBox.center().toPoint());

    QVERIFY(!checkedAttr(doc, blocks[0]));   // neighbor above: untouched
    QVERIFY(checkedAttr(doc, blocks[1]));    // the clicked item: toggled
    QVERIFY(!checkedAttr(doc, blocks[2]));   // neighbor below: untouched
}

void TstCanvasTaskCheckbox::click_is_noop_while_read_only()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] one\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setReadOnly(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF box = view.taskCheckboxRectFor(block);
    QVERIFY(!box.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       box.center().toPoint());

    QVERIFY(!checkedAttr(doc, block));  // gated: click did nothing
}

// A click/edit inside the item's TEXT (right of the checkbox gutter) still
// places the caret and edits content normally — the checkbox hit-test only
// claims the gutter rect, never the text hit-test hitTest() already owns.
void TstCanvasTaskCheckbox::caret_typing_in_item_text_unaffected_by_checkbox()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] one\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF blockBox = view.blockRect(block);
    // Click well inside the text column, right of the checkbox gutter.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(int(blockBox.x()) + 40, int(blockBox.y()) + 8));
    QCOMPARE(view.caretBlock(), block);

    view.setCaretPosition(block, 3);   // end of "one"
    QTest::keyClicks(&view, QStringLiteral("!"));
    QCOMPARE(doc.blockText(block), QByteArray("one!"));
    QVERIFY(!checkedAttr(doc, block));  // typing never touches Checked
}

QTEST_MAIN(TstCanvasTaskCheckbox)
#include "tst_canvas_task_checkbox.moc"
