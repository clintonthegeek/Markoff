// SPDX-License-Identifier: GPL-3.0-or-later
//
// Diagnostic load benchmark for markoff-core. Measures
// `MarkoffDocument::loadFromMarkdown` wall time and RSS for a given
// `.md` file, plus a follow-up cold-path sweep over `inlineSpansFor`
// (the path `LiveListModelBinding::onD2Changed` takes during
// record-build).
//
// Not registered with ctest. Run manually:
//
//   QT_QPA_PLATFORM=offscreen \
//     MARKOFF_REPLICA_ID=1 \
//     ./build-dev/bin/perf_load_bench <markdown-file>
//
// Output (qInfo) is routed by Qt's default handler; on a Wayland
// session it lands in the systemd journal, not stderr. Tail with:
//
//   journalctl --user --since "10 seconds ago" | grep "bench\]"
//
// Surfaced 2026-05-09 as the tool that pinned a CollabText::Crdt::Buffer
// O(replicaId) regression — see docs/handoff/2026-05-09-collabtext-replica-id-perf.md.

#include <QApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QDebug>

#include <markoff/core/MarkoffDocument.h>

#include <cstdlib>
#include <fstream>
#include <string>

static long readProcStatusKb(const char *key)
{
    std::ifstream f("/proc/self/status");
    std::string line;
    const std::string prefix = std::string(key) + ":";
    while (std::getline(f, line))
        if (line.rfind(prefix, 0) == 0) {
            long kb = 0;
            sscanf(line.c_str() + prefix.size(), "%ld", &kb);
            return kb;
        }
    return -1;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        qWarning("Usage: %s <markdown-file>", argv[0]);
        qWarning("Optional env: MARKOFF_REPLICA_ID=<uint16>  (default 1)");
        return 1;
    }
    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Cannot open %s", argv[1]);
        return 1;
    }
    const QByteArray content = file.readAll();

    quint16 replicaId = 1;
    if (const char *env = std::getenv("MARKOFF_REPLICA_ID"))
        replicaId = static_cast<quint16>(std::atoi(env));

    const long rssBefore = readProcStatusKb("VmRSS");
    qInfo().noquote() << QString(
        "[bench] file=%1 size=%2 bytes  replicaId=%3  rss-before=%4 KB")
        .arg(argv[1]).arg(content.size()).arg(replicaId).arg(rssBefore);

    QElapsedTimer t;
    t.start();
    auto doc = std::make_unique<Markoff::MarkoffDocument>(replicaId);
    const qint64 ctorMs = t.elapsed();

    QElapsedTimer t2;
    t2.start();
    doc->loadFromMarkdown(content);
    const qint64 loadMs = t2.elapsed();

    const long rssAfterLoad = readProcStatusKb("VmRSS");

    const auto blocks = doc->iterateBlocks();

    // Mirror what LiveListModelBinding::onD2Changed does during
    // record-build: fetch inline spans for every block. Cold-path
    // because each call constructs a fresh TreeSitterParser.
    QElapsedTimer t3;
    t3.start();
    qsizetype totalSpans = 0;
    for (const auto &id : blocks)
        totalSpans += doc->inlineSpansFor(id).size();
    const qint64 inlineMs = t3.elapsed();

    const long rssAfter = readProcStatusKb("VmRSS");
    const long vmPeak = readProcStatusKb("VmPeak");

    qInfo().noquote() << QString(
        "[bench] inlineSpansForAll=%1 ms  totalSpans=%2  rss-delta-after-inline=%3 KB  per-block-avg=%4 ms")
        .arg(inlineMs)
        .arg(totalSpans)
        .arg(rssAfter - rssAfterLoad)
        .arg(blocks.empty() ? 0.0 : double(inlineMs) / double(blocks.size()), 0, 'f', 2);

    qInfo().noquote() << QString(
        "[bench] ctor=%1 ms  loadFromMarkdown=%2 ms  blocks=%3  rss-after=%4 KB  vm-peak=%5 KB  rss-delta=%6 KB  ratio=%7x source")
        .arg(ctorMs)
        .arg(loadMs)
        .arg(blocks.size())
        .arg(rssAfter)
        .arg(vmPeak)
        .arg(rssAfter - rssBefore)
        .arg(double(rssAfter - rssBefore) * 1024.0
             / std::max<qsizetype>(1, content.size()),
             0, 'f', 1);
    return 0;
}
