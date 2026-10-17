// SPDX-License-Identifier: GPL-3.0-or-later
//
// Diagnostic load benchmark for markoff-core. Measures Markdown import
// (or legacy load), cold inline-span resolution, representative edits,
// accepted-prefix replay, reopen and RSS for a given `.md` file. In candidate
// mode, MARKOFF_BENCH_EXTRA_EDITS adds up to 500 independent accepted-action
// records and verifies text and record count after reopen.
//
// Not registered with ctest. Run manually:
//
//   QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
//     ./build-dev/bin/perf_load_bench <markdown-file>
//   MARKOFF_BENCH_EXTRA_EDITS=50 QT_QPA_PLATFORM=offscreen \
//     QT_FORCE_STDERR_LOGGING=1 ./build-dev/bin/perf_load_bench <markdown-file>
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
#include <markoff/core/StructuralOp.h>
#include <markoff/vnext/ActionBundleCodec.h>
#include <markoff/vnext/AcceptedPrefixProjection.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>

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

    const long rssBefore = readProcStatusKb("VmRSS");
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) {
        qWarning("Cannot create ephemeral benchmark provider");
        return 2;
    }
    auto provider = std::move(providerResult).value();
    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("markoff-load-benchmark"), 1};
    QElapsedTimer t;
    t.start();
    auto opened = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) {
        qWarning("Cannot open benchmark document");
        return 2;
    }
    auto doc = std::move(opened).value();
    const qint64 ctorMs = t.elapsed();
    QElapsedTimer t2;
    t2.start();
    const auto imported = doc->importInitialMarkdown(content);
    qInfo("[bench] import-state=%d provider-state=%d writable=%d",
        static_cast<int>(imported.state), static_cast<int>(doc->providerState()),
        doc->canStartOrdinaryAction());
    if (imported.state != Markoff::VNext::ActionPublicationState::committed) {
        qWarning("Cannot import benchmark Markdown");
        return 2;
    }
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
        "[bench] provider-open=%1 ms  initial-import=%2 ms  blocks=%3 first-block-counter=%7 last-block-counter=%8 rss-after=%4 KB  vm-peak=%5 KB  rss-delta=%6 KB")
        .arg(ctorMs).arg(loadMs).arg(blocks.size()).arg(rssAfter).arg(vmPeak)
        .arg(rssAfter - rssBefore)
        .arg(blocks.empty() ? 0 : blocks.front().counter())
        .arg(blocks.empty() ? 0 : blocks.back().counter());
    qInfo().noquote() << QString(
        "[bench] inlineSpansForAll=%1 ms  totalSpans=%2  rss-after-token-resolution=%3 KB")
        .arg(inlineMs).arg(totalSpans).arg(rssAfter);

    if (blocks.empty()) {
        qWarning("Benchmark document has no blocks");
        return 3;
    }
    const Markoff::BlockId first = blocks.front();
    const QByteArray firstText = doc->blockText(first);
    QElapsedTimer actionTimer;
    actionTimer.start();
    const auto edit = doc->applyBlockEdit({first,
        static_cast<uint32_t>(firstText.size()), 0, QByteArrayLiteral("x")});
    const qint64 editMs = actionTimer.elapsed();
    if (edit.state != Markoff::VNext::ActionPublicationState::committed
        && edit.state != Markoff::VNext::ActionPublicationState::committedHistoryPending) {
        qWarning("Benchmark text edit state=%d: %s",
            static_cast<int>(edit.state),
            edit.diagnostic ? edit.diagnostic->message().data() : "no diagnostic");
        return 3;
    }

    actionTimer.restart();
    const auto paste = doc->pasteMarkdownAt(first,
        static_cast<uint32_t>(firstText.size() + 1),
        QByteArrayLiteral("Pasted benchmark paragraph\n\nSecond pasted paragraph"));
    const qint64 pasteMs = actionTimer.elapsed();
    if (paste.publication.state != Markoff::VNext::ActionPublicationState::committed
        && paste.publication.state != Markoff::VNext::ActionPublicationState::committedHistoryPending) {
        qWarning("Benchmark structured paste state=%d: %s",
            static_cast<int>(paste.publication.state),
            paste.publication.diagnostic
                ? paste.publication.diagnostic->message().data() : "no diagnostic");
        doc.reset();
        auto inspectResult = provider->open(
            scope, Markoff::VNext::StorageMode::ephemeral);
        if (inspectResult.ok()) {
            auto inspect = std::move(inspectResult).value();
            const auto recovery = inspect->readRecoveryState();
            const auto next = inspect->nextCounter(QStringLiteral("markoff/order/v1"));
            if (next.ok())
                qWarning("Benchmark order counter after rejection=%llu",
                    static_cast<unsigned long long>(next.value()));
            if (recovery.ok()) {
                quint64 maxAcceptedOrderCounter = 0;
                size_t acceptedOrderOperations = 0;
                for (const auto& record : recovery.value().acceptedRecords) {
                    if (record.format != QByteArrayLiteral("MAB/3")) continue;
                    const auto bundle = Markoff::VNext::ActionBundleCodec::decode(
                        record.bytes, scope.documentKey.toUtf8().toStdString(), scope.epoch);
                    if (!bundle.ok()) continue;
                    for (const auto& operation : bundle.value().operations) {
                        if (operation.stream == "markoff/order/v1") {
                            ++acceptedOrderOperations;
                            maxAcceptedOrderCounter = std::max(
                                maxAcceptedOrderCounter,
                                static_cast<quint64>(operation.counter));
                        }
                    }
                }
                qWarning("Accepted order operations=%llu max-counter=%llu",
                    static_cast<unsigned long long>(acceptedOrderOperations),
                    static_cast<unsigned long long>(maxAcceptedOrderCounter));
            }
        }
        return 3;
    }

    const auto afterPaste = doc->iterateBlocks();
    actionTimer.restart();
    const auto structure = doc->applyStructural({
        Markoff::StructuralOp::InsertEntry{afterPaste.back(),
                                           Markoff::BlockKind::Paragraph}});
    const qint64 structureMs = actionTimer.elapsed();
    if (structure.state != Markoff::VNext::ActionPublicationState::committed
        && structure.state != Markoff::VNext::ActionPublicationState::committedHistoryPending) {
        qWarning("Benchmark structural insertion did not commit");
        return 3;
    }

    int extraEdits = 0;
    if (const char *rawCount = std::getenv("MARKOFF_BENCH_EXTRA_EDITS")) {
        char *end = nullptr;
        const long parsed = std::strtol(rawCount, &end, 10);
        if (end == rawCount || *end != '\0' || parsed < 0 || parsed > 500) {
            qWarning("MARKOFF_BENCH_EXTRA_EDITS must be an integer from 0 to 500");
            return 3;
        }
        extraEdits = static_cast<int>(parsed);
    }
    qint64 extraEditTotalMs = 0;
    for (int i = 0; i < extraEdits; ++i) {
        const QByteArray currentText = doc->blockText(first);
        actionTimer.restart();
        const auto extra = doc->applyBlockEdit({first,
            static_cast<uint32_t>(currentText.size()), 0, QByteArrayLiteral("h")});
        extraEditTotalMs += actionTimer.elapsed();
        if (extra.state != Markoff::VNext::ActionPublicationState::committed
            && extra.state != Markoff::VNext::ActionPublicationState::committedHistoryPending) {
            qWarning("Benchmark extra edit %d did not commit (state=%d)", i,
                static_cast<int>(extra.state));
            return 3;
        }
    }

    const auto expectedBlocks = doc->iterateBlocks().size();
    const QByteArray expectedFirstText = doc->blockText(first);
    doc.reset();
    qint64 recoveryReadMs = -1;
    qint64 prefixReplayMs = -1;
    size_t acceptedRecordCount = 0;
    {
        auto sessionResult = provider->open(
            scope, Markoff::VNext::StorageMode::ephemeral);
        if (!sessionResult.ok()) {
            qWarning("Benchmark recovery inspection open failed");
            return 3;
        }
        auto session = std::move(sessionResult).value();
        actionTimer.restart();
        auto recovery = session->readRecoveryState();
        recoveryReadMs = actionTimer.elapsed();
        if (!recovery.ok()) {
            qWarning("Benchmark recovery snapshot read failed");
            return 3;
        }
        acceptedRecordCount = recovery.value().acceptedRecords.size();
        actionTimer.restart();
        auto projection = Markoff::VNext::AcceptedPrefixProjection::replay(
            *session, recovery.value());
        prefixReplayMs = actionTimer.elapsed();
        if (!projection.ok()) {
            qWarning("Benchmark accepted-prefix replay failed");
            return 3;
        }
    }
    actionTimer.restart();
    auto reopened = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::ephemeral);
    const qint64 reopenMs = actionTimer.elapsed();
    if (!reopened.ok()) {
        qWarning("Benchmark provider reopen failed");
        return 3;
    }
    doc = std::move(reopened).value();
    if (doc->iterateBlocks().size() != expectedBlocks
        || doc->blockText(first) != expectedFirstText) {
        qWarning("Benchmark reopen changed accepted blocks or first-block text");
        return 3;
    }
    if (acceptedRecordCount != static_cast<size_t>(4 + extraEdits)) {
        qWarning("Benchmark accepted-record count=%llu; expected=%llu",
            static_cast<unsigned long long>(acceptedRecordCount),
            static_cast<unsigned long long>(4 + extraEdits));
        return 3;
    }
    const long rssAfterReopen = readProcStatusKb("VmRSS");
    qInfo().noquote() << QString(
        "[bench] text-edit=%1 ms  structured-paste=%2 ms  structural-insert=%3 ms  extra-edits=%9 total=%10 ms  accepted-records=%11 recovery-read=%7 ms  accepted-prefix-replay=%8 ms  reopen=%4 ms  retained-rss=%5 KB  reopened-blocks=%6")
        .arg(editMs).arg(pasteMs).arg(structureMs).arg(reopenMs)
        .arg(rssAfterReopen).arg(expectedBlocks).arg(recoveryReadMs)
        .arg(prefixReplayMs).arg(extraEdits).arg(extraEditTotalMs)
        .arg(static_cast<qulonglong>(acceptedRecordCount));
    return 0;
}
