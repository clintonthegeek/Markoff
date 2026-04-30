// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <vector>

namespace Markoff::Bench {

struct Distribution {
    quint32 count = 0;
    quint64 min   = 0;
    quint64 max   = 0;
    quint64 mean  = 0;
    quint64 p50   = 0;
    quint64 p95   = 0;
    quint64 p99   = 0;
};

/// Compute nearest-rank percentiles, min, max, and arithmetic mean over
/// `samples`. Does NOT mutate the input — internally copies (or move-copies)
/// to a scratch buffer. Returns an all-zeroed Distribution for an empty input.
Distribution reducePercentiles(const std::vector<quint64> &samples);

}  // namespace Markoff::Bench
