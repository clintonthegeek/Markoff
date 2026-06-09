// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live/Version.h>

class TstLiveRenderSkeleton : public QObject {
    Q_OBJECT
private Q_SLOTS:

    /// Confirms the library links and the test-infrastructure is wired.
    /// version() encodes the CMake project version (major*10000 +
    /// minor*100 + patch); bump this pin alongside the version in
    /// libs/markoff-live/CMakeLists.txt.
    void library_links_and_version_encodes_project_version() {
        QCOMPARE(Markoff::Live::version(), quint32{700});  // 0.7.0
    }
};

QTEST_MAIN(TstLiveRenderSkeleton)
#include "tst_live_render_skeleton.moc"
