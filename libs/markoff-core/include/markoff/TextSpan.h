// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QHashFunctions>

namespace Markoff {

/// A half-open range `[offset, offset + length)` over the canonical
/// flat markdown source owned by MarkoffDocument. Every search match
/// and every replace target is expressed as a TextSpan so views can
/// translate to their local coordinates.
struct TextSpan {
    int offset = 0;
    int length = 0;

    int end() const { return offset + length; }
    bool isEmpty() const { return length == 0; }
    bool contains(int pos) const {
        return pos >= offset && pos < end();
    }

    friend bool operator==(const TextSpan &a, const TextSpan &b) {
        return a.offset == b.offset && a.length == b.length;
    }
    friend bool operator!=(const TextSpan &a, const TextSpan &b) {
        return !(a == b);
    }
    friend bool operator<(const TextSpan &a, const TextSpan &b) {
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.length < b.length;
    }
};

inline size_t qHash(const TextSpan &s, size_t seed = 0) noexcept {
    return qHashMulti(seed, s.offset, s.length);
}

}  // namespace Markoff
