// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/AstBlockDiff.h>

#include <vector>

namespace Markoff::Live::Detail {

QList<AstBlockDiff::Op> AstBlockDiff::diff(const QList<BlockKey> &prev,
                                            const QList<BlockKey> &next)
{
    const int m = prev.size();
    const int n = next.size();

    // Fast path: identity (avoids O(m*n) on identical reparses).
    if (m == n) {
        bool same = true;
        for (int i = 0; i < m; ++i) {
            if (prev[i] != next[i]) { same = false; break; }
        }
        if (same) {
            QList<Op> ops;
            ops.reserve(m);
            for (int i = 0; i < m; ++i)
                ops.append(Op{ OpKind::Equal, i, i });
            return ops;
        }
    }

    // LCS table.
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j)
            dp[i][j] = (prev[i-1] == next[j-1])
                       ? dp[i-1][j-1] + 1
                       : std::max(dp[i-1][j], dp[i][j-1]);

    // Backtrack → forward order.
    QList<Op> ops;
    int i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && prev[i-1] == next[j-1]) {
            ops.prepend(Op{ OpKind::Equal, i-1, j-1 });
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            ops.prepend(Op{ OpKind::Insert, -1, j-1 });
            --j;
        } else {
            ops.prepend(Op{ OpKind::Delete, i-1, -1 });
            --i;
        }
    }

    return ops;
}

} // namespace Markoff::Live::Detail
