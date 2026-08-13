// SPDX-License-Identifier: GPL-3.0-or-later
//
// T6 — kind transitions (exit E5).
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click to place the caret, drive
// real QTest::keyClicks. The canvas convention decided in T6 (spec §9,
// overriding the plan's original "strip the prefix" step): a promoted
// block's buffer keeps whatever text matched the inference rule — only the
// kind changes — so a typed heading's buffer matches a loaded heading's.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;

class TstCanvasKindTransition : public QObject {
    Q_OBJECT

private slots:
    void hash_space_promotes_heading_caret_survives();
};

void TstCanvasKindTransition::hash_space_promotes_heading_caret_survives()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::Paragraph);

    // Click at byte 0 (start of the block).
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 0);

    QTest::keyClicks(&view, QStringLiteral("# "));

    // Kind promotes; the buffer keeps the matched "# " (no strip — T6's
    // reading of the T1 finding).
    QCOMPARE(doc.blockKind(block), BlockKind::Heading);
    QCOMPARE(doc.blockText(block), QByteArray("# Hello world."));

    // Caret is exactly where typing left it: byte 2, right after "# ".
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 2);

    QVERIFY(view.hasFocus());
}

QTEST_MAIN(TstCanvasKindTransition)
#include "tst_canvas_kind_transition.moc"
