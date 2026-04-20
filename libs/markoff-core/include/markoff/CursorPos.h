// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// A 1-based (line, column) position used by widgets that expose a
/// cursor (Live Preview, Source). Reading view has no cursor; its
/// MarkdownView::cursorPosition() returns a default-constructed value.
struct CursorPos {
    int line = 0;
    int column = 0;

    friend bool operator==(const CursorPos &a, const CursorPos &b) {
        return a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const CursorPos &a, const CursorPos &b) {
        return !(a == b);
    }
};

inline size_t qHash(const CursorPos &p, size_t seed = 0) noexcept {
    return qHashMulti(seed, p.line, p.column);
}

}  // namespace Markoff
