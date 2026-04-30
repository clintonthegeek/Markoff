// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

#include <markoff-bench/ScenarioRunner.h>

namespace Markoff::Bench {

/// Convert one RunResult to a JSON object matching schema_version=1.
QJsonObject toJson(const RunResult &r);

/// Wrap a list of RunResult objects into a top-level JSON object with
/// metadata (schema_version, git_sha, build_type, host) at the root.
QJsonObject toJsonReport(const QList<RunResult> &results,
                         const QString &gitSha,
                         const QString &buildType);

}  // namespace Markoff::Bench
