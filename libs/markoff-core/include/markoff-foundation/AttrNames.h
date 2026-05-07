// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockAttrsMap.h>

namespace Markoff::AttrNames {
    inline const AttrName Level        = "level";        // Heading: int 1–6
    inline const AttrName InfoString   = "infoString";   // CodeBlock: QString
    inline const AttrName MarkerStyle  = "markerStyle";  // ListItem: QString (e.g. "-", "*", "1.")
    inline const AttrName MarkerNumber = "markerNumber"; // ListItem ordered: int 1+
    inline const AttrName IndentLevel  = "indentLevel";  // ListItem: int 0-based
    inline const AttrName Checked      = "checked";      // ListItem: bool (task list checkbox)
    inline const AttrName LooseRun     = "looseRun";     // ListItem: bool — parent list was loose
    inline const AttrName Src          = "src";          // Image: QString URL/path
    inline const AttrName Alt          = "alt";          // Image: QString alt text
    inline const AttrName Title        = "title";        // Image: QString (optional)
    inline const AttrName DisplayMode  = "displayMode";  // Math: bool (true = display, false = inline)
}  // namespace Markoff::AttrNames
