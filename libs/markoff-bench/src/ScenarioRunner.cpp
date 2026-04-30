// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/ScenarioRunner.h>

#include <markoff-parser/Document.h>
#include <markoff-parser/TreeSitterParser.h>

// Foundation-internal — accessible because markoff_bench has PRIVATE
// include access into libs/markoff-foundation/src/.
#include <IncrementalParseSession.h>
#include <ParsePhases.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

#include <chrono>

namespace Markoff::Bench {

namespace {

QByteArray applyEditToBuffer(const QByteArray &doc, const Markoff::MarkoffEdit &e) {
    QByteArray out;
    out.reserve(doc.size() - (int(e.oldEnd) - int(e.oldStart)) + e.newText.size());
    out.append(doc.constData(), e.oldStart);
    out.append(e.newText);
    out.append(doc.constData() + e.oldEnd, doc.size() - int(e.oldEnd));
    return out;
}

struct PerIter {
    PhaseTable     phases{};
    quint64        totalNs            = 0;
    int            blockChangedBytes  = -1;
    int            inlineReuseCount   = 0;
    AllocSnapshot  alloc{};
};

PerIter timeOneIter(Markoff::Parse::Detail::IncrementalParseSession &session,
                    const QByteArray &newDoc)
{
    PerIter iter;

    AllocCounterScope allocScope;

    // Foundation accumulates phase nanoseconds into this table; we reset it
    // per-iter and copy out into the bench PhaseTable below. Foundation
    // ParsePhase indices 0..5 align with bench Phase indices 0..5 by
    // construction (Extract / Diff / ParseBlock / ParseInline / Queries /
    // Snapshot) — kept in sync intentionally.
    Markoff::Parse::Detail::ParsePhaseTable foundationPhases{};
    session.setPhaseTable(&foundationPhases);

    const auto t0 = std::chrono::steady_clock::now();
    session.applyEdit(QString::fromUtf8(newDoc));
    auto snap = session.snapshot();
    const auto t1 = std::chrono::steady_clock::now();

    Q_UNUSED(snap);

    iter.totalNs = static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    // Copy parse-side phase totals into the bench PhaseTable. Bench phases
    // 6..8 (PoolQueue / SignalHop / RenderFrame) stay 0 on Tier 1.
    static_assert(static_cast<int>(Markoff::Parse::Detail::ParsePhase::Count)
                      <= kPhaseCount,
                  "bench PhaseTable must hold every foundation parse phase");
    for (int i = 0; i < Markoff::Parse::Detail::kParsePhaseCount; ++i) {
        iter.phases[i] = foundationPhases[i];
    }

    session.setPhaseTable(nullptr);

    iter.alloc = currentAllocSnapshot();
    return iter;
}

}  // namespace

RunResult runDirectParse(const QByteArray &corpus, ScenarioKind scenario, quint64 seed) {
    const ScenarioMeta meta = scenarioMeta(scenario);

    Markoff::Parse::Detail::IncrementalParseSession session;
    session.reset(QString::fromUtf8(corpus));

    QByteArray currentDoc = corpus;

    std::vector<quint64> totalNs;
    std::array<std::vector<quint64>, kPhaseCount> phaseSamples;
    std::vector<quint64> blockChangedBytes;
    std::vector<quint64> inlineReuse;
    std::vector<quint64> allocBytes;
    std::vector<quint64> allocCount;

    const int totalIters = meta.warmupIters + meta.measuredIters;

    if (scenario == ScenarioKind::ColdParse) {
        // Single-shot: time a fresh reset() against the corpus.
        Markoff::Parse::Detail::IncrementalParseSession s;
        AllocCounterScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();
        s.reset(QString::fromUtf8(corpus));
        const auto t1 = std::chrono::steady_clock::now();
        const auto ns = static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        totalNs.push_back(ns);
        const auto alloc = currentAllocSnapshot();
        allocBytes.push_back(alloc.bytes);
        allocCount.push_back(alloc.count);
    } else {
        for (int i = 0; i < totalIters; ++i) {
            const Markoff::MarkoffEdit edit = nextStep(scenario, currentDoc, i, seed);
            currentDoc = applyEditToBuffer(currentDoc, edit);

            const PerIter iter = timeOneIter(session, currentDoc);

            if (i >= meta.warmupIters) {
                totalNs.push_back(iter.totalNs);
                for (int p = 0; p < kPhaseCount; ++p) phaseSamples[p].push_back(iter.phases[p]);
                allocBytes.push_back(iter.alloc.bytes);
                allocCount.push_back(iter.alloc.count);
                const int changed = session.parser().blockChangedByteCount();
                const int inlineR = session.parser().inlineTreeReuseCount();
                blockChangedBytes.push_back(changed < 0 ? 0u : static_cast<quint64>(changed));
                inlineReuse.push_back(static_cast<quint64>(inlineR));
            }
        }
    }

    RunResult r;
    r.profileName    = "";
    r.fixtureName    = "";
    r.scenarioName   = meta.name;
    r.tier           = Tier::DirectParse;
    r.iterations     = static_cast<int>(totalNs.size());
    r.warmupIters    = (scenario == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
    for (int p = 0; p < kPhaseCount; ++p) r.phases[p] = reducePercentiles(phaseSamples[p]);
    r.totalNs            = reducePercentiles(totalNs);
    r.blockChangedBytes  = reducePercentiles(blockChangedBytes);
    r.inlineReuseCount   = reducePercentiles(inlineReuse);
    r.allocBytes         = reducePercentiles(allocBytes);
    r.allocCount         = reducePercentiles(allocCount);
    return r;
}

namespace {
quint64 waitForParseUpdated(Markoff::MarkoffDocument *doc, int timeoutMs) {
    QSignalSpy spy(doc, &Markoff::MarkoffDocument::parseUpdated);
    const auto t0 = std::chrono::steady_clock::now();
    if (!spy.wait(timeoutMs)) {
        qWarning("bench: parseUpdated did not fire within %d ms", timeoutMs);
        return 0;
    }
    const auto t1 = std::chrono::steady_clock::now();
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}
}  // namespace

RunResult runPoolParse(const QByteArray &corpus, ScenarioKind scenario, quint64 seed) {
    const ScenarioMeta meta = scenarioMeta(scenario);

    // Caller is responsible for ensuring a QCoreApplication exists (the
    // CLI frontend constructs one).
    Q_ASSERT(QCoreApplication::instance() != nullptr);

    Markoff::MarkoffDocument doc(/*replicaId*/ 1);
    doc.resetContent(corpus, Markoff::Origin::TestFixture);
    waitForParseUpdated(&doc, 30'000);   // initial parse can be slow on huge corpora

    QByteArray currentDoc = corpus;
    std::vector<quint64> totalNs;
    std::vector<quint64> waitNs;             // PoolQueue + SignalHop combined
    std::vector<quint64> allocBytes;
    std::vector<quint64> allocCount;

    const int totalIters = meta.warmupIters + meta.measuredIters;

    if (scenario == ScenarioKind::ColdParse) {
        // For ColdParse at Tier 1b: rebuild the doc and wait for parseUpdated.
        Markoff::MarkoffDocument fresh(/*replicaId*/ 2);
        AllocCounterScope allocScope;
        const auto t0 = std::chrono::steady_clock::now();
        fresh.resetContent(corpus, Markoff::Origin::TestFixture);
        const quint64 wait = waitForParseUpdated(&fresh, 30'000);
        const auto t1 = std::chrono::steady_clock::now();
        const auto ns = static_cast<quint64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        totalNs.push_back(ns);
        waitNs.push_back(wait);
        const auto alloc = currentAllocSnapshot();
        allocBytes.push_back(alloc.bytes);
        allocCount.push_back(alloc.count);
    } else {
        for (int i = 0; i < totalIters; ++i) {
            const Markoff::MarkoffEdit edit = nextStep(scenario, currentDoc, i, seed);
            currentDoc = applyEditToBuffer(currentDoc, edit);

            AllocCounterScope allocScope;
            const auto t0 = std::chrono::steady_clock::now();
            doc.applyLocalEdit({edit});
            const quint64 wait = waitForParseUpdated(&doc, 5'000);
            const auto t1 = std::chrono::steady_clock::now();

            if (i >= meta.warmupIters) {
                totalNs.push_back(static_cast<quint64>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
                waitNs.push_back(wait);
                const auto alloc = currentAllocSnapshot();
                allocBytes.push_back(alloc.bytes);
                allocCount.push_back(alloc.count);
            }
        }
    }

    RunResult r;
    r.scenarioName = meta.name;
    r.tier         = Tier::PoolParse;
    r.iterations   = static_cast<int>(totalNs.size());
    r.warmupIters  = (scenario == ScenarioKind::ColdParse) ? 0 : meta.warmupIters;
    r.totalNs      = reducePercentiles(totalNs);
    // Lump PoolQueue + SignalHop into the SignalHop slot; finer-grained
    // split would require instrumentation in MarkoffDocument itself.
    r.phases[static_cast<int>(Phase::SignalHop)] = reducePercentiles(waitNs);
    r.allocBytes   = reducePercentiles(allocBytes);
    r.allocCount   = reducePercentiles(allocCount);
    return r;
}

}  // namespace Markoff::Bench
