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
    void control_and_invisible_chars_get_boxed_1to1(); // P7.2g, F1 #9
    void bidi_override_chars_are_neutralized_not_just_hidden(); // P7.2g, F1 #9 (safety subset)
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

// P7.2g (F1 #9), cosmetic/invisible group: a C0 control (SOH, 0x01 — gets
// a Control-Picture glyph, NOT a boxed-hex entry) and a soft hyphen
// (U+00AD, 2 UTF-8 bytes — gets a boxed-hex entry) in the same fixture.
// '\t'/'\n' are excluded from both treatments per the task's own note
// (this leaf already allows them through); not exercised here since
// neither appears in this fixture's text.
void TstCanvasProjection::control_and_invisible_chars_get_boxed_1to1()
{
    // "a" + SOH(0x01) + "b" + U+00AD + "c": bytes a(1) SOH(1) b(1)
    // AD-as-UTF8(2) c(1) = 6 bytes; QChars a,SOH,b,softhyphen,c = 5.
    QByteArray bytes = "a";
    bytes += char(0x01);
    bytes += "b";
    bytes += QString(QChar(0x00ad)).toUtf8();
    bytes += "c";
    QCOMPARE(bytes.size(), 6);

    const ProjectionMap map = ProjectionMap::build(bytes, {});

    QCOMPARE(map.layoutText().size(), 5);
    // C0 control -> its Control Picture glyph (U+2400 + 0x01 = U+2401),
    // rendered as an ordinary glyph — no boxed-hex entry for it.
    QCOMPARE(map.layoutText().at(1), QChar(0x2401));
    // Soft hyphen has no legible control-picture equivalent -> boxed-hex
    // sentinel, with exactly one specialCharBoxes() entry recording its
    // ORIGINAL codepoint for the paint-time hex label.
    QCOMPARE(map.specialCharBoxes().size(), 1);
    QCOMPARE(map.specialCharBoxes()[0].first, 3);
    QCOMPARE(map.specialCharBoxes()[0].second, 0x00ad);
    QVERIFY(map.layoutText().at(3) != QChar(0x00ad));  // not left as the raw invisible char

    // Byte<->QChar projection stays exact across the substitution (C4):
    // 1 QChar in, 1 QChar out, at every position including 'c' after the
    // 2-byte soft hyphen.
    QCOMPARE(map.byteToLayoutQChar(0), 0);  // 'a'
    QCOMPARE(map.byteToLayoutQChar(1), 1);  // SOH
    QCOMPARE(map.byteToLayoutQChar(2), 2);  // 'b'
    QCOMPARE(map.byteToLayoutQChar(3), 3);  // soft hyphen (2 UTF-8 bytes start here)
    QCOMPARE(map.byteToLayoutQChar(5), 4);  // 'c' (byte 5, after the 2-byte hyphen)
    QCOMPARE(map.layoutQCharToByte(4), 5);
    QCOMPARE(map.layoutQCharToByte(0), 0);

    // Falsification target for this pair: BlockLayoutCache::rebuildInline
    // stops appending the transparent FormatRange for specialCharBoxes()
    // entries, OR ProjectionMap::build stops classifying these codepoints
    // at all (reverting to bare QString::fromUtf8 + only the '\n'
    // substitution) — either break collapses this test's
    // specialCharBoxes()/layoutText() assertions back to the pre-P7.2g
    // shape (empty list; raw soft hyphen present verbatim).
}

// P7.2g (F1 #9), the safety-relevant bidi-override/isolate subset — its
// OWN falsification pair per the task's explicit instruction, kept
// separate from the cosmetic group above. U+202E (RIGHT-TO-LEFT OVERRIDE)
// is the exact "invoice.exe" filename-spoofing character; letting it
// reach QTextLayout verbatim would let Qt's own bidi algorithm silently
// reorder this block's visible text, independent of anything painted on
// top of it.
void TstCanvasProjection::bidi_override_chars_are_neutralized_not_just_hidden()
{
    // "a" + U+202E (3 UTF-8 bytes) + "b": bytes a(1) RLO(3) b(1) = 5 bytes;
    // QChars a, sentinel, b = 3.
    const QString src = QStringLiteral("a") + QChar(0x202e) + QStringLiteral("b");
    const QByteArray bytes = src.toUtf8();
    QCOMPARE(bytes.size(), 5);

    const ProjectionMap map = ProjectionMap::build(bytes, {});

    QCOMPARE(map.layoutText().size(), 3);
    // The load-bearing safety assertion: the RAW override character must
    // be ABSENT from the text a QTextLayout will ever see — not merely
    // covered by paint, actually gone from the string Qt's bidi algorithm
    // reorders on. A single boxed-hex entry records what it was, for the
    // visible warning label.
    QVERIFY(!map.layoutText().contains(QChar(0x202e)));
    QCOMPARE(map.specialCharBoxes().size(), 1);
    QCOMPARE(map.specialCharBoxes()[0].first, 1);
    QCOMPARE(map.specialCharBoxes()[0].second, 0x202e);

    // Byte<->QChar projection: 'b' after the 3-byte override lands at
    // QChar 2, byte 4 — exact, despite the byte/QChar-width mismatch this
    // substitution introduces (3 raw bytes -> 1 layout QChar).
    QCOMPARE(map.byteToLayoutQChar(4), 2);
    QCOMPARE(map.layoutQCharToByte(2), 4);

    // Same coverage for the isolate range (U+2066 LRI) and the other
    // override direction (U+202D LRO) — one assertion each, not full
    // fixtures, since the mechanism is identical per-codepoint.
    for (const char16_t cp : {char16_t(0x202d), char16_t(0x2066), char16_t(0x2067),
                               char16_t(0x2068), char16_t(0x2069)}) {
        const QString s2 = QStringLiteral("x") + QChar(cp) + QStringLiteral("y");
        const ProjectionMap m2 = ProjectionMap::build(s2.toUtf8(), {});
        QVERIFY2(!m2.layoutText().contains(QChar(cp)),
                 qPrintable(QStringLiteral("U+%1 leaked into layout text")
                                .arg(int(cp), 4, 16, QLatin1Char('0'))));
        QCOMPARE(m2.specialCharBoxes().size(), 1);
        QCOMPARE(m2.specialCharBoxes()[0].second, int(cp));
    }

    // Falsification target for THIS pair: ProjectionMap::build's
    // classifySpecialChar() drops the BoxedHex branch for the bidi
    // override/isolate range (e.g. narrows the check to C1-only) — the
    // `!layoutText().contains(...)` assertions above fail because the raw
    // U+202E/U+2066-9/U+202D characters survive into the layout text
    // verbatim, exactly the spoofing-surface regression F1 flagged.
}

QTEST_MAIN(TstCanvasProjection)
#include "tst_canvas_projection.moc"
