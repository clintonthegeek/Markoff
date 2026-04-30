// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace Markoff::Bench {

enum class CorpusProfile : int {
    Tiny              = 0,
    MidProse          = 1,
    MidMixed          = 2,
    BigProse          = 3,
    BigCodeHeavy      = 4,
    BigTableHeavy     = 5,
    BigFootnoteHeavy  = 6,
    Huge              = 7,
    Pathological      = 8,
};

constexpr int kCorpusProfileCount = 9;

/// Stable name for a profile (e.g. "mid_prose"). Used as a JSON key.
const char *profileName(CorpusProfile p);

/// Generate a deterministic markdown document matching the named profile.
/// `seed` parameterises the RNG; same (profile, seed) → byte-identical
/// output. Size is within ±10 % of the profile's target.
QByteArray generate(CorpusProfile profile, quint64 seed);

}  // namespace Markoff::Bench
