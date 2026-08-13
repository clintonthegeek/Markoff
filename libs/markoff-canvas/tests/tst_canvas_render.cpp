// SPDX-License-Identifier: GPL-3.0-or-later
//
// T0 scaffold test. The real render assertions arrive in T1 (block
// layout cache, lazy realization, y/height monotonicity). For now this
// only proves the widget constructs, shows, and paints without a
// document bound.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::View;

class TstCanvasRender : public QObject {
    Q_OBJECT

private slots:
    void constructs_and_shows_without_document();
    void accepts_and_reports_a_document();
};

void TstCanvasRender::constructs_and_shows_without_document()
{
    View view;
    view.resize(400, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.document(), nullptr);
    // A paint over an unbound view must not crash.
    view.viewport()->update();
    QTest::qWait(1);
}

void TstCanvasRender::accepts_and_reports_a_document()
{
    Markoff::MarkoffDocument doc;
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    QCOMPARE(view.document(), &doc);

    view.setDocument(nullptr);
    QCOMPARE(view.document(), nullptr);
}

QTEST_MAIN(TstCanvasRender)
#include "tst_canvas_render.moc"
