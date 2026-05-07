// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/BlockRecord.h>
#include <QList>

namespace Markoff::Live {

/// Pure C++ Myers/LCS diff over BlockKey sequences. Output is a list of
/// edit operations referencing indices in `prev` and `next`. Used by
/// LiveListModelBinding to emit the minimal Qt model signal sequence so
/// ListView preserves delegates whose AST block still exists.
class MARKOFF_LIVE_EXPORT AstBlockDiff {
public:
    enum class OpKind { Equal, Insert, Delete };

    struct Op {
        OpKind kind;
        int    prevIndex = -1;  ///< -1 for Insert
        int    nextIndex = -1;  ///< -1 for Delete
    };

    static QList<Op> diff(const QList<BlockKey> &prev,
                          const QList<BlockKey> &next);
};

}  // namespace Markoff::Live
