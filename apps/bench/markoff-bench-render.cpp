// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tier 2 — render benchmark. Drives the markoff-view-qml MarkoffEditor under
// QT_QPA_PLATFORM=offscreen and times the wall-clock from
// MarkoffDocument::applyLocalEdit() to the next QQuickWindow::frameSwapped.
//
// Phase splits captured per iteration (see docs/handoff/2026-04-30-render-tier-
// instrumentation-SESSION-BRIEF.md):
//
//   phase_apply_edit   — synchronous time inside applyLocalEdit()
//                        (CRDT + SourceTextDocumentBinding QTextDoc rebuild +
//                         KSyntaxHighlighter rehighlight + parsePool.schedule).
//   phase_pool_queue   — applyLocalEdit return → ParsePool worker entry.
//   phase_parse_work   — worker entry → worker `parsed` signal emit
//                        (lump-sum; Tier 1 splits this into extract/diff/
//                         parse_block/parse_inline/queries/snapshot).
//   phase_signal_hop   — worker emit → main-thread receipt.
//   phase_model_update — main-thread receipt → all DirectConnection slots
//                        returned (e.g. LiveListModelBinding finished).
//   phase_render_frame — model settled → next QQuickWindow::frameSwapped.
//
// Sum of the six = total_ns ±5 % (steady_clock capture overhead is the
// remaining slack).
//
// Caveats:
//   - Offscreen QPA is not a real GPU — numbers are useful for RELATIVE
//     comparisons across commits, not absolute UX latency.
//   - The bench runs in **live mode** by default. The four-phase taxonomy in
//     the brief is sequential only when the parse is on the critical path of
//     the render — true in live mode (LiveBlockModel needs the parsed
//     Document); in source mode the QTextDocument is updated synchronously
//     inside applyLocalEdit and the next frame can swap before the parse
//     completes, which makes phase_render_frame potentially negative. Pass
//     --mode source to opt out of live mode and compare against the original
//     single-bucket render bench; phases will be best-effort and may not
//     sum cleanly.
//   - We drive applyLocalEdit directly instead of QTest::keyClick to avoid
//     fragility around QML focus-routing. This still measures the full render
//     pipeline; it just bypasses key event delivery.
//   - We load a minimal inline QML root rather than the app's Main.qml.

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
#include <markoff-foundation/RenderPhases.h>
#include <markoff-foundation/Theme.h>

#include <markoff/view/qml/CompletionPopupModel.h>

#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/JsonReporter.h>
#include <markoff-bench/PercentileReducer.h>
#include <markoff-bench/Scenario.h>

namespace {

// Inline QML root that uses only the public org.markoff.view.qml module.
// Matches the app's Main.qml topology but without the private
// AstInspectorPane from org.markoff.view.qml.app. Mode is wired to a context
// property so the bench frontend can pick source/live per CLI flag.
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
        mode: ctxMode
        focus: true
    }
}
)QML";

