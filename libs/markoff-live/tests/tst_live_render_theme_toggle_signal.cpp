// SPDX-License-Identifier: GPL-3.0-or-later
// Stub — real tests land in Phase 4B when LiveActionController gains themeToggleRequested.
#include <QTest>
class TstThemeToggleSignalStub : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void placeholder() { QVERIFY(true); }
};
QTEST_GUILESS_MAIN(TstThemeToggleSignalStub)
#include "tst_live_render_theme_toggle_signal.moc"
