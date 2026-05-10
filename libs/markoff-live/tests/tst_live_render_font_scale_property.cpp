// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live/LiveListModelBinding.h>

class TstFontScaleProperty : public QObject {
    Q_OBJECT
private slots:
    void default_font_scale_is_one() {
        Markoff::Live::LiveListModelBinding b;
        QCOMPARE(b.fontScale(), 1.0);
    }

    void set_font_scale_emits_change_signal() {
        Markoff::Live::LiveListModelBinding b;
        QSignalSpy spy(&b, &Markoff::Live::LiveListModelBinding::fontScaleChanged);
        b.setFontScale(1.5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(b.fontScale(), 1.5);
    }

    void set_font_scale_clamps_above_max() {
        Markoff::Live::LiveListModelBinding b;
        b.setFontScale(10.0);
        QCOMPARE(b.fontScale(), 3.0);  // kMaxFontScale
    }

    void set_font_scale_clamps_below_min() {
        Markoff::Live::LiveListModelBinding b;
        b.setFontScale(0.1);
        QCOMPARE(b.fontScale(), 0.5);  // kMinFontScale
    }

    void set_font_scale_no_signal_when_clamped_value_unchanged() {
        Markoff::Live::LiveListModelBinding b;
        b.setFontScale(3.0);  // at max
        QSignalSpy spy(&b, &Markoff::Live::LiveListModelBinding::fontScaleChanged);
        b.setFontScale(5.0);  // also clamps to 3.0 — no change
        QCOMPARE(spy.count(), 0);
    }

    void font_scale_step_constant_is_1_1() {
        Markoff::Live::LiveListModelBinding b;
        QCOMPARE(b.fontScaleStep(), 1.10);
    }
};

QTEST_GUILESS_MAIN(TstFontScaleProperty)
#include "tst_live_render_font_scale_property.moc"
