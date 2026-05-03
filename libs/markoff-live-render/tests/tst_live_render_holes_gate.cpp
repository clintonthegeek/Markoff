// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveRealisticInputHarness.h"

#include <QQuickItem>
#include <QQuickView>
#include <QtTest/QtTest>

using namespace Markoff::LiveRender::Test;

class TstHolesGate : public QObject {
    Q_OBJECT

private slots:
    void harness_sees_v0_style_character_scramble() {
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(
            QFINDTESTDATA("synthetic/SyntheticBrokenParagraphDelegate.qml")));
        QVERIFY(view.status() == QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowActive(&view));

        LiveRealisticInputHarness h(&view, /*defaultGapMs=*/30);

        const QString target = QStringLiteral("thisisinteresting");
        h.typeString(target);

        h.idle(50);

        QString delivered = view.rootObject()->property("allDeliveredText").toString();

        QVERIFY2(delivered != target,
                 qPrintable(QStringLiteral(
                     "Harness did not expose v0-style race. "
                     "Delivered: '%1'; expected scrambled. "
                     "Tighten the harness gap (try 50 ms, 75 ms, 100 ms) "
                     "before relying on it for the rest of R5.5.")
                     .arg(delivered)));
    }
};

QTEST_MAIN(TstHolesGate)
#include "tst_live_render_holes_gate.moc"
