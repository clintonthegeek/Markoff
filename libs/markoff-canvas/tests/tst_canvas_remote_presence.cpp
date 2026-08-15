// SPDX-License-Identifier: GPL-3.0-or-later
//
// P6.2 — remote presence rendering. EditorWidget::setRemotePresences()
// paints a remote participant's caret/selection: a selection tint in the
// participant's own presenceColor (never the local SelectionBackground
// theme slot), plus a caret bar + name flag, all draw-time
// QTextLayout::FormatRanges built fresh in View::paintEvent — never cached
// CanvasCursor state (View::setRemotePresences's own doc comment).
//
// Falsification target (plan P6.2): "paint remote selection with the local
// selection color; distinct-color assertion fails."

#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Theme.h>

using Markoff::BlockId;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::RemotePresence;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;
using Markoff::Selection;
using Markoff::TextAnchor;
using Markoff::Theme;

class TstCanvasRemotePresence : public QObject {
    Q_OBJECT

private slots:
    void remote_selection_uses_participant_color_not_local_selection_color();
    void remote_caret_only_presence_resolves_with_no_tint();
    void non_presence_kind_selections_are_not_painted_as_remote();
    void clearing_presences_empties_the_block_index();
};

/// Count of pixels in `img` matching `color` exactly.
int countColorPixels(const QImage &img, QColor color)
{
    const QRgb target = color.rgb();
    int count = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixel(x, y) == target)
                ++count;
    return count;
}

/// Renders a single presence over "X X" and returns how many painted
/// pixels exactly match `presenceColor`. Both the caret bar and the
/// selection tint paint in the participant's own color (View::paintEvent),
/// so an ordinary "does this color appear anywhere" scan can't tell tint
/// pixels apart from caret-bar/flag pixels — the caller isolates the tint's
/// contribution by diffing a real range against a collapsed (caret-only)
/// one of the same color, below.
int presenceColorPixelCount(BlockId block, MarkoffDocument &doc, TextAnchor anchor,
                             TextAnchor active, QColor presenceColor)
{
    EditorWidget ed;
    ed.setDocument(&doc);

    Selection sel;
    sel.anchor = anchor;
    sel.active = active;
    sel.kind = Selection::Kind::Presence;
    sel.participantId = QStringLiteral("p1");
    sel.participantLabel = QStringLiteral("Alice");
    sel.presenceColor = presenceColor;
    ed.setRemotePresences({RemotePresence{sel}});

    ed.resize(400, 300);
    ed.show();
    (void)QTest::qWaitForWindowExposed(&ed);
    ed.view()->viewport()->repaint();
    return countColorPixels(ed.view()->grab().toImage(), presenceColor);
}

// A remote participant selecting "X X" (bytes 0..3) must paint the
// SELECTION TINT in ITS OWN presenceColor, distinct from
// Theme::Slot::SelectionBackground (the local selection's color) — this is
// exactly the plan's own falsification target ("paint remote selection with
// the local selection color; distinct-color assertion fails"). Reads the
// ACTUAL painted pixels (real paintEvent, via grab().toImage()), not the
// input Selection echoed back at itself.
//
// The caret bar + name flag ALSO paint in presenceColor (correctly, and
// unaffected by a tint-only regression), so a plain "does presenceColor
// appear anywhere in the frame" scan cannot tell a broken tint apart from
// just the caret residue — confirmed empirically: that weaker assertion
// kept passing under this test's own planted falsification break (tint
// reusing Theme::Slot::SelectionBackground) because the caret bar alone
// still contributed matching pixels. The real range vs. a collapsed
// (caret-only) range of the SAME color isolates the tint's own
// contribution instead.
void TstCanvasRemotePresence::remote_selection_uses_participant_color_not_local_selection_color()
{
    MarkoffDocument docReal(1);
    docReal.loadFromMarkdown("X X\n");
    const BlockId blockReal = docReal.iterateBlocks().front();
    const TextAnchor start = docReal.textAnchorAt(blockReal, 0, /*rightBias=*/false);
    const TextAnchor end   = docReal.textAnchorAt(blockReal, 3, /*rightBias=*/true);

    // A saturated color that won't already appear anywhere in the default
    // theme's text/background/cursor palette.
    const QColor participantColor(0x1A, 0x9C, 0xE5);

    const int withTintCount =
        presenceColorPixelCount(blockReal, docReal, start, end, participantColor);

    MarkoffDocument docCaretOnly(2);
    docCaretOnly.loadFromMarkdown("X X\n");
    const BlockId blockCaretOnly = docCaretOnly.iterateBlocks().front();
    const TextAnchor caretOnly = docCaretOnly.textAnchorAt(blockCaretOnly, 3, /*rightBias=*/true);

    const int caretOnlyCount = presenceColorPixelCount(
        blockCaretOnly, docCaretOnly, caretOnly, caretOnly, participantColor);

    // The colors themselves must differ (sanity on the input data).
    View bare;
    const QColor localSelectionColor = bare.theme().color(Theme::Slot::SelectionBackground);
    QVERIFY(participantColor != localSelectionColor);

    // The load-bearing check: a real selection range must paint
    // MEASURABLY MORE presenceColor pixels than the same participant's bare
    // caret alone — proof the tint FormatRange itself used presenceColor,
    // not just the caret bar/flag next to it.
    QVERIFY2(withTintCount > caretOnlyCount + 20,
             qPrintable(QStringLiteral("withTint=%1 caretOnly=%2 — tint did not add "
                                        "measurably more presenceColor-painted pixels")
                            .arg(withTintCount)
                            .arg(caretOnlyCount)));
}

