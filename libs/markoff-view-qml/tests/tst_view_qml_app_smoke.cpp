// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTest>

class TstViewQmlAppSmoke : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void app_launches_with_no_qml_warnings() {
        // Locate the binary. Built next to this test under build-dev/bin/.
        // Walk up from the working dir to find build-dev/bin/markoff-view-qml-app.
        QString binPath;
        for (QDir d = QDir::current(); !d.isRoot(); d.cdUp()) {
            const QString candidate = d.filePath("build-dev/bin/markoff-view-qml-app");
            if (QFile::exists(candidate)) { binPath = candidate; break; }
        }
        if (binPath.isEmpty()) {
            QSKIP("markoff-view-qml-app binary not found relative to working dir");
        }

        // Write a tiny markdown file to a temp.
        QTemporaryFile seed;
        seed.setAutoRemove(true);
        QVERIFY(seed.open());
        seed.write("# Hello\n\nThis is **markoff-view-qml**.\n");
        seed.flush();

        // Launch the app under offscreen platform with a short timeout.
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        // Don't touch QT_QUICK_CONTROLS_STYLE — let the test exercise the
        // app's own setStyle() call. If Plasma sets the env var system-wide,
        // QQuickStyle::setStyle() should still win.
        proc.setProcessEnvironment(env);
        proc.setProgram(binPath);
        proc.setArguments({ seed.fileName() });
        proc.start();
        QVERIFY(proc.waitForStarted(2000));

        // Let the app run for ~1.5 seconds so QML loads + initial render fires.
        QVERIFY(!proc.waitForFinished(1500));  // expected NOT to finish — still running
        proc.kill();
        proc.waitForFinished(2000);

        const QByteArray stderrOutput = proc.readAllStandardError();

        // Allow-list: known harmless lines we tolerate.
        // (Add more here only with a clear justification — defaults to nothing.)
        const QList<QByteArray> allowedSubstrings = {
            // Empty by design. If a benign Qt warning becomes noisy, add it
            // here with a comment explaining WHY it's benign.
        };

        // Failure phrases — any of these in stderr means the test failed.
        const QList<QByteArray> failurePhrases = {
            "Unable to assign",
            "TypeError",
            "is not a function",
            "module not found",
            "is not defined",
            "Failed to load",
            "QQmlApplicationEngine failed",
        };

        const QList<QByteArray> stderrLines = stderrOutput.split('\n');
        for (const QByteArray &line : stderrLines) {
            if (line.trimmed().isEmpty()) continue;
            bool allowed = false;
            for (const QByteArray &okSub : allowedSubstrings) {
                if (line.contains(okSub)) { allowed = true; break; }
            }
            if (allowed) continue;

            for (const QByteArray &badSub : failurePhrases) {
                if (line.contains(badSub)) {
                    qWarning() << "Bad stderr line:" << line;
                    QFAIL(qPrintable(QStringLiteral("App emitted QML failure: %1")
                                    .arg(QString::fromUtf8(line))));
                }
            }
        }
    }
};

QTEST_MAIN(TstViewQmlAppSmoke)
#include "tst_view_qml_app_smoke.moc"
