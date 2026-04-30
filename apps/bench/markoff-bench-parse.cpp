// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>
#include <QTextStream>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/FixtureLoader.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/Scenario.h>
#include <markoff-bench/ScenarioRunner.h>

namespace {

QList<Markoff::Bench::CorpusProfile> profilesFromFlags(const QStringList &names) {
    using Markoff::Bench::CorpusProfile;
    QList<CorpusProfile> out;
    auto match = [&](const QString &n) -> int {
        for (int i = 0; i < Markoff::Bench::kCorpusProfileCount; ++i) {
            if (n == Markoff::Bench::profileName(static_cast<CorpusProfile>(i)))
                return i;
        }
        return -1;
    };
    for (const auto &n : names) {
        const int i = match(n);
        if (i < 0) qWarning("unknown profile '%s'", qPrintable(n));
        else out.append(static_cast<CorpusProfile>(i));
    }
    return out;
}

QList<Markoff::Bench::ScenarioKind> scenariosFromFlags(const QStringList &names) {
    using Markoff::Bench::ScenarioKind;
    QList<ScenarioKind> out;
    static const QStringList all = {
        "cold_parse", "type_end", "type_start", "type_middle",
        "block_boundary", "paste_4kb", "replace_1kb"};
    for (const auto &n : names) {
        const int i = all.indexOf(n);
        if (i < 0) qWarning("unknown scenario '%s'", qPrintable(n));
        else out.append(static_cast<ScenarioKind>(i));
    }
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("markoff-bench-parse");

    QCommandLineParser p;
    p.setApplicationDescription("Parse-tier benchmark for the foundation pipeline");
    p.addHelpOption();

    p.addOption({QString("profile"),    "Corpus profile (repeatable). Default: all.",  "name"});
    p.addOption({QString("fixture"),    "Real-doc fixture (repeatable).",              "name"});
    p.addOption({QString("scenario"),   "Scenario (repeatable). Default: all.",        "name"});
    p.addOption({QString("seed"),       "RNG seed (default 0xBEEF).",                 "int", "48879"});
    p.addOption({QString("out"),        "Output JSON path (default: stdout).",        "path"});
    p.addOption({QString("tier"),       "tier1 | tier1b | both (default: both).",     "tier", "both"});
    p.addOption({QString("git-sha"),    "Git SHA to embed in the report.",            "sha", "unknown"});
    p.addOption({QString("build-type"), "Build type to embed.",                       "type", "RelWithDebInfo"});
    p.process(app);

    using namespace Markoff::Bench;

    QStringList profileNames = p.values("profile");
    if (profileNames.isEmpty()) {
        for (int i = 0; i < kCorpusProfileCount; ++i)
            profileNames << profileName(static_cast<CorpusProfile>(i));
    }
    const QList<CorpusProfile> profiles = profilesFromFlags(profileNames);

    const QStringList fixtureNames = p.values("fixture");

    QStringList scenarioNames = p.values("scenario");
    if (scenarioNames.isEmpty()) {
        scenarioNames << "cold_parse" << "type_end" << "type_start" << "type_middle"
                      << "block_boundary" << "paste_4kb" << "replace_1kb";
    }
    const QList<ScenarioKind> scenarios = scenariosFromFlags(scenarioNames);

    const quint64 seed = p.value("seed").toULongLong();
    const QString tierFlag = p.value("tier");
    const bool runDirect = (tierFlag == "tier1" || tierFlag == "both");
    const bool runPool   = (tierFlag == "tier1b" || tierFlag == "both");

    QList<RunResult> results;
    auto pushResult = [&](const QByteArray &corpus,
                          const char *profileLabel,
                          const char *fixtureLabel,
                          ScenarioKind sc) {
        if (runDirect) {
            RunResult r = runDirectParse(corpus, sc, seed);
            r.profileName = profileLabel ? profileLabel : "";
            r.fixtureName = fixtureLabel ? fixtureLabel : "";
            results.append(r);
        }
        if (runPool) {
            RunResult r = runPoolParse(corpus, sc, seed);
            r.profileName = profileLabel ? profileLabel : "";
            r.fixtureName = fixtureLabel ? fixtureLabel : "";
            results.append(r);
        }
    };

    for (auto pr : profiles) {
        const QByteArray corpus = generate(pr, seed);
        const char *label = profileName(pr);     // returns a static string; no lifetime issue
        for (auto sc : scenarios) pushResult(corpus, label, nullptr, sc);
    }

    // Fixture labels need a stable backing store because RunResult holds
    // const char* views into them. Keep them alive for the duration of
    // the JSON build below.
    QList<QByteArray> fixtureLabels;
    for (const QString &fname : fixtureNames) {
        const QByteArray corpus = loadFixture(fname);
        if (corpus.isEmpty()) continue;
        fixtureLabels.append(fname.toUtf8());
        const char *label = fixtureLabels.last().constData();
        for (auto sc : scenarios) pushResult(corpus, nullptr, label, sc);
    }

    const QJsonObject report = toJsonReport(
        results,
        p.value("git-sha"),
        p.value("build-type"));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);

    const QString outPath = p.value("out");
    if (outPath.isEmpty()) {
        QTextStream(stdout) << bytes;
    } else {
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning("cannot open %s", qPrintable(outPath));
            return 1;
        }
        f.write(bytes);
    }
    return 0;
}
