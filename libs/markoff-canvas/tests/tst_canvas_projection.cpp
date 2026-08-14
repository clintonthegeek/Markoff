// SPDX-License-Identifier: GPL-3.0-or-later
//
// P2.1 — ProjectionMap + emphasis/strong omission (spec §4.2).
//
// Two layers: a pure unit test of ProjectionMap itself (no widget, no
// QApplication needed — it is just QString/QByteArray arithmetic), and a
// View-integration test proving the omission is real reflow, not a
// cosmetic recolor (spec's own exit criterion). The keystroke/reveal
// interaction itself is already covered by tst_canvas_inline_formatting's
// E7 test, carried forward unchanged — this file adds what that one
// couldn't check.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

// Private to src/, same directory as View's sources — reached directly
// here (not through the public View.h surface) because ProjectionMap's
// own snap/round-trip contract is the thing under test, not anything a
// widget consumer should see.
#include "../src/ProjectionMap.h"

using Markoff::BlockId;
using Markoff::Canvas::ProjectionMap;
using Markoff::Canvas::View;

class TstCanvasProjection : public QObject {
    Q_OBJECT

private slots:
    void map_omits_and_round_trips();
    void map_seam_snaps_to_earlier_run_by_default();
    void reflow_is_real_not_cosmetic();
};

// "a **b** c": a=0, ' '=1, *=2, *=3, b=4, *=5, *=6, ' '=7, c=8 (9 bytes,
// ASCII throughout so QChar index == byte offset for the source text).
// Omit both "**" runs (charOffset 2/length 2, charOffset 5/length 2) —
// exactly what the caret-outside-the-span case hides.
static ProjectionMap buildFixtureMap()
{
    return ProjectionMap::build("a **b** c", {{2, 2}, {5, 2}});
}

void TstCanvasProjection::map_omits_and_round_trips()
{
    const ProjectionMap map = buildFixtureMap();

    // Both "**" pairs are gone from the layout text; the kept content
    // ("a ", "b", " c") survives untouched and concatenated.
    QCOMPARE(map.layoutText(), QStringLiteral("a b c"));

    // Bytes inside a kept run map straight across (0 offset within this
    // fixture's two runs before the first omission).
    QCOMPARE(map.byteToLayoutQChar(0), 0);
    QCOMPARE(map.byteToLayoutQChar(1), 1);
    // Round-trip for a kept-run interior position is exact.
    QCOMPARE(map.layoutQCharToByte(map.byteToLayoutQChar(1)), 1);

    // The block's end (byte 9, one past 'c') maps to the layout text's end.
    QCOMPARE(map.byteToLayoutQChar(9), int(map.layoutText().size()));
    QCOMPARE(map.layoutQCharToByte(int(map.layoutText().size())), 9);

    // A byte strictly inside the first hidden run (byte 3, the second '*')
    // has nowhere of its own to map to — both snap directions land on the
    // same layout position, because the omitted run has zero layout width
    // (there is no distinguishing information left once it's gone from the
    // text). That's expected, not a bug: see the seam test below for where
    // "snap left by default" actually has an observable effect (the
    // reverse, layout->byte, direction).
    const int snappedLeft  = map.byteToLayoutQChar(3, ProjectionMap::SnapDirection::Left);
    const int snappedRight = map.byteToLayoutQChar(3, ProjectionMap::SnapDirection::Right);
    QCOMPARE(snappedLeft, snappedRight);
    QCOMPARE(snappedLeft, 2);  // right before 'b' in "a b c"
}

void TstCanvasProjection::map_seam_snaps_to_earlier_run_by_default()
{
    const ProjectionMap map = buildFixtureMap();

    // Layout position 2 sits exactly on the seam between the "a " run
    // (ends at full byte 2) and the "b" run (starts at full byte 4) — the
    // whole hidden "**" collapsed to nothing between them. Spec §4.2:
    // "when no direction exists, snap left" — the reverse mapping resolves
    // this seam to the EARLIER run's edge (byte 2, right before the hidden
    // delimiter) rather than the later run's (byte 4, its far edge).
    QCOMPARE(map.layoutQCharToByte(2), 2);

    // One step further in layout space (past 'b') resolves unambiguously
    // to byte 5 (right after 'b', before the second hidden "**") — 'b'
    // itself is real, visible content with no seam ambiguity.
    QCOMPARE(map.layoutQCharToByte(3), 5);
}

void TstCanvasProjection::reflow_is_real_not_cosmetic()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("a **b** c\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(block);

    // Caret starts outside the span (Home lands at byte 0): delimiters are
    // hidden, so the layout's measured width is the SHORT ("a b c") width.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);
    const qreal hiddenWidth = view.lineNaturalWidth(block);
    QVERIFY(hiddenWidth > 0);

    // Walk the caret into the span; once it touches "**b**", the
    // delimiters reveal and the SAME layout now measures the full
    // ("a **b** c") width — strictly wider, because the reveal literally
    // put the "**" bytes back into the layout string (spec §4.2: "a caret
    // move that changes delimiter visibility is a layout TEXT change").
    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(view.caretByteOffset(), 4);  // right before 'b' — inside the span
    const qreal revealedWidth = view.lineNaturalWidth(block);
    QVERIFY(revealedWidth > hiddenWidth);

    // Move back out: the same block reflows narrower again, round-tripping
    // exactly (nothing about the underlying buffer changed, only which
    // bytes the layout currently omits).
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);
    QCOMPARE(view.lineNaturalWidth(block), hiddenWidth);
}

QTEST_MAIN(TstCanvasProjection)
#include "tst_canvas_projection.moc"
