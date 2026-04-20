// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "match_result.h"

#include "rules.h"

namespace Qutepart {

MatchResult::MatchResult(int length, const QStringList &data, bool lineContinue,
                         const ContextSwitcher &context, const Style &style,
                         const AbstractRule *rule)
    : length(length), data(data), lineContinue(lineContinue), nextContext(context), style(style),
      rule(rule) {}

MatchResult::MatchResult() : length(0), lineContinue(false) {}

} // namespace Qutepart
