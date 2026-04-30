// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tier 2 — render benchmark. Drives the markoff-view-qml MarkoffEditor
// under QT_QPA_PLATFORM=offscreen and times the wall-clock from
// MarkoffDocument::applyLocalEdit() to the next QQuickWindow::frameSwapped
// signal.
//
// Caveats:
//   - Offscreen QPA is not a real GPU — numbers are useful for
//     RELATIVE comparisons across commits, not absolute UX latency.
//   - We drive applyLocalEdit directly instead of QTest::keyClick to
//     avoid fragility around QML focus-routing. This still measures the
//     full render pipeline (document → live block model → scenegraph →
//     frameSwapped); it just bypasses key event delivery.
//   - We load a minimal inline QML root (ApplicationWindow + MarkoffEditor)
//     rather than the app's Main.qml (which belongs to the private
//     org.markoff.view.qml.app module). The render pipeline measured is
//     identical.

#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

#include <chrono>
#include <memory>
#include <vector>

#include <markoff-foundation/CodeBlockProcessorRegistry.h>
#include <markoff-foundation/CompletionRegistry.h>
#include <markoff-foundation/DefaultLinkService.h>
#include <markoff-foundation/EmojiCompletionProvider.h>
#include <markoff-foundation/Kf6SyntaxHighlightService.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

#include <markoff/view/qml/CompletionPopupModel.h>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/PercentileReducer.h>
#include <markoff-bench/Scenario.h>

namespace {

// Inline QML root that uses only the public org.markoff.view.qml module.
// Functionally equivalent to the app's Main.qml but without the private
// AstInspectorPane from org.markoff.view.qml.app.
static const char *kRootQml = R"QML(
import QtQuick
import QtQuick.Controls
import org.markoff.view.qml

ApplicationWindow {
    visible: true
    width: 800
    height: 600
    title: "markoff-bench-render"

    MarkoffEditor {
        anchors.fill: parent
        document: ctxDocument
        theme: ctxTheme
        completionModel: ctxCompletionModel
        mode: typeof startInLiveMode !== "undefined" && startInLiveMode ? "live" : "source"
        focus: true
    }
}
)QML";