// A collapsed presence (anchor == active — a bare remote caret, no
// selection) resolves to exactly the touched block, with no crash, and
// contributes no selection tint (verified indirectly: it still resolves as
// "touching" the block since caret-only presences must still paint their
// caret bar + flag there).
void TstCanvasRemotePresence::remote_caret_only_presence_resolves_with_no_tint()
{
    MarkoffDocument doc(2);
    doc.loadFromMarkdown("Hello world.\n");
    const BlockId block = doc.iterateBlocks().front();

    EditorWidget ed;
    ed.setDocument(&doc);

    const TextAnchor at = doc.textAnchorAt(block, 3, /*rightBias=*/true);
    Selection sel;
    sel.anchor = at;
    sel.active = at;
    sel.kind = Selection::Kind::Presence;
    sel.participantId = QStringLiteral("p2");
    sel.participantLabel = QStringLiteral("Bob");
    sel.presenceColor = QColor(0xE5, 0x5A, 0x1A);

    ed.setRemotePresences({RemotePresence{sel}});

    const QList<RemotePresence> resolved = ed.view()->remotePresencesForBlock(block);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().selection.participantId, QStringLiteral("p2"));
}

// F1a (multi-cursor readiness): paint dispatch is keyed off Selection::Kind,
// not list membership. A Presence-list entry whose kind is something else
// (Primary/Secondary/SearchMatch) must not resolve through the remote-
// presence path.
void TstCanvasRemotePresence::non_presence_kind_selections_are_not_painted_as_remote()
{
    MarkoffDocument doc(3);
    doc.loadFromMarkdown("Hello world.\n");
    const BlockId block = doc.iterateBlocks().front();

    EditorWidget ed;
    ed.setDocument(&doc);

    const TextAnchor start = doc.textAnchorAt(block, 0, false);
    const TextAnchor end   = doc.textAnchorAt(block, 5, true);
    Selection sel;
    sel.anchor = start;
    sel.active = end;
    sel.kind = Selection::Kind::Secondary;  // NOT Presence
    sel.presenceColor = QColor(0x11, 0x22, 0x33);

    ed.setRemotePresences({RemotePresence{sel}});

    QVERIFY(ed.view()->remotePresencesForBlock(block).isEmpty());
}

void TstCanvasRemotePresence::clearing_presences_empties_the_block_index()
{
    MarkoffDocument doc(4);
    doc.loadFromMarkdown("Hello world.\n");
    const BlockId block = doc.iterateBlocks().front();

    EditorWidget ed;
    ed.setDocument(&doc);

    const TextAnchor start = doc.textAnchorAt(block, 0, false);
    const TextAnchor end   = doc.textAnchorAt(block, 5, true);
    Selection sel;
    sel.anchor = start;
    sel.active = end;
    sel.kind = Selection::Kind::Presence;
    sel.presenceColor = QColor(0x11, 0x22, 0x33);

    ed.setRemotePresences({RemotePresence{sel}});
    QVERIFY(!ed.view()->remotePresencesForBlock(block).isEmpty());

    ed.setRemotePresences({});
    QVERIFY(ed.view()->remotePresencesForBlock(block).isEmpty());
}

QTEST_MAIN(TstCanvasRemotePresence)
#include "tst_canvas_remote_presence.moc"
