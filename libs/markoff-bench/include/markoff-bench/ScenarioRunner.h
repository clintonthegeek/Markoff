// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <vector>
#include <array>

#include <markoff-bench/AllocCounter.h>
#include <markoff-bench/CorpusGen.h>
#include <markoff-bench/PercentileReducer.h>
#include <markoff-bench/PhaseTimer.h>
#include <markoff-bench/Scenario.h>

namespace Markoff::Bench {

enum class Tier : int {
    DirectParse = 0,    // Tier 1
    PoolParse   = 1,    // Tier 1b — added in Task 10
    Render      = 2,    // Tier 2  — added in Task 14
};

struct RunResult {
    // Identification
    const char *profileName    = "";
    const char *fixtureName    = "";   // mutually exclusive with profileName
    const char *scenarioName   = "";
    Tier        tier           = Tier::DirectParse;

    // Iteration counts
    int         iterations     = 0;
    int         warmupIters    = 0;

    // Per-phase wall-time (ns) — one Distribution per Phase slot.
    std::array<Distribution, kPhaseCount> phases{};

    // Wall-time per iteration overall (sum across all phases).
    Distribution totalNs{};

    // Reuse counts (Tier 1/1b only). Block: bytes covered by changed-ranges
    // (lower = more reuse). Inline: count of inline trees reused.
    Distribution blockChangedBytes{};
    Distribution inlineReuseCount{};

    // Allocations during the timed window (Tier 1/1b only).
    Distribution allocBytes{};
    Distribution allocCount{};
};

/// Run a scenario at Tier 1 (direct parser). `corpus` is the starting
/// document bytes; the scenario advances it edit-by-edit. Returns
/// percentile-reduced metrics across the measured iters (warmup excluded).
RunResult runDirectParse(const QByteArray &corpus,
                         ScenarioKind scenario,
                         quint64 seed);

/// Run a scenario at Tier 1b (MarkoffDocument + ParsePool + signal hop).
/// Each iteration: applyLocalEdit, then run the event loop until
/// parseUpdated fires (or 5s deadline). Captures the same metrics as
/// runDirectParse plus PoolQueue + SignalHop phase splits.
RunResult runPoolParse(const QByteArray &corpus,
                       ScenarioKind scenario,
                       quint64 seed);

}  // namespace Markoff::Bench