/// Wait for the next QQuickWindow::frameSwapped signal, or a timeout.
/// Returns the absolute steady_clock nanoseconds at which the frame swapped,
/// or 0 on timeout.
quint64 waitForNextFrameAbsNs(QQuickWindow *win, int timeoutMs)
{
    QEventLoop loop;
    quint64 frameNs = 0;

    auto conn = QObject::connect(win, &QQuickWindow::frameSwapped, &loop,
                                 [&]() {
                                     if (frameNs == 0) frameNs = Markoff::Render::nowNs();
                                     loop.quit();
                                 });
    auto *guard = new QObject;
    QTimer::singleShot(timeoutMs, guard, [&]() {
        if (frameNs == 0) loop.quit();
    });

    loop.exec();

    delete guard;
    QObject::disconnect(conn);

    return frameNs;
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

quint64 deltaClampNonNeg(quint64 to, quint64 from) noexcept
{
    return (to >= from) ? (to - from) : 0u;
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
    p.addOption({"profile",  "Corpus profile (repeatable). Default: all profiles.", "name"});
    p.addOption({"scenario", "Scenario (repeatable). Default: type_end + paste_4kb + cold_parse.", "name"});
    p.addOption({"mode",     "QML view mode: live (default) or source.", "mode", "live"});
    p.addOption({"out",      "Output JSON path (default stdout).",       "path"});
    p.addOption({"git-sha",  "Git SHA.",                                 "sha", "unknown"});
    p.process(app);

    using namespace Markoff::Bench;

    // ----- Resolve --mode -----
    const QString modeFlag = p.value("mode");
    if (modeFlag != "live" && modeFlag != "source") {
        qWarning("--mode must be 'live' or 'source'");
        return 1;
    }
    const bool startInLive = (modeFlag == "live");

    // ----- Resolve --profile (repeatable) -----
    QStringList profileNames = p.values("profile");
    if (profileNames.isEmpty()) {
        for (int i = 0; i < kCorpusProfileCount; ++i)
            profileNames << profileName(static_cast<CorpusProfile>(i));
    }
    QList<int> profileIdxs;
    for (const QString &n : profileNames) {
        int idx = -1;
        for (int i = 0; i < kCorpusProfileCount; ++i) {
            if (n == profileName(static_cast<CorpusProfile>(i))) { idx = i; break; }
        }
        if (idx < 0) { qWarning("unknown profile '%s'", qPrintable(n)); return 2; }
        profileIdxs << idx;
    }

    // ----- Resolve --scenario (repeatable) -----
    static const QStringList scenarioNames = {
        "cold_parse", "type_end", "type_start", "type_middle",
        "block_boundary", "paste_4kb", "replace_1kb"};
    QStringList wantScenarios = p.values("scenario");
    if (wantScenarios.isEmpty()) {
        // Render-tier defaults match the design spec (§ Scenarios): only the
        // three meaningful scenarios for an offscreen-QPA render bench.
        wantScenarios = {"type_end", "paste_4kb", "cold_parse"};
    }
    QList<int> scenarioIdxs;
    for (const QString &n : wantScenarios) {
        const int idx = scenarioNames.indexOf(n);
        if (idx < 0) { qWarning("unknown scenario '%s'", qPrintable(n)); return 3; }
        scenarioIdxs << idx;
    }

    // ----- Build foundation services + initial document -----
    auto syntax         = std::make_unique<Markoff::Kf6SyntaxHighlightService>();
    auto codeProcessors = std::make_unique<Markoff::CodeBlockProcessorRegistry>();
    auto links          = std::make_unique<Markoff::DefaultLinkService>();
    auto completion     = std::make_unique<Markoff::CompletionRegistry>();
    completion->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    auto popupModel = std::make_unique<Markoff::View::Qml::CompletionPopupModel>();
    popupModel->registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

    // We construct a fresh MarkoffDocument per profile rather than reusing one
    // across resetContent calls. The buffer's CRDT global-version-vector
    // accumulates state across reuses in a way that intermittently faults
    // inside `Global::observe` on the next apply_local_edit (out of mandate
    // for this work-unit; tracked by the collabtext team). Constructing fresh
    // sidesteps it without changing what we measure.
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        "ctxTheme", QVariant::fromValue(Markoff::Theme::defaultLight()));
    engine.rootContext()->setContextProperty("ctxCompletionModel", popupModel.get());
    engine.rootContext()->setContextProperty(QStringLiteral("ctxMode"),
                                             QVariant(startInLive ? "live" : "source"));

    // ----- Run every (profile, scenario) combination -----
    QList<RunResult> results;
    constexpr quint64 seed = 0xBEEF;

    Markoff::Render::RenderPhaseTaps taps;
    QQuickWindow *win = nullptr;

    for (int profileIdx : profileIdxs) {
        const auto profile = static_cast<CorpusProfile>(profileIdx);
        const QByteArray corpus = generate(profile, seed);

        // Fresh document per profile, fresh QML root binding. We use a small
        // monotonic replicaId rather than a random uint16 because the latter
        // forces the CRDT Global vector onto the heap on first observe (the
        // SBO capacity is 4); a recent collabtext SBO regression intermittently
        // faults on the first heap-bound observe path. Small replica IDs keep
        // the version vector inline and sidestep the regression for the
        // duration of this work-unit. Reported to the collabtext team.
        static quint16 sReplicaSeq = 1;
        const quint16 replicaId = ++sReplicaSeq;
        auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
        doc->resetContent(corpus, Markoff::Origin::FirstOpen);

        engine.rootContext()->setContextProperty("ctxDocument", doc.get());

        if (!win) {
            // First profile: load the QML root once. Subsequent profiles
            // re-bind the existing window via the ctxDocument property.
            engine.loadData(QByteArray(kRootQml));
            if (engine.rootObjects().isEmpty()) { qWarning("QML load failed"); return 4; }
            win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!win) { qWarning("root is not a QQuickWindow"); return 5; }
        }

        doc->setRenderPhaseTaps(&taps);

        // Wait for parse + initial render to settle.
        waitForNextFrameAbsNs(win, 30'000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        for (int scenarioIdx : scenarioIdxs) {
            const auto kind = static_cast<ScenarioKind>(scenarioIdx);
            const ScenarioMeta meta = scenarioMeta(kind);

            // Per-iter sample arrays.
            std::vector<quint64> totalNs;
            std::array<std::vector<quint64>, kPhaseCount> phaseSamples;

            QByteArray currentDoc = doc->toMarkdownUtf8();

            if (kind == ScenarioKind::ColdParse) {
                // Cold parse: a fresh document, time keystroke→frame is
                // really resetContent→frame. We don't have phase taps for
                // resetContent under live-mode initial render (the taps
                // capture only parse-pool round trips), so emit a single
                // bucket sample under render_frame.
                auto fresh = std::make_unique<Markoff::MarkoffDocument>(
                    static_cast<quint16>(replicaId + 1));
                fresh->setRenderPhaseTaps(&taps);
                taps.reset();
                const quint64 t0 = Markoff::Render::nowNs();
                fresh->resetContent(corpus, Markoff::Origin::FirstOpen);
                engine.rootContext()->setContextProperty("ctxDocument", fresh.get());
                const quint64 tFrame = waitForNextFrameAbsNs(win, 30'000);
                if (tFrame > 0) {
                    const quint64 total = tFrame - t0;
                    totalNs.push_back(total);
                    phaseSamples[static_cast<int>(Phase::RenderFrame)].push_back(total);
                }
                // Restore original document binding.
                fresh->setRenderPhaseTaps(nullptr);
                engine.rootContext()->setContextProperty("ctxDocument", doc.get());
                // Wait for the rebind to render so the next scenario starts clean.
                waitForNextFrameAbsNs(win, 30'000);
            } else {
                const int totalIters = meta.warmupIters + meta.measuredIters;
                for (int i = 0; i < totalIters; ++i) {
                    const Markoff::MarkoffEdit edit =
                        nextStep(kind, currentDoc, i, seed);
                    currentDoc = applyEditToBuffer(currentDoc, edit);

                    taps.reset();
                    const quint64 t0           = Markoff::Render::nowNs();
                    doc->applyLocalEdit({edit});
                    const quint64 tApplyDone   = Markoff::Render::nowNs();
                    const quint64 tFrame       = waitForNextFrameAbsNs(win, 5'000);
                    if (tFrame == 0) {
                        // No frame within the timeout — likely the parse is
                        // still in flight or the QPA didn't schedule. Skip.
                        continue;
                    }

                    const quint64 tWorkerEntry  =
                        taps.tWorkerEntryNs.load(std::memory_order_acquire);
                    const quint64 tWorkerEmit   =
                        taps.tWorkerEmitNs.load(std::memory_order_acquire);
                    const quint64 tMainSlot     =
                        taps.tMainSlotEntryNs.load(std::memory_order_acquire);
                    const quint64 tModelDone    =
                        taps.tModelDoneNs.load(std::memory_order_acquire);

                    if (i < meta.warmupIters) continue;

                    const quint64 total = tFrame - t0;
                    totalNs.push_back(total);

                    // apply_edit and pool_queue are always defined.
                    phaseSamples[static_cast<int>(Phase::ApplyEdit)]
                        .push_back(deltaClampNonNeg(tApplyDone, t0));

                    if (tWorkerEntry > 0) {
                        phaseSamples[static_cast<int>(Phase::PoolQueue)]
                            .push_back(deltaClampNonNeg(tWorkerEntry, tApplyDone));
                    }
                    if (tWorkerEntry > 0 && tWorkerEmit > 0) {
                        phaseSamples[static_cast<int>(Phase::ParseWork)]
                            .push_back(deltaClampNonNeg(tWorkerEmit, tWorkerEntry));
                    }
                    if (tWorkerEmit > 0 && tMainSlot > 0) {
                        phaseSamples[static_cast<int>(Phase::SignalHop)]
                            .push_back(deltaClampNonNeg(tMainSlot, tWorkerEmit));
                    }
                    if (tMainSlot > 0 && tModelDone > 0) {
                        phaseSamples[static_cast<int>(Phase::ModelUpdate)]
                            .push_back(deltaClampNonNeg(tModelDone, tMainSlot));
                    }
                    if (tModelDone > 0) {
                        phaseSamples[static_cast<int>(Phase::RenderFrame)]
                            .push_back(deltaClampNonNeg(tFrame, tModelDone));
                    }
                }
            }

            RunResult r;
            r.profileName  = profileName(profile);
            r.scenarioName = meta.name;
            r.tier         = Tier::Render;
            r.iterations   = static_cast<int>(totalNs.size());
            r.warmupIters  = (kind == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
            r.totalNs      = reducePercentiles(totalNs);
            for (int ph = 0; ph < kPhaseCount; ++ph) {
                r.phases[ph] = reducePercentiles(phaseSamples[ph]);
            }
            results.append(r);
        }

        // Detach taps before this profile's doc goes out of scope. The QML
        // root will rebind to the next profile's document via the
        // ctxDocument context property at the top of the next iteration.
        doc->setRenderPhaseTaps(nullptr);
        // Drop the QML root's reference to this doc before destruction.
        engine.rootContext()->setContextProperty(
            "ctxDocument", static_cast<QObject *>(nullptr));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    const QJsonObject report =
        toJsonReport(results, p.value("git-sha"), QStringLiteral("RelWithDebInfo"));
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
