// SPDX-License-Identifier: GPL-3.0-or-later
//
// MarkdownView contract — attach-window write semantics (live leaf).
//
// The adoption brief (docs/handoff/2026-06-09-corbomite-api-adoption-brief.md,
// § ephemeral) instructs consumers to restore view state like this:
//
//     leaf->setDocument(doc);                          // (host wiring)
//     leaf->setCursorPosition({line, column});         // same call stack
//     leaf->setScrollPositionVisualLine(scrollFrac);   // same call stack
//
// i.e. base-contract writes issued in the SAME CALL STACK as document
// attach, before the QML scene has materialized the new content. The
// source/styled leaves honor these synchronously; the live leaf must
// honor them *eventually* — a write issued in the attach window may not
// be silently lost, and may not be overridden by the leaf's own
// initial-focus seed (LiveView.qml onCountChanged →
// requestTextCaretAtRow(0,0)).
//
// Found 2026-06-10 by Corbomite Phase 1 adoption
// (tst_note_editor_widget_ephemeral): one-shot attach-window writes never
// land (5 s of event-loop polling does not converge), while re-issuing
// the same write after materialization converges in ~131 ms.
//
// INVARIANTS #5: these slots drive the REAL production call path — the
// QQuickWidget scene, the QML initial-focus seed, and the
// LiveCursorState chokepoint — not a mock.

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/EditorWidget.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveListModelBinding.h>

#include <QQuickItem>
#include <QQuickWidget>
#include <QStringList>
#include <QTest>

using namespace Markoff::Live;

namespace {
QString makeParagraphs(int count)
{
    QStringList blocks;
    blocks.reserve(count);
    for (int i = 0; i < count; ++i)
        blocks.append(QStringLiteral("Paragraph %1 line A.").arg(i));
    return blocks.join(QStringLiteral("\n\n"));
}
} // namespace

class TestViewContractLiveAttachWindow : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // A consumer cursor write issued in the same call stack as
    // setDocument must eventually land on the requested flat line —
    // not be dropped, and not lose to the QML initial-focus seed
    // (which requests row 0 once the ListView count changes).
    void attachWindow_cursorWrite_survives()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(makeParagraphs(40).toUtf8());

        EditorWidget w;
        w.resize(800, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        // Attach + write, same call stack (the brief's restore recipe).
        w.setDocument(&doc);
        w.setCursorPosition({7, 3});

        // Deep convergence: the chokepoint's canonical caret must sit on
        // row 6 (40 single-line paragraph blocks ⇒ flat line N == row N-1).
        auto *cs = w.binding()->cursorState();
        QTRY_VERIFY2(cs->currentTextCaret().has_value(),
                     "no TextCaret ever established — attach-window write "
                     "dropped and initial seed missing too");
        QTRY_COMPARE(w.cursorPosition().line, 7);
        QCOMPARE(w.cursorPosition().column, 3);
    }

    // Same for scroll: a fraction written in the attach window (while
    // the QML contentHeight is still 0) must apply once the view has
    // scrollable content — not be silently lost.
    void attachWindow_scrollWrite_applies()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(makeParagraphs(40).toUtf8());

        EditorWidget w;
        w.resize(800, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        w.setDocument(&doc);
        w.setScrollPositionVisualLine(0.5f);

        // Deep convergence: the QML root's contentY must actually move
        // (40 paragraphs in a 300 px viewport guarantees scrollable > 0).
        auto *quick = w.findChild<QQuickWidget *>();
        QVERIFY(quick);
        QTRY_VERIFY2(quick->rootObject()
                         && quick->rootObject()->property("contentY").toReal() > 0.0,
                     "attach-window scroll write never reached the QML root");
        QTRY_VERIFY2(w.scrollPositionVisualLine() > 0.3f,
                     qPrintable(QStringLiteral("scrollPositionVisualLine drifted: %1")
                                    .arg(w.scrollPositionVisualLine())));
    }

    // The seed must still work when the consumer writes nothing: a fresh
    // attach with no consumer cursor write seeds the caret at row 0
    // (line 1) — the 2026-05-16 startup-focus behavior is preserved.
    void attachWindow_noWrite_seedStillLands()
    {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(makeParagraphs(40).toUtf8());

        EditorWidget w;
        w.resize(800, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        w.setDocument(&doc);

        auto *cs = w.binding()->cursorState();
        QTRY_VERIFY2(cs->currentTextCaret().has_value(),
                     "initial-focus seed no longer establishes a caret");
        QCOMPARE(w.cursorPosition().line, 1);
    }
};

QTEST_MAIN(TestViewContractLiveAttachWindow)
#include "tst_view_contract_live_attach_window.moc"