/// Wait for the next QQuickWindow::frameSwapped signal, or a timeout.
/// Returns elapsed nanoseconds, or 0 on timeout.
quint64 waitForNextFrame(QQuickWindow *win, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;
    const auto t0 = std::chrono::steady_clock::now();

    auto conn = QObject::connect(win, &QQuickWindow::frameSwapped, &loop,
                                 [&]() { fired = true; loop.quit(); });
    auto *guard = new QObject;
    QTimer::singleShot(timeoutMs, guard, [&]() {
        if (!fired) loop.quit();
    });

    loop.exec();

    delete guard;
    QObject::disconnect(conn);

    if (!fired) return 0;

    const auto t1 = std::chrono::steady_clock::now();
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

QByteArray applyEditToBuffer(const QByteArray &doc, const Markoff::MarkoffEdit &e)
{
    QByteArray out;
    out.reserve(doc.size() - (int(e.oldEnd) - int(e.oldStart)) + e.newText.size());
    out.append(doc.constData(), e.oldStart);
    out.append(e.newText);
    out.append(doc.constData() + e.oldEnd, doc.size() - int(e.oldEnd));
    return out;
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("markoff-bench-render");

    QCommandLineParser p;
    p.setApplicationDescription("Render-tier benchmark for markoff-view-qml");
    p.addHelpOption();
    p.addOption({"profile",  "Corpus profile (default mid_prose).", "name", "mid_prose"});
    p.addOption({"scenario", "Scenario (default type_end).",        "name", "type_end"});
    p.addOption({"out",      "Output JSON path (default stdout).",  "path"});
    p.addOption({"git-sha",  "Git SHA.",                            "sha", "unknown"});
    p.process(app);

    using namespace Markoff::Bench;

    // 1) Resolve the requested profile.
    int profileIdx = -1;
    for (int i = 0; i < kCorpusProfileCount; ++i) {
        if (p.value("profile") == profileName(static_cast<CorpusProfile>(i)))
            profileIdx = i;
    }
    if (profileIdx < 0) { qWarning("unknown profile"); return 2; }
    const QByteArray corpus = generate(static_cast<CorpusProfile>(profileIdx), 0xBEEF);

    // 2) Resolve scenario.
    static const QStringList scenarioNames = {
        "cold_parse", "type_end", "type_start", "type_middle",
        "block_boundary", "paste_4kb", "replace_1kb"};
    const int scenarioIdx = scenarioNames.indexOf(p.value("scenario"));
    if (scenarioIdx < 0) { qWarning("unknown scenario"); return 3; }
    const auto kind = static_cast<ScenarioKind>(scenarioIdx);

    // 3) Build the foundation services exactly like the real app does.
    auto syntax         = std::make_unique<Markoff::Kf6SyntaxHighlightService>();
    auto codeProcessors = std::make_unique<Markoff::CodeBlockProcessorRegistry>();
    auto links          = std::make_unique<Markoff::DefaultLinkService>();
    auto completion     = std::make_unique<Markoff::CompletionRegistry>();
    completion->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    const quint16 replicaId =
        static_cast<quint16>(QRandomGenerator::global()->generate() & 0xFFFF);
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    doc->resetContent(corpus, Markoff::Origin::FirstOpen);

    auto popupModel = std::make_unique<Markoff::View::Qml::CompletionPopupModel>();
    popupModel->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    // 4) Load the QML root via inline QML string (avoids dependency on the
    //    private org.markoff.view.qml.app module while testing the same pipeline).
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("ctxDocument",      doc.get());
    engine.rootContext()->setContextProperty(
        "ctxTheme", QVariant::fromValue(Markoff::Theme::defaultLight()));
    engine.rootContext()->setContextProperty("ctxCompletionModel", popupModel.get());
    engine.rootContext()->setContextProperty(QStringLiteral("startInLiveMode"),
                                             QVariant(false));

    engine.loadData(QByteArray(kRootQml));
    if (engine.rootObjects().isEmpty()) { qWarning("QML load failed"); return 4; }

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!win) { qWarning("root is not a QQuickWindow"); return 5; }

    // 5) Wait for the initial render. Without this, the first measured
    //    iteration would include first-frame setup cost.
    waitForNextFrame(win, 30'000);

    // 6) Run the scenario. We drive applyLocalEdit directly; the QML view
    //    observes the document via parseUpdated and rebuilds the scene.
    const ScenarioMeta meta = scenarioMeta(kind);
    std::vector<quint64> latencies;
    QByteArray currentDoc = corpus;

    if (kind == ScenarioKind::ColdParse) {
        // Time a fresh resetContent + frame swap.
        Markoff::MarkoffDocument fresh(static_cast<quint16>(replicaId + 1));
        const auto t0 = std::chrono::steady_clock::now();
        fresh.resetContent(corpus, Markoff::Origin::FirstOpen);
        engine.rootContext()->setContextProperty("ctxDocument", &fresh);
        waitForNextFrame(win, 30'000);
        const auto t1 = std::chrono::steady_clock::now();
        latencies.push_back(static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        // Restore original document binding.
        engine.rootContext()->setContextProperty("ctxDocument", doc.get());
    } else {
        const int totalIters = meta.warmupIters + meta.measuredIters;
        for (int i = 0; i < totalIters; ++i) {
            const Markoff::MarkoffEdit edit =
                nextStep(kind, currentDoc, i, /*seed*/ 0xBEEF);
            currentDoc = applyEditToBuffer(currentDoc, edit);

            const auto t0 = std::chrono::steady_clock::now();
            doc->applyLocalEdit({edit});
            waitForNextFrame(win, 5'000);
            const auto t1 = std::chrono::steady_clock::now();

            if (i >= meta.warmupIters) {
                latencies.push_back(static_cast<quint64>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
            }
        }
    }

    RunResult r;
    r.profileName  = profileName(static_cast<CorpusProfile>(profileIdx));
    r.scenarioName = meta.name;
    r.tier         = Tier::Render;
    r.iterations   = static_cast<int>(latencies.size());
    r.warmupIters  = (kind == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
    r.totalNs      = reducePercentiles(latencies);
    r.phases[static_cast<int>(Phase::RenderFrame)] = reducePercentiles(latencies);

    const QJsonObject report =
        toJsonReport({r}, p.value("git-sha"), QStringLiteral("RelWithDebInfo"));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);

    const QString outPath = p.value("out");
    if (outPath.isEmpty()) {
        QTextStream(stdout) << bytes;
    } else {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(bytes);
    }
    return 0;
}
