// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/ScenarioRunner.h>

#include <markoff-parser/Document.h>
#include <markoff-parser/TreeSitterParser.h>

// Foundation-internal — accessible because markoff_bench has PRIVATE
// include access into libs/markoff-foundation/src/.
#include <IncrementalParseSession.h>

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

    const auto t0 = std::chrono::steady_clock::now();

    // For Phase-0 of this plan, total parse cost is bucketed into ParseBlock.
    // Per-phase splits land in a follow-up if profile data motivates them.
    {
        PhaseTimer guard(iter.phases, Phase::ParseBlock);
        session.applyEdit(QString::fromUtf8(newDoc));
    }

    const auto t1 = std::chrono::steady_clock::now();
    iter.totalNs = static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    // Reuse counters are wired in Task 11 once IncrementalParseSession
    // exposes its TreeSitterParser. For now, emit zeros so the JSON
    // schema stays consistent.
    auto snap = session.snapshot();
    Q_UNUSED(snap);

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
                // blockChangedBytes / inlineReuseCount: emit zeros for now;
                // wired in Task 11.
                blockChangedBytes.push_back(0);
                inlineReuse.push_back(0);
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

}  // namespace Markoff::Bench
