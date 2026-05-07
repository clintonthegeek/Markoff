// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/AstBlockDiff.h>

#include <vector>

namespace Markoff::Live {

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

    // Post-pass: collapse adjacent Delete+Insert (in either order) at the
    // same logical position with the same kind. This is the foundation's
    // BlockAnchor-instability case — typing at qtPos 0 of a block changes
    // the byte-0 character's CRDT identity, so computeBlockAnchors hands
    // out a "new" anchor for the same logical block. Without this collapse
    // the model emits Delete+Insert per keystroke and the QML delegate is
    // destroyed and recreated, mid-typing focus is lost. The collapsed
    // Equal carries both prevIndex (so the model finds the existing row)
    // and nextIndex (so the BlockRecord — including the new anchor — is
    // picked up). LiveBlockModel emits anchorRenumbered for these so
    // LiveCursorState updates its TextCaret in lockstep.
    QList<Op> collapsed;
    collapsed.reserve(ops.size());
    for (int idx = 0; idx < ops.size(); ++idx) {
        if (idx + 1 < ops.size()
            && ops[idx].kind == OpKind::Delete
            && ops[idx+1].kind == OpKind::Insert
            && prev[ops[idx].prevIndex].kind == next[ops[idx+1].nextIndex].kind) {
            collapsed.append(Op{ OpKind::Equal, ops[idx].prevIndex, ops[idx+1].nextIndex });
            ++idx;
            continue;
        }
        if (idx + 1 < ops.size()
            && ops[idx].kind == OpKind::Insert
            && ops[idx+1].kind == OpKind::Delete
            && next[ops[idx].nextIndex].kind == prev[ops[idx+1].prevIndex].kind) {
            collapsed.append(Op{ OpKind::Equal, ops[idx+1].prevIndex, ops[idx].nextIndex });
            ++idx;
            continue;
        }
        collapsed.append(ops[idx]);
    }
    return collapsed;
}

}  // namespace Markoff::Live
