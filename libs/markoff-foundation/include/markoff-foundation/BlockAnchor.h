// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/TextAnchor.h>

namespace Markoff {

/// Stable identity for a top-level block in a parsed Markoff::Document.
/// Backed by a TextAnchor at the block's first byte (Left bias). Equal
/// iff the underlying first-byte TextAnchors refer to the same
/// character (i.e. share Lamport identity).
struct MARKOFF_FOUNDATION_EXPORT BlockAnchor {
    TextAnchor firstByte;

    bool operator==(const BlockAnchor &) const = default;
};

}  // namespace Markoff
