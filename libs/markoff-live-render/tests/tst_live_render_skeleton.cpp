// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live-render/Version.h>

class TstLiveRenderSkeleton : public QObject {
    Q_OBJECT
private Q_SLOTS:

    /// Confirms the library links and the test-infrastructure is wired.
    /// Replaced by real tests as R2 onwards add real public surfaces.
    void library_links_and_version_is_zero() {
        QCOMPARE(Markoff::LiveRender::version(), quint32{0});
    }
};

QTEST_MAIN(TstLiveRenderSkeleton)
#include "tst_live_render_skeleton.moc"
