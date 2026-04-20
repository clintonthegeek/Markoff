// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QStringList>

#include "context.h"
#include "style.h"

namespace Qutepart {

class AbstractRule;

class MatchResult {
  public:
    MatchResult(int length, const QStringList &data, bool lineContinue,
                const ContextSwitcher &context, const Style &style, const AbstractRule *rule);
    MatchResult();

    int length;
    QStringList data;
    bool lineContinue;
    ContextSwitcher nextContext;
    Style style;
    const AbstractRule *rule;
};

} // namespace Qutepart
