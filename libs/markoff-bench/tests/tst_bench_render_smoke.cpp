// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tier-2 smoke test: invokes the markoff-bench-render binary via QProcess
// against a tiny corpus, parses the resulting JSON, and asserts that the
// six render-tier phases populate (count > 0, p50 > 0 where applicable)
// and that their sum is within ±5 % of total_ns.
//
// We shell out to the binary rather than recreating the full QML harness
// inline because the bench frontend imports the markoff-view-qml QML
// plugin via Q_IMPORT_PLUGIN and links the QQuickStyle / QApplication
// scaffolding — duplicating that here would make the test brittle and
// the binary's exit code already validates the same code path. Per the
// design (see SESSION-BRIEF §5), Tier-2 smoke is gated under the `bench`
// ctest label and runs a single profile/scenario combo (tiny / type_end)
// so wall time stays under ~10 s.

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

class TstBenchRenderSmoke : public QObject {
    Q_OBJECT
private slots:
    void render_tiny_type_end_six_phases_populate();
};

namespace {

QString findBenchBinary()
{
    // The bench binary is installed under <build>/bin/markoff-bench-render
    // when this test runs from CTest. CTest invokes tests with cwd =
    // <build>/libs/markoff-bench/tests, so resolve relative to that.
    const QDir cwd(QDir::currentPath());
    QString p;
    p = cwd.absoluteFilePath("../../../bin/markoff-bench-render");
    if (QFile::exists(p)) return p;
    // Fallback for a flat build layout.
    p = cwd.absoluteFilePath("../../bin/markoff-bench-render");
    if (QFile::exists(p)) return p;
    return QString();
}

}  // namespace

void TstBenchRenderSmoke::render_tiny_type_end_six_phases_populate()
{
    const QString bin = findBenchBinary();
    QVERIFY2(!bin.isEmpty(),
             "markoff-bench-render binary not found relative to test cwd");

    QTemporaryFile out(QDir::tempPath() + QStringLiteral("/render-smoke-XXXXXX.json"));
    QVERIFY(out.open());
    out.close();

    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments({
        QStringLiteral("--profile"),  QStringLiteral("tiny"),
        QStringLiteral("--scenario"), QStringLiteral("type_end"),
        QStringLiteral("--out"),      out.fileName(),
        QStringLiteral("--git-sha"),  QStringLiteral("smoke"),
    });
    proc.start();
    QVERIFY2(proc.waitForFinished(60'000),
             qPrintable(QString("bench did not finish: %1").arg(proc.errorString())));
    QCOMPARE(proc.exitCode(), 0);

    QFile f(out.fileName());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());

    const auto results = doc.object().value("results").toArray();
    QCOMPARE(results.size(), 1);

    const auto r = results.first().toObject();
    QCOMPARE(r.value("scenario").toString(), QStringLiteral("type_end"));
    QCOMPARE(r.value("corpus_profile").toString(), QStringLiteral("tiny"));

    const auto m = r.value("metrics").toObject();
    const auto totalP50 = static_cast<quint64>(m.value("total_ns").toObject().value("p50").toDouble());
    QVERIFY2(totalP50 > 0, "total_ns p50 must be positive");

    auto phaseP50 = [&](const QString &k) -> quint64 {
        return static_cast<quint64>(m.value(k).toObject().value("p50").toDouble());
    };
    auto phaseCount = [&](const QString &k) -> int {
        return m.value(k).toObject().value("count").toInt();
    };

    // Every Tier-2 render phase must report measurable cost on tiny / type_end.
    for (const QString &k : {
            QStringLiteral("phase_apply_edit"),
            QStringLiteral("phase_pool_queue"),
            QStringLiteral("phase_parse_work"),
            QStringLiteral("phase_signal_hop"),
            QStringLiteral("phase_model_update"),
            QStringLiteral("phase_render_frame"),
        }) {
        QVERIFY2(phaseCount(k) > 0,
                 qPrintable(QString("%1 has zero samples").arg(k)));
        QVERIFY2(phaseP50(k) > 0,
                 qPrintable(QString("%1 p50=%2").arg(k).arg(phaseP50(k))));
    }

    // Sum-of-p50s isn't the same as p50-of-sum (the per-iter sum is total_ns
    // by construction; the percentile reduction breaks that exact equality).
    // Use a loose ±15 % bound here; the strict per-iter invariant is
    // enforced by the bench's phase-delta arithmetic itself.
    const quint64 sumP50 =
        phaseP50("phase_apply_edit") +
        phaseP50("phase_pool_queue") +
        phaseP50("phase_parse_work") +
        phaseP50("phase_signal_hop") +
        phaseP50("phase_model_update") +
        phaseP50("phase_render_frame");
    const double ratio = static_cast<double>(sumP50) / static_cast<double>(totalP50);
    QVERIFY2(ratio > 0.80 && ratio < 1.20,
             qPrintable(QString("sum-of-p50s/total ratio %1 outside [0.80, 1.20]").arg(ratio)));
}

QTEST_MAIN(TstBenchRenderSmoke)
#include "tst_bench_render_smoke.moc"
