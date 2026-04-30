// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/JsonReporter.h>

#include <QJsonValue>
#include <QSysInfo>

namespace Markoff::Bench {

namespace {
QJsonObject distToJson(const Distribution &d) {
    QJsonObject o;
    o["count"] = static_cast<qint64>(d.count);
    o["min"]   = static_cast<qint64>(d.min);
    o["mean"]  = static_cast<qint64>(d.mean);
    o["p50"]   = static_cast<qint64>(d.p50);
    o["p95"]   = static_cast<qint64>(d.p95);
    o["p99"]   = static_cast<qint64>(d.p99);
    o["max"]   = static_cast<qint64>(d.max);
    return o;
}

const char *tierName(Tier t) {
    switch (t) {
    case Tier::DirectParse: return "direct_parse";
    case Tier::PoolParse:   return "pool_parse";
    case Tier::Render:      return "render";
    }
    return "unknown";
}

const char *phaseJsonKey(int p) {
    switch (static_cast<Phase>(p)) {
    case Phase::Extract:     return "phase_extract";
    case Phase::Diff:        return "phase_diff";
    case Phase::ParseBlock:  return "phase_parse_block";
    case Phase::ParseInline: return "phase_parse_inline";
    case Phase::Queries:     return "phase_queries";
    case Phase::Snapshot:    return "phase_snapshot";
    case Phase::PoolQueue:   return "phase_pool_queue";
    case Phase::SignalHop:   return "phase_signal_hop";
    case Phase::RenderFrame: return "phase_render_frame";
    case Phase::Count:       return "_count";
    }
    return "phase_unknown";
}
}  // namespace

QJsonObject toJson(const RunResult &r) {
    QJsonObject o;
    o["tier"]            = tierName(r.tier);
    o["scenario"]        = r.scenarioName;
    if (r.profileName && r.profileName[0] != 0)  o["corpus_profile"] = r.profileName;
    if (r.fixtureName && r.fixtureName[0] != 0)  o["corpus_fixture"] = r.fixtureName;
    o["iterations"]      = r.iterations;
    o["warmup_iterations"] = r.warmupIters;

    QJsonObject metrics;
    metrics["total_ns"] = distToJson(r.totalNs);
    for (int p = 0; p < kPhaseCount; ++p) {
        metrics[phaseJsonKey(p)] = distToJson(r.phases[p]);
    }
    metrics["block_changed_bytes"] = distToJson(r.blockChangedBytes);
    metrics["inline_reuse_count"]  = distToJson(r.inlineReuseCount);
    metrics["alloc_bytes"]         = distToJson(r.allocBytes);
    metrics["alloc_count"]         = distToJson(r.allocCount);
    o["metrics"] = metrics;
    return o;
}

QJsonObject toJsonReport(const QList<RunResult> &results,
                         const QString &gitSha,
                         const QString &buildType) {
    QJsonObject root;
    root["schema_version"] = 1;
    root["git_sha"]        = gitSha;
    root["build_type"]     = buildType;

    QJsonObject host;
    host["cpu"]    = QSysInfo::currentCpuArchitecture();
    host["kernel"] = QSysInfo::kernelVersion();
    host["qt"]     = QString(qVersion());
    root["host"] = host;

    QJsonArray arr;
    for (const auto &r : results) arr.append(toJson(r));
    root["results"] = arr;
    return root;
}

}  // namespace Markoff::Bench
