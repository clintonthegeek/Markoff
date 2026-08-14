// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Markoff::Canvas {

/// A pipe-table column's alignment (GFM delimiter-row tri-state, contract-v2
/// P5.2). `None` is a bare `---` cell — no explicit alignment requested
/// (renders left by CSS convention, but is a distinct written state from an
/// explicit `Left`, matching the markdown source's own round-trip: writing
/// `None` erases the colons rather than pinning them left).
enum class TableAlign { None, Left, Center, Right };

}  // namespace Markoff::Canvas
