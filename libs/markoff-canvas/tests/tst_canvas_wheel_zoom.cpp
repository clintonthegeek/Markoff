// SPDX-License-Identifier: GPL-3.0-or-later
//
// [cluster-k] P3 — Ctrl+Scroll zoom. Before this task View had no
// wheelEvent override at all, so Ctrl+Scroll just accelerated the default
// QAbstractScrollArea wheel-scrolling instead of invoking zoom. The fix
// routes through View::fontScaleStepRequested up to EditorWidget's own
// setFontScale (contract-v2's base-dispatch seam — see the signal's own
// doc comment in View.h for why View can't just mutate its font scale in
// place), so this test drives the composed EditorWidget rather than a
// bare View: the whole point is that Ctrl+Scroll ends up in the SAME
// state MainWindow's View-menu Zoom In/Out actions already do.

#include <QApplication>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;

namespace {

// QTest has no mouseWheel helper (unlike mouseClick/mouseDClick) — wheel
// events are posted directly, same technique Qt's own qabstractscrollarea
// tests use.
void postWheel(QWidget *target, QPoint pos, int angleDeltaY, Qt::KeyboardModifiers mods)
{
    QWheelEvent event(QPointF(pos), target->mapToGlobal(QPointF(pos)),
                       QPoint(0, 0), QPoint(0, angleDeltaY), Qt::NoButton, mods,
                       Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
}

}  // namespace

class TstCanvasWheelZoom : public QObject {
    Q_OBJECT

private slots:
    void ctrl_scroll_up_zooms_in();
    void ctrl_scroll_down_zooms_out();
    void plain_scroll_does_not_zoom();
};

void TstCanvasWheelZoom::ctrl_scroll_up_zooms_in()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello world\n");
    EditorWidget ed;
    ed.resize(400, 300);
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));

    QSignalSpy scaleChanged(&ed, &Markoff::MarkdownView::fontScaleChanged);
    const qreal before = ed.fontScale();

    postWheel(ed.view()->viewport(), QPoint(50, 50), /*angleDeltaY=*/120,
              Qt::ControlModifier);

    QVERIFY(scaleChanged.count() >= 1);
    QVERIFY(ed.fontScale() > before);
    // View's own composed scale must track the wrapper's — no drift
    // between the two (see fontScaleStepRequested's doc comment).
    QCOMPARE(ed.view()->fontScale(), ed.fontScale());
}

void TstCanvasWheelZoom::ctrl_scroll_down_zooms_out()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello world\n");
    EditorWidget ed;
    ed.resize(400, 300);
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));

    const qreal before = ed.fontScale();

    postWheel(ed.view()->viewport(), QPoint(50, 50), /*angleDeltaY=*/-120,
              Qt::ControlModifier);

    QVERIFY(ed.fontScale() < before);
    QCOMPARE(ed.view()->fontScale(), ed.fontScale());
}

// Ctrl+Scroll must consume the event (falls through nothing); a PLAIN
// scroll (no Ctrl) must leave fontScale untouched and fall through to
// QAbstractScrollArea's normal scrolling instead.
void TstCanvasWheelZoom::plain_scroll_does_not_zoom()
{
    QByteArray src;
    for (int i = 0; i < 60; ++i)
        src += "paragraph number " + QByteArray::number(i) + "\n\n";
    MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    EditorWidget ed;
    ed.resize(300, 200);
    ed.setDocument(&doc);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));

    QSignalSpy scaleChanged(&ed, &Markoff::MarkdownView::fontScaleChanged);
    const qreal before = ed.fontScale();
    const int scrollBefore = ed.view()->verticalScrollBar()->value();

    postWheel(ed.view()->viewport(), QPoint(50, 50), /*angleDeltaY=*/-360,
              Qt::NoModifier);

    QCOMPARE(scaleChanged.count(), 0);
    QCOMPARE(ed.fontScale(), before);
    QVERIFY(ed.view()->verticalScrollBar()->value() != scrollBefore);
}

QTEST_MAIN(TstCanvasWheelZoom)
#include "tst_canvas_wheel_zoom.moc"
