// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/PercentileReducer.h>

#include <algorithm>
#include <cmath>

namespace Markoff::Bench {

namespace {
quint64 nearestRank(std::vector<quint64> &scratch, double pct) {
    // 1-indexed rank, then clamp to [0, n-1] for 0-indexed access.
    const auto n = scratch.size();
    if (n == 0) return 0;
    auto rank = static_cast<size_t>(std::ceil(pct * static_cast<double>(n)));
    if (rank == 0) rank = 1;
    if (rank > n) rank = n;
    const size_t idx = rank - 1;
    std::nth_element(scratch.begin(), scratch.begin() + idx, scratch.end());
    return scratch[idx];
}
}  // namespace

Distribution reducePercentiles(const std::vector<quint64> &samples)
{
    Distribution d;
    if (samples.empty()) return d;

    d.count = static_cast<quint32>(samples.size());
    d.min   = *std::min_element(samples.begin(), samples.end());
    d.max   = *std::max_element(samples.begin(), samples.end());

    quint64 sum = 0;
    for (auto v : samples) sum += v;
    d.mean = sum / d.count;

    std::vector<quint64> scratch = samples;
    d.p50 = nearestRank(scratch, 0.50);
    scratch = samples;
    d.p95 = nearestRank(scratch, 0.95);
    scratch = samples;
    d.p99 = nearestRank(scratch, 0.99);
    return d;
}

}  // namespace Markoff::Bench
