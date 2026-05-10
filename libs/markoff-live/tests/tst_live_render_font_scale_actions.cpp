// SPDX-License-Identifier: GPL-3.0-or-later
// Stub — real tests land in Phase 4A when LiveActionController gains zoom actions.
#include <QTest>
class TstFontScaleActionsStub : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void placeholder() { QVERIFY(true); }
};
QTEST_GUILESS_MAIN(TstFontScaleActionsStub)
#include "tst_live_render_font_scale_actions.moc"
