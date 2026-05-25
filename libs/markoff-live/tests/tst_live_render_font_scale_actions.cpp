// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QAction>

#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveListModelBinding.h>

using namespace Markoff::Live;

class TstFontScaleActions : public QObject {
    Q_OBJECT
private slots:
    void zoom_in_action_multiplies_font_scale_by_step() {
        LiveListModelBinding b;
        LiveActionController ac;
        ac.setBinding(&b);
        const qreal before = b.fontScale();
        ac.zoomInAction()->trigger();
        QCOMPARE(b.fontScale(), before * b.fontScaleStep());
    }

    void zoom_out_action_divides_font_scale_by_step() {
        LiveListModelBinding b;
        LiveActionController ac;
        ac.setBinding(&b);
        b.setFontScale(2.0);
        ac.zoomOutAction()->trigger();
        QCOMPARE(b.fontScale(), 2.0 / b.fontScaleStep());
    }

    void zoom_reset_action_returns_to_one() {
        LiveListModelBinding b;
        LiveActionController ac;
        ac.setBinding(&b);
        b.setFontScale(2.5);
        ac.zoomResetAction()->trigger();
        QCOMPARE(b.fontScale(), 1.0);
    }

    void zoom_in_clamps_at_max() {
        LiveListModelBinding b;
        LiveActionController ac;
        ac.setBinding(&b);
        for (int i = 0; i < 50; ++i) ac.zoomInAction()->trigger();
        QCOMPARE(b.fontScale(), 3.0);  // kMaxFontScale
    }

    void zoom_out_clamps_at_min() {
        LiveListModelBinding b;
        LiveActionController ac;
        ac.setBinding(&b);
        for (int i = 0; i < 50; ++i) ac.zoomOutAction()->trigger();
        QCOMPARE(b.fontScale(), 0.5);  // kMinFontScale
    }

    void zoom_actions_are_no_op_without_binding() {
        LiveActionController ac;
        // No setBinding call.
        ac.zoomInAction()->trigger();   // must not crash
        ac.zoomOutAction()->trigger();
        ac.zoomResetAction()->trigger();
    }
};

QTEST_MAIN(TstFontScaleActions)
#include "tst_live_render_font_scale_actions.moc"
