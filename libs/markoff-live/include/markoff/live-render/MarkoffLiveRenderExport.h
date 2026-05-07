// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reserved for symbol-visibility decoration when the library is shared.
// As of R1C the library is STATIC, so this expands to nothing. Public
// classes that go in the export-protected boundary will use this macro
// when exported (R2 onwards).
#define MARKOFF_LIVE_RENDER_EXPORT
