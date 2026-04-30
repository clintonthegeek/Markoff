// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/Scenario.h>

#include <array>
#include <random>

namespace Markoff::Bench {

namespace {
constexpr std::array<ScenarioMeta, kScenarioCount> kMeta{{
    {"cold_parse",     0,   1},
    {"type_end",      20, 180},
    {"type_start",    20, 180},
    {"type_middle",   20, 180},
    {"block_boundary", 5,  50},
    {"paste_4kb",      5,  20},
    {"replace_1kb",    5,  20},
}};

quint64 mixSeed(quint64 seed, int iterIndex) {
    return seed ^ (static_cast<quint64>(iterIndex) * 0x9E3779B97F4A7C15ull);
}

char pickPrintableChar(std::mt19937_64 &eng) {
    std::uniform_int_distribution<int> d(static_cast<int>('a'), static_cast<int>('z'));
    return static_cast<char>(d(eng));
}

QByteArray makeBlob(std::mt19937_64 &eng, int n) {
    QByteArray out;
    out.resize(n);
    for (int i = 0; i < n; ++i) out[i] = pickPrintableChar(eng);
    return out;
}

}  // namespace

ScenarioMeta scenarioMeta(ScenarioKind kind) {
    return kMeta[static_cast<int>(kind)];
}

Markoff::MarkoffEdit
nextStep(ScenarioKind kind, const QByteArray &doc, int iterIndex, quint64 seed)
{
    Markoff::MarkoffEdit e;
    std::mt19937_64 eng(mixSeed(seed, iterIndex));
    const quint32 docSize = static_cast<quint32>(doc.size());

    switch (kind) {
    case ScenarioKind::ColdParse:
        return e;       // no-op — runner does not call this for ColdParse

    case ScenarioKind::TypeEnd: {
        e.oldStart = docSize;
        e.oldEnd   = docSize;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::TypeStart: {
        e.oldStart = 0;
        e.oldEnd   = 0;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::TypeMiddle: {
        std::uniform_int_distribution<quint32> d(0, docSize);
        const quint32 off = d(eng);
        e.oldStart = off;
        e.oldEnd   = off;
        e.newText  = QByteArray(1, pickPrintableChar(eng));
        return e;
    }
    case ScenarioKind::BlockBoundary: {
        std::uniform_int_distribution<int> d(0, std::max(0, static_cast<int>(doc.size()) - 16));
        const int target = d(eng);
        const int idx = doc.indexOf("\n\n", target);
        const quint32 at = (idx >= 0) ? static_cast<quint32>(idx + 1) : docSize;
        e.oldStart = at;
        e.oldEnd   = at;
        e.newText  = QByteArray("\n");
        return e;
    }
    case ScenarioKind::Paste4Kb: {
        e.oldStart = docSize;
        e.oldEnd   = docSize;
        e.newText  = makeBlob(eng, 4096);
        return e;
    }
    case ScenarioKind::Replace1Kb: {
        const int span = 1024;
        const int maxStart = std::max(0, static_cast<int>(doc.size()) - span);
        std::uniform_int_distribution<int> d(0, maxStart);
        const quint32 start = static_cast<quint32>(d(eng));
        e.oldStart = start;
        e.oldEnd   = start + span;
        e.newText  = makeBlob(eng, span);
        return e;
    }
    }
    return e;
}

}  // namespace Markoff::Bench
