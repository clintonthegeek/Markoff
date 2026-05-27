// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// markoff_styled is currently built as a STATIC library; the export macro
// is a no-op. Keep it as a hook so a future SHARED build needs no header
// surgery (mirrors MarkoffSourceExport / MarkoffCoreExport conventions).
#define MARKOFF_STYLED_EXPORT
