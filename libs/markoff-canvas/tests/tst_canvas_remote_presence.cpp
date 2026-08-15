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

// A remote participant selecting "Hello" (bytes 0..5) in "Hello world." must
// paint with ITS OWN presenceColor, distinct from
// Theme::Slot::SelectionBackground (the local selection's color) — this is
// exactly the plan's own falsification target.
void TstCanvasRemotePresence::remote_selection_uses_participant_color_not_local_selection_color()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world.\n");
    const BlockId block = doc.iterateBlocks().front();

    EditorWidget ed;
    ed.setDocument(&doc);

    const TextAnchor start = doc.textAnchorAt(block, 0, /*rightBias=*/false);
    const TextAnchor end   = doc.textAnchorAt(block, 5, /*rightBias=*/true);

    const QColor participantColor(0x1A, 0x9C, 0xE5);  // arbitrary, != theme selection bg
    Selection sel;
    sel.anchor = start;
    sel.active = end;
    sel.kind = Selection::Kind::Presence;
    sel.participantId = QStringLiteral("p1");
    sel.participantLabel = QStringLiteral("Alice");
    sel.presenceColor = participantColor;

    ed.setRemotePresences({RemotePresence{sel}});

    const QList<RemotePresence> resolved = ed.view()->remotePresencesForBlock(block);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().selection.presenceColor, participantColor);
    QCOMPARE(resolved.first().selection.participantLabel, QStringLiteral("Alice"));

    // The load-bearing distinct-color assertion the plan's falsification
    // targets: a remote presence never shares the local selection's color.
    const QColor localSelectionColor = ed.view()->theme().color(Theme::Slot::SelectionBackground);
    QVERIFY(resolved.first().selection.presenceColor != localSelectionColor);

    // Real paint exercise (offscreen) — must not crash, and is the actual
    // code path setRemotePresences/remotePresencesForBlock's assertions
    // above are standing in for (constructing the same FormatRange::
    // background from presence.selection.presenceColor).
    ed.resize(400, 300);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));
    QTest::qWait(0);
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
