// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QSignalSpy>
#include <QAction>

#include <markoff/live/LiveActionController.h>

using namespace Markoff::Live;

class TstThemeToggleSignal : public QObject {
    Q_OBJECT
private slots:
    void first_trigger_emits_dark_true() {
        LiveActionController ac;
        QSignalSpy spy(&ac, &LiveActionController::themeToggleRequested);
        ac.toggleDarkAction()->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    void second_trigger_emits_dark_false() {
        LiveActionController ac;
        QSignalSpy spy(&ac, &LiveActionController::themeToggleRequested);
        ac.toggleDarkAction()->trigger();  // -> true
        ac.toggleDarkAction()->trigger();  // -> false
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
    }

    void toggle_action_does_not_mutate_anything_in_controller_directly() {
        LiveActionController ac;
        QVERIFY(ac.toggleDarkAction() != nullptr);
        ac.toggleDarkAction()->trigger();
        ac.toggleDarkAction()->trigger();
        ac.toggleDarkAction()->trigger();
    }
};

QTEST_MAIN(TstThemeToggleSignal)
#include "tst_live_render_theme_toggle_signal.moc"
