// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// Identifies a collapsible heading by (starting line, heading level).
/// Used by MarkdownView::foldedHeadings() / setFoldedHeadings().
/// Reading, Source, and Live Preview each map FoldSpec to their
/// internal fold representation.
struct FoldSpec {
    int line = 0;
    int level = 0;

    friend bool operator==(const FoldSpec &a, const FoldSpec &b) {
        return a.line == b.line && a.level == b.level;
    }
    friend bool operator!=(const FoldSpec &a, const FoldSpec &b) {
        return !(a == b);
    }
};

inline size_t qHash(const FoldSpec &f, size_t seed = 0) noexcept {
    return qHashMulti(seed, f.line, f.level);
}

}  // namespace Markoff
