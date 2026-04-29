// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <markoff/view/qml/BlockRecord.h>

namespace Markoff::View::Qml {

/// Pure C++ Myers/LCS diff over BlockKey sequences. Output is a list of
/// edit operations referencing indices in `prev` and `next`. Used by
/// LiveBlockModel to emit the minimal Qt model signal sequence so ListView
/// preserves delegates whose AST block still exists.
class AstBlockDiff {
public:
    enum class OpKind {
        Equal,    ///< prev[prevIndex] == next[nextIndex]; delegate persists
        Insert,   ///< next[nextIndex] is new (no prev counterpart)
        Delete    ///< prev[prevIndex] is gone (no next counterpart)
    };

    struct Op {
        OpKind kind;
        int    prevIndex = -1;   ///< -1 for Insert
        int    nextIndex = -1;   ///< -1 for Delete
    };

    static QList<Op> diff(const QList<BlockKey> &prev,
                          const QList<BlockKey> &next);
};

}  // namespace Markoff::View::Qml
