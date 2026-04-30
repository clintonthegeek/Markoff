// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <markoff-foundation/MarkoffEdit.h>

namespace Markoff::Bench {

enum class ScenarioKind : int {
    ColdParse       = 0,
    TypeEnd         = 1,
    TypeStart       = 2,
    TypeMiddle      = 3,
    BlockBoundary   = 4,
    Paste4Kb        = 5,
    Replace1Kb      = 6,
};

constexpr int kScenarioCount = 7;

struct ScenarioMeta {
    const char *name;
    int         warmupIters;
    int         measuredIters;
};

ScenarioMeta scenarioMeta(ScenarioKind kind);

/// Produce the i-th edit for this scenario against `currentDoc`.
/// `iterIndex` is 0-based and counts both warmup and measured iters.
/// `seed` is mixed into the iteration so two different bench runs with
/// different seeds produce different per-iter byte payloads.
///
/// For ColdParse the function returns an empty (no-op) edit; the runner
/// is expected to skip nextStep() entirely for that scenario.
Markoff::MarkoffEdit
nextStep(ScenarioKind kind, const QByteArray &currentDoc, int iterIndex, quint64 seed);

}  // namespace Markoff::Bench
