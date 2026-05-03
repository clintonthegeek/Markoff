// SPDX-License-Identifier: GPL-3.0-or-later
//
// R5.5 Task 17: stress-typing 200 chars into a hole at 30 ms gap.
// The load-bearing R5.5 regression test. The harness has been gated
// against v0-style scramble (R5.5 Task 2 — synthetic stub deleted in
// Task 3). If this test fails, R5.5 cannot ship.

#include "LiveRealisticInputHarness.h"

#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender;
using Markoff::LiveRender::Test::LiveRealisticInputHarness;

class TstHolesQml : public QObject {
    Q_OBJECT

private slots:
    void stress_type_into_hole_no_scramble_no_loss() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);

        QQuickView view;
        view.engine()->rootContext()->setContextProperty(
            QStringLiteral("ctxDocument"), &doc);
        view.setSource(QUrl::fromLocalFile(
            QFINDTESTDATA("qml/HoleStressView.qml")));
        QVERIFY(view.status() == QQuickView::Ready);

        // Now seed the doc — the binding's onParseUpdated will populate
        // the model.
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        view.show();
        QVERIFY(QTest::qWaitForWindowActive(&view));

        LiveRealisticInputHarness h(&view, /*defaultGapMs=*/30);

        // Click in the upper-left of the view to focus row 0's TextEdit.
        // Coordinates: middle of the first ~30 px row.
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                          QPoint(40, 15));
        h.idle(50);

        // Press End to put the caret at end of "hello".
        h.keyClick(Qt::Key_End);
        h.idle(30);

        // Press Enter — opens hole at end of row 0 (EOB-Enter path).
        // Idle 100 ms to let the model reset propagate, the new hole delegate
        // complete, and Qt.callLater focus routing settle before typing starts.
        h.keyClick(Qt::Key_Return);
        h.idle(100);

        // Type 200 characters at 30 ms gap (= ~33 char/sec sustained).
        const QString sentence =
            QStringLiteral("This is a live render stress test of two hundred "
                           "characters typed into a hole at human speed without "
                           "character scramble or data loss every byte must arrive "
                           "in order and reify after the idle commits ok");
        QCOMPARE(sentence.length(), 200);   // verify the constant

        h.typeString(sentence);

        // Idle commit fires at 250 ms after last keystroke; allow 500 ms
        // for parse-back to round-trip after applyLocalEdit.
        h.idle(500);

        // Wait for the parse-back to settle the source view.
        QTRY_COMPARE_WITH_TIMEOUT(doc.toMarkdown(),
                                  QString("hello\n\n") + sentence,
                                  3000);
    }
};

QTEST_MAIN(TstHolesQml)
#include "tst_live_render_holes_qml.moc"
