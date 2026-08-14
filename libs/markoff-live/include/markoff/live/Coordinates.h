// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/TextUnits.h>

/// Compatibility alias. The byte↔QChar helpers moved to markoff-core at
/// P1.2 (`<markoff/core/TextUnits.h>`) so live and canvas share one copy;
/// this header keeps the `Markoff::Live::Detail::Coordinates` spelling
/// working for existing call sites (in-tree and in consumers). New code
/// should include the core header and call `Markoff::TextUnits::` directly.
namespace Markoff::Live::Detail::Coordinates {

using Markoff::TextUnits::byteToQtPos;
using Markoff::TextUnits::qtPosToByte;

}  // namespace Markoff::Live::Detail::Coordinates
